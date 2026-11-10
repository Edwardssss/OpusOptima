#ifndef REAL_COLLISION
static unsigned motion_collision_calls;
static bool collision(struct solution* solution, struct board* board, float increment,
                      struct vector* location) {
    (void)solution;
    (void)location;
    assert(increment > 0 && increment <= 0.125f);
    assert(board->movements.length > 0);
    ++motion_collision_calls;
    return false;
}
#endif

static void run_motion_case(uint32_t type, char instruction, bool blocked, bool bonded) {
    struct mechanism arm = {.type = type, .direction_u = {1, 0}, .direction_v = {0, 1}};
    char tape_data[] = {'f', instruction, 'r'};
    char* tape = tape_data;
    size_t length = sizeof tape_data, halt = SIZE_MAX;
    int64_t start = 0;
    struct solution solution = {
        .arms = &arm,
        .number_of_arms = 1,
        .arm_tape = &tape,
        .arm_tape_length = &length,
        .arm_tape_start_cycle = &start,
        .arm_tape_halt_index = &halt,
    };
    struct vector wall = {0, 1};
    if (blocked) {
        solution.cabinet_walls = &wall;
        solution.number_of_cabinet_walls = 1;
    }
    struct board board = {0};
    board.grid.board = &board;
    if (type & ANY_WHEEL) {
        arm.type |= GRABBING | GRABBING_EVERYTHING;
        create_wheel_atom(&board, arm, 1, 0, SALT);
        create_wheel_atom(&board, arm, 0, 1, WATER);
        create_wheel_atom(&board, arm, -1, 1, AIR);
        create_wheel_atom(&board, arm, -1, 0, SALT);
        create_wheel_atom(&board, arm, 0, -1, FIRE);
        create_wheel_atom(&board, arm, 1, -1, EARTH);
    } else {
        insert_atom(&board, (struct vector){1, 0},
                    VALID | SALT | (bonded ? (1ULL << NORMAL_BOND) : 0));
        if (bonded) {
            assert(type == PISTON && instruction == 'w' && !blocked);
            insert_atom(&board, (struct vector){2, 0}, VALID | WATER | (1ULL << (NORMAL_BOND + 3)));
            insert_atom(&board, (struct vector){8, 8}, VALID | FIRE);
        }
    }
    board.half_cycle = 1;
    perform_arm_instructions(&solution, &board);
    assert(arm.type & GRABBING);
    assert(*lookup_atom(&board, (struct vector){1, 0}) & GRABBED);
    board.cycle = 1;
    perform_arm_instructions(&solution, &board);
    assert(board.movements.length == 0);
#ifndef REAL_COLLISION
    unsigned before = motion_collision_calls;
#endif
    board.half_cycle = 2;
    perform_arm_instructions(&solution, &board);
    assert(board.collision == blocked);
    if (blocked) {
        destroy(NULL, &board);
        return;
    }
#ifndef REAL_COLLISION
    assert(motion_collision_calls == before + 1);
#endif
    assert(board.movements.length == ((type & ANY_WHEEL) ? 6u : 1u));
    if (bonded) {
        assert(board.moving_atoms.length == 2);
        assert(board.movements.movements[0].number_of_atoms == 2);
        atom companion = *lookup_atom(&board, (struct vector){3, 0});
        assert((companion & (VALID | REMOVED | WATER | NORMAL_BONDS | GRABBED)) ==
               (VALID | WATER | (1ULL << (NORMAL_BOND + 3))));
        assert(*lookup_atom(&board, (struct vector){8, 8}) == (VALID | FIRE));
    }
    struct vector expected = instruction == 'a'   ? (struct vector){0, 1}
                             : instruction == 'd' ? (struct vector){1, -1}
                                                  : (struct vector){2, 0};
    atom moved = *lookup_atom(&board, expected);
    assert((moved & (VALID | REMOVED | SALT | GRABBED)) == (VALID | SALT | GRABBED_ONCE));
    if (bonded) assert((moved & NORMAL_BONDS) == (1ULL << NORMAL_BOND));
    assert(((moved & WHEEL_ATOM) != 0) == ((type & ANY_WHEEL) != 0));
    if (!(type & ANY_WHEEL)) assert(*lookup_atom(&board, (struct vector){1, 0}) & REMOVED);
    board.cycle = 2;
    board.half_cycle = 1;
    perform_arm_instructions(&solution, &board);
    moved = *lookup_atom(&board, expected);
    if (type & ANY_WHEEL) {
        assert(arm.type & GRABBING);
        assert(moved & GRABBED);
    } else {
        assert(!(arm.type & GRABBING));
        assert(!(moved & GRABBED));
        assert(moved & BEING_DROPPED);
        reset_temporary_flags(&board);
        assert(!(*lookup_atom(&board, expected) & BEING_DROPPED));
        if (bonded) {
            assert((*lookup_atom(&board, expected) & (VALID | SALT | NORMAL_BONDS | GRABBED)) ==
                   (VALID | SALT | (1ULL << NORMAL_BOND)));
            assert((*lookup_atom(&board, (struct vector){3, 0}) &
                    (VALID | REMOVED | WATER | NORMAL_BONDS | GRABBED | BEING_DROPPED)) ==
                   (VALID | WATER | (1ULL << (NORMAL_BOND + 3))));
            assert(*lookup_atom(&board, (struct vector){8, 8}) == (VALID | FIRE));
        }
    }
    destroy(NULL, &board);
}

int main(void) {
    run_motion_case(ARM, 'a', false, false);
    run_motion_case(ARM, 'd', false, false);
    run_motion_case(PISTON, 'w', false, false);
    run_motion_case(VAN_BERLO, 'a', false, false);
    run_motion_case(VAN_BERLO, 'd', false, false);
    run_motion_case(PISTON, 'w', false, true);
#ifdef REAL_COLLISION
    run_motion_case(ARM, 'a', true, false);
#endif
#ifndef REAL_COLLISION
    assert(motion_collision_calls == 6);
    puts("instruction motion: 6 grab/swing/piston/wheel/drop lifecycles including bonded piston "
         "passed (collision stub)");
#else
    puts("instruction motion: 6 grab/swing/piston/wheel/drop lifecycles including bonded piston "
         "and 1 blocked swing passed (locked collision)");
#endif
    return 0;
}
