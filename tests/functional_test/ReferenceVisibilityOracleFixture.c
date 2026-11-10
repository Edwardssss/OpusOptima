int main(void) {
    struct board board = {0};
    const uint32_t types[] = {ARM,       TWO_ARM, THREE_ARM,   SIX_ARM,       PISTON,
                              VAN_BERLO, RAVARI,  DUPLICATION, PROLIFERATION, CALCIFICATION};
    const atom flags[] = {VALID,
                          REMOVED,
                          MOVED,
                          WHEEL_ATOM,
                          GRABBED_ONCE,
                          1ULL << NORMAL_BOND,
                          1ULL << RECENT_BOND,
                          HAS_DISJOINT_BOND};
    unsigned cases = 0;
    for (size_t t = 0; t < sizeof(types) / sizeof(types[0]); ++t) {
        struct mechanism m = {0};
        m.type = types[t];
        for (unsigned mask = 0; mask < 256; ++mask) {
            stored = SALT;
            for (unsigned bit = 0; bit < 8; ++bit)
                if (mask & (1u << bit)) stored |= flags[bit];
            bool valid = mask & 1;
            bool removed = mask & 2;
            bool moved = mask & 4;
            bool wheel = mask & 8;
            bool arm = t < 7;
            bool wheel_reader = t >= 5 && t <= 8;
            bool visible =
                valid && ((arm && removed && moved) || (!removed && (!wheel || wheel_reader)));
            lookups = 0;
            struct atom_ref_at_position found = get_atom(&board, m, 3, -2);
            assert(lookups == 1);
            assert(found.position.u == 3 && found.position.v == -2);
            assert((*found.atom != 0) == visible);
            if (visible) assert(found.atom == &stored);
            struct atom_ref_at_position input = conversion_input(&board, m, 3, -2);
            bool blocked = mask & (16 | 32 | 64 | 128);
            assert((*input.atom != 0) == (visible && !blocked));
            assert(input.position.u == 3 && input.position.v == -2);
            assert(conversion_output(&board, m, 3, -2) == (!valid || removed || wheel));
            assert(lookups == 3);
            ++cases;
        }
    }
    printf("visibility: %u bounded cases passed\n", cases);
    return 0;
}
