int main(void) {
    const uint32_t sizes[] = {0, 8, 80, 800000, UINT32_MAX};
    const uint32_t expected_sizes[] = {1, 1, 10, 99999, 99999};
    const atom elements[] = {SALT, WATER, AIR,  SALT,   FIRE,   EARTH,
                             TIN,  LEAD,  GOLD, SILVER, COPPER, IRON};
    const struct vector offsets[] = {{1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {1, -1}};
    for (unsigned test = 0; test < 5; ++test) {
        struct mechanism arms[] = {
            {.type = VAN_BERLO,
             .position = {17, -9},
             .direction_u = {0, 1},
             .direction_v = {-1, 1}},
            {.type = RAVARI, .position = {-4, 3}, .direction_u = {1, 0}, .direction_v = {0, 1}},
            {.type = ARM, .position = {5, 5}, .direction_u = {1, 0}, .direction_v = {0, 1}},
        };
        struct solution solution = {.arms = arms, .number_of_arms = 3, .track_overlap = 7};
        struct board board = {0};
        setup_stage = setup_insertions = setup_rehash_size = 0;
        setup_base = VALID;
        initial_setup(&solution, &board, sizes[test]);
        assert(setup_stage == 4 && setup_insertions == 12);
        assert(setup_rehash_size == expected_sizes[test]);
        assert(board.overlap == 7 && board.grid.board == &board);
        for (unsigned i = 0; i < 12; ++i) {
            unsigned wheel = i / 6;
            struct vector p = offsets[i % 6];
            int32_t u = wheel ? -4 + p.u : 17 - p.v;
            int32_t v = wheel ? 3 + p.v : -9 + p.u + p.v;
            assert(setup_positions[i].u == u && setup_positions[i].v == v);
            assert(setup_atoms[i] == (VALID | GRABBED_ONCE | WHEEL_ATOM | elements[i]));
        }
        assert((arms[0].type & (GRABBING | GRABBING_EVERYTHING)) ==
               (GRABBING | GRABBING_EVERYTHING));
        assert((arms[1].type & (GRABBING | GRABBING_EVERYTHING)) ==
               (GRABBING | GRABBING_EVERYTHING));
        assert(arms[2].type == ARM);
    }
    puts("initial setup: 5 table sizes, 60 wheel atoms, input boundary ordering passed");
    return 0;
}
