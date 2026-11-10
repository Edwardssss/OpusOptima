#pragma once

#include "BinaryParser.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace opusoptima::solvers {

// Metrics do not change a machine; object order can change track and glyph behavior.
[[nodiscard]] inline std::string PhysicalCandidateKey(const io::SolutionData& solution) {
    std::vector<std::string> objects;
    objects.reserve(solution.objects.size());
    for (const auto& object : solution.objects) {
        std::ostringstream part;
        part << object.typeName << ':' << object.position.q << ',' << object.position.r << ':';
        for (const auto value : object.bodyData)
            part << value << ',';
        part << ':';
        for (const auto& instruction : object.instructions)
            part << instruction.action << instruction.parameter << ',';
        objects.push_back(part.str());
    }
    std::string key;
    for (const auto& object : objects) {
        key += std::to_string(object.size());
        key += ':';
        key += object;
    }
    return key;
}

} // namespace opusoptima::solvers
