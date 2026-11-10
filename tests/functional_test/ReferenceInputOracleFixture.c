int main(void) {
    struct board board = {0};
    expected_board = &board;
    struct atom_at_position atoms[] = {
        {.position = {0, 7}, .atom = SALT},
        {.position = {1, 7}, .atom = FIRE},
    };
    struct disjoint_bond bond = {.from_position = {0, 7}, .to_position = {1, 7}};
    const atom flags[] = {0,
                          VALID,
                          REMOVED,
                          VALID | REMOVED,
                          VALID | WHEEL_ATOM,
                          VALID | REMOVED | MOVED,
                          GRABBED,
                          VALID | GRABBED};
    unsigned cases = 0;
    for (unsigned type = 0; type < 8; ++type) {
        enum input_output_type io_type =
            (enum input_output_type)((type & 1 ? INPUT : 0) | (type & 2 ? SINGLE_OUTPUT : 0) |
                                     (type & 4 ? REPEATING_OUTPUT : 0));
        for (unsigned a = 0; a < 8; ++a) {
            for (unsigned b = 0; b < 8; ++b) {
                for (unsigned stale = 0; stale < 2; ++stale) {
                    occupancy[0] = flags[a];
                    occupancy[1] = flags[b];
                    struct input_output io = {
                        .type = (enum input_output_type)(io_type | (stale ? BLOCKED : 0)),
                        .atoms = atoms,
                        .number_of_atoms = 2,
                        .disjoint_bonds = &bond,
                        .number_of_disjoint_bonds = 1,
                    };
                    struct solution solution = {.inputs_and_outputs = &io,
                                                .number_of_inputs_and_outputs = 1};
                    int first_blocks = (flags[a] & VALID) && !(flags[a] & REMOVED);
                    int second_blocks = (flags[b] & VALID) && !(flags[b] & REMOVED);
                    int blocked = (io_type & INPUT) && (first_blocks || second_blocks);
                    lookups = events = 0;
                    flag_blocked_inputs(&solution, &board);
                    assert(io.type == (enum input_output_type)(io_type | (blocked ? BLOCKED : 0)));
                    assert(lookups == (io_type & INPUT ? (first_blocks ? 1u : 2u) : 0u));
                    spawn_inputs(&solution, &board);
                    assert(events == ((io_type & INPUT) && !blocked ? 3u : 0u));
                    ++cases;
                }
            }
        }
    }
    struct input_output empty = {.type = INPUT | BLOCKED};
    struct solution solution = {.inputs_and_outputs = &empty, .number_of_inputs_and_outputs = 1};
    lookups = events = 0;
    flag_blocked_inputs(&solution, &board);
    spawn_inputs(&solution, &board);
    assert(empty.type == INPUT && lookups == 0 && events == 0);
    solution.number_of_inputs_and_outputs = 0;
    flag_blocked_inputs(&solution, &board);
    spawn_inputs(&solution, &board);
    assert(lookups == 0 && events == 0);
    unsigned shared_cases = 0;
    record_insertions = 1;
    for (unsigned a = 0; a < 8; ++a) {
        for (unsigned b = 0; b < 8; ++b) {
            occupancy[0] = flags[a];
            occupancy[1] = flags[b];
            struct input_output shared[2] = {
                {.type = INPUT | BLOCKED,
                 .atoms = atoms,
                 .number_of_atoms = 2,
                 .disjoint_bonds = &bond,
                 .number_of_disjoint_bonds = 1},
                {.type = INPUT,
                 .atoms = atoms,
                 .number_of_atoms = 2,
                 .disjoint_bonds = &bond,
                 .number_of_disjoint_bonds = 1},
            };
            solution.inputs_and_outputs = shared;
            solution.number_of_inputs_and_outputs = 2;
            int first_blocks = (flags[a] & VALID) && !(flags[a] & REMOVED);
            int blocked = first_blocks || ((flags[b] & VALID) && !(flags[b] & REMOVED));
            lookups = events = 0;
            flag_blocked_inputs(&solution, &board);
            assert(shared[0].type == (INPUT | (blocked ? BLOCKED : 0)));
            assert(shared[1].type == shared[0].type);
            assert(lookups == (first_blocks ? 2u : 4u));
            spawn_inputs(&solution, &board);
            assert(events == (blocked ? 0u : 6u));
            assert(lookups == (first_blocks ? 2u : 4u));
            if (!blocked) {
                assert(occupancy[0] == (VALID | SALT));
                assert(occupancy[1] == (VALID | FIRE));
            }
            events = lookups = 0;
            flag_blocked_inputs(&solution, &board);
            assert(shared[0].type == (INPUT | BLOCKED));
            assert(shared[1].type == (INPUT | BLOCKED));
            spawn_inputs(&solution, &board);
            assert(events == 0 && lookups == (first_blocks || !blocked ? 2u : 4u));
            ++shared_cases;
        }
    }
    assert(cases == 1024 && shared_cases == 64);
    puts("input lifecycle: 1024 flag cases, 64 shared-input cases, empty inputs and "
         "atom-before-bond ordering passed");
    return 0;
}
