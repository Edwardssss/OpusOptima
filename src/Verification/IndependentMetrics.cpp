#include "IndependentMetrics.hpp"
#include "IndependentCapabilities.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>

namespace opusoptima::verification {
namespace {
using Coord = core::HexCoord;
constexpr std::array<Coord, 6> directions{{{1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {1, -1}}};
constexpr double gridX = 82., gridY = 71., atomDiameter = 58., baseAreaRadius = 49.;
constexpr int coordinateLimit = 1 << 20;
constexpr std::size_t areaCellLimit = 1000000;
// Partial observations must never survive an exception as certifiable metrics.
class InvalidateOnFailure {
    bool& initialized_;
    int exceptions_ = std::uncaught_exceptions();

    public:
    explicit InvalidateOnFailure(bool& initialized) : initialized_(initialized) {}
    ~InvalidateOnFailure() {
        if (std::uncaught_exceptions() > exceptions_) initialized_ = false;
    }
};
int Turns(int rotation) {
    return (rotation % 6 + 6) % 6;
}
Coord Checked(int64_t q, int64_t r) {
    if (q < -coordinateLimit || q > coordinateLimit || r < -coordinateLimit ||
        r > coordinateLimit)
        throw std::invalid_argument("independent metric coordinate bound exceeded");
    return {static_cast<int>(q), static_cast<int>(r)};
}
Coord AddChecked(Coord a, Coord b) {
    return Checked(int64_t(a.q) + b.q, int64_t(a.r) + b.r);
}
Coord Rotate(Coord cell, int rotation) {
    cell = Checked(cell.q, cell.r);
    for (int i = 0; i < Turns(rotation); ++i)
        cell = Checked(-int64_t(cell.r), int64_t(cell.q) + cell.r);
    return cell;
}
Coord World(Coord origin, Coord local, int rotation) {
    return AddChecked(origin, Rotate(local, rotation));
}
int Word(const io::SolutionObject& part, std::size_t index) {
    if (index >= part.bodyData.size()) throw std::invalid_argument("truncated metric part data");
    return part.bodyData[index];
}
bool Mechanical(const std::string& type) {
    return type == "arm1" || type == "arm2" || type == "arm3" || type == "arm6" ||
           type == "piston" || type == "baron";
}
int Grabbers(const std::string& type) {
    return type == "arm2" ? 2 : type == "arm3" ? 3 : type == "arm6" || type == "baron" ? 6 : 1;
}
std::vector<Coord> GlyphFootprint(const std::string& type) {
    if (type == "glyph-disposal")
        return {{0, 0}, {1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {1, -1}};
    if (type == "glyph-calcification" || type == "glyph-marker") return {{0, 0}};
    if (type == "bonder" || type == "unbonder" || type == "glyph-duplication" ||
        type == "glyph-projection")
        return {{0, 0}, {1, 0}};
    if (type == "bonder-prisma" || type == "glyph-purification") return {{0, 0}, {1, 0}, {0, 1}};
    if (type == "bonder-speed") return {{0, 0}, {1, 0}, {0, -1}, {-1, 1}};
    if (type == "glyph-life-and-death") return {{0, 0}, {1, 0}, {0, 1}, {1, -1}};
    if (type == "glyph-unification") return {{0, 0}, {0, 1}, {-1, 1}, {0, -1}, {1, -1}};
    if (type == "glyph-dispersion") return {{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {1, -1}};
    throw std::invalid_argument("independent metrics exclude unknown part: " + type);
}
int32_t MetricValue(int64_t value) {
    if (value < 0 || value > std::numeric_limits<int32_t>::max())
        throw std::invalid_argument("independent metric integer bound exceeded");
    return static_cast<int32_t>(value);
}
} // namespace

void IndependentMetrics::Mark(Coord cell) {
    cell = Checked(cell.q, cell.r);
    if (!area_.contains({cell.q, cell.r}) && area_.size() >= areaCellLimit)
        throw std::invalid_argument("independent area cell bound exceeded");
    if (area_.emplace(cell.q, cell.r).second) ++work_.markedCells;
    uncertainCells_.erase({cell.q, cell.r});
}

void IndependentMetrics::Initialize(const io::PuzzleData& puzzle,
                                    const io::SolutionData& solution) {
    *this = IndependentMetrics{};
    InvalidateOnFailure guard(initialized_);
    initialized_ = true;
    if (puzzle.version != 3)
        throw std::invalid_argument("independent metrics require version-3 puzzle");
    // Prices, footprint union and tape-count semantics are pinned by omsim a88d12d8 decode.c/sim.c.
    static const std::map<std::string, int> prices{{"arm1", 20},
                                                   {"arm2", 30},
                                                   {"arm3", 30},
                                                   {"arm6", 30},
                                                   {"piston", 40},
                                                   {"baron", 30},
                                                   {"bonder", 10},
                                                   {"unbonder", 10},
                                                   {"bonder-prisma", 20},
                                                   {"bonder-speed", 30},
                                                   {"glyph-calcification", 10},
                                                   {"glyph-duplication", 20},
                                                   {"glyph-projection", 20},
                                                   {"glyph-purification", 20},
                                                   {"glyph-life-and-death", 20},
                                                   {"glyph-unification", 20},
                                                   {"glyph-dispersion", 20},
                                                   {"glyph-disposal", 0},
                                                   {"glyph-marker", 0},
                                                   {"input", 0},
                                                   {"out-std", 0},
                                                   {"pipe", 0},
                                                   {"track", 0}};
    if (puzzle.productionInfo) {
        for (const auto& chamber : puzzle.productionInfo->chambers) {
            int radius = 0, stretch = 0;
            if (chamber.chamberType == "Small")
                radius = 1;
            else if (chamber.chamberType == "SmallWide") {
                radius = 1;
                stretch = 1;
            } else if (chamber.chamberType == "SmallWider") {
                radius = 1;
                stretch = 2;
            } else if (chamber.chamberType == "Medium")
                radius = 2;
            else if (chamber.chamberType == "MediumWide") {
                radius = 2;
                stretch = 1;
            } else if (chamber.chamberType == "Large")
                radius = 3;
            else
                throw std::invalid_argument("independent metrics exclude unknown chamber");
            for (int q = -radius - 1; q <= radius + stretch + 1; ++q)
                for (int r = -radius - 1; r <= radius + 1; ++r) {
                    int distance = 100;
                    for (int shift = 0; shift <= stretch; ++shift)
                        distance = std::min(distance, std::max({std::abs(q - shift), std::abs(r),
                                                                std::abs(q - shift + r)}));
                    if (distance == radius + 1) Mark(AddChecked(chamber.position, {q, r}));
                }
        }
    }
    for (const auto& part : solution.objects) {
        const auto price = prices.find(part.typeName);
        if (price == prices.end())
            throw std::invalid_argument("independent metrics exclude unknown part: " +
                                        part.typeName);
        cost_ += price->second;
        const int rotation = Word(part, 1);
        if (rotation < 0 || rotation >= 6)
            throw std::invalid_argument("invalid metric part rotation");
        if (Mechanical(part.typeName)) {
            const int length = part.typeName == "baron" ? 1 : Word(part, 0);
            ObserveArm(part.position, length, rotation, Grabbers(part.typeName),
                       part.typeName == "baron");
            std::set<int> positions;
            for (const auto& instruction : part.instructions) {
                if (!positions.insert(instruction.parameter).second)
                    throw std::invalid_argument("duplicate metric tape index");
                switch (instruction.action) {
                case 'G':
                case 'g':
                case 'R':
                case 'r':
                case 'P':
                case 'p':
                case 'E':
                case 'e':
                case 'A':
                case 'a':
                    ++instructions_;
                    break;
                case 'O':
                case 'B':
                case ' ':
                    break;
                default:
                    throw std::invalid_argument(
                        "independent metrics exclude unexpanded or unknown instruction");
                }
            }
        } else if (part.typeName == "input" || part.typeName == "out-std") {
            const auto& molecules = part.typeName == "input" ? puzzle.inputs : puzzle.outputs;
            const int index = Word(part, 2);
            if (index < 0 || static_cast<std::size_t>(index) >= molecules.size())
                throw std::invalid_argument("invalid metric port index");
            for (const auto& atom : molecules[index].atoms) {
                if (atom.type == core::AtomType::Repeat)
                    throw std::invalid_argument("independent metrics exclude repeating output");
                Mark(World(part.position, atom.position, rotation));
            }
        } else if (part.typeName == "track" || part.typeName == "pipe") {
            const bool track = part.typeName == "track";
            const std::size_t countIndex = track ? 4 : 6, begin = track ? 5 : 7;
            const int count = Word(part, countIndex);
            if (count < 1 || count > 4096 ||
                part.bodyData.size() < begin + 2 * static_cast<std::size_t>(count))
                throw std::invalid_argument("invalid metric track/conduit footprint");
            if (track) cost_ += int64_t(count) * 5;
            for (int i = 0; i < count; ++i)
                Mark(World(part.position,
                           {Word(part, begin + 2 * i), Word(part, begin + 2 * i + 1)},
                           track ? 0 : rotation));
        } else {
            for (auto cell : GlyphFootprint(part.typeName))
                Mark(World(part.position, cell, rotation));
        }
    }
    (void)MetricValue(cost_);
    (void)MetricValue(instructions_);
}

void IndependentMetrics::ObserveAtom(Coord cell) {
    InvalidateOnFailure guard(initialized_);
    if (!initialized_) throw std::invalid_argument("metrics not initialized");
    Mark(cell);
}

void IndependentMetrics::ObserveArm(Coord base, int length, int direction, int grabbers,
                                    bool wheel) {
    InvalidateOnFailure guard(initialized_);
    if (!initialized_ || length < 1 || length > 3 ||
        (grabbers != 1 && grabbers != 2 && grabbers != 3 && grabbers != 6))
        throw std::invalid_argument("unsupported metric arm pose");
    if (wheel) {
        length = 1;
        grabbers = 6;
    }
    Mark(base);
    for (int grip = 0; grip < grabbers; ++grip)
        for (int reach = 1; reach <= length; ++reach)
            Mark(World(base, {reach, 0}, Turns(direction) + grip * 6 / grabbers));
}

void IndependentMetrics::AddSweep(Sweep sweep) {
    constexpr std::size_t limit = finiteOrdinaryProfile.maxInventory +
                                  finiteOrdinaryProfile.maxArms;
    if (sweeps_.size() >= limit)
        throw std::invalid_argument("independent metric pending-sweep bound exceeded");
    sweeps_.push_back(sweep);
}

void IndependentMetrics::BeginMotions() {
    InvalidateOnFailure guard(initialized_);
    if (!initialized_ || frameOpen_)
        throw std::invalid_argument("invalid metric motion-frame state");
    frameOpen_ = true;
    sweeps_.clear();
}

void IndependentMetrics::ObserveAtomMotion(Coord position, Coord center, Coord translation,
                                           int turns) {
    InvalidateOnFailure guard(initialized_);
    if (!initialized_ || !frameOpen_) throw std::invalid_argument("metric motion frame not open");
    position = Checked(position.q, position.r);
    center = Checked(center.q, center.r);
    translation = Checked(translation.q, translation.r);
    if (turns == 5) turns = -1;
    if (turns < -1 || turns > 1 || (turns && translation != Coord{}))
        throw std::invalid_argument("unsupported metric composite motion");
    Mark(position);
    const auto end = AddChecked(
        World(center, Checked(int64_t(position.q) - center.q, int64_t(position.r) - center.r),
              turns),
        translation);
    Mark(end);
    if (turns || translation != Coord{}) {
        observedMotion_ = true;
        AddSweep({position, center, translation, turns, atomDiameter});
    }
}

void IndependentMetrics::ObserveArmMotion(Coord oldBase, int oldLength, int oldDirection,
                                          Coord newBase, int newLength, int newDirection,
                                          int grabbers, bool wheel) {
    InvalidateOnFailure guard(initialized_);
    if (!initialized_ || !frameOpen_) throw std::invalid_argument("metric motion frame not open");
    oldDirection = Turns(oldDirection);
    newDirection = Turns(newDirection);
    ObserveArm(oldBase, oldLength, oldDirection, grabbers, wheel);
    ObserveArm(newBase, newLength, newDirection, grabbers, wheel);
    if (oldBase != newBase || oldLength != newLength || oldDirection != newDirection)
        observedMotion_ = true;
    if (oldBase != newBase)
        AddSweep({oldBase,
                  {},
                  Checked(int64_t(newBase.q) - oldBase.q, int64_t(newBase.r) - oldBase.r),
                  0,
                  baseAreaRadius});
    const int turn = Turns(newDirection - oldDirection);
    if (!turn) return;
    if ((turn != 1 && turn != 5) || oldBase != newBase || oldLength != newLength)
        throw std::invalid_argument("unsupported metric arm rotation");
    if (wheel) return;
    if (oldLength < 2) return;
    const int start = turn == 1 ? oldDirection : newDirection;
    for (int grip = 0; grip < grabbers; ++grip) {
        const auto a = directions[Turns(start + grip * 6 / grabbers)];
        const auto b = directions[Turns(start + grip * 6 / grabbers + 1)];
        for (int u = 1; u <= oldLength; ++u)
            for (int v = 1; u + v <= oldLength + 1; ++v)
                Mark(AddChecked(oldBase, {u * a.q + v * b.q, u * a.r + v * b.r}));
    }
}

void IndependentMetrics::RasterPoint(double x, double y, double radius, double uncertainty) {
    ++work_.rasterPoints;
    const double axialR = y / gridY, axialQ = x / gridX - axialR * .5;
    const double outerRadius = radius + uncertainty;
    if (!std::isfinite(axialQ) || !std::isfinite(axialR) || !std::isfinite(outerRadius) ||
        radius < 0. || uncertainty < 0. || outerRadius > coordinateLimit)
        throw std::invalid_argument("independent raster numeric bound exceeded");
    // A pixel disk projects to |dr| <= R/71 and
    // |dq| <= R*(1/82 + 1/(2*71)). Keep the ordinary 6x6 stencil,
    // but expand it when the uncertainty envelope extends beyond it.
    const int extentQ = std::max(2, static_cast<int>(std::ceil(
                                        outerRadius * (1. / gridX + .5 / gridY))));
    const int extentR = std::max(2, static_cast<int>(std::ceil(outerRadius / gridY)));
    const double baseQ = std::floor(axialQ), baseR = std::floor(axialR);
    if (baseQ < -coordinateLimit + extentQ || baseQ > coordinateLimit - extentQ - 1 ||
        baseR < -coordinateLimit + extentR || baseR > coordinateLimit - extentR - 1)
        throw std::invalid_argument("independent raster coordinate bound exceeded");
    const int lowQ = static_cast<int>(baseQ) - extentQ,
              highQ = static_cast<int>(baseQ) + extentQ + 1,
              lowR = static_cast<int>(baseR) - extentR,
              highR = static_cast<int>(baseR) + extentR + 1;
    if (uint64_t(highQ - lowQ + 1) * uint64_t(highR - lowR + 1) > areaCellLimit)
        throw std::invalid_argument("independent raster candidate bound exceeded");
    for (int q = lowQ; q <= highQ; ++q)
        for (int r = lowR; r <= highR; ++r) {
            ++work_.rasterCandidates;
            const double dx = x - gridX * (q + r * .5), dy = y - gridY * r;
            const double squared = dx * dx + dy * dy;
            const auto key = std::pair{q, r};
            if (uncertainty < radius && squared < (radius - uncertainty) * (radius - uncertainty))
                Mark({q, r});
            else if (squared <= (radius + uncertainty) * (radius + uncertainty) &&
                     (!area_.contains(key) || uncertainCells_.contains(key))) {
                if (!uncertainCells_.contains(key) && uncertainCells_.size() >= areaCellLimit)
                    throw std::invalid_argument("independent uncertain area cell bound exceeded");
                if (squared < radius * radius && !area_.contains(key) &&
                    area_.size() >= areaCellLimit)
                    throw std::invalid_argument("independent area cell bound exceeded");
                uncertainCells_.insert(key);
                if (squared < radius * radius) area_.insert(key);
            }
        }
}

void IndependentMetrics::EndMotions() {
    InvalidateOnFailure guard(initialized_);
    if (!initialized_ || !frameOpen_) throw std::invalid_argument("metric motion frame not open");
    int maximumRadius = 1;
    for (const auto& sweep : sweeps_)
        if (sweep.turns) {
            const auto q = int64_t(sweep.position.q) - sweep.center.q,
                       r = int64_t(sweep.position.r) - sweep.center.r;
            maximumRadius =
                std::max(maximumRadius,
                         static_cast<int>(std::max({std::abs(q), std::abs(r), std::abs(q + r)})));
        }
    // The area metric raster cadence is separate from continuous-collision certification.
    const int exponent = std::max(3, 2 + static_cast<int>(std::round(std::log2(maximumRadius))));
    if (exponent > 16) throw std::invalid_argument("independent metric sweep bound exceeded");
    const int steps = 1 << exponent;
    for (const auto& sweep : sweeps_) {
        const double cx = gridX * (sweep.center.q + sweep.center.r * .5),
                     cy = gridY * sweep.center.r;
        const double px = gridX * (sweep.position.q + sweep.position.r * .5),
                     py = gridY * sweep.position.r;
        const double dx = gridX * (sweep.translation.q + sweep.translation.r * .5),
                     dy = gridY * sweep.translation.r;
        // Pixel-grid anisotropy and the reference float arithmetic can change boundary membership.
        const double uncertainty = .05 + (sweep.turns ? .03 * maximumRadius : 0.) +
                                   16. * std::numeric_limits<float>::epsilon() *
                                       (std::abs(cx) + std::abs(cy) + std::abs(px) + std::abs(py) +
                                        std::abs(dx) + std::abs(dy));
        for (int sample = 1; sample < steps; ++sample) {
            const double t = static_cast<double>(sample) / steps,
                         angle = sweep.turns * std::acos(-1.) / 3 * t;
            RasterPoint(cx + (px - cx) * std::cos(angle) - (py - cy) * std::sin(angle) + dx * t,
                        cy + (px - cx) * std::sin(angle) + (py - cy) * std::cos(angle) + dy * t,
                        sweep.radius, uncertainty);
        }
    }
    sweeps_.clear();
    frameOpen_ = false;
    ++work_.motionFrames;
}

io::SolutionMetrics IndependentMetrics::Finish(int cycles) const {
    if (!initialized_ || frameOpen_) throw std::invalid_argument("unfinished metric collection");
    return {MetricValue(cycles), MetricValue(cost_),
            MetricValue(static_cast<int64_t>(area_.size())), MetricValue(instructions_)};
}

} // namespace opusoptima::verification
