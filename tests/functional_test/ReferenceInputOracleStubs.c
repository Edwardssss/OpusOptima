static atom occupancy[2];
static unsigned lookups, events;
static int record_insertions;
static struct board *expected_board;
atom *lookup_atom(struct board *board, struct vector p)
{
    assert(board == expected_board && p.u >= 0 && p.u < 2 && p.v == 7);
    ++lookups;
    return &occupancy[p.u];
}
void insert_atom(struct board *board, struct vector p, atom a)
{
    unsigned index = events % 3;
    assert(board == expected_board && index < 2);
    assert(p.u == (int32_t)index && p.v == 7);
    assert(a == (VALID | (index == 0 ? SALT : FIRE)));
    if (record_insertions) occupancy[p.u] = a;
    ++events;
}
static void insert_disjoint_bond(struct disjoint_bond_table *table, struct disjoint_bond bond)
{
    assert(table == &expected_board->disjoint_bond_table && events % 3 == 2);
    assert(bond.from_position.u == 0 && bond.from_position.v == 7);
    assert(bond.to_position.u == 1 && bond.to_position.v == 7);
    ++events;
}
