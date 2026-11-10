static void check_grid_rehash(void)
{
    struct board board = {0};
    board.grid.board = &board;
    atom permanent = VALID | GRABBED_ONCE | (1ULL << NORMAL_BOND);
    for (int i = 0; i < 128; ++i) {
        struct vector p = { i < 64 ? i - 32 : i + 100, i < 64 ? 0 : -100 };
        insert_atom(&board, p, permanent | BEING_DROPPED | (1ULL << RECENT_BOND));
        schedule_flag_reset_if_needed(&board, lookup_atom(&board, p));
    }
    assert(board.area == 128);
    for (int round = 0; round < 4; ++round) {
        rehash(&board.grid, board.grid.hash_capacity * 2);
        assert(board.flag_reset_length == 128);
        for (int i = 0; i < 128; ++i) {
            struct vector p = { i < 64 ? i - 32 : i + 100, i < 64 ? 0 : -100 };
            atom *current = lookup_atom(&board, p);
            unsigned matches = 0;
            for (uint32_t j = 0; j < board.flag_reset_length; ++j)
                matches += board.flag_reset[j] == current;
            assert(matches == 1);
            assert(*current == (permanent | BEING_DROPPED | (1ULL << RECENT_BOND)));
        }
    }
    reset_temporary_flags(&board);
    for (int i = 0; i < 128; ++i) {
        struct vector p = { i < 64 ? i - 32 : i + 100, i < 64 ? 0 : -100 };
        assert(*lookup_atom(&board, p) == permanent);
    }
    assert(board.flag_reset_length == 0 && board.area == 128);
    free(board.grid.atoms_at_positions);
    free(board.flag_reset);
    puts("rehash: 128 live atoms across four forced growth rounds passed");
}

static void check_removal_and_reinsertion(void)
{
    struct board board = {0};
    board.grid.board = &board;
    struct vector positions[2] = {{0, 0}, {100, -100}};
    atom values[3] = {VALID | GRABBED_ONCE, VALID | WHEEL_ATOM,
        VALID | (1ULL << NORMAL_BOND)};
    for (unsigned layer = 0; layer < 3; ++layer)
        for (unsigned i = 0; i < 2; ++i)
            insert_atom(&board, positions[i], values[layer] | BEING_DROPPED);
    assert(board.area == 2 && board.number_of_overlapped_atoms == 4);
    for (unsigned layer = 0; layer < 2; ++layer) {
        for (unsigned i = 0; i < 2; ++i) {
            atom *current = lookup_atom(&board, positions[i]);
            assert(!remove_atom(&board, (struct atom_ref_at_position){current, positions[i]}));
            assert(*current == (values[layer + 1] | BEING_DROPPED |
                (layer == 0 ? OVERLAPS_ATOMS : 0)));
            assert(board.number_of_overlapped_atoms == 3 - layer * 2 - i);
        }
    }
    reset_temporary_flags(&board);
    for (unsigned i = 0; i < 2; ++i) {
        atom *current = lookup_atom(&board, positions[i]);
        assert(*current == values[2]);
        assert(remove_atom(&board, (struct atom_ref_at_position){current, positions[i]}));
        assert(*current == (VALID | REMOVED));
        insert_atom(&board, positions[i], values[0]);
        assert(*lookup_atom(&board, positions[i]) == values[0]);
        assert(board.area == 2);
    }
    free(board.grid.atoms_at_positions);
    free(board.flag_reset);
    free(board.overlapped_atoms);
    puts("removal: two interleaved three-layer stacks and removed-slot reinsertion passed");
}

static void check_used_area(void)
{
    struct board board = {0};
    board.grid.board = &board;
    uint64_t overlap = 0;
    for (int i = 0; i < 128; ++i) {
        struct vector p = {i < 64 ? i - 32 : i + 100, i < 64 ? 0 : -100};
        assert(mark_used_area_with_overlap(&board, p, &overlap) == (VALID | REMOVED));
        assert(board.area == (uint64_t)(i + 1) && overlap == 0);
    }
    for (int i = 0; i < 128; ++i) {
        struct vector p = {i < 64 ? i - 32 : i + 100, i < 64 ? 0 : -100};
        atom value = VALID | (i & 1 ? VISITED : REMOVED);
        *lookup_atom(&board, p) = value;
        assert(mark_used_area_with_overlap(&board, p, &overlap) == value);
        assert(overlap == (uint64_t)(i / 2 + 1));
        assert(mark_used_area(&board, p) == value);
        assert(board.area == 128);
    }
    free(board.grid.atoms_at_positions);
    free(board.flag_reset);
    puts("area: 128 array/hash cells preserve visited and removed overlap rules passed");
}

static void check_move_buffer(void)
{
    struct board board = {0};
    board.grid.board = &board;
    atom value = VALID | GRABBED_ONCE | (1ULL << NORMAL_BOND);
    uint32_t chain = UINT32_MAX;
    for (unsigned i = 0; i < 128; ++i) {
        struct vector p = {i < 64 ? (int)i - 32 : (int)i + 100, i < 64 ? 0 : -100};
        insert_atom(&board, p, value);
        move_atom(&board, lookup_atom(&board, p), p, i, &chain);
        assert(board.moving_atoms.length == i + 1);
        assert(board.moving_atoms.capacity >= i + 1);
        assert(*lookup_atom(&board, p) == (VALID | REMOVED | MOVED | MOVEMENT_INDEX(i)));
        assert(chain == UINT32_MAX);
    }
    for (unsigned i = 0; i < 128; ++i) {
        struct atom_at_position moved = board.moving_atoms.atoms_at_positions[i];
        struct vector p = {i < 64 ? (int)i - 32 : (int)i + 100, i < 64 ? 0 : -100};
        assert(moved.atom == value && vectors_equal(moved.position, p));
        insert_atom(&board, p, moved.atom | BEING_DROPPED);
        schedule_flag_reset_if_needed(&board, lookup_atom(&board, p));
    }
    reset_temporary_flags(&board);
    for (unsigned i = 0; i < 128; ++i) {
        struct vector p = board.moving_atoms.atoms_at_positions[i].position;
        assert(*lookup_atom(&board, p) == value);
    }
    assert(board.area == 128);
    free(board.moving_atoms.atoms_at_positions);
    free(board.grid.atoms_at_positions);
    free(board.flag_reset);
    puts("move: 128 ordinary atoms preserve buffer growth, movement indices and reinsertion passed");
}

static void check_move_overlap(void)
{
    struct board board = {0};
    board.grid.board = &board;
    struct vector p = {0, 0};
    atom first = VALID | GRABBED_ONCE;
    atom second = VALID | WHEEL_ATOM;
    insert_atom(&board, p, first);
    insert_atom(&board, p, second);
    uint32_t chain = UINT32_MAX;
    move_atom(&board, lookup_atom(&board, p), p, 7, &chain);
    assert(board.moving_atoms.length == 1);
    assert(board.moving_atoms.atoms_at_positions[0].atom == (first | OVERLAPS_ATOMS));
    assert(*lookup_atom(&board, p) == second);
    assert(board.number_of_overlapped_atoms == 0);
    move_atom(&board, lookup_atom(&board, p), p, 8, &chain);
    assert(board.moving_atoms.length == 2);
    assert(board.moving_atoms.atoms_at_positions[1].atom == second);
    assert(*lookup_atom(&board, p) == (VALID | REMOVED | MOVED | MOVEMENT_INDEX(8)));
    assert(board.area == 1);
    free(board.moving_atoms.atoms_at_positions);
    free(board.grid.atoms_at_positions);
    free(board.flag_reset);
    free(board.overlapped_atoms);
    puts("move: overlap removal preserves survivor before marking final vacancy passed");
}

static void check_move_chain(void)
{
    struct board board = {0};
    board.grid.board = &board;
    uint32_t bucket = 0;
    struct chain_atom atoms[3] = {0};
    board.chain_atoms = atoms;
    board.chain_atom_table = &bucket;
    board.chain_atom_table_size = 1;
    for (uint32_t i = 0; i < 3; ++i) {
        atoms[i].current_position = (struct vector){(int)i, 0};
        atoms[i].prev_in_list = i ? &atoms[i - 1].next_in_list : &bucket;
        atoms[i].next_in_list = i == 2 ? UINT32_MAX : i + 1;
        insert_atom(&board, atoms[i].current_position, VALID | IS_CHAIN_ATOM);
    }
    uint32_t moving = UINT32_MAX;
    const uint32_t order[3] = {1, 0, 2};
    for (unsigned step = 0; step < 3; ++step) {
        uint32_t i = order[step];
        struct vector p = atoms[i].current_position;
        assert(lookup_chain_atom(&board, p) == i);
        move_atom(&board, lookup_atom(&board, p), p, step, &moving);
        assert(moving == i && atoms[i].prev_in_list == &moving);
        assert(lookup_chain_atom(&board, p) == UINT32_MAX);
        assert(board.moving_atoms.atoms_at_positions[step].atom == (VALID | IS_CHAIN_ATOM));
        assert(*lookup_atom(&board, p) == (VALID | REMOVED | MOVED | MOVEMENT_INDEX(step)));
        if (step)
            assert(atoms[order[step - 1]].prev_in_list == &atoms[i].next_in_list);
    }
    assert(bucket == UINT32_MAX && board.area == 3);
    assert(atoms[2].next_in_list == 0 && atoms[0].next_in_list == 1);
    assert(atoms[1].next_in_list == UINT32_MAX);
    move_chain_atom_to_list(&board, 0, NULL);
    assert(atoms[2].next_in_list == 1 && atoms[1].prev_in_list == &atoms[2].next_in_list);
    assert(!atoms[0].prev_in_list && atoms[0].next_in_list == UINT32_MAX);
    free(board.moving_atoms.atoms_at_positions);
    free(board.grid.atoms_at_positions);
    free(board.flag_reset);
    puts("move: three chain atoms preserve colliding-bucket and movement-list links passed");
}

static void check_move_bfs(void)
{
    struct board board = {0};
    board.grid.board = &board;
    struct vector positions[] = {{0, 0}, {1, 0}, {2, 0}, {8, 8}, {0, 1}};
    insert_atom(&board, positions[0], VALID | (1ULL << NORMAL_BOND) | (RECENT_BONDS & (BOND_LOW_BITS << 1)));
    insert_atom(&board, positions[1], VALID | (1ULL << (NORMAL_BOND + 3)) | (1ULL << NORMAL_BOND));
    insert_atom(&board, positions[2], VALID | (1ULL << (NORMAL_BOND + 3)) | HAS_DISJOINT_BOND);
    insert_atom(&board, positions[3], VALID | HAS_DISJOINT_BOND);
    insert_atom(&board, positions[4], VALID);
    insert_disjoint_bond(&board.disjoint_bond_table, (struct disjoint_bond){positions[2], positions[3]});
    insert_disjoint_bond(&board.disjoint_bond_table, (struct disjoint_bond){positions[3], positions[2]});
    struct movement movement = {.absolute_grab_position = {0, 0}, .translation = {1, 0}};
    move_atoms(&board, lookup_atom(&board, positions[0]), movement);
    assert(!board.collision && board.movements.length == 1);
    assert(board.moving_atoms.cursor == 4 && board.moving_atoms.length == 4);
    struct movement *stored = &board.movements.movements[0];
    assert(stored->number_of_atoms == 4 && stored->first_atom_index == 0);
    assert(stored->first_chain_atom == UINT32_MAX);
    for (unsigned i = 0; i < 4; ++i) {
        assert(vectors_equal(board.moving_atoms.atoms_at_positions[i].position, positions[i]));
        assert(*lookup_atom(&board, positions[i]) == (VALID | REMOVED | MOVED));
    }
    assert(*lookup_atom(&board, positions[4]) == VALID);
    assert(vectors_equal(lookup_disjoint_bond(&stored->disjoint_bonds, positions[2]).to_position, positions[3]));
    assert(vectors_equal(lookup_disjoint_bond(&stored->disjoint_bonds, positions[3]).to_position, positions[2]));
    assert(disjoint_bonds_equal(lookup_disjoint_bond(&board.disjoint_bond_table, positions[2]), uninitialized_disjoint_bond));
    move_atoms(&board, lookup_atom(&board, positions[0]), movement);
    assert(!board.collision && board.movements.length == 1 && board.moving_atoms.length == 4);
    movement.translation = (struct vector){0, 1};
    move_atoms(&board, lookup_atom(&board, positions[0]), movement);
    assert(board.collision && board.movements.length == 1 && board.moving_atoms.length == 4);
    assert(strcmp(board.collision_reason, "atom moved in two directions simultaneously") == 0);
    assert(vectors_equal(board.collision_location, positions[0]));
    report_collision(&board, positions[3], "later collision");
    assert(vectors_equal(board.collision_location, positions[0]));
    assert(strcmp(board.collision_reason, "atom moved in two directions simultaneously") == 0);
    struct movement left = {.type = PIVOT_MOVEMENT, .translation = {1, 0}};
    struct movement right = {.type = PIVOT_MOVEMENT, .translation = {1, 0}, .absolute_grab_position = {9, 9}};
    assert(movements_equal(left, right));
    left.rotation = 1;
    assert(!movements_equal(left, right));
    right.rotation = 1;
    assert(!movements_equal(left, right));
    right.absolute_grab_position = left.absolute_grab_position;
    assert(movements_equal(left, right));
    left.type = right.type = SWING_MOVEMENT;
    right.base = (struct vector){1, 0};
    assert(!movements_equal(left, right));
    right.base = left.base;
    right.absolute_grab_position = (struct vector){9, 9};
    assert(movements_equal(left, right));
    free(stored->disjoint_bonds.bonds);
    free(board.disjoint_bond_table.bonds);
    free(board.movements.movements);
    free(board.moving_atoms.atoms_at_positions);
    free(board.grid.atoms_at_positions);
    free(board.flag_reset);
    puts("move: real BFS follows ordinary and disjoint bonds, excludes recent bonds and detects conflicting movement passed");
}

static void check_input_grid_lifecycle(void)
{
    unsigned cases = 0;
    for (unsigned hashed = 0; hashed < 2; ++hashed) {
        for (unsigned reverse = 0; reverse < 2; ++reverse) {
            for (unsigned removed = 0; removed < 2; ++removed) {
                struct board board = {0};
                board.grid.board = &board;
                struct vector p = {hashed ? 200 : 0, 7};
                struct vector q = {p.u + 1, p.v};
                struct atom_at_position atoms[2][2] = {
                    {{.position = p, .atom = SALT}, {.position = q, .atom = FIRE}},
                    {{.position = p, .atom = WATER}, {.position = q, .atom = EARTH}},
                };
                struct disjoint_bond bonds[2] = {
                    {.from_position = p, .to_position = q},
                    {.from_position = q, .to_position = p},
                };
                struct input_output ios[2] = {0};
                for (unsigned i = 0; i < 2; ++i) {
                    unsigned identity = i ^ reverse;
                    ios[i] = (struct input_output){.type = INPUT | BLOCKED,
                        .atoms = atoms[identity], .number_of_atoms = 2,
                        .disjoint_bonds = &bonds[identity], .number_of_disjoint_bonds = 1};
                }
                struct solution solution = {.inputs_and_outputs = ios, .number_of_inputs_and_outputs = 2};
                if (removed) {
                    insert_atom(&board, p, VALID | REMOVED);
                    insert_atom(&board, q, VALID | REMOVED);
                }
                flag_blocked_inputs(&solution, &board);
                assert(ios[0].type == INPUT && ios[1].type == INPUT);
                spawn_inputs(&solution, &board);
                assert(board.area == 2 && board.number_of_overlapped_atoms == 2);
                assert(*lookup_atom(&board, p) == (VALID | atoms[reverse][0].atom | OVERLAPS_ATOMS));
                assert(*lookup_atom(&board, q) == (VALID | atoms[reverse][1].atom | OVERLAPS_ATOMS));
                assert(board.overlapped_atoms[0].atom == (VALID | atoms[1 ^ reverse][0].atom));
                assert(board.overlapped_atoms[1].atom == (VALID | atoms[1 ^ reverse][1].atom));
                assert(vectors_equal(board.overlapped_atoms[0].position, p));
                assert(vectors_equal(board.overlapped_atoms[1].position, q));
                assert(disjoint_bonds_equal(lookup_disjoint_bond(&board.disjoint_bond_table, p), bonds[0]));
                assert(disjoint_bonds_equal(lookup_disjoint_bond(&board.disjoint_bond_table, q), bonds[1]));
                flag_blocked_inputs(&solution, &board);
                assert((ios[0].type & BLOCKED) && (ios[1].type & BLOCKED));
                spawn_inputs(&solution, &board);
                assert(board.area == 2 && board.number_of_overlapped_atoms == 2);
                assert(!remove_atom(&board, (struct atom_ref_at_position){lookup_atom(&board, p), p}));
                assert(!remove_atom(&board, (struct atom_ref_at_position){lookup_atom(&board, q), q}));
                assert(*lookup_atom(&board, p) == (VALID | atoms[1 ^ reverse][0].atom));
                assert(*lookup_atom(&board, q) == (VALID | atoms[1 ^ reverse][1].atom));
                flag_blocked_inputs(&solution, &board);
                assert((ios[0].type & BLOCKED) && (ios[1].type & BLOCKED));
                assert(remove_atom(&board, (struct atom_ref_at_position){lookup_atom(&board, p), p}));
                assert(remove_atom(&board, (struct atom_ref_at_position){lookup_atom(&board, q), q}));
                flag_blocked_inputs(&solution, &board);
                assert(ios[0].type == INPUT && ios[1].type == INPUT);
                spawn_inputs(&solution, &board);
                assert(board.area == 2 && board.number_of_overlapped_atoms == 2);
                free(board.grid.atoms_at_positions);
                free(board.overlapped_atoms);
                free(board.flag_reset);
                free(board.disjoint_bond_table.bonds);
                ++cases;
            }
        }
    }
    assert(cases == 8);
    puts("input grid: 8 array/hash, identity-order and removed-slot lifecycles passed");
}

int main(void)
{
    check_input_grid_lifecycle();
    check_move_bfs();
    check_move_chain();
    check_move_overlap();
    check_move_buffer();
    check_used_area();
    check_grid_rehash();
    check_removal_and_reinsertion();
    struct board board = {0};
    atom permanent = VALID | GRABBED_ONCE | WHEEL_ATOM | (1ULL << NORMAL_BOND);
    atom ordinary[64];
    for (unsigned i = 0; i < 64; ++i) {
        ordinary[i] = permanent | ((i & 1) ? BEING_DROPPED : 0) |
            ((i & 2) ? (1ULL << RECENT_BOND) : 0);
        uint32_t before = board.flag_reset_length;
        schedule_flag_reset_if_needed(&board, &ordinary[i]);
        assert(board.flag_reset_length == before + ((i & 3) != 0));
        if (i & 3)
            assert(board.flag_reset[before] == &ordinary[i]);
    }
    assert(board.flag_reset_capacity >= board.flag_reset_length);
    uint32_t length = board.flag_reset_length;
    schedule_flag_reset_if_needed(&board, &ordinary[3]);
    assert(board.flag_reset_length == length + 1);
    struct atom_at_position overlap[4] = {0};
    for (unsigned i = 0; i < 4; ++i)
        overlap[i].atom = permanent | ((i & 1) ? BEING_DROPPED : 0) |
            ((i & 2) ? (1ULL << RECENT_BOND) : 0);
    board.overlapped_atoms = overlap;
    board.number_of_overlapped_atoms = 4;
    reset_temporary_flags(&board);
    assert(board.flag_reset_length == 0);
    for (unsigned i = 0; i < 64; ++i)
        assert(ordinary[i] == permanent);
    for (unsigned i = 0; i < 4; ++i)
        assert(overlap[i].atom == permanent);
    reset_temporary_flags(&board);
    assert(board.flag_reset_length == 0);
    free(board.flag_reset);
    puts("reset: 68 atoms, duplicate scheduling and idempotence passed");
    return 0;
}
