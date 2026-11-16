#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "BinaryParser.hpp"

namespace opusoptima::wasm {
// Parses the browser editor's JSON schema into a base or production puzzle.
// The caller owns the returned data and serialises it through BinaryWriter.
std::optional<io::PuzzleData> BuildEditorPuzzle(std::string_view json, std::string& error);
} // namespace opusoptima::wasm
