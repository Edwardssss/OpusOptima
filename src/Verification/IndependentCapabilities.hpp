#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace opusoptima::verification {
struct IndependentCapabilityProfile {
    std::string_view id, rulesetVersion, modelEvidence;
    std::size_t maxMoleculeAtoms, maxParts, maxArms, maxInventory;
    int maxCycleLimit;
    int maxArmLength, maxTapePosition, maxPartCoordinate;
    std::array<std::string_view, 5> exclusions;
};
inline constexpr IndependentCapabilityProfile finiteOrdinaryProfile{
    "finite-ordinary-v1",
    "finite-82x71-v1",
    "omsim-a88d12d8-rules; independent interval kernel",
    64,
    1024,
    128,
    8192,
    150000,
    3,
    20000,
    1024,
    {"production walls and conduits", "Repeat/repeating outputs and unexpanded repeat/reset",
     "disjoint implicit links", "looping/ambiguous tracks", "unestablished cooperative motion"}};
inline constexpr std::array independentProfiles{finiteOrdinaryProfile};
} // namespace opusoptima::verification
