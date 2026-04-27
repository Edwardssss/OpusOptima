#pragma once

#include <filesystem>
#include <string>

#include "BinaryParser.hpp"

namespace opusoptima::render {

class SvgExporter {
    public:
    static std::string BuildPuzzleSvg(const io::PuzzleData& puzzle);
    static void WritePuzzleSvg(const io::PuzzleData& puzzle,
                               const std::filesystem::path& outputPath);
};

} // namespace opusoptima::render
