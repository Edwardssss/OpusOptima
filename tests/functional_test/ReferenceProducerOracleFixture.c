int main(void) {
    struct board board = {0};
    struct mechanism m = {
        .position = {17, -9},
        .direction_u = {0, 1},
        .direction_v = {-1, 1},
    };
    board.half_cycle = 1;
    unsigned expected_capacity = 0;
    for (int i = 0; i < 257; ++i) {
        if ((unsigned)i >= expected_capacity) expected_capacity = (expected_capacity + 3) * 4 / 3;
        produce_atom(&board, m, i, -i, SALT);
        assert(board.atoms_being_produced_capacity == expected_capacity);
        assert(insertions == 0);
        assert(board.number_of_atoms_being_produced == (uint32_t)i + 1);
        assert(board.atoms_being_produced_capacity >= board.number_of_atoms_being_produced);
        assert(board.atoms_being_produced[i].u == 17 + i);
        assert(board.atoms_being_produced[i].v == -9);
    }
    produce_atom(&board, m, 0, 0, SALT);
    assert(board.number_of_atoms_being_produced == 258 && insertions == 0);
    assert(board.atoms_being_produced[257].u == board.atoms_being_produced[0].u);
    assert(board.atoms_being_produced[257].v == board.atoms_being_produced[0].v);
    board.half_cycle = 2;
    produce_atom(&board, m, 3, -2, WATER);
    assert(insertions == 1 && inserted_atom == (VALID | WATER));
    assert(inserted_position.u == 19 && inserted_position.v == -8);
    assert(board.number_of_atoms_being_produced == 258);
    create_wheel_atom(&board, m, 1, -1, FIRE);
    assert(insertions == 2);
    assert(inserted_atom == (VALID | GRABBED_ONCE | WHEEL_ATOM | FIRE));
    assert(inserted_position.u == 18 && inserted_position.v == -9);

    const uint32_t types[] = {ARM, DUPLICATION, PROLIFERATION, VAN_BERLO, RAVARI, 0};
    unsigned cases = 0;
    for (unsigned t = 0; t < sizeof(types) / sizeof(types[0]); ++t) {
        m.type = types[t];
        for (unsigned bits = 0; bits < 32; ++bits) {
            visible_atom = SALT;
            if (bits & 1) visible_atom |= VALID;
            if (bits & 2) visible_atom |= REMOVED;
            if (bits & 4) visible_atom |= MOVED;
            if (bits & 8) visible_atom |= WHEEL_ATOM;
            if (bits & 16) visible_atom |= GRABBED;
            bool expected = (bits & 1) != 0;
            if (expected && (bits & 2))
                expected = (t == 0 || t == 3 || t == 4) && (bits & 4);
            else if (expected && (bits & 8))
                expected = t >= 1 && t <= 4;
            struct atom_ref_at_position result = get_atom(&board, m, 3, -2);
            assert((*result.atom != 0) == expected);
            if (expected)
                assert(result.atom == &visible_atom && *result.atom == visible_atom);
            else
                assert(result.atom != &visible_atom);
            assert(result.position.u == 19 && result.position.v == -8);
            ++cases;
        }
    }
    free(board.atoms_being_produced);
    printf("producer classification: %u visibility cases, 258 queued atoms, wheel flags passed\n",
           cases);
    return 0;
}
