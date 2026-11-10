static unsigned setup_stage, setup_insertions, setup_rehash_size;
static atom setup_base;
static struct vector setup_positions[12];
static atom setup_atoms[12];
static void rehash(struct atom_grid *grid, uint32_t size)
{
    assert(grid->board && grid->board->half_cycle == 1);
    assert(grid->board->wrong_output_index == SIZE_MAX);
    assert(setup_stage == 0);
    setup_rehash_size = size;
    setup_stage = 1;
}
atom mark_used_area(struct board *board, struct vector point)
{
    (void)board; (void)point;
    assert(setup_stage == 1);
    return 0;
}
static atom mark_used_area_with_overlap(struct board *board, struct vector point, uint64_t *overlap)
{
    (void)point;
    assert(setup_stage == 1 && overlap == &board->overlap);
    return 0;
}
const struct vector *glyph_footprint(uint32_t type)
{
    (void)type;
    assert(0 && "this fixture has no glyphs");
    return 0;
}
atom *lookup_atom(struct board *board, struct vector point)
{
    (void)board; (void)point;
    assert(setup_stage == 1);
    return &setup_base;
}
void insert_atom(struct board *board, struct vector point, atom value)
{
    assert(setup_stage == 1 && board->half_cycle == 1);
    assert(!(setup_base & VISITED));
    assert(setup_insertions < 12);
    setup_positions[setup_insertions] = point;
    setup_atoms[setup_insertions++] = value;
}
static void mark_arm_area(struct solution *solution, struct board *board)
{
    (void)solution; (void)board;
    assert(setup_stage == 1 && !(setup_base & VISITED));
    setup_stage = 2;
}
static void flag_blocked_inputs(struct solution *solution, struct board *board)
{
    (void)solution; (void)board;
    assert(setup_stage == 2);
    setup_stage = 3;
}
static void spawn_inputs(struct solution *solution, struct board *board)
{
    (void)solution; (void)board;
    assert(setup_stage == 3);
    setup_stage = 4;
}
