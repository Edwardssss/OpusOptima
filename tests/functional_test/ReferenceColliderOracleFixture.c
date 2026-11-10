#include <fenv.h>
#include <float.h>
#include <string.h>
#if defined(__SSE__)
#    include <xmmintrin.h>
#endif

static bool qualified_arithmetic(void) {
#if !defined(__STDC_IEC_559__) || defined(__FAST_MATH__)
    return false;
#else
    if (FLT_RADIX != 2 || FLT_MANT_DIG != 24 || FLT_MAX_EXP != 128 || DBL_MANT_DIG != 53 ||
        DBL_MAX_EXP != 1024 || FLT_EVAL_METHOD != 0 || sizeof(float) != 4 || sizeof(double) != 8 ||
        fegetround() != FE_TONEAREST)
        return false;
#    if defined(__SSE__)
    if ((_mm_getcsr() & ((1u << 15) | (1u << 6))) != 0) return false;
#    endif
    volatile float minimum = FLT_MIN;
    volatile float half = 0.5f;
    volatile float tiny = 0x1p-149f;
    volatile float scale = 0x1p23f;
    volatile float one = 1.f;
    volatile float half_ulp = 0x1p-24f;
    volatile float three_half_ulps = 0x1.8p-23f;
    return minimum * half == 0x1p-127f && tiny * scale == FLT_MIN && one + half_ulp == 1.f &&
           one + three_half_ulps == 0x1.000004p0f && round(0.5) == 1.0 && round(-0.5) == -1.0;
#endif
}

static int configure_arithmetic_test(int argc, char** argv) {
    if (argc == 1) return 0;
    if (argc != 2) return 78;
    int mode;
    if (strcmp(argv[1], "--round-upward") == 0)
        mode = FE_UPWARD;
    else if (strcmp(argv[1], "--round-downward") == 0)
        mode = FE_DOWNWARD;
    else if (strcmp(argv[1], "--round-toward-zero") == 0)
        mode = FE_TOWARDZERO;
    else {
#if defined(__SSE__)
        if (strcmp(argv[1], "--flush-to-zero") == 0) {
            _mm_setcsr(_mm_getcsr() | (1u << 15));
            return 0;
        }
        if (strcmp(argv[1], "--denormals-are-zero") == 0) {
            _mm_setcsr(_mm_getcsr() | (1u << 6));
            return 0;
        }
#endif
        return 78;
    }
    return fesetround(mode) == 0 ? 0 : 78;
}

static struct collider_list fresh(struct collider* storage) {
    return (struct collider_list){
        .colliders = storage,
        .bounding_box = empty_rect,
        .bounding_box_up_to_cursor = empty_rect,
        .ignore_board = true,
    };
}

static void advance(struct collider_list* list) {
    list->cursor = list->length;
    list->bounding_box_up_to_cursor = list->bounding_box;
}

static bool in_query(struct vector query, struct vector cell) {
    int u = cell.u - query.u;
    int v = cell.v - query.v;
    return u >= -1 && u <= 1 && v >= -1 && v <= 1 && u + v >= -1 && u + v <= 1;
}

static void check_board_center(struct xy_vector center, size_t* contacts) {
    struct vector query = from_xy(center);
    for (int u = -2; u <= 2; ++u) {
        for (int v = -2; v <= 2; ++v) {
            struct vector cell = {query.u + u, query.v + v};
            float distance = xy_dist(center, to_xy(cell));
            for (int radius = 15; radius <= 29; radius += radius == 15 ? 5 : 9) {
                if (distance < (float)(radius + 29)) {
                    assert(in_query(query, cell));
                    ++*contacts;
                }
            }
        }
    }
}

static void check_board_coverage(void) {
    uint32_t state = 0x713582u;
    size_t contacts = 0;
    for (size_t i = 0; i < 20000; ++i) {
        state = state * 1664525u + 1013904223u;
        float x = (float)(state % 524289u) - 262144.f;
        state = state * 1664525u + 1013904223u;
        float y = (float)(state % 524289u) - 262144.f;
        check_board_center((struct xy_vector){x, y}, &contacts);
    }
    size_t boundary_centers = 0;
    const int anchors[] = {-1024, -1, 0, 1, 1024};
    const struct xy_vector offsets[] = {
        {41.f, 0.f}, {20.5f, 35.5f}, {-41.f, 0.f}, {-20.5f, -35.5f}, {0.25f, -0.75f}};
    for (size_t u = 0; u < sizeof anchors / sizeof *anchors; ++u) {
        for (size_t v = 0; v < sizeof anchors / sizeof *anchors; ++v) {
            struct xy_vector base = to_xy((struct vector){anchors[u], anchors[v]});
            for (size_t o = 0; o < sizeof offsets / sizeof *offsets; ++o) {
                float x = base.x + offsets[o].x;
                float y = base.y + offsets[o].y;
                const float xs[] = {nextafterf(x, -INFINITY), x, nextafterf(x, INFINITY)};
                const float ys[] = {nextafterf(y, -INFINITY), y, nextafterf(y, INFINITY)};
                for (size_t i = 0; i < 3; ++i) {
                    for (size_t j = 0; j < 3; ++j) {
                        check_board_center((struct xy_vector){xs[i], ys[j]}, &contacts);
                        ++boundary_centers;
                    }
                }
            }
        }
    }
    assert(boundary_centers == 1125);
    assert(contacts > 0);
    struct xy_vector center = {0, 0};
    struct vector omitted = {0, 2};
    assert(!in_query(from_xy(center), omitted));
    assert(xy_dist(center, to_xy(omitted)) < 136.f + 29.f);
    puts("board coverage regression: 20000 integer centers; 1125 boundary centers; radii 15/20/29; "
         "oversized-radius omission reproduced");
}

int main(int argc, char** argv) {
    int configured = configure_arithmetic_test(argc, argv);
    if (configured != 0) return configured;
    if (!qualified_arithmetic()) {
        fputs("unqualified floating-point environment\n", stderr);
        return 77;
    }
    puts("floating-point qualification: binary32/binary64; nearest-even; evaluation=0; gradual "
         "underflow");
    check_board_coverage();
    struct collider storage[8];
    struct board board = {0};
    struct collider_list list = fresh(storage);
    struct collider origin = {{0, 0}, 29};
    add_collider(&list, &board, origin);
    add_collider(&list, &board, origin);
    assert(list.length == 2 && list.cursor == 0 && !list.collision);
    advance(&list);
    add_collider(&list, &board, origin);
    assert(list.length == 3 && list.collision && board.collision_checks == 2);

    board = (struct board){0};
    list = fresh(storage);
    add_collider(&list, &board, (struct collider){{0, 0}, 20});
    advance(&list);
    add_collider(&list, &board, (struct collider){{200, 0}, 20});
    add_collider(&list, &board, (struct collider){{200, 0}, 15});
    assert(list.length == 3 && list.cursor == 1 && !list.collision);
    advance(&list);
    add_collider(&list, &board, (struct collider){{200, 0}, 15});
    assert(list.collision && board.collision_checks == 3);

    board = (struct board){0};
    list = fresh(storage);
    add_collider(&list, &board, origin);
    advance(&list);
    add_collider(&list, &board, (struct collider){{58, 0}, 29});
    assert(!list.collision && board.collision_checks == 1);
    add_collider(&list, &board, (struct collider){{nextafterf(58, 0), 0}, 29});
    assert(list.collision);

    board = (struct board){.response = REMOVED};
    list = fresh(storage);
    list.ignore_board = false;
    add_collider(&list, &board, origin);
    assert(board.marks == 1 && board.marked[0].u == 0 && board.marked[0].v == 0);
    assert(board.collision_checks == 70 && !list.collision && list.length == 1);
    board = (struct board){0};
    list = fresh(storage);
    list.ignore_board = false;
    struct vector location = {99, 99};
    list.collision_location = &location;
    add_collider(&list, &board, origin);
    assert(board.marks == 1 && list.collision && list.length == 1);
    assert(location.u == 0 && location.v == 0);

    board = (struct board){.response = REMOVED};
    list = fresh(storage);
    list.ignore_board = false;
    add_collider(&list, &board, (struct collider){{0, 0}, 54});
    static const struct vector expected[7] = {
        {0, 0}, {1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {1, -1},
    };
    assert(board.marks == 7 && !list.collision);
    for (size_t i = 0; i < 7; ++i)
        assert(board.marked[i].u == expected[i].u && board.marked[i].v == expected[i].v);

    for (int u = -100; u <= 100; ++u) {
        for (int v = -100; v <= 100; ++v) {
            struct vector result = from_xy(to_xy((struct vector){u, v}));
            assert(result.u == u && result.v == v);
        }
    }
    struct vector positive_tie = from_xy((struct xy_vector){41, 0});
    struct vector negative_tie = from_xy((struct xy_vector){-41, 0});
    assert(positive_tie.u == 1 && positive_tie.v == 0);
    assert(negative_tie.u == -1 && negative_tie.v == 0);

    board = (struct board){0};
    list = fresh(storage);
    list.bounding_box_unused = true;
    add_collider(&list, &board, origin);
    assert(isinf(list.bounding_box.min_x) && list.bounding_box.min_x > 0);
    assert(list.length == 1);
    puts("ordinary collider oracle: 8 scenario groups; 40401 lattice round trips; 2 signed ties "
         "passed");
    return 0;
}
