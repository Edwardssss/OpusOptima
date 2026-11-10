static int hex_distance(struct vector p) {
    return (abs(p.u) + abs(p.v) + abs(p.u + p.v)) / 2;
}

static size_t check_bond_rotation(void) {
    const int groups[] = {RECENT_BOND, NORMAL_BOND, TRIPLEX_BOND_R, TRIPLEX_BOND_Y, TRIPLEX_BOND_K};
    size_t count = 0;
    for (size_t group = 0; group < sizeof(groups) / sizeof(groups[0]); ++group) {
        for (unsigned pattern = 0; pattern < 64; ++pattern) {
            for (int rotation = -10; rotation <= 10; ++rotation) {
                atom original = ~ALL_BONDS | ((atom)pattern << groups[group]);
                atom expected = ~ALL_BONDS;
                for (int direction = 0; direction < 6; ++direction) {
                    if (pattern & (1u << direction)) {
                        int rotated = (direction + rotation) % 6;
                        if (rotated < 0) rotated += 6;
                        expected |= 1ULL << (groups[group] + rotated);
                    }
                }
                atom actual = original;
                rotate_bonds(&actual, rotation);
                assert(actual == expected);
                rotate_bonds(&actual, -rotation);
                assert(actual == original);
                ++count;
            }
        }
    }
    assert(count == 6720);
    return count;
}

int main(void) {
    assert(check_bond_rotation() == 6720);
    puts("bond rotation: 6720 single-group cases passed");
    const struct vector bases[] = {
        {0, 0}, {1024, -1024}, {-1024, 1024}, {1024, 1024}, {-1024, -1024}};
    const struct vector translations[] = {{0, 0},  {1, 0},  {0, 1}, {-1, 1},
                                          {-1, 0}, {0, -1}, {1, -1}};
    size_t count = 0;
    for (size_t b = 0; b < sizeof(bases) / sizeof(bases[0]); ++b) {
        for (size_t t = 0; t < sizeof(translations) / sizeof(translations[0]); ++t) {
            for (int rotation = -10; rotation <= 10; ++rotation) {
                for (int du = -4; du <= 4; ++du) {
                    for (int dv = -4; dv <= 4; ++dv) {
                        struct vector original = {bases[b].u + du, bases[b].v + dv};
                        struct vector result = original;
                        apply_movement_to_position(bases[b], translations[t],
                                                   u_offset_for_direction(rotation),
                                                   v_offset_for_direction(rotation), &result);
                        struct vector relative = {result.u - bases[b].u - translations[t].u,
                                                  result.v - bases[b].v - translations[t].v};
                        assert(hex_distance(relative) == hex_distance((struct vector){du, dv}));
                        struct vector translated_base = {bases[b].u + translations[t].u,
                                                         bases[b].v + translations[t].v};
                        struct vector inverse_translation = {-translations[t].u,
                                                             -translations[t].v};
                        apply_movement_to_position(translated_base, inverse_translation,
                                                   u_offset_for_direction(-rotation),
                                                   v_offset_for_direction(-rotation), &result);
                        assert(result.u == original.u && result.v == original.v);
                        ++count;
                    }
                }
            }
        }
    }
    assert(count == 59535);
    struct vector p = {1, 0};
    apply_movement_to_position((struct vector){0, 0}, (struct vector){0, 0},
                               u_offset_for_direction(1), v_offset_for_direction(1), &p);
    assert(p.u == 0 && p.v == 1);
    // The reference's 82-by-71 embedding is not an equilateral lattice.
    int original_norm_squared = 82 * 82;
    int rotated_norm_squared =
        (82 * p.u + 41 * p.v) * (82 * p.u + 41 * p.v) + (71 * p.v) * (71 * p.v);
    assert(original_norm_squared == 6724 && rotated_norm_squared == 6722);
    puts("integer snap: 59535 transforms passed; anisotropic rotation counterexample reproduced");
    return 0;
}
