static void close_to(float actual, float expected) {
    assert(fabsf(actual - expected) < 0.0001f);
}

int main(int argc, char** argv) {
    int configured = configure_arithmetic_test(argc, argv);
    if (configured) return configured;
    if (!qualified_arithmetic()) {
        fputs("outer-loop arithmetic environment unqualified\n", stderr);
        return 77;
    }
    struct board board = {0};
    struct solution solution = {0};
    board.chain_mode = DISCOVER_CHAIN;
    struct mechanism arms[2] = {0};
    struct vector walls[2] = {{0, 0}, {0, 0}};
    struct vector produced[2] = {{0, 0}, {0, 0}};
    solution.cabinet_walls = walls;
    solution.number_of_cabinet_walls = 2;
    board.atoms_being_produced = produced;
    board.number_of_atoms_being_produced = 2;
    /* Fixed walls and produced atoms do not advance the comparison prefix. */
    assert(!collision(&solution, &board, .25f, NULL));
    assert(event_count == 4);
    for (size_t i = 0; i < event_count; ++i)
        assert(events[i].cursor == 0);
    assert(events[0].collider.radius == 20);
    assert(events[2].collider.radius == 15);
    event_count = 0;
    solution.arms = arms;
    solution.number_of_arms = 2;
    assert(collision(&solution, &board, .25f, NULL));
    assert(event_count == 6);
    assert(events[0].cursor == 0 && events[1].cursor == 1);
    for (size_t i = 2; i < event_count; ++i)
        assert(events[i].cursor == 2);

    solution = (struct solution){0};
    board = (struct board){.chain_mode = DISCOVER_CHAIN};
    struct atom_at_position atoms[2] = {{.position = {10, 0}}, {.position = {10, 0}}};
    struct movement movements[2] = {
        {.base = {10, 0}, .absolute_grab_position = {10, 0}, .number_of_atoms = 2},
        {.base = {10, 0}, .absolute_grab_position = {10, 0}, .number_of_atoms = 1}};
    board.moving_atoms.atoms_at_positions = atoms;
    board.moving_atoms.length = 2;
    board.movements.movements = movements;
    board.movements.length = 1;
    event_count = 0;
    assert(!collision(&solution, &board, .25f, NULL));
    assert(event_count == 6);
    for (size_t i = 0; i < event_count; ++i) {
        assert(events[i].cursor == 0);
        assert(events[i].collider.radius == 29);
    }
    movements[0].number_of_atoms = 1;
    board.movements.length = 2;
    event_count = 0;
    assert(collision(&solution, &board, .25f, NULL));
    assert(event_count == 6);
    for (size_t i = 0; i < event_count; ++i)
        assert(events[i].cursor == i % 2);

    /* End-state track translation is reversed at .25, .5, .75 only. */
    board.movements.length = 1;
    board.moving_atoms.length = 1;
    atoms[0].position = (struct vector){1, 0};
    movements[0] = (struct movement){.type = TRACK_MOVEMENT,
                                     .base = {1, 0},
                                     .absolute_grab_position = {1, 0},
                                     .translation = {1, 0},
                                     .number_of_atoms = 1};
    arms[0] = (struct mechanism){.position = {1, 0}, .movement = {1, 0}};
    solution.arms = arms;
    solution.number_of_arms = 1;
    event_count = 0;
    assert(collision(&solution, &board, .25f, NULL));
    assert(event_count == 6);
    for (size_t i = 0; i < 3; ++i) {
        close_to(events[2 * i].collider.center.x, 20.5f * (float)(i + 1));
        close_to(events[2 * i + 1].collider.center.x, 20.5f * (float)(i + 1));
        assert(events[2 * i].cursor == 0);
        assert(events[2 * i + 1].cursor == 1);
    }
    event_count = 0;
    assert(!collision(&solution, &board, 1.f, NULL));
    assert(event_count == 0);
    /* A single atom isolates grabber swing, atom pivot and piston extension. */
    solution = (struct solution){0};
    const unsigned types[] = {SWING_MOVEMENT, PIVOT_MOVEMENT, PISTON_MOVEMENT | IS_PISTON};
    for (size_t kind = 0; kind < 3; ++kind) {
        atoms[0].position = (struct vector){1, 0};
        movements[0] = (struct movement){.type = types[kind],
                                         .grabber_offset = {1, 0},
                                         .absolute_grab_position = {1, 0},
                                         .rotation = kind < 2 ? 1 : 0,
                                         .piston_extension = 1,
                                         .number_of_atoms = 1};
        if (kind == 1) atoms[0].position = (struct vector){2, 0};
        event_count = 0;
        assert(!collision(&solution, &board, .5f, NULL));
        assert(event_count == 1 && events[0].cursor == 0);
        const float diagonal = 71.014083f;
        close_to(events[0].collider.center.x, kind == 0   ? diagonal
                                              : kind == 1 ? 82.f + diagonal
                                                          : 41.f);
        close_to(events[0].collider.center.y, kind < 2 ? -41.f : 0.f);
    }
    event_count = 0;
    assert(!collision(&solution, &board, 1.f, NULL));
    assert(event_count == 0);
    solution = (struct solution){0};
    board = (struct board){.chain_mode = DISCOVER_CHAIN};
    stationary_atom_enabled = true;
    stationary_atom = (struct atom_at_position){.position = {0, 0}, .atom = VALID};
    solution.arms = arms;
    solution.number_of_arms = 1;
    arms[0] = (struct mechanism){0};
    struct vector location = {99, 99};
    event_count = 0;
    assert(collision(&solution, &board, .5f, &location));
    assert(event_count == 1 && vectors_equal(location, stationary_atom.position));
    stationary_atom.atom = VALID | REMOVED;
    event_count = 0;
    assert(!collision(&solution, &board, .5f, NULL));
    assert(event_count == 1);
    stationary_atom.atom = VALID;
    stationary_atom.position = (struct vector){1, 0};
    event_count = 0;
    assert(!collision(&solution, &board, .5f, NULL));
    assert(event_count == 1);
    stationary_atom_enabled = false;
    solution = (struct solution){0};
    board = (struct board){.chain_mode = DISCOVER_CHAIN};
    stationary_atom_enabled = true;
    stationary_atom = (struct atom_at_position){.position = {0, 0}, .atom = VALID};
    atoms[0] = (struct atom_at_position){.position = {1, 0}};
    movements[0] = (struct movement){.type = TRACK_MOVEMENT,
                                     .base = {1, 0},
                                     .absolute_grab_position = {1, 0},
                                     .translation = {1, 0},
                                     .number_of_atoms = 1};
    board.moving_atoms.atoms_at_positions = atoms;
    board.moving_atoms.length = 1;
    board.movements.movements = movements;
    board.movements.length = 1;
    const float tangent_progress = 58.f / 82.f;
    const float boundary_progress[] = {nextafterf(tangent_progress, 0.f), tangent_progress,
                                       nextafterf(tangent_progress, 1.f)};
    for (size_t i = 0; i < 3; ++i) {
        event_count = 0;
        location = (struct vector){99, 99};
        assert(collision(&solution, &board, boundary_progress[i], &location) == (i == 0));
        assert(event_count == 1 && events[0].collider.radius == 29.f);
        const float x = events[0].collider.center.x;
        assert(i == 0 ? x < 58.f : i == 1 ? x == 58.f : x > 58.f);
        assert(
            vectors_equal(location, i == 0 ? stationary_atom.position : (struct vector){99, 99}));
    }
    stationary_atom_enabled = false;
    event_count = 0;
    assert(!collision(&solution, &board, .3f, NULL));
    assert(event_count == 3);
    float progress = .3f;
    for (size_t i = 0; i < event_count; ++i, progress += .3f) {
        assert(events[i].collider.center.x == 82.f - 82.f * (1.f - progress));
        assert(events[i].cursor == 0);
    }
    assert(progress >= 1.f);
    /* All unordered fixed-class pairs, followed by moving versus each class. */
    for (unsigned first = 0; first < 3; ++first) {
        for (unsigned second = first; second < 3; ++second) {
            solution = (struct solution){0};
            board = (struct board){.chain_mode = DISCOVER_CHAIN};
            arms[0] = arms[1] = (struct mechanism){0};
            solution.arms = arms;
            solution.cabinet_walls = walls;
            board.atoms_being_produced = produced;
            unsigned counts[3] = {0};
            ++counts[first];
            ++counts[second];
            solution.number_of_arms = counts[0];
            solution.number_of_cabinet_walls = counts[1];
            board.number_of_atoms_being_produced = counts[2];
            event_count = 0;
            assert(collision(&solution, &board, .5f, NULL) == (counts[0] != 0));
            assert(event_count == 2);
            const float radii[] = {20.f, 20.f, 15.f};
            assert(events[0].collider.radius == radii[first]);
            assert(events[1].collider.radius == radii[second]);
            assert(events[0].cursor == 0);
            assert(events[1].cursor == (first == 0 ? 1u : 0u));
        }
    }
    for (unsigned fixed = 0; fixed < 3; ++fixed) {
        solution = (struct solution){0};
        board = (struct board){.chain_mode = DISCOVER_CHAIN};
        solution.arms = arms;
        solution.cabinet_walls = walls;
        board.atoms_being_produced = produced;
        solution.number_of_arms = fixed == 0;
        solution.number_of_cabinet_walls = fixed == 1;
        board.number_of_atoms_being_produced = fixed == 2;
        atoms[0] = (struct atom_at_position){0};
        movements[0] = (struct movement){.type = TRACK_MOVEMENT, .number_of_atoms = 1};
        board.moving_atoms.atoms_at_positions = atoms;
        board.moving_atoms.length = 1;
        board.movements.movements = movements;
        board.movements.length = 1;
        event_count = 0;
        assert(collision(&solution, &board, .5f, NULL));
        assert(event_count == 2 && events[1].cursor == 1);
        assert(events[1].collider.radius == 29.f);
    }
    for (unsigned other = 0; other < 5; ++other) {
        solution = (struct solution){0};
        board = (struct board){.chain_mode = DISCOVER_CHAIN};
        arms[0] = arms[1] = (struct mechanism){0};
        solution.arms = arms;
        solution.cabinet_walls = walls;
        board.atoms_being_produced = produced;
        solution.number_of_arms = other == 0 || other == 4 ? 2 : 1;
        solution.number_of_cabinet_walls = other == 1;
        board.number_of_atoms_being_produced = other == 2;
        const unsigned moving = other == 0 ? 1 : 0;
        arms[moving].position = (struct vector){1, 0};
        arms[moving].movement = (struct vector){2, 0};
        if (other == 4) arms[1] = arms[0];
        if (other == 3) {
            atoms[0] = (struct atom_at_position){0};
            movements[0] = (struct movement){.type = TRACK_MOVEMENT, .number_of_atoms = 1};
            board.moving_atoms.atoms_at_positions = atoms;
            board.moving_atoms.length = 1;
            board.movements.movements = movements;
            board.movements.length = 1;
        }
        event_count = 0;
        assert(collision(&solution, &board, .5f, NULL));
        assert(event_count == 2 && events[0].cursor == 0 && events[1].cursor == 1);
        close_to(events[0].collider.center.x, 0.f);
        close_to(events[1].collider.center.x, 0.f);
        assert(events[other == 3 ? 0 : 1].collider.radius == 20.f);
    }
    /* These flags do not change ordinary collider geometry at this call boundary. */
    const atom classification_flags[] = {WHEEL_ATOM, GRABBED_ONCE, GRABBED, WHEEL_ATOM | GRABBED};
    for (size_t kind = 0; kind < sizeof(classification_flags) / sizeof(classification_flags[0]);
         ++kind) {
        solution = (struct solution){.arms = arms, .number_of_arms = 1};
        arms[0] = (struct mechanism){0};
        board = (struct board){.chain_mode = DISCOVER_CHAIN};
        stationary_atom_enabled = true;
        stationary_atom = (struct atom_at_position){.position = {0, 0},
                                                    .atom = VALID | classification_flags[kind]};
        event_count = 0;
        location = (struct vector){99, 99};
        assert(collision(&solution, &board, .5f, &location));
        assert(event_count == 1 && events[0].collider.radius == 20.f);
        assert(vectors_equal(location, stationary_atom.position));
        stationary_atom.atom |= REMOVED;
        event_count = 0;
        assert(!collision(&solution, &board, .5f, NULL));
        assert(event_count == 1);

        stationary_atom_enabled = false;
        atoms[0] = (struct atom_at_position){.position = {0, 0},
                                             .atom = VALID | classification_flags[kind]};
        movements[0] = (struct movement){.type = TRACK_MOVEMENT, .number_of_atoms = 1};
        board.moving_atoms.atoms_at_positions = atoms;
        board.moving_atoms.length = 1;
        board.movements.movements = movements;
        board.movements.length = 1;
        event_count = 0;
        assert(collision(&solution, &board, .5f, NULL));
        assert(event_count == 2 && events[1].cursor == 1);
        assert(events[1].collider.radius == 29.f);
        close_to(events[1].collider.center.x, 0.f);
        close_to(events[1].collider.center.y, 0.f);
    }
    puts(
        "ordinary outer-loop fixtures passed: 39 scenario groups; ordinary wheel/grabbed flag "
        "invariance; moving-base pair matrix; fixed-class pair matrix and moving/fixed pairs; "
        "fixed-prefix, movement exclusion, track reverse interpolation, moving bases, track/piston "
        "sample endpoints, swing, pivot, piston, stationary-board contact/removal/separation, "
        "strict tangency and adjacent-progress boundaries, non-divisor cadence; synthetic one-cell "
        "board; chain paths abort; no game-equivalence claim");
    return 0;
}
