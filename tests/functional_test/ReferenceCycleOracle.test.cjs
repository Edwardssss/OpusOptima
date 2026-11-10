'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const test = require('node:test');

const source = process.env.OPUSOPTIMA_REFERENCE_SIM_SOURCE;
const header = process.env.OPUSOPTIMA_REFERENCE_MOTION_HEADER;
const sourceSha = 'a7e23da18c225ec24b2337ed691fcc0a468bc27c7b87d4b8dc6644c10a08491f';
const headerSha = '198a3c4a74bc94fe1595e0159881797d450f387e92598191b77cbec3e8864a22';
function lockedInput(file, expected) {
    const bytes = fs.readFileSync(file);
    assert.equal(crypto.createHash('sha256').update(bytes).digest('hex'), expected);
    return bytes.toString('utf8');
}
function extract(text, begin, end) {
    const first = text.indexOf(begin);
    const last = text.indexOf(end, first + begin.length);
    assert.ok(first >= 0 && first === text.lastIndexOf(begin));
    assert.ok(last > first && last === text.lastIndexOf(end));
    return text.slice(first, last);
}

function extractToEnd(text, begin) {
    const first = text.indexOf(begin);
    assert.ok(first >= 0 && first === text.lastIndexOf(begin));
    const result = text.slice(first);
    assert.match(result, /board->flag_reset\[board->flag_reset_length\+\+\] = a;\n}\s*$/);
    return result;
}

test('cycle oracle rejects changed inputs and ambiguous extraction boundaries', () => {
    const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'opus-cycle-lock-'));
    try {
        const file = path.join(temp, 'changed');
        fs.writeFileSync(file, 'changed');
        for (const sha of [sourceSha, headerSha]) {
            assert.throws(() => lockedInput(file, sha), { code: 'ERR_ASSERTION' });
        }
        for (const text of ['missing', 'begin begin', 'begin invalid tail']) {
            assert.throws(() => extractToEnd(text, 'begin'), { code: 'ERR_ASSERTION' });
        }
        for (const text of ['missing', 'begin begin end', 'begin end end', 'end begin']) {
            assert.throws(() => extract(text, 'begin', 'end'), { code: 'ERR_ASSERTION' });
        }
    } finally {
        fs.rmSync(temp, { recursive: true, force: true });
    }
});

test('locked producers preserve queued geometry, wheel flags and atom visibility', {
    skip: source && header ? false : 'set OPUSOPTIMA_REFERENCE_SIM_SOURCE and OPUSOPTIMA_REFERENCE_MOTION_HEADER',
}, () => {
    const text = lockedInput(source, sourceSha);
    const declarations = lockedInput(header, headerSha);
    const coordinates = extract(text, 'struct vector mechanism_relative_position(', 'struct vector position_within_mechanism(');
    const producers = extract(text, 'static void add_produced_atom_collider(', 'static struct atom_ref_at_position conversion_input(');
    const wheel = extract(text, 'static void create_wheel_atom(', 'uint8_t cabinet_for_position(');
    const stubs = fs.readFileSync(path.join(__dirname, 'ReferenceProducerOracleStubs.c'), 'utf8');
    const fixture = fs.readFileSync(path.join(__dirname, 'ReferenceProducerOracleFixture.c'), 'utf8');
    const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'opus-producer-oracle-'));
    try {
        fs.writeFileSync(path.join(temp, 'sim.h'), declarations);
        const unit = path.join(temp, 'producer.c');
        fs.writeFileSync(unit, `#include "sim.h"\n#include <assert.h>\n#include <stdio.h>\n#include <stdlib.h>\nstruct atom_ref_at_position { atom *atom; struct vector position; };\n${stubs}\n${coordinates}\n${producers}\n${wheel}\n${fixture}`);
        const executable = path.join(temp, 'producer');
        const compiled = spawnSync(process.env.CC || 'cc', [
            '-std=c11', '-O0', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=undefined', '-fno-sanitize-recover=undefined', unit, '-o', executable,
        ], { encoding: 'utf8', timeout: 10000 });
        assert.ifError(compiled.error);
        assert.equal(compiled.status, 0, compiled.stderr);
        const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 5000 });
        assert.ifError(result.error);
        assert.equal(result.status, 0, result.stderr);
        assert.match(result.stdout, /producer classification: 192 visibility cases, 258 queued atoms, wheel flags passed/);
    } finally {
        fs.rmSync(temp, { recursive: true, force: true });
    }
});

test('locked inputs clear stale blocking and insert atoms before disjoint bonds', {
    skip: source && header ? false : 'set OPUSOPTIMA_REFERENCE_SIM_SOURCE and OPUSOPTIMA_REFERENCE_MOTION_HEADER',
}, () => {
    const text = lockedInput(source, sourceSha);
    const declarations = lockedInput(header, headerSha);
    const inputs = extract(text, 'static void flag_blocked_inputs(', 'static int32_t gcd(');
    const stubs = fs.readFileSync(path.join(__dirname, 'ReferenceInputOracleStubs.c'), 'utf8');
    const fixture = fs.readFileSync(path.join(__dirname, 'ReferenceInputOracleFixture.c'), 'utf8');
    const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'opus-input-oracle-'));
    try {
        fs.writeFileSync(path.join(temp, 'sim.h'), declarations);
        const unit = path.join(temp, 'input.c');
        fs.writeFileSync(unit, `#include "sim.h"\n#include <assert.h>\n#include <stdio.h>\n${stubs}\n${inputs}\n${fixture}`);
        const executable = path.join(temp, 'input');
        const compiled = spawnSync(process.env.CC || 'cc', [
            '-std=c11', '-O0', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=undefined', '-fno-sanitize-recover=undefined', unit, '-o', executable,
        ], { encoding: 'utf8', timeout: 10000 });
        assert.ifError(compiled.error);
        assert.equal(compiled.status, 0, compiled.stderr);
        const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 5000 });
        assert.ifError(result.error);
        assert.equal(result.status, 0, result.stderr);
        assert.match(result.stdout, /input lifecycle: 1024 flag cases, 64 shared-input cases, empty inputs and atom-before-bond ordering passed/);
    } finally {
        fs.rmSync(temp, { recursive: true, force: true });
    }
});

test('locked initial setup inserts wheels before input boundary calls', {
    skip: source && header ? false : 'set OPUSOPTIMA_REFERENCE_SIM_SOURCE and OPUSOPTIMA_REFERENCE_MOTION_HEADER',
}, () => {
    const text = lockedInput(source, sourceSha);
    const declarations = lockedInput(header, headerSha);
    const coordinates = extract(text, 'struct vector mechanism_relative_position(', 'struct vector position_within_mechanism(');
    const wheel = extract(text, 'static void create_wheel_atom(', 'uint8_t cabinet_for_position(');
    const setup = extract(text, 'void initial_setup(struct solution *solution, struct board *board, uint32_t initial_board_size)\n{', 'void destroy(');
    const stubs = fs.readFileSync(path.join(__dirname, 'ReferenceSetupOracleStubs.c'), 'utf8');
    const fixture = fs.readFileSync(path.join(__dirname, 'ReferenceSetupOracleFixture.c'), 'utf8');
    const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'opus-setup-oracle-'));
    try {
        fs.writeFileSync(path.join(temp, 'sim.h'), declarations);
        const unit = path.join(temp, 'setup.c');
        fs.writeFileSync(unit, `#include "sim.h"\n#include <assert.h>\n#include <stdio.h>\n${stubs}\n${coordinates}\n${wheel}\n${setup}\n${fixture}`);
        const executable = path.join(temp, 'setup');
        const compiled = spawnSync(process.env.CC || 'cc', [
            '-std=c11', '-O0', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=undefined', '-fno-sanitize-recover=undefined', unit, '-o', executable,
        ], { encoding: 'utf8', timeout: 10000 });
        assert.ifError(compiled.error);
        assert.equal(compiled.status, 0, compiled.stderr);
        const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 5000 });
        assert.ifError(result.error);
        assert.equal(result.status, 0, result.stderr);
        assert.match(result.stdout, /initial setup: 5 table sizes, 60 wheel atoms, input boundary ordering passed/);
    } finally {
        fs.rmSync(temp, { recursive: true, force: true });
    }
});

test('locked run preserves half-cycle scheduling and completion state', {
    skip: source && header ? false : 'set OPUSOPTIMA_REFERENCE_SIM_SOURCE and OPUSOPTIMA_REFERENCE_MOTION_HEADER',
}, () => {
    const text = lockedInput(source, sourceSha);
    const declarations = lockedInput(header, headerSha);
    const run = extract(text, 'void run(struct solution *solution, struct board *board)\n{', 'struct footprint {');
    const stubs = fs.readFileSync(path.join(__dirname, 'ReferenceCycleOracleStubs.c'), 'utf8');
    const fixture = fs.readFileSync(path.join(__dirname, 'ReferenceCycleOracleFixture.c'), 'utf8');
    const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'opus-cycle-oracle-'));
    try {
        fs.writeFileSync(path.join(temp, 'sim.h'), declarations);
        const unit = path.join(temp, 'cycle.c');
        fs.writeFileSync(unit, `#include "sim.h"\n#include <assert.h>\n#include <stdio.h>\n#include <string.h>\n${stubs}\n${run}\n${fixture}`);
        const executable = path.join(temp, 'cycle');
        const compiled = spawnSync(process.env.CC || 'cc', [
            '-std=c11', '-O0', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=undefined', '-fno-sanitize-recover=undefined', unit, '-o', executable,
        ], { encoding: 'utf8', timeout: 10000 });
        assert.ifError(compiled.error);
        assert.equal(compiled.status, 0, compiled.stderr);
        const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 5000 });
        assert.ifError(result.error);
        assert.equal(result.status, 0, result.stderr);
        assert.match(result.stdout, /cycle schedule: 6 bounded cases passed/);
    } finally {
        fs.rmSync(temp, { recursive: true, force: true });
    }
});

test('locked temporary reset preserves permanent flags and clears scheduled and overlap atoms', {
    skip: source && header ? false : 'set OPUSOPTIMA_REFERENCE_SIM_SOURCE and OPUSOPTIMA_REFERENCE_MOTION_HEADER',
}, () => {
    const text = lockedInput(source, sourceSha);
    const declarations = lockedInput(header, headerSha);
    const reset = extract(text, 'static void reset_temporary_flags(struct board *board)\n{', 'static void check_completion(');
    const schedule = extractToEnd(text, 'static void schedule_flag_reset_if_needed(struct board *board, atom *a)\n{');
    const grid = extract(text, 'static struct atom_at_position *lookup_atom_at_position_in_array(', 'static void schedule_flag_reset_if_needed(struct board *board, atom *a)\n{');
    const atomRef = extract(text, 'struct atom_ref_at_position {', '// hash table functions -- see appendix.');
    const removal = extract(text, '__attribute__((noinline))\nstatic void remove_overlapping_atom(', 'static inline void transform_atom(');
    const move = extract(text, 'static size_t reserve_movement_index(struct board *board)\n{', 'static void apply_movement_to_position(');
    const disjoint = extract(text, 'static bool disjoint_bonds_equal(', 'static struct atom_at_position *lookup_atom_at_position_in_array(');
    const report = extract(text, 'static void report_collision(', 'static bool conversion_output(');
    const direction = extract(text, 'static int normalize_direction(int direction)\n{', 'atom rotate_bond_bits(') + extract(text, 'struct vector u_offset_for_direction(int direction)\n{', 'struct vector v_offset_for_direction(');
    const chainMove = extract(text, 'void move_chain_atom_to_list(struct board *board,', 'void add_chain_atom_to_table(');
    const chainLookup = extract(text, 'uint32_t lookup_chain_atom(struct board *board,', 'static bool disjoint_bonds_equal(');
    const inputs = extract(text, 'static void flag_blocked_inputs(', 'static int32_t gcd(');
    const fixture = fs.readFileSync(path.join(__dirname, 'ReferenceResetOracleFixture.c'), 'utf8');
    const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'opus-reset-oracle-'));
    try {
        fs.writeFileSync(path.join(temp, 'sim.h'), declarations);
        const unit = path.join(temp, 'reset.c');
        fs.writeFileSync(unit, `#include "sim.h"\n#include <assert.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\nstatic void rehash(struct atom_grid *, uint32_t);\nstatic void schedule_flag_reset_if_needed(struct board *, atom *);\n${grid}\n${reset}\n${schedule}\n${atomRef}\n${removal}\n${chainMove}\n${chainLookup}\nstatic const struct disjoint_bond uninitialized_disjoint_bond;\nstatic const struct disjoint_bond removed_disjoint_bond = { { 0, 1 }, { 0, 1 } };\n${disjoint}\n${report}\n${direction}\n${move}\n${inputs}\n${fixture}`);
        const executable = path.join(temp, 'reset');
        const compiled = spawnSync(process.env.CC || 'cc', [
            '-std=c11', '-O0', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=undefined', '-fno-sanitize-recover=undefined', unit, '-o', executable,
        ], { encoding: 'utf8', timeout: 10000 });
        assert.ifError(compiled.error);
        assert.equal(compiled.status, 0, compiled.stderr);
        const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 5000 });
        assert.ifError(result.error);
        assert.equal(result.status, 0, result.stderr);
        assert.match(result.stdout, /move: 128 ordinary atoms preserve buffer growth, movement indices and reinsertion passed/);
        assert.match(result.stdout, /move: real BFS follows ordinary and disjoint bonds, excludes recent bonds and detects conflicting movement passed/);
        assert.match(result.stdout, /move: three chain atoms preserve colliding-bucket and movement-list links passed/);
        assert.match(result.stdout, /move: overlap removal preserves survivor before marking final vacancy passed/);
        assert.match(result.stdout, /area: 128 array\/hash cells preserve visited and removed overlap rules passed/);
        assert.match(result.stdout, /rehash: 128 live atoms across four forced growth rounds passed/);
        assert.match(result.stdout, /removal: two interleaved three-layer stacks and removed-slot reinsertion passed/);
        assert.match(result.stdout, /input grid: 8 array\/hash, identity-order and removed-slot lifecycles passed/);
        assert.match(result.stdout, /reset: 68 atoms, duplicate scheduling and idempotence passed/);
    } finally {
        fs.rmSync(temp, { recursive: true, force: true });
    }
});

test('locked collision insertion rasterizes against the real array and hash grid', {
    skip: source && header && process.env.OPUSOPTIMA_REFERENCE_COLLISION_SOURCE ? false : 'set locked sim, header and collision sources',
}, () => {
    const text = lockedInput(source, sourceSha);
    const declarations = lockedInput(header, headerSha);
    const collision = lockedInput(process.env.OPUSOPTIMA_REFERENCE_COLLISION_SOURCE,
        'a2df55f5a12ec8a107c6408e44d7e4373447b6741a8f59259829c2f6f96558b5');
    const grid = extract(text, 'static struct atom_at_position *lookup_atom_at_position_in_array(', 'static void schedule_flag_reset_if_needed(struct board *board, atom *a)\n{');
    const reset = extract(text, 'static void reset_temporary_flags(struct board *board)\n{', 'static void check_completion(');
    const schedule = extractToEnd(text, 'static void schedule_flag_reset_if_needed(struct board *board, atom *a)\n{');
    const types = extract(collision, 'struct xy_vector {', '// based on image dimensions');
    const geometry = extract(collision, 'static struct xy_vector to_xy(', 'static float to_radians(');
    const rectangles = extract(collision, 'static struct xy_rect xy_rect_add_collider(', 'static bool xy_rect_intersects_ray(');
    const insertion = extract(collision, 'static void mark_area_and_check_board(', 'static void add_chain_atom_collider(');
    const fixture = fs.readFileSync(path.join(__dirname, 'ReferenceRasterOracleFixture.c'), 'utf8');
    const arithmetic = extract(fs.readFileSync(path.join(__dirname, 'ReferenceColliderOracleFixture.c'), 'utf8'),
        '#include <fenv.h>', 'static struct collider_list fresh(');
    const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'opus-raster-oracle-'));
    try {
        fs.writeFileSync(path.join(temp, 'sim.h'), declarations);
        const unit = path.join(temp, 'raster.c');
        fs.writeFileSync(unit, `#include "sim.h"\n#include <assert.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <math.h>\nstatic void rehash(struct atom_grid *, uint32_t);\nstatic void schedule_flag_reset_if_needed(struct board *, atom *);\n${grid}\n${reset}\n${schedule}\n${types}\nstatic const float hexSizeX = 82, hexSizeY = 71, atomRadius = 29;\n${geometry}\n${rectangles}\n${insertion}\n${arithmetic}\n${fixture}`);
        const executable = path.join(temp, 'raster');
        const compiled = spawnSync(process.env.CC || 'cc', [
            '-std=c11', '-O0', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=undefined', '-fno-sanitize-recover=undefined', unit, '-lm', '-o', executable,
        ], { encoding: 'utf8', timeout: 10000 });
        assert.ifError(compiled.error);
        assert.equal(compiled.status, 0, compiled.stderr);
        const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 5000 });
        assert.ifError(result.error);
        assert.equal(result.status, 0, result.stderr);
        assert.match(result.stdout, /raster: 3042 real-grid finite insertion cases passed/);
        for (const mode of ['--round-upward', '--round-downward', '--round-toward-zero',
            '--flush-to-zero', '--denormals-are-zero']) {
            const rejected = spawnSync(executable, [mode], { encoding: 'utf8', timeout: 5000 });
            assert.ifError(rejected.error);
            assert.doesNotMatch(rejected.stdout, /cases passed/);
            if (rejected.status === 78 && ['--flush-to-zero', '--denormals-are-zero'].includes(mode)) continue;
            assert.equal(rejected.status, 77, `${mode}: ${rejected.stderr}`);
            assert.match(rejected.stderr, /unsupported floating-point environment/);
        }
    } finally {
        fs.rmSync(temp, { recursive: true, force: true });
    }
});

test('real-grid raster evidence remains finite and non-certifying', () => {
    const evidence = require('./VerifierNumericalEvidence.json').reference_trajectory_audit.real_grid_raster_oracle;
    assert.equal(evidence.test, 'tests/functional_test/ReferenceCycleOracle.test.cjs');
    assert.equal(evidence.fixture, 'tests/functional_test/ReferenceRasterOracleFixture.c');
    assert.equal(evidence.cases, 3042);
    assert.equal(evidence.certification_eligible, false);
    assert.match(evidence.scope, /real sim grid.*UBSan.*integer squared-distance/);
    assert.match(evidence.arithmetic_environment, /binary32\/binary64.*subnormal.*FTZ\/DAZ.*toward-zero.*refuse/);
    assert.match(evidence.limitations, /not continuous motion.*game certification/);
});

test('temporary reset evidence remains bounded and non-certifying', () => {
    const evidence = require('./VerifierNumericalEvidence.json').reference_trajectory_audit.temporary_reset_oracle;
    assert.equal(evidence.source_sha256, sourceSha);
    assert.equal(evidence.header_sha256, headerSha);
    assert.equal(evidence.test, 'tests/functional_test/ReferenceCycleOracle.test.cjs');
    assert.equal(evidence.fixture, 'tests/functional_test/ReferenceResetOracleFixture.c');
    assert.equal(evidence.atoms, 68);
    assert.equal(evidence.rehash_atoms, 128);
    assert.equal(evidence.forced_rehash_rounds, 4);
    assert.equal(evidence.removal_stacks, 2);
    assert.equal(evidence.removal_stack_depth, 3);
    assert.equal(evidence.used_area_cells, 128);
    assert.equal(evidence.moving_atoms, 128);
    assert.equal(evidence.moving_overlap_layers, 2);
    assert.equal(evidence.moving_chain_atoms, 3);
    assert.equal(evidence.bfs_moved_atoms, 4);
    assert.equal(evidence.bfs_recent_bond_excluded_atoms, 1);
    assert.equal(evidence.input_grid_cases, 8);
    assert.equal(evidence.certification_eligible, false);
    assert.match(evidence.scope, /duplicate scheduling/);
    assert.match(evidence.scope, /real input.*array\/hash/);
    assert.match(evidence.limitations, /two INPUTs with two atoms each.*distinct from-position.*duplicate-key/);
    assert.match(evidence.limitations, /pointer lifetime.*grab\/drop.*game equivalence/);
});

test('producer, setup and input evidence stays hash-locked and non-certifying', () => {
    const audit = JSON.parse(fs.readFileSync(path.join(__dirname, 'VerifierNumericalEvidence.json'), 'utf8')).reference_trajectory_audit;
    const cases = [
        ['producer_oracle', 'ReferenceProducerOracleFixture.c', { visibility_cases: 192, queued_atoms: 258 }],
        ['initial_setup_oracle', 'ReferenceSetupOracleFixture.c', { table_sizes: 5, wheel_atoms: 60 }],
        ['input_lifecycle_oracle', 'ReferenceInputOracleFixture.c', { flag_cases: 1024, shared_input_cases: 64 }],
    ];
    for (const [key, fixture, counts] of cases) {
        const evidence = audit[key];
        assert.equal(evidence.source_sha256, sourceSha);
        assert.equal(evidence.header_sha256, headerSha);
        assert.equal(evidence.test, 'tests/functional_test/ReferenceCycleOracle.test.cjs');
        assert.equal(evidence.fixture, `tests/functional_test/${fixture}`);
        assert.ok(fs.existsSync(path.join(__dirname, fixture)));
        for (const [name, count] of Object.entries(counts)) assert.equal(evidence[name], count);
        assert.equal(evidence.certification_eligible, false);
        assert.match(evidence.limitations, /stubs/);
        assert.match(evidence.limitations, /metrics or game equivalence/);
    }
});

test('cycle_schedule_oracle evidence remains bounded and non-certifying', () => {
    const evidence = JSON.parse(fs.readFileSync(path.join(__dirname, 'VerifierNumericalEvidence.json'), 'utf8')).reference_trajectory_audit.cycle_schedule_oracle;
    assert.equal(evidence.source_sha256, sourceSha);
    assert.equal(evidence.header_sha256, headerSha);
    assert.equal(evidence.cases, 6);
    assert.equal(evidence.certification_eligible, false);
    assert.match(evidence.limitations, /stub/);
});
