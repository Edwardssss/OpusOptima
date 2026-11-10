#pragma once

// One way to find the repository root from a test translation unit.
//
// Tests read `asset/puzzle/...` and write under `result/...`, so they need the
// repository root regardless of where the test file itself sits. Deriving it
// from `__FILE__` with a fixed number of `parent_path()` calls silently breaks
// as soon as a test moves to another directory depth, so the search is anchored
// on the repository marker instead: walk up until a directory containing
// `CMakeLists.txt` is found.
//
// Usage:
//   #include "RepoRoot.hpp"
//   const auto root = opusoptima::test::RepoRoot(__FILE__);
//
// The working directory must still be inside the repository for the relative
// asset paths to resolve; `env -C <repo> ./build/OpusOptimaTests` or a run from
// the repository root is the supported invocation.

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace opusoptima::test {

/// Marker file that identifies the repository root.
inline constexpr const char* kRepoMarker = "CMakeLists.txt";

/// Walks up from `start` (inclusive) to the first directory holding the marker.
/// Throws when the filesystem root is reached without finding it.
inline std::filesystem::path FindRepoRoot(std::filesystem::path start) {
    std::error_code error;
    for (std::filesystem::path cursor = std::move(start);; cursor = cursor.parent_path()) {
        if (std::filesystem::exists(cursor / kRepoMarker, error)) return cursor;
        if (cursor.empty() || cursor == cursor.parent_path()) {
            throw std::runtime_error("repository root (holding " +
                                     std::string(kRepoMarker) +
                                     ") not found above the test source file");
        }
    }
}

/// Repository root derived from a source file path (`__FILE__`).
///
/// `OPUSOPTIMA_REPO_ROOT` overrides the search, which is useful when a test
/// binary is copied outside the source tree.
inline std::filesystem::path RepoRoot(const char* sourceFile) {
    if (const char* override_dir = std::getenv("OPUSOPTIMA_REPO_ROOT");
        override_dir != nullptr && *override_dir != '\0')
        return std::filesystem::path(override_dir);
    return FindRepoRoot(std::filesystem::path(sourceFile).parent_path());
}

}  // namespace opusoptima::test
