static struct vector inserted_position;
static atom inserted_atom;
static unsigned insertions;
static atom visible_atom;

void insert_atom(struct board* board, struct vector position, atom value) {
    (void)board;
    inserted_position = position;
    inserted_atom = value;
    ++insertions;
}

atom* lookup_atom(struct board* board, struct vector position) {
    (void)board;
    (void)position;
    return &visible_atom;
}
