#include "SvgExporter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <numbers>
#include <sstream>
#include <string_view>

#include "CoreConstants.hpp"
#include "HexGrid.hpp"
#include "PuzzleSpecs.hpp"

namespace opusoptima::render {

namespace {

struct Vec2 {
    double x{};
    double y{};
};

struct Extents {
    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double maxY = std::numeric_limits<double>::lowest();
    bool valid = false;
};

void Expand(Extents& extents, const Vec2 point) {
    extents.minX = std::min(extents.minX, point.x);
    extents.minY = std::min(extents.minY, point.y);
    extents.maxX = std::max(extents.maxX, point.x);
    extents.maxY = std::max(extents.maxY, point.y);
    extents.valid = true;
}

std::string EscapeXml(const std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());

    for (const char chr : value) {
        switch (chr) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        case '\"':
            escaped += "&quot;";
            break;
        case '\'':
            escaped += "&apos;";
            break;
        default:
            escaped.push_back(chr);
            break;
        }
    }

    return escaped;
}

std::string AtomColor(const core::AtomType atomType) {
    switch (atomType) {
    case core::AtomType::Salt:
        return "#f2f2f2";
    case core::AtomType::Air:
        return "#bfe9ff";
    case core::AtomType::Earth:
        return "#96c06d";
    case core::AtomType::Fire:
        return "#ff7444";
    case core::AtomType::Water:
        return "#3b82f6";
    case core::AtomType::Quicksilver:
        return "#cdd6e1";
    case core::AtomType::Gold:
        return "#f6c945";
    case core::AtomType::Silver:
        return "#c8d0d9";
    case core::AtomType::Copper:
        return "#d8844a";
    case core::AtomType::Iron:
        return "#8f9aab";
    case core::AtomType::Tin:
        return "#b6bfca";
    case core::AtomType::Lead:
        return "#606070";
    case core::AtomType::Vitae:
        return "#8ef083";
    case core::AtomType::Mors:
        return "#46304f";
    case core::AtomType::Repeat:
        return "#f198ff";
    case core::AtomType::Quintessence:
        return "#ffe97c";
    }
    return "#cccccc";
}

Vec2 HexCenter(const core::HexCoord hex) {
    const core::Point2D point = core::AxialToPixel(hex, core::kHexRadius);
    return Vec2{.x = point.x, .y = point.y};
}

std::array<Vec2, core::kHexVertexCount> HexPolygon(const Vec2 center) {
    std::array<Vec2, core::kHexVertexCount> points{};
    for (std::size_t i = 0; i < points.size(); ++i) {
        const double angle =
            (std::numbers::pi / 180.0) *
            (core::kHexVertexAngleStep * static_cast<double>(i) - core::kHexStartAngleOffset);
        points[i] = Vec2{
            .x = center.x + (core::kHexRadius * std::cos(angle)),
            .y = center.y + (core::kHexRadius * std::sin(angle)),
        };
    }
    return points;
}

std::string PolygonPoints(const std::array<Vec2, core::kHexVertexCount>& points) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < points.size(); ++i) {
        oss << points[i].x << ',' << points[i].y;
        if (i + 1 != points.size()) {
            oss << ' ';
        }
    }
    return oss.str();
}

void DrawMoleculePanel(std::ostringstream& svg, const io::Molecule& molecule,
                       const std::string& title, const double panelX, const double panelY,
                       const double panelWidth, const double panelHeight) {

    constexpr double kTitleOffsetX = 16.0;
    constexpr double kTitleOffsetY = 28.0;
    constexpr int kTitleFontSize = 16;
    constexpr double kEmptyTextOffsetY = 58.0;
    constexpr int kEmptyTextFontSize = 13;
    constexpr double kContentCenterOffsetY = 12.0;
    constexpr double kHexPolygonStrokeWidth = 1.25;
    constexpr double kCircleStrokeWidth = 1.2;
    constexpr int kSymbolFontSize = 11;

    svg << "<rect x=\"" << panelX << "\" y=\"" << panelY << "\" width=\"" << panelWidth
        << "\" height=\"" << panelHeight << "\" rx=\"" << core::kPanelCornerRadius
        << "\" fill=\"#f9fafb\" stroke=\"#d1d5db\"/>\n";
    svg << "<text x=\"" << (panelX + kTitleOffsetX) << "\" y=\"" << (panelY + kTitleOffsetY)
        << "\" font-size=\"" << kTitleFontSize << R"(" font-family="monospace" fill="#111827">)"
        << EscapeXml(title) << "</text>\n";

    if (molecule.atoms.empty()) {
        svg << "<text x=\"" << (panelX + kTitleOffsetX) << "\" y=\"" << (panelY + kEmptyTextOffsetY)
            << "\" font-size=\"" << kEmptyTextFontSize
            << "\" font-family=\"monospace\" fill=\"#6b7280\">(empty molecule)</text>\n";
        return;
    }

    Extents extents;
    for (const auto& atom : molecule.atoms) {
        Expand(extents, HexCenter(atom.position));
    }
    for (const auto& bond : molecule.bonds) {
        Expand(extents, HexCenter(bond.from));
        Expand(extents, HexCenter(bond.to));
    }

    const double contentCenterX = extents.valid ? (extents.minX + extents.maxX) / 2.0 : 0.0;
    const double contentCenterY = extents.valid ? (extents.minY + extents.maxY) / 2.0 : 0.0;
    const double targetCenterX = panelX + (panelWidth / 2.0);
    const double targetCenterY = panelY + (panelHeight / 2.0) + kContentCenterOffsetY;
    const double translateX = targetCenterX - contentCenterX;
    const double translateY = targetCenterY - contentCenterY;

    for (const auto& bond : molecule.bonds) {
        const Vec2 from = HexCenter(bond.from);
        const Vec2 dest = HexCenter(bond.to);
        const std::string color =
            (bond.bits & static_cast<std::uint8_t>(core::BondType::Normal)) != 0U ? "#374151"
                                                                                  : "#ef4444";

        svg << "<line x1=\"" << (from.x + translateX) << "\" y1=\"" << (from.y + translateY)
            << "\" x2=\"" << (dest.x + translateX) << "\" y2=\"" << (dest.y + translateY)
            << "\" stroke=\"" << color << "\" stroke-width=\"" << core::kBondStrokeWidth
            << "\" stroke-linecap=\"round\"/>\n";
    }

    for (const auto& atom : molecule.atoms) {
        const Vec2 center = HexCenter(atom.position);
        const Vec2 translated{.x = center.x + translateX, .y = center.y + translateY};
        const auto polygon = HexPolygon(translated);

        svg << "<polygon points=\"" << PolygonPoints(polygon)
            << R"(" fill="#ffffff" stroke="#9ca3af" stroke-width=")" << kHexPolygonStrokeWidth
            << "\"/>\n";

        svg << "<circle cx=\"" << translated.x << "\" cy=\"" << translated.y << "\" r=\""
            << core::kAtomCircleRadius << "\" fill=\"" << AtomColor(atom.type)
            << R"(" stroke="#111827" stroke-width=")" << kCircleStrokeWidth << "\"/>\n";

        svg << "<text x=\"" << translated.x << "\" y=\""
            << (translated.y + core::kAtomSymbolBaselineOffset)
            << R"(" text-anchor="middle" font-size=")" << kSymbolFontSize
            << R"(" font-family="monospace" fill="#111827">)" << core::AtomTypeSymbol(atom.type)
            << "</text>\n";
    }
}

} // namespace

std::string SvgExporter::BuildPuzzleSvg(const io::PuzzleData& puzzle) {
    constexpr double kPageMargin = 28.0;
    constexpr double kColumnGap = 18.0;
    constexpr double kRowGap = 24.0;
    constexpr double kTitleRowY = 40.0;
    constexpr double kSubtitleRowY = 62.0;
    constexpr int kTitleFontSize = 26;
    constexpr int kSubtitleFontSize = 13;
    constexpr double kTitleAreaHeight = 74.0;
    constexpr double kExtraBottomSpace = 64.0;

    const std::size_t columns =
        std::max<std::size_t>(1, std::max(puzzle.inputs.size(), puzzle.outputs.size()));
    const double svgWidth = (kPageMargin * 2.0) +
                            (core::kPanelWidth * static_cast<double>(columns)) +
                            (kColumnGap * static_cast<double>(columns - 1));
    const double svgHeight = (kPageMargin * 2.0) + (core::kPanelHeight * 2.0) + kRowGap +
                             kTitleAreaHeight + kExtraBottomSpace;

    std::ostringstream svg;
    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg << R"(<svg xmlns="http://www.w3.org/2000/svg" width=")" << svgWidth << "\" height=\""
        << svgHeight << "\" viewBox=\"0 0 " << svgWidth << ' ' << svgHeight << "\">\n";
    svg << "<defs>\n";
    svg << "  <linearGradient id=\"bg\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\">\n";
    svg << "    <stop offset=\"0%\" stop-color=\"#fff8ed\"/>\n";
    svg << "    <stop offset=\"100%\" stop-color=\"#e8f4ff\"/>\n";
    svg << "  </linearGradient>\n";
    svg << "</defs>\n";
    svg << R"(<rect x="0" y="0" width=")" << svgWidth << "\" height=\"" << svgHeight
        << "\" fill=\"url(#bg)\"/>\n";
    svg << "<text x=\"" << kPageMargin << "\" y=\"" << kTitleRowY << "\" font-size=\""
        << kTitleFontSize << R"(" font-family="monospace" fill="#111827">)"
        << EscapeXml(puzzle.name) << "</text>\n";
    svg << "<text x=\"" << kPageMargin << "\" y=\"" << kSubtitleRowY << "\" font-size=\""
        << kSubtitleFontSize << R"(" font-family="monospace" fill="#4b5563">)"
        << "inputs=" << puzzle.inputs.size() << ", outputs=" << puzzle.outputs.size()
        << ", multiplier=" << puzzle.outputMultiplier << "</text>\n";

    const double inputRowY = kPageMargin + kTitleAreaHeight;
    const double outputRowY = inputRowY + core::kPanelHeight + kRowGap;

    for (std::size_t i = 0; i < puzzle.inputs.size(); ++i) {
        const double panelX =
            kPageMargin + (static_cast<double>(i) * (core::kPanelWidth + kColumnGap));
        DrawMoleculePanel(svg, puzzle.inputs[i], "Input " + std::to_string(i + 1), panelX,
                          inputRowY, core::kPanelWidth, core::kPanelHeight);
    }

    for (std::size_t i = 0; i < puzzle.outputs.size(); ++i) {
        const double panelX =
            kPageMargin + (static_cast<double>(i) * (core::kPanelWidth + kColumnGap));
        DrawMoleculePanel(svg, puzzle.outputs[i], "Output " + std::to_string(i + 1), panelX,
                          outputRowY, core::kPanelWidth, core::kPanelHeight);
    }

    svg << "</svg>\n";
    return svg.str();
}

void SvgExporter::WritePuzzleSvg(const io::PuzzleData& puzzle,
                                 const std::filesystem::path& outputPath) {
    const std::string svg = BuildPuzzleSvg(puzzle);

    if (outputPath.has_parent_path()) {
        std::filesystem::create_directories(outputPath.parent_path());
    }

    std::ofstream output(outputPath, std::ios::binary);
    if (!output) {
        throw io::ParseError("Cannot open SVG output path: " + outputPath.string());
    }

    output << svg;
    if (!output) {
        throw io::ParseError("Failed to write SVG file: " + outputPath.string());
    }
}

} // namespace opusoptima::render
