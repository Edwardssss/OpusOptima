#include "StaticProductionVerifier.hpp"
#include "ContinuousCollision.hpp"
#include "IndependentCapabilities.hpp"
#include "IndependentMetrics.hpp"
#include "IndependentVerifier.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace opusoptima::verification {
namespace {
using Cell = std::pair<int, int>;
using Id = std::uint64_t;
using Edge = std::pair<Id, Id>;
struct Unsupported : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct Rejected : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct TimedOut : std::runtime_error {
    using std::runtime_error::runtime_error;
};
constexpr std::array<Cell, 6> directions{{{1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {1, -1}}};
Cell Add(Cell a, Cell b) {
    return {a.first + b.first, a.second + b.second};
}
Cell Sub(Cell a, Cell b) {
    return {a.first - b.first, a.second - b.second};
}
Cell Rotate(Cell c, int n) {
    for (int i = 0; i < (n % 6 + 6) % 6; ++i)
        c = {-c.second, c.first + c.second};
    return c;
}
Cell Position(core::HexCoord c) {
    return {c.q, c.r};
}
int Radius(Cell c) {
    return std::max({std::abs(c.first), std::abs(c.second), std::abs(c.first + c.second)});
}
Edge Bond(Id a, Id b) {
    return {std::min(a, b), std::max(a, b)};
}
bool Mask(unsigned bits) {
    return bits == 1 || (bits && !(bits & ~14U));
}
struct Frame {
    Cell origin;
    int rotation{};
    Cell World(Cell c) const {
        return Add(origin, Rotate(c, rotation));
    }
    Cell Local(Cell c) const {
        return Rotate(Sub(c, origin), 6 - rotation);
    }
};
struct Chamber {
    Cell center;
    int radius{}, stretch{};
    std::set<Cell> inside, walls;
};
struct Atom {
    int type{};
    Cell position;
};
struct Arm {
    Cell base;
    int length{}, direction{}, room{}, grabbers{1};
    bool piston{}, wheel{}, halted{}, grabbing{};
    std::vector<std::optional<Id>> grips;
    std::map<int, char> tape;
    int start{};
    bool finite{};
};
struct ArmMotionSnapshot {
    Cell base;
    int length{}, direction{}, room{}, grabbers{};
    bool wheel{};
    std::vector<std::optional<Id>> grips;
    char action{};
};
struct Port {
    std::size_t index{};
    Frame frame;
    int room{};
};
struct Pipe {
    std::size_t index{}, part{};
    Frame frame;
    int room{};
    std::vector<Cell> shape;
};
struct Transit {
    std::size_t source{}, destination{};
    std::vector<Id> atoms;
    std::map<Id, Atom> material;
    std::map<Edge, std::uint8_t> bonds;
};
struct Products {
    std::vector<std::pair<Cell, int>> atoms;
};
struct Motion {
    Cell center{}, translation{};
    int turn{};
};
struct Point {
    double x{}, y{};
};
Point XY(Cell c) {
    return {c.first + c.second * 0.5, c.second * std::sqrt(3.0) * 0.5};
}
Point AtMotion(Cell cell, const Motion& motion, double t) {
    const auto relative = XY(Sub(cell, motion.center));
    const auto origin = XY(motion.center);
    const auto delta = XY(motion.translation);
    constexpr double pi = 3.14159265358979323846;
    const double angle = (motion.turn == 5 ? -1 : motion.turn) * pi / 3 * t;
    return {origin.x + relative.x * std::cos(angle) - relative.y * std::sin(angle) + delta.x * t,
            origin.y + relative.x * std::sin(angle) + relative.y * std::cos(angle) + delta.y * t};
}

class Replay {
    public:
    Replay(const io::PuzzleData& puzzle, const io::SolutionData& solution, int limit,
           bool allowOrdinary = false, bool certify = false, bool collectTimings = false,
           std::optional<std::chrono::steady_clock::time_point> started = std::nullopt,
           bool repeating = false)
        : puzzle_(puzzle), solution_(solution), allowOrdinary_(allowOrdinary), certify_(certify),
          collectTimings_(collectTimings), repeating_(repeating), limit_(limit), sharedBudget_(started.has_value()),
          started_(started.value_or(std::chrono::steady_clock::now())) {}
    StaticProductionReport Run() {
        Measure(io::VerificationPhase::Setup, [&] {
            if (sharedBudget_) Budget();
            CheckScope();
            if (sharedBudget_) Budget();
            if (certify_) metrics_.Initialize(puzzle_, solution_);
            Spawn(0);
            Observe();
            Boundaries();
        });
        for (int cycle = 0; cycle < limit_; ++cycle) {
            currentCycle_ = cycle;
            phase_ = StaticProductionEvent::Phase::GrabDrop;
            Budget();
            std::set<Id> dropped;
            Measure(io::VerificationPhase::GrabDrop, [&] {
                GrabDrop(cycle, dropped);
                Capture(dropped);
            });
            phase_ = StaticProductionEvent::Phase::BeforeMotion;
            Measure(io::VerificationPhase::Spawn, [&] {
                Spawn(cycle);
                Observe();
                if (certify_) Boundaries();
            });
            Measure(io::VerificationPhase::Glyphs, [&] {
                Glyphs(cycle, false);
                Observe();
            });
            Measure(io::VerificationPhase::Outputs, [&] { ConsumeOutputs(cycle); });
            phase_ = StaticProductionEvent::Phase::Motion;
            Measure(io::VerificationPhase::Motions, [&] { Motions(cycle); });
            phase_ = StaticProductionEvent::Phase::AfterMotion;
            Measure(io::VerificationPhase::Spawn, [&] {
                Spawn(cycle);
                Observe();
                if (certify_) Boundaries();
            });
            Measure(io::VerificationPhase::Glyphs, [&] {
                Glyphs(cycle, true);
                Observe();
            });
            Measure(io::VerificationPhase::Outputs, [&] { ConsumeOutputs(cycle); });
            Measure(io::VerificationPhase::Boundaries, [&] { Boundaries(); });
            if (certify_) Budget();
            diagnostics_.completedCycles = static_cast<std::uint64_t>(cycle) + 1;
            if (std::all_of(report_.outputs.begin(), report_.outputs.end(), [&](std::size_t n) {
                    return n >= static_cast<std::size_t>(6 * puzzle_.outputMultiplier);
                })) {
                report_.status = io::VerificationStatus::Passed;
                report_.cycles = cycle + 1;
                report_.remaining = atoms_.size();
                if (report_.acquired + reactionProduced_ !=
                    report_.disposed + emitted_ + report_.remaining + reactionConsumed_)
                    throw Rejected("independent atom accounting failed");
                report_.detail =
                    puzzle_.productionInfo
                        ? "independent static-production execution completed every ordinary output"
                        : "independent finite execution completed every ordinary output";
                Measure(io::VerificationPhase::Finish, [&] {
                    if (certify_) {
                        report_.measuredMetrics = metrics_.Finish(report_.cycles);
                        report_.metricsConclusive = metrics_.CertificationEligible();
                        report_.continuousCertified = true;
                        Budget();
                    }
                });
                if (allowOrdinary_) report_.diagnostics = Diagnostics();
                return report_;
            }
        }
        report_.status = io::VerificationStatus::CycleLimit;
        report_.cycles = limit_;
        report_.remaining = atoms_.size();
        report_.detail = "independent execution did not complete within cycle limit";
        if (allowOrdinary_) report_.diagnostics = Diagnostics();
        return report_;
    }
    StaticProductionReport Failed(io::VerificationStatus status, const std::string& detail) const {
        auto report = report_;
        report.status = status;
        report.detail = detail;
        report.cycles = currentCycle_;
        report.remaining = atoms_.size();
        if (allowOrdinary_) report.diagnostics = Diagnostics();
        return report;
    }

    private:
    template <class Action> void Measure(io::VerificationPhase phase, Action action) {
        diagnostics_.phase = phase;
        MeasureWork(diagnostics_.phaseMicros[static_cast<std::size_t>(phase)], action);
    }
    template <class Action> auto MeasureWork(std::uint64_t& micros, Action action) const {
        if (!collectTimings_) return action();
        const auto start = std::chrono::steady_clock::now();
        struct Timer {
            std::uint64_t& micros;
            std::chrono::steady_clock::time_point start;
            ~Timer() {
                micros += static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - start)
                        .count());
            }
        } timer{micros, start};
        return action();
    }
    void Count(io::VerificationCounter counter, std::uint64_t n = 1) const {
        if (allowOrdinary_) diagnostics_.counters[static_cast<std::size_t>(counter)] += n;
    }
    io::VerificationDiagnostics Diagnostics() const {
        auto snapshot = diagnostics_;
        snapshot.cycle = currentCycle_;
        snapshot.inventory = atoms_.size();
        snapshot.inventoryHighWater =
            std::max<std::uint64_t>(snapshot.inventoryHighWater, atoms_.size());
        snapshot.timingsCollected = collectTimings_;
        snapshot.elapsedMicros =
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                           std::chrono::steady_clock::now() - started_)
                                           .count());
        snapshot.outputs.assign(report_.outputs.begin(), report_.outputs.end());
        const auto put = [&](io::VerificationCounter counter, std::uint64_t value) {
            snapshot.counters[static_cast<std::size_t>(counter)] = value;
        };
        put(io::VerificationCounter::KernelCalls, kernelWork_.calls);
        put(io::VerificationCounter::KernelLinear, kernelWork_.linear);
        put(io::VerificationCounter::KernelRigid, kernelWork_.rigid);
        put(io::VerificationCounter::KernelBroad, kernelWork_.broad);
        put(io::VerificationCounter::KernelSegments, kernelWork_.segments);
        put(io::VerificationCounter::KernelMaxDepth, kernelWork_.maxDepth);
        put(io::VerificationCounter::KernelUncertain, kernelWork_.uncertain);
        const auto& metrics = metrics_.WorkStats();
        put(io::VerificationCounter::RasterPoints, metrics.rasterPoints);
        put(io::VerificationCounter::RasterCandidates, metrics.rasterCandidates);
        put(io::VerificationCounter::MetricMarkedCells, metrics.markedCells);
        put(io::VerificationCounter::MotionFrames, metrics.motionFrames);
        snapshot.trace.reserve(std::min<std::size_t>(traceCount_, io::kVerificationTraceLimit));
        const auto size = std::min<std::size_t>(traceCount_, io::kVerificationTraceLimit);
        const auto begin = traceCount_ >= io::kVerificationTraceLimit
                               ? traceCount_ % io::kVerificationTraceLimit
                               : 0;
        for (std::size_t i = 0; i < size; ++i)
            snapshot.trace.push_back(recentTrace_[(begin + i) % io::kVerificationTraceLimit]);
        snapshot.traceTruncated =
            snapshot.traceTruncated || traceCount_ > io::kVerificationTraceLimit;
        return snapshot;
    }
    void Event(StaticProductionEvent::Kind kind, int cycle, std::size_t object,
               std::vector<Id> atoms) {
        if (allowOrdinary_) {
            auto& entry = recentTrace_[traceCount_++ % io::kVerificationTraceLimit];
            entry = {};
            entry.kind = static_cast<std::uint32_t>(kind);
            entry.phase = static_cast<std::uint32_t>(phase_);
            entry.cycle = cycle;
            entry.object = static_cast<std::uint32_t>(object);
            entry.atomCount = static_cast<std::uint32_t>(
                std::min<std::size_t>(atoms.size(), io::kVerificationTraceAtoms));
            std::copy_n(atoms.begin(), entry.atomCount, entry.atoms.begin());
            entry.atomsTruncated = atoms.size() > io::kVerificationTraceAtoms;
            diagnostics_.traceTruncated |= entry.atomsTruncated;
            if (atoms.size() > 16) {
                atoms.resize(16);
                report_.traceTruncated = true;
            }
        }
        if (allowOrdinary_ && report_.events.size() >= 4096) {
            report_.traceTruncated = true;
            return;
        }
        report_.events.push_back({kind, cycle, object, std::move(atoms), phase_});
        if (kind == StaticProductionEvent::Kind::Output) {
            for (const auto id : report_.events.back().atoms) {
                StaticProductionEvent::AtomState state{id, atoms_.at(id).type, Held(id), {}};
                for (const auto& [edge, bits] : bonds_) {
                    if (edge.first == id) state.bonds.emplace_back(edge.second, bits);
                    else if (edge.second == id) state.bonds.emplace_back(edge.first, bits);
                }
                report_.events.back().outputState.push_back(std::move(state));
            }
        }
    }
    void Observe() {
        if (allowOrdinary_)
            diagnostics_.inventoryHighWater =
                std::max<std::uint64_t>(diagnostics_.inventoryHighWater, atoms_.size());
        if (!certify_) return;
        MeasureWork(diagnostics_.observeMicros, [&] { ObserveFootprints(); });
    }
    void ObserveFootprints() {
        for (const auto& [id, atom] : atoms_) {
            (void)id;
            metrics_.ObserveAtom({atom.position.first, atom.position.second});
        }
        for (const auto& arm : arms_)
            metrics_.ObserveArm({arm.base.first, arm.base.second}, arm.length, arm.direction,
                                arm.wheel ? 6 : arm.grabbers, arm.wheel);
        for (const auto& [cell, type] : Wheels()) {
            (void)type;
            metrics_.ObserveAtom({cell.first, cell.second});
        }
    }
    void Budget() const {
        if (std::chrono::steady_clock::now() > deadline_)
            throw TimedOut("independent execution wall-clock budget exhausted");
    }
    int Word(const io::SolutionObject& part, std::size_t i) const {
        if (i >= part.bodyData.size()) throw Rejected("truncated part data");
        return part.bodyData[i];
    }
    void Permission(int bit) const {
        if (!(puzzle_.permissionBits & (1ULL << bit)))
            throw Rejected("disabled permission bit " + std::to_string(bit));
    }
    int Room(Cell cell) const {
        if (!puzzle_.productionInfo) return 0;
        int result = -1;
        for (std::size_t i = 0; i < rooms_.size(); ++i)
            if (rooms_[i].inside.contains(cell)) {
                if (result >= 0) throw Rejected("overlapping chamber interiors");
                result = static_cast<int>(i);
            }
        return result;
    }
    bool InsidePoint(Point point, int room) const {
        if (!puzzle_.productionInfo) return true;
        if (room < 0) return false;
        const auto& shape = rooms_[room];
        const auto center = XY(shape.center);
        const double r = (point.y - center.y) * 2 / std::sqrt(3.0);
        const double q = point.x - center.x - r * 0.5;
        for (int shift = 0; shift <= shape.stretch; ++shift)
            if (std::max({std::abs(q - shift), std::abs(r), std::abs(q - shift + r)}) <=
                shape.radius + 0.4)
                return true;
        return false;
    }
    void Molecule(const io::Molecule& molecule) const {
        if (molecule.atoms.empty() ||
            molecule.atoms.size() > finiteOrdinaryProfile.maxMoleculeAtoms)
            throw Unsupported("independent molecule size bound exceeded");
        std::set<Cell> cells, reached;
        for (const auto& atom : molecule.atoms) {
            const int type = static_cast<int>(atom.type);
            const auto c = Position(atom.position);
            if (type < 1 || type > 16 || type == 15)
                throw Unsupported("independent profile excludes Repeat/unknown elements");
            if (c.first < -128 || c.first > 127 || c.second < -128 || c.second > 127)
                throw Rejected("invalid molecule coordinate");
            if (!cells.insert(c).second) throw Rejected("duplicate molecule cell");
        }
        std::set<std::pair<Cell, Cell>> edges;
        for (const auto& edge : molecule.bonds) {
            auto a = Position(edge.from), b = Position(edge.to);
            if (!Mask(edge.bits) || !cells.contains(a) || !cells.contains(b) ||
                Radius(Sub(a, b)) != 1)
                throw Rejected("invalid molecule bond");
            if (b < a) std::swap(a, b);
            if (!edges.emplace(a, b).second) throw Rejected("duplicate molecule edge");
        }
        reached.insert(*cells.begin());
        for (std::size_t before = 0; before != reached.size();) {
            before = reached.size();
            for (const auto& [a, b] : edges)
                if (reached.contains(a) || reached.contains(b)) {
                    reached.insert(a);
                    reached.insert(b);
                }
        }
        if (reached != cells)
            throw Unsupported("independent disjoint-input links not yet established");
    }
    int GlyphPermission(const std::string& name) const {
        if (name == "bonder") return 8;
        if (name == "unbonder") return 9;
        if (name == "bonder-speed") return 10;
        if (name == "bonder-prisma") return 11;
        if (name == "glyph-calcification") return 12;
        if (name == "glyph-duplication") return 13;
        if (name == "glyph-projection") return 14;
        if (name == "glyph-purification") return 15;
        if (name == "glyph-life-and-death") return 16;
        if (name == "glyph-disposal") return 17;
        if (name == "glyph-unification" || name == "glyph-dispersion") return 18;
        return -1;
    }
    std::vector<Cell> GlyphCells(const std::string& name) const {
        if (name == "glyph-life-and-death") return {{0, 0}, {1, 0}, {0, 1}, {1, -1}};
        if (name == "glyph-purification") return {{0, 0}, {1, 0}, {0, 1}};
        if (name == "glyph-unification") return {{0, 0}, {0, 1}, {-1, 1}, {0, -1}, {1, -1}};
        if (name == "glyph-dispersion") return {{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {1, -1}};
        if (name == "bonder-prisma") return {{0, 0}, {0, 1}, {1, 0}};
        if (name == "bonder-speed") return {{0, 0}, {1, 0}, {0, -1}, {-1, 1}};
        if (name == "glyph-calcification" || name == "glyph-disposal" || name == "glyph-marker")
            return {{0, 0}};
        return {{0, 0}, {1, 0}};
    }
    void CheckScope() {
        if (limit_ <= 0 || limit_ > finiteOrdinaryProfile.maxCycleLimit || puzzle_.version != 3 ||
            (!puzzle_.productionInfo && !allowOrdinary_))
            throw Unsupported("independent finite format/budget unsupported");
        if (certify_ && puzzle_.productionInfo)
            throw Unsupported("production-wall-profile-uncertified");
        const io::ProductionInfo ordinaryEnvironment{};
        const auto& info = puzzle_.productionInfo ? *puzzle_.productionInfo : ordinaryEnvironment;
        if (puzzle_.outputMultiplier < 1 || puzzle_.outputMultiplier > 8 ||
            puzzle_.inputs.empty() || puzzle_.outputs.empty() || puzzle_.inputs.size() > 16 ||
            puzzle_.outputs.size() > 16 || (puzzle_.productionInfo && info.chambers.empty()) ||
            info.chambers.size() > 16 || info.conduits.size() > 16)
            throw Unsupported("independent production dimensions exceeded");
        if (info.vials.size() > 64) throw Unsupported("vial visual-record bound exceeded");
        for (const auto& vial : info.vials)
            if (vial.count < 0 || vial.count > 64)
                throw Unsupported("vial visual-count bound exceeded");
        for (const auto& room : info.chambers) {
            int radius = 0, stretch = 0;
            if (room.chamberType == "Small")
                radius = 1;
            else if (room.chamberType == "SmallWide") {
                radius = 1;
                stretch = 1;
            } else if (room.chamberType == "SmallWider") {
                radius = 1;
                stretch = 2;
            } else if (room.chamberType == "Medium")
                radius = 2;
            else if (room.chamberType == "MediumWide") {
                radius = 2;
                stretch = 1;
            } else if (room.chamberType == "Large")
                radius = 3;
            else
                throw Unsupported("unknown independent chamber geometry");
            const auto center = Position(room.position);
            if (center.first < -256 || center.first > 256 || center.second < -256 ||
                center.second > 256 || Radius(center) > 256)
                throw Rejected("chamber coordinate outside supported file range");
            Chamber shape{center, radius, stretch, {}, {}};
            for (int q = -radius - 1; q <= radius + stretch + 1; ++q)
                for (int r = -radius - 1; r <= radius + 1; ++r) {
                    int distance = 100;
                    for (int shift = 0; shift <= stretch; ++shift)
                        distance = std::min(distance, Radius({q - shift, r}));
                    if (distance <= radius)
                        shape.inside.insert(Add(center, {q, r}));
                    else if (distance == radius + 1)
                        shape.walls.insert(Add(center, {q, r}));
                }
            for (const auto& old : rooms_) {
                for (auto cell : shape.inside)
                    if (old.inside.contains(cell) || old.walls.contains(cell))
                        throw Rejected("production interiors/walls overlap");
                for (auto cell : shape.walls)
                    if (old.inside.contains(cell))
                        throw Rejected("production wall crosses another interior");
            }
            rooms_.push_back(std::move(shape));
        }
        for (const auto& molecule : puzzle_.inputs)
            Molecule(molecule);
        if (repeating_ && (puzzle_.productionInfo || certify_))
            throw Unsupported("repeating opt-in excludes production/certification");
        for (std::size_t i = 0; i < puzzle_.outputs.size(); ++i) {
            const auto& molecule = puzzle_.outputs[i];
            const auto marker = std::find_if(molecule.atoms.begin(), molecule.atoms.end(),
                [](const auto& atom) { return atom.type == core::AtomType::Repeat; });
            if (!repeating_ || marker == molecule.atoms.end()) {
                Molecule(molecule);
                continue;
            }
            repeatingDisplacements_[i] = marker->position;
            const auto checked = CheckFiniteRepeatingGraph({molecule, {}, marker->position, 6});
            if (const auto* failure = std::get_if<FiniteRepeatingGraphFailure>(&checked))
                throw Unsupported(failure->detail);
            repeatingPorts_[i] = true;
        }
        if (repeating_) {
            if (std::none_of(repeatingPorts_.begin(), repeatingPorts_.end(), [](bool p) { return p; }))
                throw Unsupported("repeating opt-in requires a repeating output");
            std::vector<FiniteRepeatingPortConfig> configs(puzzle_.outputs.size(),
                {6, static_cast<std::size_t>(puzzle_.outputMultiplier), 6});
            repeatingProgress_.emplace(configs, deadline_);
        }
        if (solution_.objects.size() > finiteOrdinaryProfile.maxParts)
            throw Unsupported("independent part count bound exceeded");
        std::set<int> numbers;
        for (std::size_t index = 0; index < solution_.objects.size(); ++index) {
            const auto& part = solution_.objects[index];
            const int rotation = Word(part, 1);
            const Frame frame{Position(part.position), rotation};
            if (rotation < 0 || rotation >= 6 || frame.origin.first < -1024 ||
                frame.origin.first > 1024 || frame.origin.second < -1024 ||
                frame.origin.second > 1024 || Radius(frame.origin) > 1024) {
                if (allowOrdinary_)
                    throw Unsupported("part transform exceeds the finite profile bounds");
                throw Rejected("part transform outside supported range");
            }
            const int room = Room(frame.origin);
            const bool mechanical = part.typeName == "arm1" || part.typeName == "arm2" ||
                                    part.typeName == "arm3" || part.typeName == "arm6" ||
                                    part.typeName == "piston" || part.typeName == "baron";
            if (mechanical) {
                if (arms_.size() >= finiteOrdinaryProfile.maxArms)
                    throw Unsupported("independent arm count bound exceeded");
                const bool wheel = part.typeName == "baron", piston = part.typeName == "piston";
                Permission(wheel ? 28 : piston ? 2 : part.typeName == "arm1" ? 0 : 1);
                const int length = wheel ? 1 : Word(part, 0),
                          grabbers = wheel                     ? 0
                                     : part.typeName == "arm2" ? 2
                                     : part.typeName == "arm3" ? 3
                                     : part.typeName == "arm6" ? 6
                                                               : 1;
                if (allowOrdinary_ && (length < 1 || length > finiteOrdinaryProfile.maxArmLength))
                    throw Unsupported("arm length exceeds the finite profile bounds");
                const bool staticWheel = wheel && part.instructions.empty();
                const bool duplicateNumber =
                    !(allowOrdinary_ && staticWheel) && !numbers.insert(Word(part, 4)).second;
                if (allowOrdinary_ && duplicateNumber)
                    throw Unsupported("active arm numbering is ambiguous in the finite profile");
                if (room < 0 || length < 1 || length > 3 || duplicateNumber)
                    throw Rejected("invalid arm placement/length/identity");
                Arm arm{frame.origin,
                        length,
                        rotation,
                        room,
                        grabbers,
                        piston,
                        wheel,
                        false,
                        false,
                        std::vector<std::optional<Id>>(grabbers),
                        {}};
                for (const auto& instruction : part.instructions) {
                    const bool outsideTapePosition =
                        instruction.parameter < -finiteOrdinaryProfile.maxTapePosition ||
                        instruction.parameter > finiteOrdinaryProfile.maxTapePosition;
                    if (allowOrdinary_ && outsideTapePosition)
                        throw Unsupported("tape position exceeds the finite profile bounds");
                    if (outsideTapePosition ||
                        !arm.tape.emplace(instruction.parameter, instruction.action).second)
                        throw Rejected("invalid or duplicate tape position");
                    switch (instruction.action) {
                    case 'G':
                        Permission(23);
                        break;
                    case 'g':
                    case 'R':
                    case 'r':
                        Permission(22);
                        break;
                    case 'A':
                    case 'a':
                        Permission(3);
                        break;
                    case 'E':
                    case 'e':
                        Permission(2);
                        if (!piston) throw Rejected("extension on non-piston");
                        break;
                    case 'P':
                    case 'p':
                        Permission(26);
                        if (grabbers != 1) {
                            if (allowOrdinary_)
                                throw Unsupported("multi-gripper pivot profile not established");
                            throw Rejected("pivot on unsupported gripper geometry");
                        }
                        break;
                    case 'O':
                        Permission(25);
                        break;
                    case ' ':
                    case 'B':
                        break;
                    default:
                        throw Unsupported(
                            "unexpanded repeat/reset or unknown independent instruction");
                    }
                    if (instruction.action == 'B') arm.finite = true;
                }
                if (!arm.tape.empty()) {
                    arm.start = arm.tape.begin()->first;
                    if (!arm.finite)
                        period_ = std::max(period_, arm.tape.rbegin()->first - arm.start + 1);
                    if (arm.start > 0) {
                        for (auto it = arm.tape.begin(); it != arm.tape.end();) {
                            auto node = arm.tape.extract(it++);
                            node.key() -= arm.start;
                            arm.tape.insert(std::move(node));
                        }
                    } else if (arm.start < 0) {
                        // Raising keys must proceed backwards to avoid unprocessed keys.
                        auto it = arm.tape.end();
                        while (it != arm.tape.begin()) {
                            auto current = std::prev(it);
                            auto node = arm.tape.extract(current);
                            node.key() -= arm.start;
                            it = arm.tape.insert(std::move(node)).position;
                        }
                    }
                }
                for (int grip = 0; grip < std::max(grabbers, 1); ++grip)
                    if (Room(Add(arm.base,
                                 Rotate({length, 0},
                                        rotation + (grabbers ? grip * 6 / grabbers : 0)))) != room)
                        throw Rejected("initial arm tip reaches outside its room");
                arms_.push_back(std::move(arm));
            } else if (part.typeName == "input" || part.typeName == "out-std" ||
                       (repeating_ && part.typeName == "out-rep")) {
                if (!part.instructions.empty()) throw Rejected("non-arm port carries tape");
                const int port = Word(part, 2);
                const auto& molecules = part.typeName == "input" ? puzzle_.inputs : puzzle_.outputs;
                if (port < 0 || static_cast<std::size_t>(port) >= molecules.size())
                    throw Rejected("invalid port index");
                if (repeating_ && part.typeName != "input" &&
                    ((part.typeName == "out-rep") != repeatingPorts_[port]))
                    throw Unsupported("repeating output kind does not match template");
                int containing = -1;
                for (const auto& atom : molecules[port].atoms) {
                    const int next = Room(frame.World(Position(atom.position)));
                    if (next < 0 || (containing >= 0 && containing != next))
                        throw Rejected("port crosses a room wall");
                    containing = next;
                }
                (part.typeName == "input" ? inputs_ : outputs_)
                    .push_back({static_cast<std::size_t>(port), frame, containing});
            } else if (part.typeName == "pipe") {
                if (!puzzle_.productionInfo)
                    throw Unsupported("ordinary conduit profile not established");
                if (!part.instructions.empty()) throw Rejected("pipe carries tape");
                const int identity = Word(part, 5) - 100, count = Word(part, 6);
                if (identity < 0 || static_cast<std::size_t>(identity) >= info.conduits.size() ||
                    count < 1 || count > 64 || room < 0)
                    throw Rejected("invalid conduit identity/shape/room");
                const auto& original = info.conduits[identity];
                if (original.path.size() != static_cast<std::size_t>(count))
                    throw Rejected("altered conduit length");
                Pipe pipe{static_cast<std::size_t>(identity), index, frame, room, {}};
                std::set<Cell> unique;
                for (int cell = 0; cell < count; ++cell) {
                    const Cell offset{Word(part, 7 + 2 * cell), Word(part, 8 + 2 * cell)};
                    if (offset != Position(original.path[cell]) || !unique.insert(offset).second ||
                        Room(frame.World(offset)) != room)
                        throw Rejected("altered ordered conduit footprint");
                    pipe.shape.push_back(offset);
                }
                pipes_.push_back(std::move(pipe));
            } else if (part.typeName == "track") {
                Permission(3);
                if (!part.instructions.empty()) throw Rejected("track carries tape");
                const int count = Word(part, 4);
                if (count < 1 || count > 256) throw Unsupported("independent track bound exceeded");
                std::vector<Cell> path;
                for (int cell = 0; cell < count; ++cell) {
                    const Cell offset{Word(part, 5 + 2 * cell), Word(part, 6 + 2 * cell)};
                    if (offset.first < -1024 || offset.first > 1024 || offset.second < -1024 ||
                        offset.second > 1024)
                        throw Rejected("track coordinate outside supported range");
                    // Track offsets are translated, never rotated, in the solution format.
                    const auto position = Add(frame.origin, offset);
                    if (Room(position) < 0) throw Rejected("track outside chamber");
                    if (!path.empty() && Radius(Sub(position, path.back())) != 1)
                        throw Rejected("nonadjacent track path");
                    if (std::find(path.begin(), path.end(), position) != path.end())
                        throw Unsupported("independent looping track profile not established");
                    path.push_back(position);
                }
                if (path.size() > 2 && Radius(Sub(path.front(), path.back())) == 1)
                    throw Unsupported("independent looping track profile not established");
                tracks_.push_back(std::move(path));
            } else {
                const int permission = GlyphPermission(part.typeName);
                if (permission < 0)
                    throw Unsupported("independent glyph not implemented: " + part.typeName);
                Permission(permission);
                if (!part.instructions.empty()) throw Rejected("glyph carries tape");
                for (auto cell : GlyphCells(part.typeName))
                    if (room < 0 || Room(frame.World(cell)) != room)
                        throw Rejected("glyph crosses a room boundary");
                glyphs_.insert(index);
            }
        }
        if (pipes_.size() != 2 * info.conduits.size())
            throw Rejected("required conduit pair missing");
        for (std::size_t index = 0; index < info.conduits.size(); ++index) {
            std::vector<int> actual;
            for (const auto& pipe : pipes_)
                if (pipe.index == index) actual.push_back(pipe.room);
            const auto& original = info.conduits[index];
            const int a = Room(Position(original.positionA)),
                      b = Room(Position(original.positionB));
            std::vector<int> expected{a, b};
            std::sort(actual.begin(), actual.end());
            std::sort(expected.begin(), expected.end());
            if (a < 0 || b < 0 || a == b || actual != expected)
                throw Rejected("conduit endpoint moved to a different chamber");
        }
        if (info.isolation)
            for (const auto& input : inputs_)
                for (const auto& output : outputs_)
                    if (input.room == output.room)
                        throw Rejected("input/output isolation violated");
        if (!arms_.empty()) {
            const auto first =
                std::min_element(arms_.begin(), arms_.end(), [](const auto& a, const auto& b) {
                    return a.start < b.start;
                })->start;
            for (auto& arm : arms_)
                arm.start -= first;
        }
        period_ = std::max(period_, 1);
        if (repeating_) {
            std::array<unsigned, 16> bindings{};
            for (const auto& port : outputs_) ++bindings[port.index];
            for (std::size_t i = 0; i < puzzle_.outputs.size(); ++i)
                if (bindings[i] != 1) throw Unsupported("repeating requires unique output bindings");
        }
        report_.outputs.resize(puzzle_.outputs.size());
    }
    std::optional<Id> At(Cell cell) const {
        Count(io::VerificationCounter::AtomLookups);
        for (const auto& [id, atom] : atoms_)
            if (atom.position == cell) return id;
        return {};
    }
    std::vector<Id> Fragment(Id atom) const {
        return MeasureWork(diagnostics_.fragmentMicros, [&]() -> std::vector<Id> {
            Count(io::VerificationCounter::FragmentQueries);
            if (!atoms_.contains(atom)) throw Rejected("unknown or consumed atom identity");
            std::set<Id> ids{atom};
            for (std::size_t before = 0; before != ids.size();) {
                before = ids.size();
                if (repeating_) Budget();
                Count(io::VerificationCounter::FragmentEdgeVisits, bonds_.size());
                for (const auto& [edge, bits] : bonds_)
                    if (ids.contains(edge.first) || ids.contains(edge.second)) {
                        ids.insert(edge.first);
                        ids.insert(edge.second);
                    }
            }
            return {ids.begin(), ids.end()};
        });
    }
    bool Held(Id atom) const {
        return MeasureWork(diagnostics_.heldMicros, [&] {
            Count(io::VerificationCounter::HeldQueries);
            for (const auto& arm : arms_)
                for (const auto& grip : arm.grips)
                    if (grip) {
                        const auto fragment = Fragment(*grip);
                        if (std::binary_search(fragment.begin(), fragment.end(), atom)) return true;
                    }
            return false;
        });
    }
    bool Free(Id atom) const {
        return !recentlyUnbonded_.contains(atom) && !Held(atom) && Fragment(atom).size() == 1;
    }
    void Remove(const std::vector<Id>& ids) {
        for (auto id : ids)
            atoms_.erase(id);
        std::erase_if(bonds_, [&](const auto& edge) {
            return !atoms_.contains(edge.first.first) || !atoms_.contains(edge.first.second);
        });
    }
    std::map<Cell, int> Wheels() const {
        const std::array<int, 6> types{1, 5, 2, 1, 4, 3};
        std::map<Cell, int> cells;
        for (const auto& arm : arms_)
            if (arm.wheel)
                for (int i = 0; i < 6; ++i) {
                    const auto cell = Add(arm.base, Rotate(directions[i], arm.direction));
                    if (!cells.emplace(cell, types[i]).second)
                        throw Rejected("overlapping wheel atoms");
                }
        return cells;
    }
    void Spawn(int cycle) {
        const auto wheels = Wheels();
        for (const auto& port : inputs_) {
            const auto& input = puzzle_.inputs[port.index];
            if (std::any_of(input.atoms.begin(), input.atoms.end(), [&](const auto& atom) {
                    const auto cell = port.frame.World(Position(atom.position));
                    return At(cell).has_value() || wheels.contains(cell);
                }))
                continue;
            if (atoms_.size() > finiteOrdinaryProfile.maxInventory ||
                input.atoms.size() > finiteOrdinaryProfile.maxInventory - atoms_.size())
                throw Unsupported("independent inventory bound exceeded");
            std::map<Cell, Id> ids;
            std::vector<Id> batch;
            for (const auto& atom : input.atoms) {
                const auto id = next_++;
                ids[Position(atom.position)] = id;
                batch.push_back(id);
                atoms_.emplace(id, Atom{static_cast<int>(atom.type),
                                        port.frame.World(Position(atom.position))});
            }
            for (const auto& edge : input.bonds)
                bonds_[Bond(ids.at(Position(edge.from)), ids.at(Position(edge.to)))] = edge.bits;
            report_.acquired += batch.size();
            Event(StaticProductionEvent::Kind::Acquire, cycle, port.index, std::move(batch));
        }
        if (atoms_.size() > finiteOrdinaryProfile.maxInventory)
            throw Unsupported("independent inventory bound exceeded");
    }
    char Instruction(const Arm& arm, int cycle) const {
        if (arm.halted || cycle < arm.start) return ' ';
        const int elapsed = cycle - arm.start;
        const auto instruction = arm.tape.find(arm.finite ? elapsed : elapsed % period_);
        return instruction == arm.tape.end() ? ' ' : instruction->second;
    }
    void GrabDrop(int cycle, std::set<Id>& dropped) {
        for (auto& arm : arms_) {
            const auto action = Instruction(arm, cycle);
            if (arm.wheel) continue;
            if (action == 'G' && !arm.grabbing) {
                arm.grabbing = true;
                for (std::size_t grip = 0; grip < arm.grips.size(); ++grip)
                    arm.grips[grip] = At(Add(
                        arm.base, Rotate({arm.length, 0}, arm.direction + static_cast<int>(grip) *
                                                                              6 / arm.grabbers)));
                if (allowOrdinary_) {
                    std::vector<Id> ids;
                    for (auto id : arm.grips)
                        if (id) ids.push_back(*id);
                    Event(StaticProductionEvent::Kind::Grab, cycle,
                          static_cast<std::size_t>(&arm - arms_.data()), std::move(ids));
                }
            } else if (action == 'g') {
                if (allowOrdinary_) {
                    std::vector<Id> ids;
                    for (auto id : arm.grips)
                        if (id) ids.push_back(*id);
                    Event(StaticProductionEvent::Kind::Release, cycle,
                          static_cast<std::size_t>(&arm - arms_.data()), std::move(ids));
                }
                arm.grabbing = false;
                for (auto& grip : arm.grips)
                    if (grip) {
                        const auto fragment = Fragment(*grip);
                        dropped.insert(fragment.begin(), fragment.end());
                        grip.reset();
                    }
            }
        }
    }
    void CertifiedMotions(const std::map<Id, Atom>& before,
                          const std::vector<ArmMotionSnapshot>& oldArms,
                          const std::map<Id, Motion>& motions) {
        MeasureWork(diagnostics_.collisionMicros, [&] {
            std::set<Cell> initialPositions;
            const auto insert = [&](Cell cell) {
                if (!initialPositions.insert(cell).second)
                    throw Rejected("independent instantaneous body overlap");
            };
            for (const auto& [id, atom] : before) insert(atom.position);
            for (const auto& old : oldArms) {
                insert(old.base);
                if (old.wheel)
                    for (auto offset : directions)
                        insert(Add(old.base, Rotate(offset, old.direction)));
            }
            // A base swap meets at the shared edge midpoint in both motion directions.
            // This exact lattice case does not depend on the unproved atom rotation model.
            for (std::size_t i = 0; i < oldArms.size(); ++i)
                for (std::size_t j = 0; j < i; ++j)
                    if (oldArms[i].base != oldArms[j].base &&
                        arms_[i].base == oldArms[j].base && arms_[j].base == oldArms[i].base)
                        throw Rejected("independent simultaneous base swap");
            for (const auto& [id, motion] : motions)
                if (motion.turn != 0 || motion.translation != Cell{})
                    throw Unsupported("dynamic reference trajectories and area are not certified");
            for (std::size_t i = 0; i < oldArms.size(); ++i)
                if (oldArms[i].base != arms_[i].base || oldArms[i].length != arms_[i].length ||
                    oldArms[i].direction != arms_[i].direction ||
                    (oldArms[i].wheel && (oldArms[i].action == 'r' || oldArms[i].action == 'R')))
                    throw Unsupported("dynamic reference trajectories and area are not certified");
        });
        // Distinct stationary lattice sites are at least sqrt(41^2 + 71^2)
        // pixels apart, exceeding every supported pair's combined radius (58).
        metrics_.BeginMotions();
        for (const auto& [id, atom] : before)
            metrics_.ObserveAtomMotion({atom.position.first, atom.position.second}, {}, {}, 0);
        for (std::size_t i = 0; i < oldArms.size(); ++i) {
            const auto& old = oldArms[i];
            metrics_.ObserveArmMotion({old.base.first, old.base.second}, old.length, old.direction,
                                      {old.base.first, old.base.second}, old.length,
                                      old.direction, old.wheel ? 6 : old.grabbers, old.wheel);
            if (old.wheel)
                for (auto offset : directions) {
                    const auto cell = Add(old.base, Rotate(offset, old.direction));
                    metrics_.ObserveAtomMotion({cell.first, cell.second}, {}, {}, 0);
                }
        }
        MeasureWork(diagnostics_.rasterMicros, [&] { metrics_.EndMotions(); });
    }
    void Motions(int cycle) {
        const auto before = MeasureWork(diagnostics_.snapshotMicros, [&] { return atoms_; });
        const auto oldArms = MeasureWork(diagnostics_.snapshotMicros, [&] {
            std::vector<ArmMotionSnapshot> snapshots;
            snapshots.reserve(arms_.size());
            for (const auto& arm : arms_)
                snapshots.push_back({arm.base, arm.length, arm.direction, arm.room, arm.grabbers,
                                     arm.wheel, arm.grips, Instruction(arm, cycle)});
            return snapshots;
        });
        std::map<Id, Motion> motions;
        std::vector<Motion> baseMotions(arms_.size());
        for (std::size_t index = 0; index < arms_.size(); ++index) {
            auto& arm = arms_[index];
            const auto& old = oldArms[index];
            const auto action = old.action;
            Motion motion;
            motion.center = old.base;
            Cell baseDelta{};
            if (action == 'R' || action == 'r') {
                motion.turn = action == 'r' ? 1 : 5;
                arm.direction = (arm.direction + motion.turn) % 6;
            } else if (action == 'P' || action == 'p') {
                motion.turn = action == 'p' ? 1 : 5;
                motion.center = Add(old.base, Rotate({old.length, 0}, old.direction));
            } else if (action == 'E' || action == 'e') {
                const int next = std::clamp(arm.length + (action == 'E' ? 1 : -1), 1, 3);
                motion.translation = Rotate({next - arm.length, 0}, old.direction);
                arm.length = next;
            } else if (action == 'A' || action == 'a') {
                const std::vector<Cell>* path = nullptr;
                std::size_t position = 0;
                for (const auto& track : tracks_) {
                    const auto found = std::find(track.begin(), track.end(), old.base);
                    if (found != track.end()) {
                        if (path) throw Unsupported("ambiguous overlapping track reset/motion");
                        path = &track;
                        position = found - track.begin();
                    }
                }
                if (!path) throw Rejected("track instruction without a track");
                const auto destination =
                    (*path)[action == 'A' ? std::min(position + 1, path->size() - 1)
                            : position    ? position - 1
                                          : 0];
                baseDelta = Sub(destination, old.base);
                arm.base = destination;
                motion.translation = baseDelta;
            } else if (action == 'B')
                arm.halted = true;
            baseMotions[index].translation = baseDelta;
            if (Room(arm.base) != old.room) throw Rejected("arm base crosses production boundary");
            for (int grip = 0; grip < std::max(arm.grabbers, 1); ++grip)
                if (Room(Add(arm.base, Rotate({arm.length, 0},
                                              arm.direction +
                                                  (arm.grabbers ? grip * 6 / arm.grabbers : 0)))) !=
                    old.room)
                    throw Rejected("arm tip crosses production boundary");
            for (const auto& grip : old.grips)
                if (grip)
                    for (auto atom : Fragment(*grip)) {
                        const auto prior = motions.find(atom);
                        if (prior != motions.end()) {
                            const auto a = AtMotion(before.at(atom).position, prior->second, 1),
                                       b = AtMotion(before.at(atom).position, motion, 1);
                            if (std::abs(a.x - b.x) > 1e-6 || std::abs(a.y - b.y) > 1e-6)
                                throw Rejected(
                                    "multiple grippers impose inconsistent material motion");
                            if (prior->second.turn != motion.turn ||
                                prior->second.translation != motion.translation ||
                                (motion.turn && prior->second.center != motion.center))
                                throw Unsupported(
                                    "independent cooperative curved motion not established");
                        } else
                            motions.emplace(atom, motion);
                    }
        }
        if (certify_)
            CertifiedMotions(before, oldArms, motions);
        else {
            MeasureWork(diagnostics_.collisionMicros, [&] {
                // Simultaneous trajectories are compared in one world snapshot. This is
                // independent sampling; native continuous-collision acceptance remains mandatory.
                constexpr int samples = 64;
                const auto oldWheels = [&]() {
                    std::vector<std::pair<std::size_t, Cell>> result;
                    for (std::size_t arm = 0; arm < oldArms.size(); ++arm)
                        if (oldArms[arm].wheel)
                            for (int i = 0; i < 6; ++i) {
                                result.emplace_back(arm,
                                                    Add(oldArms[arm].base,
                                                        Rotate(directions[i], oldArms[arm].direction)));
                            }
                    return result;
                }();
                // Unmoving pairs have the same separation at every sample.
                const auto moving = [](const Motion& motion) {
                    return motion.turn != 0 || motion.translation != Cell{};
                };
                std::set<Id> movingAtoms;
                for (const auto& [id, motion] : motions)
                    if (moving(motion)) movingAtoms.insert(id);
                std::vector<bool> movingBases(arms_.size()), movingWheels(arms_.size());
                for (std::size_t arm = 0; arm < arms_.size(); ++arm) {
                    movingBases[arm] = moving(baseMotions[arm]);
                    movingWheels[arm] =
                        oldArms[arm].wheel &&
                        (movingBases[arm] || oldArms[arm].direction != arms_[arm].direction);
                }
                const bool hasMotion = !movingAtoms.empty() ||
                                       std::any_of(movingBases.begin(), movingBases.end(),
                                                   [](bool value) { return value; }) ||
                                       std::any_of(movingWheels.begin(), movingWheels.end(),
                                                   [](bool value) { return value; });
                std::map<Id, Point> points;
                std::map<Id, int> originalRooms;
                for (const auto& [id, atom] : before) {
                    points.emplace(id, Point{});
                    originalRooms.emplace(id, Room(atom.position));
                }
                std::vector<Point> bases(oldArms.size()), wheelPoints(oldWheels.size());
                std::vector<Motion> wheelMotions(oldArms.size());
                for (std::size_t arm = 0; arm < oldArms.size(); ++arm)
                    if (oldArms[arm].wheel) {
                        wheelMotions[arm].center = oldArms[arm].base;
                        wheelMotions[arm].translation = baseMotions[arm].translation;
                        const auto action = oldArms[arm].action;
                        if (action == 'r' || action == 'R')
                            wheelMotions[arm].turn = action == 'r' ? 1 : 5;
                    }
                for (int sample = 0; sample <= (hasMotion ? samples : 0); ++sample) {
                    if (sample % 16 == 0) Budget();
                    const double t = static_cast<double>(sample) / samples;
                    for (std::size_t arm = 0; arm < oldArms.size(); ++arm)
                        bases[arm] = AtMotion(oldArms[arm].base, baseMotions[arm], t);
                    for (std::size_t wheel = 0; wheel < oldWheels.size(); ++wheel)
                        wheelPoints[wheel] =
                            AtMotion(oldWheels[wheel].second, wheelMotions[oldWheels[wheel].first], t);
                    for (std::size_t arm = 0; arm < oldArms.size(); ++arm) {
                        const auto base = bases[arm];
                        for (std::size_t other = 0; other < arm; ++other) {
                            if (sample && !movingBases[arm] && !movingBases[other]) continue;
                            const auto position = bases[other];
                            const double dx = base.x - position.x, dy = base.y - position.y;
                            if (dx * dx + dy * dy < 0.20)
                                throw Rejected("independent simultaneous arm-base collision");
                        }
                    }
                    for (const auto& [id, atom] : before)
                        points.at(id) = AtMotion(atom.position,
                                                 motions.contains(id) ? motions.at(id) : Motion{}, t);
                    for (const auto& [id, point] : points) {
                        const bool dynamic = movingAtoms.contains(id);
                        const int room = originalRooms.at(id);
                        if ((!sample || dynamic) && !InsidePoint(point, room))
                            throw Rejected("atom sweep crosses a production wall");
                        for (const auto& [other, position] : points)
                            if (other > id && (!sample || dynamic || movingAtoms.contains(other))) {
                                const double dx = point.x - position.x, dy = point.y - position.y;
                                if (dx * dx + dy * dy < 0.36)
                                    throw Rejected("independent simultaneous atom collision at cycle " +
                                                   std::to_string(cycle));
                            }
                        for (std::size_t arm = 0; arm < oldArms.size(); ++arm) {
                            if (sample && !dynamic && !movingBases[arm]) continue;
                            const auto base = bases[arm];
                            const double dx = point.x - base.x, dy = point.y - base.y;
                            if (dx * dx + dy * dy < 0.20)
                                throw Rejected("independent atom/arm-base collision");
                        }
                        for (std::size_t wheel = 0; wheel < oldWheels.size(); ++wheel) {
                            const auto arm = oldWheels[wheel].first;
                            if (sample && !dynamic && !movingWheels[arm]) continue;
                            const auto position = wheelPoints[wheel];
                            const double dx = point.x - position.x, dy = point.y - position.y;
                            if (dx * dx + dy * dy < 0.36)
                                throw Rejected("independent material/wheel collision");
                        }
                    }
                }
            });
        }
        if (allowOrdinary_) {
            std::vector<Id> ids;
            for (const auto& [id, motion] : motions)
                if (motion.turn || motion.translation != Cell{}) ids.push_back(id);
            if (!ids.empty()) Event(StaticProductionEvent::Kind::Motion, cycle, 0, std::move(ids));
        }
        for (const auto& [id, motion] : motions) {
            auto cell = before.at(id).position;
            cell = Add(Add(motion.center, Rotate(Sub(cell, motion.center), motion.turn)),
                       motion.translation);
            atoms_.at(id).position = cell;
        }
    }
    void Capture(const std::set<Id>& dropped) {
        captured_.clear();
        std::set<Id> reserved;
        for (std::size_t index = 0; index < pipes_.size(); ++index)
            for (auto atom : dropped) {
                if (!atoms_.contains(atom) || reserved.contains(atom) || Held(atom)) continue;
                const auto ids = Fragment(atom);
                const auto& pipe = pipes_[index];
                if (std::any_of(ids.begin(), ids.end(), [&](Id id) {
                        return std::find(pipe.shape.begin(), pipe.shape.end(),
                                         pipe.frame.Local(atoms_.at(id).position)) ==
                               pipe.shape.end();
                    }))
                    continue;
                const auto peer =
                    std::find_if(pipes_.begin(), pipes_.end(), [&](const auto& other) {
                        return other.index == pipe.index && other.room != pipe.room;
                    });
                if (peer == pipes_.end()) throw Rejected("conduit has no peer");
                Transit transit;
                transit.source = index;
                transit.destination = peer - pipes_.begin();
                transit.atoms = ids;
                captured_.push_back(std::move(transit));
                reserved.insert(ids.begin(), ids.end());
            }
    }
    void PipeGlyph(std::size_t part, int cycle, bool produce) {
        for (auto& transit : captured_) {
            const auto& source = pipes_[transit.source];
            const auto& destination = pipes_[transit.destination];
            if (!produce && source.part == part) {
                if (std::any_of(transit.atoms.begin(), transit.atoms.end(),
                                [&](Id id) { return !atoms_.contains(id); })) {
                    transit.atoms.clear();
                    continue;
                }
                for (const auto& [edge, bits] : bonds_) {
                    const bool a = std::binary_search(transit.atoms.begin(), transit.atoms.end(),
                                                      edge.first),
                               b = std::binary_search(transit.atoms.begin(), transit.atoms.end(),
                                                      edge.second);
                    if (a != b)
                        throw Unsupported("conduit cloning/cross-boundary bond semantics not yet "
                                          "independently established");
                    if (a) transit.bonds.emplace(edge, bits);
                }
                for (auto id : transit.atoms) {
                    auto atom = atoms_.at(id);
                    atom.position = destination.frame.World(source.frame.Local(atom.position));
                    transit.material.emplace(id, atom);
                }
                Remove(transit.atoms);
            } else if (produce && destination.part == part && !transit.material.empty()) {
                const auto wheels = Wheels();
                for (const auto& [id, atom] : transit.material) {
                    if (At(atom.position) || wheels.contains(atom.position))
                        throw Rejected("conduit arrival footprint occupied");
                    atoms_.emplace(id, atom);
                }
                bonds_.insert(transit.bonds.begin(), transit.bonds.end());
                Event(StaticProductionEvent::Kind::Transfer, cycle, source.index, transit.atoms);
                transit.material.clear();
            }
        }
    }
    void AddBond(Id a, Id b, std::uint8_t bits) {
        const auto key = Bond(a, b);
        const auto old = bonds_.contains(key) ? bonds_.at(key) : 0;
        if (old == 1 && bits != 1) return;
        if (old && bits == 1) return;
        bonds_[key] = old | bits;
    }
    void Convert(const std::vector<Id>& inputs, const std::vector<std::pair<Cell, int>>& products,
                 std::size_t glyph) {
        if (allowOrdinary_)
            Event(StaticProductionEvent::Kind::Reaction, currentCycle_, glyph, inputs);
        Remove(inputs);
        reactionConsumed_ += inputs.size();
        pending_[glyph] = {products};
    }
    void Glyphs(int cycle, bool produce) {
        recentlyUnbonded_.clear();
        const auto wheels = Wheels();
        for (std::size_t index = 0; index < solution_.objects.size(); ++index) {
            const auto& glyph = solution_.objects[index];
            if (glyph.typeName == "pipe") {
                PipeGlyph(index, cycle, produce);
                continue;
            }
            if (!glyphs_.contains(index)) continue;
            const Frame frame{Position(glyph.position), Word(glyph, 1)};
            const auto at = [&](Cell local) { return At(frame.World(local)); };
            const auto type = [&](std::optional<Id> id) { return id ? atoms_.at(*id).type : 0; };
            const auto vacant = [&](Cell local) {
                const auto cell = frame.World(local);
                return !At(cell) && !wheels.contains(cell);
            };
            if (produce && pending_.contains(index)) {
                for (const auto& [cell, element] : pending_.at(index).atoms) {
                    if (At(cell) || wheels.contains(cell))
                        throw Rejected("conversion product outlet occupied");
                    atoms_.emplace(next_++, Atom{element, cell});
                    ++reactionProduced_;
                }
                pending_.erase(index);
                continue;
            }
            const auto a = at({0, 0}), b = at({1, 0});
            const auto& name = glyph.typeName;
            if (name == "bonder" || name == "unbonder") {
                if (a && b) {
                    if (name == "bonder")
                        AddBond(*a, *b, 1);
                    else if (bonds_.erase(Bond(*a, *b))) {
                        recentlyUnbonded_.insert(*a);
                        recentlyUnbonded_.insert(*b);
                    }
                }
            } else if (name == "bonder-prisma") {
                const std::array<Cell, 3> vertices{{{0, 0}, {0, 1}, {1, 0}}};
                const std::array<std::uint8_t, 3> colors{8, 2, 4};
                for (int side = 0; side < 3; ++side) {
                    const auto x = at(vertices[side]), y = at(vertices[(side + 1) % 3]);
                    if (type(x) == 4 && type(y) == 4) AddBond(*x, *y, colors[side]);
                }
            } else if (name == "bonder-speed") {
                for (auto offset : std::array<Cell, 3>{{{1, 0}, {0, -1}, {-1, 1}}}) {
                    const auto other = at(offset);
                    if (a && other) AddBond(*a, *other, 1);
                }
            } else if (name == "glyph-calcification") {
                if (type(a) >= 2 && type(a) <= 5) atoms_.at(*a).type = 1;
            } else if (name == "glyph-duplication") {
                int catalyst = type(a);
                if (!catalyst) {
                    const auto found = wheels.find(frame.origin);
                    if (found != wheels.end()) catalyst = found->second;
                }
                if (catalyst >= 2 && catalyst <= 5 && type(b) == 1) atoms_.at(*b).type = catalyst;
            } else if (name == "glyph-projection") {
                if (type(a) == 6 && Free(*a) && type(b) >= 8 && type(b) <= 12) {
                    Remove({*a});
                    ++reactionConsumed_;
                    --atoms_.at(*b).type;
                }
            } else if (name == "glyph-disposal") {
                if (a && Free(*a)) {
                    Remove({*a});
                    ++report_.disposed;
                    Event(StaticProductionEvent::Kind::Dispose, cycle, index, {*a});
                }
            } else if (!produce && name == "glyph-purification") {
                if (a && b && Free(*a) && Free(*b) && type(a) >= 8 && type(a) <= 12 &&
                    type(a) == type(b) && vacant({0, 1}))
                    Convert({*a, *b}, {{frame.World({0, 1}), type(a) - 1}}, index);
            } else if (!produce && name == "glyph-life-and-death") {
                if (type(a) == 1 && type(b) == 1 && Free(*a) && Free(*b) && vacant({0, 1}) &&
                    vacant({1, -1}))
                    Convert({*a, *b}, {{frame.World({0, 1}), 13}, {frame.World({1, -1}), 14}},
                            index);
            } else if (!produce && name == "glyph-dispersion") {
                if (type(a) == 16 && Free(*a) && vacant({-1, 0}) && vacant({1, 0}) &&
                    vacant({0, -1}) && vacant({1, -1}))
                    Convert({*a},
                            {{frame.World({-1, 0}), 2},
                             {frame.World({1, 0}), 3},
                             {frame.World({0, -1}), 4},
                             {frame.World({1, -1}), 5}},
                            index);
            } else if (!produce && name == "glyph-unification") {
                const std::array<Cell, 4> contacts{{{0, 1}, {-1, 1}, {0, -1}, {1, -1}}};
                std::set<int> elements;
                std::vector<Id> operands;
                for (auto cell : contacts) {
                    const auto operand = at(cell);
                    if (operand && Free(*operand)) {
                        elements.insert(type(operand));
                        operands.push_back(*operand);
                    }
                }
                if (elements == std::set<int>{2, 3, 4, 5} && operands.size() == 4 && vacant({0, 0}))
                    Convert(operands, {{frame.origin, 16}}, index);
            }
        }
    }
    void ConsumeOutputs(int cycle) {
        for (const auto& port : outputs_) {
            const auto& output = puzzle_.outputs[port.index];
            if (repeating_ && repeatingPorts_[port.index]) {
                Budget();
                io::Molecule component;
                if (const auto anchor = At(port.frame.World({0, 0}))) {
                    const auto fragment = Fragment(*anchor);
                    Budget();
                    std::set<Id> ids(fragment.begin(), fragment.end());
                    for (const auto id : fragment) {
                        const auto& atom = atoms_.at(id);
                        const auto local = port.frame.Local(atom.position);
                        component.atoms.push_back({static_cast<core::AtomType>(atom.type),
                                                   {local.first, local.second}});
                    }
                    for (const auto& [edge, bits] : bonds_) {
                        if (!ids.contains(edge.first) || !ids.contains(edge.second)) continue;
                        const auto a = port.frame.Local(atoms_.at(edge.first).position);
                        const auto b = port.frame.Local(atoms_.at(edge.second).position);
                        component.bonds.push_back({bits, {a.first, a.second}, {b.first, b.second}});
                    }
                }
                Budget();
                const auto observed = repeatingProgress_->Observe(port.index, output, component,
                    repeatingDisplacements_[port.index]);
                if (const auto* failure = std::get_if<FiniteRepeatingGraphFailure>(&observed))
                    throw Unsupported(failure->detail);
                Budget();
                report_.outputs[port.index] = repeatingProgress_->Port(port.index)->outputCount;
                continue;
            }
            std::map<Cell, Id> ids;
            bool matches = true;
            for (const auto& atom : output.atoms) {
                const auto id = At(port.frame.World(Position(atom.position)));
                if (!id || Held(*id) || atoms_.at(*id).type != static_cast<int>(atom.type)) {
                    matches = false;
                    break;
                }
                ids.emplace(Position(atom.position), *id);
            }
            if (!matches) continue;
            const auto fragment = Fragment(ids.begin()->second);
            if (fragment.size() != ids.size()) continue;
            std::map<Edge, std::uint8_t> expected, actual;
            for (const auto& edge : output.bonds)
                expected.emplace(Bond(ids.at(Position(edge.from)), ids.at(Position(edge.to))),
                                 edge.bits);
            for (const auto& [edge, bits] : bonds_)
                if (std::binary_search(fragment.begin(), fragment.end(), edge.first))
                    actual.emplace(edge, bits);
            if (expected != actual) continue;
            ++report_.outputs[port.index];
            emitted_ += fragment.size();
            Event(StaticProductionEvent::Kind::Output, cycle, port.index, fragment);
            Remove(fragment);
        }
    }
    void Boundaries() const {
        const auto wheels = Wheels();
        std::set<Cell> occupied;
        for (const auto& [id, atom] : atoms_) {
            if (Room(atom.position) < 0) throw Rejected("atom outside production chambers");
            if (!occupied.insert(atom.position).second || wheels.contains(atom.position))
                throw Rejected("independent atom overlap");
            for (const auto& arm : arms_)
                if (atom.position == arm.base) throw Rejected("atom overlaps arm base");
        }
        for (const auto& [cell, type] : wheels)
            if (Room(cell) < 0) throw Rejected("wheel atom outside production chamber");
        for (std::size_t i = 0; i < arms_.size(); ++i)
            for (std::size_t j = 0; j < i; ++j)
                if (arms_[i].base == arms_[j].base) throw Rejected("mechanical arm bases overlap");
    }
    const io::PuzzleData& puzzle_;
    const io::SolutionData& solution_;
    int currentCycle_{};
    StaticProductionEvent::Phase phase_{StaticProductionEvent::Phase::Initial};
    mutable io::VerificationDiagnostics diagnostics_;
    continuous::Work kernelWork_;
    std::array<io::VerificationTrace, io::kVerificationTraceLimit> recentTrace_{};
    std::size_t traceCount_{};
    bool allowOrdinary_{}, certify_{}, collectTimings_{}, repeating_{};
    std::array<bool, 16> repeatingPorts_{};
    std::array<core::HexCoord, 16> repeatingDisplacements_{};
    std::optional<FiniteRepeatingProgress> repeatingProgress_;
    IndependentMetrics metrics_;
    int limit_{}, period_{};
    Id next_{1};
    std::vector<Chamber> rooms_;
    std::vector<Arm> arms_;
    std::vector<Port> inputs_, outputs_;
    std::vector<Pipe> pipes_;
    std::vector<std::vector<Cell>> tracks_;
    std::set<std::size_t> glyphs_;
    std::map<Id, Atom> atoms_;
    std::map<Edge, std::uint8_t> bonds_;
    std::set<Id> recentlyUnbonded_;
    std::vector<Transit> captured_;
    std::map<std::size_t, Products> pending_;
    std::size_t emitted_{}, reactionConsumed_{}, reactionProduced_{};
    StaticProductionReport report_;
    bool sharedBudget_{};
    const std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point deadline_ = started_ + std::chrono::seconds(2);
};
StaticProductionReport ExecuteReplay(const io::PuzzleData& puzzle, const io::SolutionData& solution,
                                     int limit, bool ordinary = false, bool certify = false,
                                     bool timings = false,
                                     std::optional<std::chrono::steady_clock::time_point> started = std::nullopt) {
    Replay replay(puzzle, solution, limit, ordinary, certify, timings, started);
    try {
        return replay.Run();
    } catch (const TimedOut& error) {
        return replay.Failed(io::VerificationStatus::Timeout, error.what());
    } catch (const Unsupported& error) {
        return replay.Failed(io::VerificationStatus::Unavailable, error.what());
    } catch (const Rejected& error) {
        return replay.Failed(io::VerificationStatus::Rejected, error.what());
    } catch (const std::invalid_argument& error) {
        return replay.Failed(certify ? io::VerificationStatus::Unavailable
                                     : io::VerificationStatus::Error,
                             error.what());
    } catch (const std::exception& error) {
        return replay.Failed(io::VerificationStatus::Error,
                             std::string("independent execution error: ") + error.what());
    }
}
} // namespace
StaticProductionReport AuditOrdinaryMachine(const io::PuzzleData& puzzle,
                                             const io::SolutionData& solution, int cycleLimit) {
    return ExecuteReplay(puzzle, solution, cycleLimit, true);
}
StaticProductionReport VerifyStaticProduction(const io::PuzzleData& puzzle,
                                              const io::SolutionData& solution, int cycleLimit) {
    try {
        return ExecuteReplay(puzzle, solution, cycleLimit);
    } catch (const TimedOut& error) {
        return {.status = io::VerificationStatus::Timeout, .detail = error.what()};
    } catch (const Unsupported& error) {
        return {.status = io::VerificationStatus::Unavailable, .detail = error.what()};
    } catch (const Rejected& error) {
        return {.status = io::VerificationStatus::Rejected, .detail = error.what()};
    } catch (const std::exception& error) {
        return {.status = io::VerificationStatus::Error,
                .detail = std::string("independent execution error: ") + error.what()};
    }
}
StaticProductionReport VerifyStaticProductionFiles(const std::filesystem::path& puzzle,
                                                   const std::filesystem::path& solution,
                                                   int cycleLimit) {
    try {
        return VerifyStaticProduction(io::BinaryParser::ParsePuzzleFile(puzzle),
                                      io::BinaryParser::ParseSolutionFile(solution), cycleLimit);
    } catch (const std::exception& error) {
        return {.status = io::VerificationStatus::Error,
                .detail = std::string("independent file decoding failed: ") + error.what()};
    }
}
static FiniteReplayReport ReplayFiniteMachineWithStart(const io::PuzzleData& puzzle,
                                       const io::SolutionData& solution, int cycleLimit,
                                       bool collectTimings,
                                       std::optional<std::chrono::steady_clock::time_point> started) {
    try {
        auto report = ExecuteReplay(puzzle, solution, cycleLimit, true, false, collectTimings, started);
        ReplayOutcome outcome = ReplayOutcome::Error;
        switch (report.status) {
        case io::VerificationStatus::Passed:
            outcome = ReplayOutcome::Completed;
            break;
        case io::VerificationStatus::CycleLimit:
            outcome = ReplayOutcome::CycleLimit;
            break;
        case io::VerificationStatus::Timeout:
            outcome = ReplayOutcome::Timeout;
            break;
        case io::VerificationStatus::Unavailable:
            outcome = ReplayOutcome::Unavailable;
            break;
        case io::VerificationStatus::Rejected:
            outcome = ReplayOutcome::Rejected;
            break;
        case io::VerificationStatus::Error:
            outcome = ReplayOutcome::Error;
            break;
        }
        return {outcome,
                report.detail,
                report.cycles,
                std::move(report.outputs),
                report.acquired,
                report.disposed,
                report.remaining,
                std::move(report.events),
                report.traceTruncated,
                std::move(report.diagnostics)};
    } catch (const TimedOut& error) {
        return {.outcome = ReplayOutcome::Timeout, .detail = error.what()};
    } catch (const Unsupported& error) {
        return {.outcome = ReplayOutcome::Unavailable, .detail = error.what()};
    } catch (const Rejected& error) {
        return {.outcome = ReplayOutcome::Rejected, .detail = error.what()};
    } catch (const std::exception& error) {
        return {.outcome = ReplayOutcome::Error, .detail = error.what()};
    }
}
RepeatingFiniteReplayReport ReplayRepeatingFiniteMachine(const io::PuzzleData& puzzle,
                                                         const io::SolutionData& solution,
                                                         int cycleLimit) {
    const auto started = std::chrono::steady_clock::now();
    try {
        Replay replay(puzzle, solution, cycleLimit, true, false, false, started, true);
        auto report = replay.Run();
        if (std::chrono::steady_clock::now() >= started + std::chrono::seconds(2))
            return {{.outcome = ReplayOutcome::Timeout}};
        const auto outcome = report.status == io::VerificationStatus::Passed
            ? ReplayOutcome::Completed : ReplayOutcome::CycleLimit;
        RepeatingFiniteReplayReport result{{outcome, std::move(report.detail), report.cycles,
                 std::move(report.outputs), report.acquired, report.disposed, report.remaining,
                 std::move(report.events), report.traceTruncated, std::move(report.diagnostics)}};
        if (outcome == ReplayOutcome::Completed)
            result.execution.detail = "finite repeating opt-in targets reached (non-certifying)";
        if (std::chrono::steady_clock::now() >= started + std::chrono::seconds(2))
            return {{.outcome = ReplayOutcome::Timeout}};
        return result;
    } catch (const std::bad_alloc&) {
        return {{.outcome = ReplayOutcome::Unavailable}, false, true};
    } catch (const std::length_error&) {
        return {{.outcome = ReplayOutcome::Unavailable}, false, true};
    } catch (...) {
        // Exception diagnostics may allocate too: resource failure must remain allocation-free.
        try {
            throw;
        } catch (const TimedOut&) {
            return {{.outcome = ReplayOutcome::Timeout}};
        } catch (const Unsupported& error) {
            try { return {{.outcome = ReplayOutcome::Unavailable, .detail = error.what()}}; }
            catch (...) { return {{.outcome = ReplayOutcome::Unavailable}, false, true}; }
        } catch (const Rejected& error) {
            try { return {{.outcome = ReplayOutcome::Rejected, .detail = error.what()}}; }
            catch (...) { return {{.outcome = ReplayOutcome::Unavailable}, false, true}; }
        } catch (...) {
            return {{.outcome = ReplayOutcome::Unavailable}};
        }
    }
}
FiniteReplayReport ReplayFiniteMachine(const io::PuzzleData& puzzle,
                                       const io::SolutionData& solution, int cycleLimit,
                                       bool collectTimings) {
    return ReplayFiniteMachineWithStart(puzzle, solution, cycleLimit, collectTimings, std::nullopt);
}
ExpandedFiniteReplayReport ReplayExpandedFiniteMachine(const io::PuzzleData& puzzle,
                                                       const io::SolutionData& raw,
                                                       int cycleLimit) {
    const auto started = std::chrono::steady_clock::now();
    const auto deadline = started + std::chrono::seconds(2);
    ExpandedFiniteReplayReport result;
    try {
        result.rawSource = raw;
        auto expanded = ExpandFiniteArm1Tape(puzzle, raw, deadline);
        if (const auto* failure = std::get_if<TapeExpansionFailure>(&expanded)) {
            result.expansionFailure = *failure;
            const char* category = "UnknownState";
            switch (failure->code) {
            case TapeExpansionError::Malformed: category = "Malformed"; break;
            case TapeExpansionError::OutsideDomain: category = "OutsideDomain"; break;
            case TapeExpansionError::BoundExceeded: category = "BoundExceeded"; break;
            case TapeExpansionError::Conflict: category = "Conflict"; break;
            case TapeExpansionError::UnknownState: break;
            case TapeExpansionError::ResourceUnavailable: category = "ResourceUnavailable"; break;
            }
            result.execution.detail = std::string("finite expansion failed: ") + category;
            result.execution.outcome = ReplayOutcome::Unavailable;
        } else {
            result.expandedTape = std::move(std::get<ExpandedFiniteTape>(expanded));
            auto machine = raw;
            machine.objects[result.expandedTape->armObject].instructions = result.expandedTape->slots;
            if (std::chrono::steady_clock::now() <= deadline)
                result.execution = ReplayFiniteMachineWithStart(puzzle, machine, cycleLimit, false, started);
        }
    } catch (const std::exception& error) {
        result.execution.outcome = ReplayOutcome::Error;
        result.execution.detail = std::string("finite expansion setup failed: ") + error.what();
    }
    if (std::chrono::steady_clock::now() > deadline) {
        result.execution.outcome = ReplayOutcome::Timeout;
        result.execution.detail = "finite expansion/setup/execution exceeded shared 2s budget";
    }
    result.totalMicros = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started).count());
    return result;
}
io::VerificationReport VerifyIndependent(const io::PuzzleData& puzzle,
                                         const io::SolutionData& solution, int cycleLimit,
                                         bool collectTimings) {
    io::VerificationReport result;
    result.engineVersion = "opus-independent-v1.1";
    result.rulesetVersion = finiteOrdinaryProfile.rulesetVersion;
    if (!puzzle.productionInfo) result.capabilityProfile = finiteOrdinaryProfile.id;
    try {
        auto report = ExecuteReplay(puzzle, solution, cycleLimit, true, true, collectTimings);
        result.status = report.status;
        result.detail = report.detail;
        result.diagnostics = std::move(report.diagnostics);
        if (result.diagnostics) result.errorCycle = result.diagnostics->cycle;
        if (report.status == io::VerificationStatus::Passed) {
            if (!report.metricsConclusive || !report.continuousCertified) {
                result.status = io::VerificationStatus::Unavailable;
                result.reasonCode = "independent-profile-uncertified";
                result.detail =
                    "Independent replay completed but metric membership is inconclusive";
            } else {
                result.metrics = report.measuredMetrics;
                result.authority = io::VerificationAuthority::Independent;
                result.reasonCode = "independent-certified-completion";
            }
        } else {
            switch (report.status) {
            case io::VerificationStatus::CycleLimit:
                result.reasonCode = "independent-cycle-limit";
                break;
            case io::VerificationStatus::Timeout:
                result.reasonCode = "independent-timeout";
                break;
            case io::VerificationStatus::Unavailable:
                result.reasonCode = "independent-profile-uncertified";
                break;
            case io::VerificationStatus::Rejected:
                result.reasonCode = "independent-machine-rejected";
                break;
            default:
                result.reasonCode = "independent-execution-error";
                break;
            }
        }
    } catch (const TimedOut& error) {
        result.status = io::VerificationStatus::Timeout;
        result.detail = error.what();
        result.reasonCode = "independent-timeout";
    } catch (const Unsupported& error) {
        result.status = io::VerificationStatus::Unavailable;
        result.detail = error.what();
        result.reasonCode = "independent-profile-uncertified";
    } catch (const Rejected& error) {
        result.status = io::VerificationStatus::Rejected;
        result.detail = error.what();
        result.reasonCode = "independent-machine-rejected";
    } catch (const std::invalid_argument& error) {
        result.status = io::VerificationStatus::Unavailable;
        result.detail = error.what();
        result.reasonCode = "independent-metric-unsupported";
    } catch (const std::exception& error) {
        result.status = io::VerificationStatus::Error;
        result.detail = error.what();
        result.reasonCode = "independent-execution-error";
    }
    return result;
}
io::VerificationReport VerifyIndependentFiles(const std::filesystem::path& puzzle,
                                              const std::filesystem::path& solution, int cycleLimit,
                                              bool collectTimings) {
    const auto parseStart =
        collectTimings ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    try {
        const auto data = io::BinaryParser::ParsePuzzleFile(puzzle);
        const auto machine = io::BinaryParser::ParseSolutionFile(solution);
        const auto parseMicros =
            collectTimings
                ? static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                                 std::chrono::steady_clock::now() - parseStart)
                                                 .count())
                : 0;
        auto report = VerifyIndependent(data, machine, cycleLimit, collectTimings);
        if (report.diagnostics) report.diagnostics->parseMicros = parseMicros;
        return report;
    } catch (const std::exception& error) {
        io::VerificationReport result;
        result.status = io::VerificationStatus::Error;
        result.detail = error.what();
        result.reasonCode = "independent-file-error";
        result.engineVersion = "opus-independent-v1.1";
        result.rulesetVersion = "finite-82x71-v1";
        return result;
    }
}
} // namespace opusoptima::verification
