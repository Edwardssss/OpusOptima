int main(void)
{
    unsigned cases = 0;
    for (unsigned start = 1; start <= 2; ++start) {
        for (overlap_mode = 0; overlap_mode <= 2; ++overlap_mode) {
            struct solution solution = {0};
            struct atom_at_position overlap = {0};
            struct board board = {0};
            board.half_cycle = start;
            board.cycle = 41;
            board.number_of_atoms_being_produced = 7;
            board.overlapped_atoms = &overlap;
            board.number_of_overlapped_atoms = overlap_mode == 1;
            event_count = 0;
            run(&solution, &board);
            size_t cursor = 0;
#define EXPECT(id) assert(cursor < event_count && events[cursor++] == phase * 100 + (id))
            for (unsigned phase = start; phase <= 2; ++phase) {
                bool before = overlap_mode == 1;
                bool after = before || (phase == 2 && overlap_mode == 2);
                if (phase == 2 && before) { EXPECT(10); }
                EXPECT(1);
                if (phase == 2 && after) { EXPECT(11); }
                for (unsigned id = 2; id <= 7; ++id) { EXPECT(id); }
                if (after) { EXPECT(8); }
                EXPECT(9);
            }
#undef EXPECT
            assert(cursor + 1 == event_count);
            assert(events[cursor] == 112);
            ++cases;
        }
    }
    printf("cycle schedule: %u bounded cases passed\n", cases);
    return 0;
}
