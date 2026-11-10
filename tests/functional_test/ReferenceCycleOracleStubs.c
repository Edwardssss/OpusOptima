static unsigned events[64];
static size_t event_count;
static unsigned overlap_mode;
static void event(struct board *board, unsigned id)
{
    assert(event_count < sizeof(events) / sizeof(events[0]));
    events[event_count++] = board->half_cycle * 100 + id;
}
#define PHASE_STUB(name, id) \
    static void name(struct solution *solution, struct board *board) \
    { (void)solution; event(board, id); }
PHASE_STUB(mark_arm_area, 2)
PHASE_STUB(fill_conduits, 3)
PHASE_STUB(flag_blocked_inputs, 4)
PHASE_STUB(spawn_inputs, 5)
PHASE_STUB(apply_glyphs, 7)
PHASE_STUB(consume_outputs, 9)
#undef PHASE_STUB
static void perform_arm_instructions(struct solution *solution, struct board *board)
{
    (void)solution;
    event(board, 1);
    if (board->half_cycle == 2 && overlap_mode == 2)
        board->number_of_overlapped_atoms = 1;
}
static void reset_temporary_flags(struct board *board) { event(board, 6); }
static void raise_overlapped_atoms(struct board *board) { event(board, 8); }
static void report_collision(struct board *board, struct vector position, const char *reason)
{
    (void)position;
    event(board, strstr(reason, "before") ? 10 : 11);
}
static void check_completion(struct solution *solution, struct board *board)
{
    (void)solution;
    assert(board->half_cycle == 1);
    assert(board->cycle == 42);
    assert(board->number_of_atoms_being_produced == 0);
    event(board, 12);
}
