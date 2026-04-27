#pragma once

#include <string>

#include "BinaryParser.hpp"

namespace opusoptima::render {

class AsciiRenderer {
    public:
    static std::string RenderPuzzle(const io::PuzzleData& puzzle);

    private:
    static std::string RenderMolecule(const io::Molecule& molecule, const std::string& title);
};

} // namespace opusoptima::render
