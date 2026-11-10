static atom stored;
static unsigned lookups;
struct vector mechanism_relative_position(struct mechanism m, int32_t du, int32_t dv, int32_t w)
{
    (void)m;
    assert(du == 3 && dv == -2 && w == 1);
    return (struct vector){3, -2};
}
atom *lookup_atom(struct board *board, struct vector query)
{
    (void)board;
    assert(query.u == 3 && query.v == -2);
    ++lookups;
    return &stored;
}
