static void check_raster(void)
{
    const float radii[] = {15, 20, 29};
    const struct vector origins[] = {{0, 0}, {100, -100}};
    unsigned cases = 0;
    for (unsigned origin = 0; origin < 2; ++origin)
        for (unsigned radius = 0; radius < 3; ++radius)
            for (int x = -60; x <= 60; x += 10)
                for (int y = -60; y <= 60; y += 10)
                    for (unsigned occupied = 0; occupied < 3; ++occupied) {
                        struct board board = {0};
                        board.grid.board = &board;
                        struct vector p = origins[origin];
                        if (occupied)
                            insert_atom(&board, p, VALID | (occupied == 2 ? REMOVED : 0));
                        struct xy_vector center = to_xy(p);
                        struct collider collider = {{center.x + x, center.y + y}, radii[radius]};
                        struct collider storage[1];
                        struct vector location = {999, 999};
                        struct collider_list list = {.colliders = storage,
                            .bounding_box = empty_rect, .bounding_box_up_to_cursor = empty_rect,
                            .collision_location = &location};
                        add_collider(&list, &board, collider);
                        unsigned expected = 0;
                        bool hit = false;
                        for (int u = -4; u <= 4; ++u)
                            for (int v = -4; v <= 4; ++v) {
                                struct vector cell = {p.u + u, p.v + v};
                                // Integer centers make this an exact squared-distance predicate.
                                int dx = x - 82 * u - 41 * v;
                                int dy = y - 71 * v;
                                int reach = (int)radii[radius] + 29;
                                bool marked = dx * dx + dy * dy < reach * reach;
                                bool original = occupied && u == 0 && v == 0;
                                atom value = *lookup_atom(&board, cell);
                                assert(!!(value & VALID) == (marked || original));
                                expected += marked || original;
                                hit |= marked && occupied == 1 && u == 0 && v == 0;
                            }
                        assert(board.area == expected);
                        assert(list.collision == hit);
                        if (hit)
                            assert(location.u == p.u && location.v == p.v);
                        else
                            assert(location.u == 999 && location.v == 999);
                        assert(board.collision_checks == 70 && list.length == 1);
                        reset_temporary_flags(&board);
                        free(board.grid.atoms_at_positions);
                        free(board.flag_reset);
                        ++cases;
                    }
    assert(cases == 3042);
    puts("raster: 3042 real-grid finite insertion cases passed");
}

int main(int argc, char **argv)
{
    int configured = configure_arithmetic_test(argc, argv);
    if (configured) return configured;
    if (!qualified_arithmetic()) {
        fputs("unsupported floating-point environment\n", stderr);
        return 77;
    }
    check_raster();
    return 0;
}
