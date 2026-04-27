#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace opusoptima::core {

enum class AtomType : std::uint8_t {
    Salt = 1,
    Air,
    Earth,
    Fire,
    Water,
    Quicksilver,
    Gold,
    Silver,
    Copper,
    Iron,
    Tin,
    Lead,
    Vitae,
    Mors,
    Repeat,
    Quintessence,
};

inline constexpr std::array<std::string_view, 16> kAtomTypeNames = {
    "Salt",   "Air",  "Earth", "Fire", "Water", "Quicksilver", "Gold",   "Silver",
    "Copper", "Iron", "Tin",   "Lead", "Vitae", "Mors",        "Repeat", "Quintessence",
};

[[nodiscard]] inline std::optional<AtomType> AtomTypeFromId(const std::uint8_t item_id) {
    if (item_id == 0 || item_id > kAtomTypeNames.size()) {
        return std::nullopt;
    }
    return static_cast<AtomType>(item_id);
}

[[nodiscard]] inline std::string_view AtomTypeName(const AtomType type) {
    const auto index = static_cast<std::size_t>(type) - 1U;
    return kAtomTypeNames[index];
}

[[nodiscard]] inline char AtomTypeSymbol(const AtomType type) {
    switch (type) {
    case AtomType::Salt:
        return 'S';
    case AtomType::Air:
        return 'A';
    case AtomType::Earth:
        return 'E';
    case AtomType::Fire:
        return 'F';
    case AtomType::Water:
        return 'W';
    case AtomType::Quicksilver:
        return 'Q';
    case AtomType::Gold:
        return 'G';
    case AtomType::Silver:
        return 'V';
    case AtomType::Copper:
        return 'C';
    case AtomType::Iron:
        return 'I';
    case AtomType::Tin:
        return 'T';
    case AtomType::Lead:
        return 'L';
    case AtomType::Vitae:
        return '+';
    case AtomType::Mors:
        return '-';
    case AtomType::Repeat:
        return 'R';
    case AtomType::Quintessence:
        return 'X';
    }
    return '?';
}

[[nodiscard]] inline std::string AtomTypeLabel(const AtomType type) {
    return std::string(AtomTypeName(type));
}

enum class BondType : std::uint8_t {
    Normal = 0x01,
    TriplexRed = 0x02,
    TriplexBlack = 0x04,
    TriplexYellow = 0x08,
};

[[nodiscard]] inline std::string BondBitsToLabel(const std::uint8_t bits) {
    if (bits == static_cast<std::uint8_t>(BondType::Normal)) {
        return "Normal";
    }

    std::string label;
    if ((bits & static_cast<std::uint8_t>(BondType::TriplexRed)) != 0U) {
        label += "TriplexRed|";
    }
    if ((bits & static_cast<std::uint8_t>(BondType::TriplexBlack)) != 0U) {
        label += "TriplexBlack|";
    }
    if ((bits & static_cast<std::uint8_t>(BondType::TriplexYellow)) != 0U) {
        label += "TriplexYellow|";
    }
    if (!label.empty()) {
        label.pop_back();
        return label;
    }
    return "Unknown(" + std::to_string(bits) + ')';
}

} // namespace opusoptima::core
