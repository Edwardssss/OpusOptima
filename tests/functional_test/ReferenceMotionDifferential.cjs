'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { execFileSync } = require('node:child_process');

const sourcePath = process.argv[2];
const headerPath = process.argv[3];
assert(sourcePath && headerPath, 'Usage: node ReferenceMotionDifferential.cjs <locked collision.c> <locked sim.h>');
const source = fs.readFileSync(sourcePath);
const header = fs.readFileSync(headerPath);
const headerSha256 = crypto.createHash('sha256').update(header).digest('hex');
assert.equal(headerSha256, '198a3c4a74bc94fe1595e0159881797d450f387e92598191b77cbec3e8864a22');
const sha256 = crypto.createHash('sha256').update(source).digest('hex');
assert.equal(sha256, 'a2df55f5a12ec8a107c6408e44d7e4373447b6741a8f59259829c2f6f96558b5');
const headerText = header.toString('utf8');
const movementEnum = headerText.match(/enum movement_type \{[^}]+\};/g);
assert.equal(movementEnum?.length, 1, 'Expected one authoritative movement enum');
const text = source.toString('utf8');
function extract(begin, end) {
    const start = text.indexOf(begin);
    assert(start >= 0 && start === text.lastIndexOf(begin), `Nonunique extraction start: ${begin}`);
    const stop = text.indexOf(end, start + begin.length);
    assert(stop > start, `Missing extraction end: ${end}`);
    return text.slice(start, stop);
}
const helpers = [
    extract('static struct xy_vector to_xy(', 'static struct vector from_xy('),
    extract('static struct vector from_xy(', 'static float xy_len2('),
    extract('static float xy_len2(', 'static float to_radians('),
    extract('static float to_radians(', 'static struct xy_rect xy_rect_add_collider('),
].join('\n');
const offset = extract('            struct vector p = board->moving_atoms.atoms_at_positions[atom_index].position;', '\n        }\n        if (chain_mode == EXTEND_CHAIN)');
const motion = extract('            struct xy_vector v = to_xy(m.base);', '            for (size_t j = 0; j < m.number_of_atoms; ++j)');
const atom = extract('                struct xy_vector xy = moving_atom_offsets[atom_index++];', '\n            }\n            while (chain_atom_cursor');
const rasterPredicate = extract('static void mark_area_and_check_board(', '    // mark area.').split('{\n').slice(1).join('{\n');
const rasterCandidates = extract('        struct vector p = from_xy(collider.center);\n        mark_area_and_check_board', '\n    }\n    list->colliders');
const colliderTypes = extract('struct xy_rect {', 'struct chain_atom_collider {');
const colliderList = extract('struct collider_list {', '// based on image dimensions');
const colliderBounds = extract('static struct xy_rect xy_rect_add_collider(', 'static bool xy_rect_intersects_ray(');
const colliderBoard = extract('static void mark_area_and_check_board(', '__attribute__((always_inline))');
const colliderAppend = extract('static inline void add_collider(', 'static void add_chain_atom_collider(');
const chainColliderType = extract('struct chain_atom_collider {', 'struct collider_list {');
const schedulerStart = 'bool collision(struct solution *solution,';
assert.equal(text.indexOf(schedulerStart), text.lastIndexOf(schedulerStart));
const collisionScheduler = text.slice(text.indexOf(schedulerStart));
assert(collisionScheduler.startsWith(schedulerStart));
const root = path.resolve(__dirname, '../..');
const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'opus-motion-differential-'));
try {
    const c = `#include <stdint.h>
#include <math.h>
#define M_PI 3.14159265358979323846
${movementEnum[0]}
struct vector { int u, v; };
struct xy_vector { float x, y; };
struct collider { struct xy_vector center; float radius; };
struct movement { uint32_t type; struct vector base, grabber_offset, absolute_grab_position, translation; int32_t base_rotation, rotation; int piston_extension; };
struct stored_atom { struct vector position; };
struct board { struct { struct stored_atom *atoms_at_positions; } moving_atoms; };
struct capture { struct xy_vector center; };
static const float hexSizeX = 82, hexSizeY = 71, atomRadius = 29;
static void add_collider(struct capture *list, struct board *board, struct collider collider) { (void)board; list->center = collider.center; }
${helpers}
void reference_snap_oracle(float x, float y, int *out) {
    struct vector p = from_xy((struct xy_vector){x, y});
    out[0] = p.u; out[1] = p.v;
}
static void mark_area_and_check_board(int **list, void *board, struct collider collider, int32_t u, int32_t v) {
    (void)board;
${rasterPredicate}
    *(*list)++ = p.u;
    *(*list)++ = p.v;
}
int reference_raster_oracle(float x, float y, float radius, int *out) {
    struct collider collider = {{x, y}, radius};
    int *cursor = out, **list = &cursor;
    void *board = 0;
${rasterCandidates}
    return (int)(cursor - out) / 2;
}
void reference_motion_oracle(const int *a, float progress, float *out) {
    static const uint32_t mechanisms[] = {SWING_MOVEMENT, PIVOT_MOVEMENT, TRACK_MOVEMENT, PISTON_MOVEMENT};
    struct movement m = { .type = mechanisms[a[0]] | (a[1] ? IS_PISTON : 0),
        .base = {a[2], a[3]}, .grabber_offset = {a[4], a[5]}, .absolute_grab_position = {a[6], a[7]},
        .translation = {a[10], a[11]}, .base_rotation = a[12], .rotation = a[13], .piston_extension = a[14] };
    struct stored_atom stored = {{a[8], a[9]}};
    struct board storage = {0}, *board = &storage;
    storage.moving_atoms.atoms_at_positions = &stored;
    struct xy_vector moving_atom_offsets[1];
    unsigned atom_index = 0;
${offset}
    atom_index = 0;
    struct capture list = {0};
${motion}
${atom}
    out[0] = list.center.x; out[1] = list.center.y;
}
`;
    fs.writeFileSync(path.join(temp, 'oracle.c'), c);
    // This hand-authored schedule exercises the extracted append operation, not the simulator scheduler.
    const colliderFixture = `#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#define M_PI 3.14159265358979323846
struct vector { int u, v; };
struct xy_vector { float x, y; };
${colliderTypes}
${colliderList}
typedef uint64_t atom;
#define REMOVED UINT64_C(1)
struct board { uint64_t collision_checks; atom marked;
    struct vector visits[7]; size_t visit_count; };
static const float hexSizeX = 82, hexSizeY = 71, atomRadius = 29;
${helpers}
${colliderBounds}
static atom mark_used_area(struct board *board, struct vector p) {
    assert(board->visit_count < 7);
    board->visits[board->visit_count++] = p;
    return board->marked;
}
${colliderBoard}
${colliderAppend}
static void commit(struct collider_list *list) {
    list->cursor = list->length;
    list->bounding_box_up_to_cursor = list->bounding_box;
}
static struct collider_list fresh(struct collider *storage, struct vector *location) {
    return (struct collider_list){ .colliders = storage, .ignore_board = true,
        .bounding_box = empty_rect, .bounding_box_up_to_cursor = empty_rect,
        .collision_location = location };
}
int main(void) {
    (void)to_radians;
    (void)to_xy;
    struct collider storage[16];
    struct vector location = {123, 456};
    struct board board = {0};
    struct collider near = {{0, 0}, 29}, far = {{820, 0}, 20};
    struct collider_list list = fresh(storage, &location);
    add_collider(&list, &board, near);
    add_collider(&list, &board, near);
    assert(list.length == 2 && list.cursor == 0 && !list.collision);
    assert(board.collision_checks == 0 && location.u == 123 && location.v == 456);
    commit(&list);
    add_collider(&list, &board, near);
    assert(list.length == 3 && list.cursor == 2 && list.collision);
    assert(board.collision_checks == 2 && location.u == 0 && location.v == 0);

    // Sequential commits model fixed/moving bases and boundaries between movement batches.
    board.collision_checks = 0;
    list = fresh(storage, &location);
    add_collider(&list, &board, near);
    commit(&list);
    add_collider(&list, &board, far);
    assert(!list.collision && board.collision_checks == 0);
    commit(&list);
    add_collider(&list, &board, (struct collider){{410, 0}, 20});
    assert(!list.collision && board.collision_checks == 2);
    add_collider(&list, &board, far);
    assert(list.collision && board.collision_checks == 4);
    assert(location.u == 10 && location.v == 0);

    // The batch mixes wall/producer radii; their labels have no meaning to add_collider.
    board.collision_checks = 0;
    list = fresh(storage, NULL);
    add_collider(&list, &board, near);
    commit(&list);
    add_collider(&list, &board, far);
    add_collider(&list, &board, (struct collider){{820, 0}, 15});
    assert(!list.collision && list.cursor == 1 && board.collision_checks == 0);
    commit(&list);
    add_collider(&list, &board, (struct collider){{820, 0}, 15});
    assert(list.collision && board.collision_checks == 3);

    for (int radius = 15; radius <= 29; radius += radius == 15 ? 5 : 9) {
        float tangent = 29.f + radius;
        for (int side = -1; side <= 1; ++side) {
            board.collision_checks = 0;
            list = fresh(storage, NULL);
            add_collider(&list, &board, near);
            commit(&list);
            float x = side < 0 ? nextafterf(tangent, -INFINITY) :
                side > 0 ? nextafterf(tangent, INFINITY) : tangent;
            add_collider(&list, &board, (struct collider){{x, 0}, (float)radius});
            assert(list.collision == (side < 0));
            assert(board.collision_checks == (side > 0 ? 0u : 1u));
        }
    }
    board.collision_checks = 0;
    list = fresh(storage, &location);
    add_collider(&list, &board, near);
    commit(&list);
    struct xy_rect saved = list.bounding_box;
    list.bounding_box_unused = true;
    add_collider(&list, &board, far);
    assert(list.length == 2 && list.bounding_box.min_x == saved.min_x &&
        list.bounding_box.max_x == saved.max_x && list.bounding_box.min_y == saved.min_y &&
        list.bounding_box.max_y == saved.max_y);
    assert(!list.collision && board.collision_checks == 0);
    add_collider(&list, &board, near);
    assert(list.collision && board.collision_checks == 1);
    // A latched collision does not stop append or suppress a later location update.
    add_collider(&list, &board, (struct collider){{41, 0}, 29});
    assert(list.length == 4 && list.cursor == 1 && board.collision_checks == 2);
    struct vector expected = from_xy((struct xy_vector){41, 0});
    assert(location.u == expected.u && location.v == expected.v);

    // Stubbed area storage controls REMOVED; the extracted board predicate and call order are real.
    const struct vector cells[7] = {{0,0}, {1,0}, {0,1}, {-1,1}, {-1,0}, {0,-1}, {1,-1}};
    for (int removed = 0; removed <= 1; ++removed) {
        board = (struct board){ .marked = removed ? REMOVED : 0 };
        location = (struct vector){123,456};
        list = fresh(storage, &location);
        list.ignore_board = false;
        add_collider(&list, &board, (struct collider){{0,0},100});
        assert(board.collision_checks == 70 && board.visit_count == 7);
        assert(list.length == 1 && list.cursor == 0 && list.collision == !removed);
        for (size_t i = 0; i < 7; ++i)
            assert(board.visits[i].u == cells[i].u && board.visits[i].v == cells[i].v);
        assert(location.u == (removed ? 123 : 1) && location.v == (removed ? 456 : -1));
    }
    board = (struct board){ .marked = REMOVED };
    list = fresh(storage, NULL);
    add_collider(&list, &board, near);
    commit(&list);
    list.ignore_board = false;
    add_collider(&list, &board, near);
    assert(board.collision_checks == 71 && board.visit_count == 1 && list.collision);
    assert(board.visits[0].u == 0 && board.visits[0].v == 0);
    return 0;
}
`;
    fs.writeFileSync(path.join(temp, 'collider-fixture.c'), colliderFixture);
    const run = (program, args) => execFileSync(program, args, { cwd: root, stdio: 'inherit' });
    run(process.env.CC || 'cc', ['-std=c11', '-O0', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror', path.join(temp, 'collider-fixture.c'), '-lm', '-o', path.join(temp, 'collider-fixture')]);
    run(path.join(temp, 'collider-fixture'), []);
    console.log('Extracted C collider append fixtures: prefix/batch, bounds, tangency, counters, collision location and board REMOVED/call order passed (stubbed area storage; hand-authored schedule)');
    const schedulerFixture = `#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#define M_PI 3.14159265358979323846
${movementEnum[0]}
struct vector { int u, v; };
struct xy_vector { float x, y; };
${colliderTypes}
${chainColliderType}
${colliderList}
typedef uint64_t atom;
#define REMOVED UINT64_C(1)
#define VALID UINT64_C(2)
#define IS_CHAIN_ATOM UINT64_C(4)
enum chain_mode { NO_CHAIN, EXTEND_CHAIN };
enum { GROWTH_LINEAR = 1 };
struct atom_at_position { struct vector position; atom atom; };
struct chain_atom { bool prev_in_list; uint32_t next_in_list; };
struct movement { uint32_t type; struct vector base, grabber_offset, absolute_grab_position, translation;
    int32_t base_rotation, rotation; int piston_extension; size_t number_of_atoms; uint32_t first_chain_atom; };
struct arm { struct vector position, movement; };
struct solution { size_t number_of_arms, number_of_cabinet_walls; struct arm *arms; struct vector *cabinet_walls; };
struct board { enum chain_mode chain_mode; size_t number_of_atoms_being_produced;
    struct vector *atoms_being_produced;
    struct { size_t length; struct atom_at_position *atoms_at_positions; } moving_atoms;
    struct { size_t length; struct movement *movements; } movements;
    struct { struct atom_at_position *atoms_at_positions; } grid;
    uint32_t number_of_chain_atoms; struct chain_atom *chain_atoms;
    int area_growth_order; uint64_t collision_checks; size_t marks; atom area_atom; };
#define BOARD_CAPACITY(board) ((void)(board), (size_t)0)
static const struct vector zero_vector = {0,0};
static bool vectors_equal(struct vector a, struct vector b) { return a.u == b.u && a.v == b.v; }
static const float hexSizeX = 82, hexSizeY = 71, atomRadius = 29, producedAtomRadius = 15, armBaseRadius = 20;
${helpers}
${colliderBounds}
static atom mark_used_area(struct board *board, struct vector p) { (void)p; board->marks++; return board->area_atom; }
${colliderBoard}
${colliderAppend}
// Chain paths must remain unreachable, not silently succeed through reduced-layout stubs.
static uint32_t lookup_chain_atom(struct board *b, struct vector p) { (void)b; (void)p; abort(); }
static void add_chain_atom_collider(struct collider_list *l, struct board *b, size_t i, uint32_t c) { (void)l; (void)b; (void)i; (void)c; abort(); }
static void combine_chain_atoms(struct collider_list *l, size_t i) { (void)l; (void)i; abort(); }
static void mark_area_at_infinity(struct board *b, struct chain_atom_collider c) { (void)b; (void)c; abort(); }
static void resolve_chain_atom_collisions(struct collider_list *l) { (void)l; abort(); }
${collisionScheduler}
static bool run_case(int kind, size_t groups, uint64_t *checks, size_t *marks) {
    struct arm arms[2] = {{{0,0},{0,0}}, {{0,0},{0,0}}};
    struct vector positions[2] = {{0,0},{0,0}};
    struct atom_at_position atoms[2] = {{{0,0},VALID}, {{0,0},VALID}};
    struct movement movements[2] = {{.type = PIVOT_MOVEMENT, .number_of_atoms = groups == 1 ? 2 : 1},
        {.type = PIVOT_MOVEMENT, .number_of_atoms = 1}};
    struct solution solution = {0};
    struct board board = {.chain_mode = NO_CHAIN, .area_atom = REMOVED};
    if (kind == 0) { solution.number_of_arms = 2; solution.arms = arms; }
    if (kind == 1) { solution.number_of_cabinet_walls = 2; solution.cabinet_walls = positions; }
    if (kind == 2) { board.number_of_atoms_being_produced = 2; board.atoms_being_produced = positions; }
    if (kind == 3) {
        board.moving_atoms.length = 2; board.moving_atoms.atoms_at_positions = atoms;
        board.movements.length = groups; board.movements.movements = movements;
    }
    if (kind == 4) {
        solution.number_of_arms = 2; solution.arms = arms;
        arms[0].movement = arms[1].movement = (struct vector){1,0};
    }
    if (kind == 5) {
        solution.number_of_cabinet_walls = 1; solution.cabinet_walls = positions;
        board.number_of_atoms_being_produced = 1; board.atoms_being_produced = positions;
    }
    if (kind == 6 || kind == 7) {
        if (kind == 6) { solution.number_of_cabinet_walls = 1; solution.cabinet_walls = positions; }
        else { board.number_of_atoms_being_produced = 1; board.atoms_being_produced = positions; }
        board.moving_atoms.length = 1; board.moving_atoms.atoms_at_positions = atoms;
        movements[0].number_of_atoms = 1;
        board.movements.length = 1; board.movements.movements = movements;
    }
    if (kind == 8 || kind == 11) {
        solution.number_of_arms = 1; solution.arms = arms;
        board.number_of_atoms_being_produced = 1; board.atoms_being_produced = positions;
        if (kind == 11) positions[0] = (struct vector){3,0};
    }
    if (kind == 9) {
        solution.number_of_arms = 2; solution.arms = arms;
        arms[1].position = (struct vector){3,0};
    }
    if (kind == 10) {
        board.moving_atoms.length = 2; board.moving_atoms.atoms_at_positions = atoms;
        board.movements.length = 2; board.movements.movements = movements;
        atoms[1].position = (struct vector){3,0};
    }
    if (kind == 12) {
        solution.number_of_arms = 1; solution.arms = arms;
        solution.number_of_cabinet_walls = 1; solution.cabinet_walls = positions;
    }
    bool result = collision(&solution, &board, groups == 3 ? 0.25f : 0.5f, NULL);
    *checks = board.collision_checks; *marks = board.marks;
    return result;
}
static void check_pair(int first, int second, bool separated) {
    struct arm arms[2] = {0};
    struct vector walls[2] = {0}, producers[2] = {0};
    struct atom_at_position atoms[2] = {0};
    struct movement movements[2] = {0};
    struct solution solution = {.arms = arms, .cabinet_walls = walls};
    struct board board = {.chain_mode = NO_CHAIN, .area_atom = REMOVED, .atoms_being_produced = producers,
        .moving_atoms.atoms_at_positions = atoms, .movements.movements = movements};
    int categories[2] = {first, second};
    for (size_t i = 0; i < 2; ++i) {
        struct vector p = {i == 1 && separated ? 3 : 0, 0};
        switch (categories[i]) {
        case 0: case 3: {
            struct arm *arm = &arms[solution.number_of_arms++];
            arm->position = p;
            if (categories[i] == 3) arm->movement = (struct vector){1,0};
            break;
        }
        case 1: walls[solution.number_of_cabinet_walls++] = p; break;
        case 2: producers[board.number_of_atoms_being_produced++] = p; break;
        case 4:
            atoms[board.moving_atoms.length++] = (struct atom_at_position){p, VALID};
            movements[board.movements.length++] = (struct movement){.type = PIVOT_MOVEMENT, .number_of_atoms = 1};
            break;
        default: abort();
        }
    }
    // A single .75 sample puts moving bases at x=-20.5: all overlapping pairs intersect.
    bool excluded = (first == 1 || first == 2) && (second == 1 || second == 2);
    bool expected = !separated && !excluded;
    assert(collision(&solution, &board, 0.75f, NULL) == expected);
    assert(board.collision_checks == (uint64_t)(140 + expected));
    assert(board.marks == 2);
}
static void check_board_category(int category, atom area_atom, bool with_location) {
    struct arm arm = {.position = {0,0}};
    struct vector position = {0,0};
    struct atom_at_position moving_atom = {{0,0},VALID};
    struct movement movement = {.type = PIVOT_MOVEMENT, .number_of_atoms = 1};
    struct solution solution = {.arms = &arm, .cabinet_walls = &position};
    struct board board = {.chain_mode = NO_CHAIN, .area_atom = area_atom,
        .atoms_being_produced = &position, .moving_atoms.atoms_at_positions = &moving_atom,
        .movements.movements = &movement};
    switch (category) {
    case 0: solution.number_of_arms = 1; break;
    case 1: solution.number_of_cabinet_walls = 1; break;
    case 2: board.number_of_atoms_being_produced = 1; break;
    case 3: solution.number_of_arms = 1; arm.movement = (struct vector){1,0}; break;
    case 4: board.moving_atoms.length = board.movements.length = 1; break;
    default: abort();
    }
    struct vector location = {123,456};
    bool expected = !(area_atom & REMOVED);
    assert(collision(&solution, &board, 0.75f, with_location ? &location : NULL) == expected);
    assert(board.collision_checks == 70 && board.marks == 1);
    assert(location.u == (with_location && expected ? 0 : 123));
    assert(location.v == (with_location && expected ? 0 : 456));
}
static void check_board_latching(bool moving, bool multiple_samples, bool with_location) {
    struct arm arms[2] = {{{0,0},{0,0}}, {{3,0},{0,0}}};
    if (moving) arms[1].movement = (struct vector){1,0};
    struct solution solution = {.number_of_arms = 2, .arms = arms};
    struct board board = {.chain_mode = NO_CHAIN, .area_atom = VALID};
    struct vector location = {123,456};
    assert(collision(&solution, &board, multiple_samples ? 0.25f : 0.75f,
        with_location ? &location : NULL));
    assert(board.collision_checks == (uint64_t)(moving && multiple_samples ? 280 : 140));
    assert(board.marks == (size_t)(moving && multiple_samples ? 5 : 2));
    assert(location.u == (with_location ? 3 : 123));
    assert(location.v == (with_location ? 0 : 456));
}
static void check_pair_location(bool with_location, bool board_collision, bool multiple_samples) {
    struct arm arms[2] = {{{0,0},{1,0}}, {{0,0},{1,0}}};
    struct solution solution = {.number_of_arms = 2, .arms = arms};
    struct board board = {.chain_mode = NO_CHAIN, .area_atom = board_collision ? VALID : REMOVED};
    struct vector location = {123,456};
    assert(collision(&solution, &board, multiple_samples ? 0.25f : 0.75f,
        with_location ? &location : NULL));
    assert(board.collision_checks == (uint64_t)(multiple_samples ? 423 : 141));
    assert(board.marks == (size_t)(multiple_samples ? 8 : 2));
    assert(location.u == (with_location ? 0 : 123));
    assert(location.v == (with_location ? 0 : 456));
}
int main(void) {
    for (int location = 0; location < 2; ++location)
        for (int board_collision = 0; board_collision < 2; ++board_collision)
            for (int samples = 0; samples < 2; ++samples)
                check_pair_location(location, board_collision, samples);
    for (int moving = 0; moving < 2; ++moving)
        for (int samples = 0; samples < 2; ++samples)
            for (int location = 0; location < 2; ++location)
                check_board_latching(moving, samples, location);
    const atom area_atoms[] = {0, VALID, REMOVED, VALID | REMOVED};
    for (int category = 0; category < 5; ++category)
        for (size_t i = 0; i < sizeof(area_atoms) / sizeof(area_atoms[0]); ++i) {
            check_board_category(category, area_atoms[i], false);
            check_board_category(category, area_atoms[i], true);
        }
    for (int first = 0; first < 5; ++first)
        for (int second = first; second < 5; ++second) {
            check_pair(first, second, false);
            check_pair(first, second, true);
        }
    uint64_t checks; size_t marks;
    assert(run_case(0,0,&checks,&marks) && checks == 141 && marks == 2);
    assert(!run_case(1,0,&checks,&marks) && checks == 140 && marks == 2);
    assert(!run_case(2,0,&checks,&marks) && checks == 140 && marks == 2);
    assert(!run_case(3,1,&checks,&marks) && checks == 140 && marks == 2);
    assert(run_case(3,2,&checks,&marks) && checks == 141 && marks == 2);
    assert(run_case(4,0,&checks,&marks) && checks == 141 && marks == 4);
    // At x=-61.5/-41/-20.5, radius+29=49 reaches respectively 1/2/1 cells per base.
    assert(run_case(4,3,&checks,&marks) && checks == 423 && marks == 8);
    assert(!run_case(5,0,&checks,&marks) && checks == 140 && marks == 2);
    assert(run_case(6,0,&checks,&marks) && checks == 141 && marks == 2);
    assert(run_case(7,0,&checks,&marks) && checks == 141 && marks == 2);
    assert(run_case(8,0,&checks,&marks) && checks == 141 && marks == 2);
    assert(!run_case(9,0,&checks,&marks) && checks == 140 && marks == 2);
    assert(!run_case(10,0,&checks,&marks) && checks == 140 && marks == 2);
    assert(!run_case(11,0,&checks,&marks) && checks == 140 && marks == 2);
    assert(run_case(12,0,&checks,&marks) && checks == 141 && marks == 2);
    return 0;
}
`;
    fs.writeFileSync(path.join(temp, 'scheduler-fixture.c'), schedulerFixture);
    run(process.env.CC || 'cc', ['-std=c11', '-O0', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror', path.join(temp, 'scheduler-fixture.c'), '-lm', '-o', path.join(temp, 'scheduler-fixture')]);
    run(path.join(temp, 'scheduler-fixture'), []);
    console.log('Extracted C ordinary scheduler: 101 cases passed (30 category-pair controls, 15 batching/sample cases, 40 board flag/location cases, 8 board latching/last-location cases, 8 moving-prefix location cases); three-sample reset checked (configurable area stub; NO_CHAIN; reduced layout)');
    run(process.env.CC || 'cc', ['-std=c11', '-O0', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror', '-c', path.join(temp, 'oracle.c'), '-o', path.join(temp, 'oracle.o')]);
    run(process.env.CXX || 'c++', ['-std=c++20', '-O0', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror', '-Isrc', 'src/Verification/ReferenceMotion.cpp', 'tests/functional_test/ReferenceMotionDifferential.cpp', path.join(temp, 'oracle.o'), '-lm', '-o', path.join(temp, 'differential')]);
    run(path.join(temp, 'differential'), []);
    console.log(JSON.stringify({ sourceSha256: sha256, headerSha256, scope: 'ordinary atom, snap, seven-cell candidates and strict distance predicate statement extraction; extracted collider append/bounds and board predicate with hand-authored prefix/batch and REMOVED/call-order fixtures; extracted ordinary scheduler: 101 finite cases including 15 category pairs with overlapping/separated controls, three-sample reset, 40 board flag/location cases, 8 board collision-latching/last-location cases and 8 moving-prefix location cases; configurable area storage stub (zero, VALID, REMOVED, VALID|REMOVED), NO_CHAIN, optional location for board cases; authoritative movement enum; no overall scheduler equivalence, real board state or continuous area validation; reduced layout (not ABI validation); host libm; O0; FP contraction disabled' }));
} finally {
    fs.rmSync(temp, { recursive: true, force: true });
}
