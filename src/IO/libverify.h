#pragma once

// ── C linkage for asset/libverify.so ────────────────────────
// This library is used for verifying simulation correctness.
// On non-Windows platforms we link directly against the .so;
// on Windows a .dll with equivalent API would be used.

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

// Opaque handle types.
typedef struct Verifier Verifier;
typedef struct PuzzleFile PuzzleFile;
typedef struct SolutionFile SolutionFile;

// ── Puzzle file parsing ──

/// Parses a .puzzle file from a byte buffer.
/// Returns NULL on error; check verifier_error() for details.
PuzzleFile* parse_puzzle_byte_string(const unsigned char* bytes, size_t length);

/// Parse a .puzzle file from disk.
PuzzleFile* parse_puzzle_file(const char* path);

/// Free a puzzle file handle.
void free_puzzle_file(PuzzleFile* puzzle_file);

// ── Solution file parsing ──

/// Parses a .solution file from a byte buffer.
SolutionFile* parse_solution_byte_string(const unsigned char* bytes, size_t length);

/// Parse a .solution file from disk.
SolutionFile* parse_solution_file(const char* path);

/// Free a solution file handle.
void free_solution_file(SolutionFile* solution_file);

/// Get the number of instructions for a solution.
int solution_file_cost(SolutionFile* solution_file);

// ── Decoding ──

/// Decode a solution file into internal representation.
/// Returns the number of objects decoded, or -1 on error.
int decode_solution(SolutionFile* solution_file);

// ── Verifier API ──

/// Create a verifier from parsed puzzle/solution files.
Verifier* verifier_create(PuzzleFile* puzzle_file, SolutionFile* solution_file);

/// Create a verifier from raw bytes (makes internal copies).
Verifier* verifier_create_from_bytes(const unsigned char* puzzle_bytes, size_t puzzle_len,
                                     const unsigned char* solution_bytes, size_t solution_len);

/// Destroy a verifier.
void verifier_destroy(Verifier* verifier);

/// Returns non-zero if the verifier encountered an error.
int verifier_error(Verifier* verifier);

/// Returns the cycle at which an error occurred (0 if no error).
int verifier_error_cycle(Verifier* verifier);

/// Returns the u/v coordinates of the error location.
int verifier_error_location_u(Verifier* verifier);
int verifier_error_location_v(Verifier* verifier);

/// Clear any pending error.
void verifier_error_clear(Verifier* verifier);

/// Set a cycle limit for the verifier.
void verifier_set_cycle_limit(Verifier* verifier, int limit);

/// Disable all limits on the verifier.
void verifier_disable_limits(Verifier* verifier);

/// Set whether the verifier should fail on wrong output atoms.
void verifier_set_fails_on_wrong_output(Verifier* verifier, int fail);

/// Set whether the verifier should fail on wrong output bonds.
void verifier_set_fails_on_wrong_output_bonds(Verifier* verifier, int fail);

/// Evaluate the solution. Returns the number of cycles taken.
/// Returns -1 on failure; check verifier_error() for details.
long long verifier_evaluate_metric(Verifier* verifier);

/// Evaluate an approximate metric (faster, less accurate).
long long verifier_evaluate_approximate_metric(Verifier* verifier);

/// Run the simulation.
long long run(PuzzleFile* puzzle_file, SolutionFile* solution_file);

/// Run until steady state.
long long run_until_steady_state(PuzzleFile* puzzle_file, SolutionFile* solution_file);

// ── Low-level helpers ──

/// Check if a position is occupied by a specific arm.
int collision(PuzzleFile* puzzle_file, int arm_id, int in_u, int in_v);

/// Look up track segments.
int lookup_track(PuzzleFile* puzzle_file, int in_u, int in_v, int* out_u, int* out_v);

/// Look up an atom at a position.
int lookup_atom_at_position(PuzzleFile* puzzle_file, int pos_u, int pos_v);

/// Initial setup: place atoms on grid.
void initial_setup(PuzzleFile* puzzle_file, SolutionFile* solution_file);

/// Insert an atom at a position.
void insert_atom(PuzzleFile* puzzle_file, int element_id, int pos_u, int pos_v);

/// Get the used area of the puzzle.
int used_area(PuzzleFile* puzzle_file);

/// Destroy all allocated state (call at end).
void destroy(void);

// ── Coordinate utilities ──

/// Convert hex coordinates to u/v offset.
int u_offset_for_direction(int direction);
int v_offset_for_direction(int direction);

/// Get the direction for a given hex offset.
int direction_for_offset(int dir_u, int dir_v);

/// Get the mechanism's relative position.
void mechanism_relative_position(int arm_id, int* out_u, int* out_v);

#ifdef __cplusplus
} // extern "C"
#endif
