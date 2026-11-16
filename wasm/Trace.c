#include <emscripten/emscripten.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "decode.h"
#include "parse.h"
#include "sim.h"

struct Buffer {
    char *data;
    size_t length;
    size_t capacity;
};

static bool append(struct Buffer *buffer, const char *format, ...) {
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    const int needed = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (needed < 0) { va_end(args); return false; }
    const size_t required = buffer->length + (size_t)needed + 1;
    if (required > buffer->capacity) {
        size_t capacity = buffer->capacity ? buffer->capacity : 4096;
        while (capacity < required) capacity *= 2;
        char *data = realloc(buffer->data, capacity);
        if (!data) { va_end(args); return false; }
        buffer->data = data;
        buffer->capacity = capacity;
    }
    vsnprintf(buffer->data + buffer->length, buffer->capacity - buffer->length, format, args);
    va_end(args);
    buffer->length += (size_t)needed;
    return true;
}

static int atom_type(atom value) {
    for (int type = 0; type < NUMBER_OF_ATOM_TYPES; ++type)
        if (value & ATOM_OF_TYPE(type)) return type + 1;
    return 0;
}

static bool add_frame(struct Buffer *buffer, struct solution *solution, struct board *board) {
    if (!append(buffer, "{\"cycle\":%llu,\"atoms\":[",
                (unsigned long long)board->cycle))
        return false;
    bool first = true;
    for (uint32_t i = 0; i < BOARD_CAPACITY(board); ++i) {
        const atom value = board->grid.atoms_at_positions[i].atom;
        if (!(value & VALID) || (value & REMOVED)) continue;
        const int type = atom_type(value);
        if (!type) continue;
        const struct vector p = board->grid.atoms_at_positions[i].position;
        if (!append(buffer, "%s{\"q\":%d,\"r\":%d,\"type\":%d}", first ? "" : ",", p.u, p.v,
                    type))
            return false;
        first = false;
    }
    append(buffer, "],\"arms\":[");
    first = true;
    for (size_t i = 0; i < solution->number_of_arms; ++i) {
        const struct mechanism *arm = &solution->arms[i];
        const struct vector p = arm->position;
        if (!append(buffer,
                    "%s{\"q\":%d,\"r\":%d,\"du\":%d,\"dv\":%d,\"type\":%u,\"rotation\":%d}",
                    first ? "" : ",", p.u, p.v, arm->direction_u.u, arm->direction_u.v,
                    arm->type, arm->arm_rotation))
            return false;
        first = false;
    }
    append(buffer, "],\"glyphs\":[");
    first = true;
    for (size_t i = 0; i < solution->number_of_glyphs; ++i) {
        const struct mechanism *glyph = &solution->glyphs[i];
        const struct vector p = glyph->position;
        if (!append(buffer, "%s{\"q\":%d,\"r\":%d,\"type\":%u}", first ? "" : ",", p.u, p.v,
                    glyph->type))
            return false;
        first = false;
    }
    return append(buffer, "]}");
}

EMSCRIPTEN_KEEPALIVE char *opusoptima_trace(const char *puzzleBytes, int puzzleLength,
                                             const char *solutionBytes, int solutionLength) {
    if (!puzzleBytes || puzzleLength <= 0 || !solutionBytes || solutionLength <= 0)
        return strdup("{\"error\":\"invalid trace input\"}");
    struct puzzle_file *puzzle = parse_puzzle_byte_string(
        (struct byte_string){(unsigned char *)puzzleBytes, (size_t)puzzleLength});
    struct solution_file *solutionFile = parse_solution_byte_string(
        (struct byte_string){(unsigned char *)solutionBytes, (size_t)solutionLength});
    if (!puzzle || !solutionFile) {
        free_puzzle_file(puzzle);
        free_solution_file(solutionFile);
        return strdup("{\"error\":\"puzzle or solution parse failed\"}");
    }
    struct solution solution = {0};
    struct board board = {0};
    const char *error = NULL;
    if (!decode_solution(&solution, puzzle, solutionFile, &error)) {
        free_puzzle_file(puzzle);
        free_solution_file(solutionFile);
        return strdup("{\"error\":\"solution decode failed\"}");
    }
    initial_setup(&solution, &board, solutionFile->area);
    struct Buffer buffer = {0};
    if (!append(&buffer, "{\"frames\":[")) goto fail;
    if (!add_frame(&buffer, &solution, &board)) goto fail;
    const uint32_t limit = solutionFile->cycles ? solutionFile->cycles : 1000;
    for (uint32_t cycle = 0; cycle < limit && !board.complete; ++cycle) {
        run(&solution, &board);
        if (!append(&buffer, ",") || !add_frame(&buffer, &solution, &board)) goto fail;
    }
    if (!append(&buffer, "]}")) goto fail;
    destroy(&solution, &board);
    free_puzzle_file(puzzle);
    free_solution_file(solutionFile);
    return buffer.data;
fail:
    free(buffer.data);
    destroy(&solution, &board);
    free_puzzle_file(puzzle);
    free_solution_file(solutionFile);
    return strdup("{\"error\":\"trace allocation failed\"}");
}
