#include "Verification/FiniteTapeExpansion.hpp"
#include "Verification/FiniteRepeatingGraph.hpp"
#include "IO/BinaryWriter.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <tuple>

namespace {
struct AllocationTrace {
    bool active{}, persistent{};
    std::size_t count{}, failAt{}, failures{};
    std::array<std::size_t, 32> bytes{};
} trace;

struct Measure {
    explicit Measure(std::size_t failAt = 0) {
        trace = {};
        trace.failAt = failAt;
        trace.active = true;
    }
    ~Measure() { trace.active = false; }
};
void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
} // namespace

// This executable is single-threaded; injection is armed only around the converter call.
void* operator new(std::size_t bytes) {
    if (trace.active) {
        const auto ordinal = ++trace.count;
        if (ordinal <= trace.bytes.size()) trace.bytes[ordinal - 1] = bytes;
        if (trace.failAt && (ordinal == trace.failAt || (trace.persistent && ordinal >= trace.failAt))) {
            ++trace.failures;
            throw std::bad_alloc();
        }
    }
    if (void* memory = std::malloc(bytes ? bytes : 1)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {
namespace io = opusoptima::io;
using namespace opusoptima::verification;
io::SolutionData Machine(std::vector<io::ArmInstruction> raw) {
    io::SolutionData machine;
    machine.puzzleName = "allocation-puzzle";
    machine.solutionName = "allocation-solution";
    machine.objects.push_back({"arm1", {}, std::move(raw), {1, 0, 0, 0, 0}});
    return machine;
}
auto Origin(const TapeOrigin& origin) {
    return std::tie(origin.kind, origin.rawOrdinal, origin.rawIndex,
                    origin.copySite, origin.sourceSlot, origin.resetStep);
}
bool Same(const ExpandedFiniteTape& a, const ExpandedFiniteTape& b) {
    if (std::tie(a.armObject, a.rawStart, a.rawEntries, a.rawSpan, a.expandedLength,
                 a.staticInstructions, a.haltSlot) !=
        std::tie(b.armObject, b.rawStart, b.rawEntries, b.rawSpan, b.expandedLength,
                 b.staticInstructions, b.haltSlot) ||
        a.slots.size() != b.slots.size() || a.origins.size() != b.origins.size()) return false;
    for (std::size_t i = 0; i < a.slots.size(); ++i)
        if (a.slots[i].action != b.slots[i].action ||
            a.slots[i].parameter != b.slots[i].parameter ||
            Origin(a.origins[i]) != Origin(b.origins[i])) return false;
    return true;
}
void Unchanged(const io::PuzzleData& puzzle, const io::SolutionData& machine,
               const std::vector<std::byte>& saved) {
    Require(io::BinaryWriter::WriteSolution(machine) == saved, "solution changed");
    Require(puzzle.version == 3 && puzzle.permissionBits == ~0ULL &&
            puzzle.name == "allocation-puzzle" && puzzle.creatorSteamId == 123 &&
            puzzle.outputMultiplier == 6 && puzzle.inputs.empty() && puzzle.outputs.empty() &&
            !puzzle.productionInfo, "puzzle changed");
}
std::size_t Sweep(const char* name, const io::PuzzleData& puzzle,
                  const io::SolutionData& machine, const char* literal,
                  std::size_t expectedAllocations) {
    const auto saved = io::BinaryWriter::WriteSolution(machine);
    auto baseline = [&] {
        Measure measure;
        return ExpandFiniteArm1Tape(puzzle, machine);
    }();
    const auto allocations = trace.count;
    const auto sizes = trace.bytes;
    Require(allocations == expectedAllocations, "unexpected allocation topology");
    Require(std::holds_alternative<ExpandedFiniteTape>(baseline), "baseline failed");
    const auto& expected = std::get<ExpandedFiniteTape>(baseline);
    std::size_t length = 0;
    while (literal[length]) ++length;
    Require(expected.slots.size() == length, "literal length mismatch");
    for (std::size_t i = 0; i < length; ++i)
        Require(expected.slots[i].action == literal[i], "literal action mismatch");
    Unchanged(puzzle, machine, saved);
    std::printf("%s: allocations=%zu bytes=", name, allocations);
    for (std::size_t i = 0; i < allocations; ++i) std::printf("%s%zu", i ? "," : "", sizes[i]);
    std::puts("");
    for (std::size_t failAt = 1; failAt <= allocations; ++failAt) {
        auto result = [&] {
            Measure measure(failAt);
            return ExpandFiniteArm1Tape(puzzle, machine);
        }();
        Require(trace.failures == 1 && trace.count == failAt, "injection not reached or allocated after failure");
        Require(std::holds_alternative<TapeExpansionFailure>(result), "partial result escaped");
        const auto& failure = std::get<TapeExpansionFailure>(result);
        Require(failure.code == TapeExpansionError::ResourceUnavailable &&
                !failure.rawOrdinal && !failure.slot, "wrong resource failure metadata");
        Unchanged(puzzle, machine, saved);
        const auto recovered = ExpandFiniteArm1Tape(puzzle, machine);
        Require(std::holds_alternative<ExpandedFiniteTape>(recovered) &&
                Same(expected, std::get<ExpandedFiniteTape>(recovered)), "recovery mismatch");
        Unchanged(puzzle, machine, saved);
        std::printf("  fail-at=%zu: ResourceUnavailable; input unchanged; recovery identical\n", failAt);
    }
    return allocations;
}
void Boundary(const char* name, const io::PuzzleData& puzzle, const io::SolutionData& machine,
              std::size_t allocations, bool success) {
    const auto saved = io::BinaryWriter::WriteSolution(machine);
    const auto result = [&] {
        Measure measure;
        return ExpandFiniteArm1Tape(puzzle, machine);
    }();
    Require(trace.count == allocations, "boundary rejection allocation stage changed");
    if (success) {
        Require(std::holds_alternative<ExpandedFiniteTape>(result), "boundary success rejected");
        const auto& out = std::get<ExpandedFiniteTape>(result);
        Require(out.rawSpan == 40001 && out.expandedLength == 40001 &&
                out.slots.front().parameter == -20000 && out.slots.back().parameter == 20000,
                "maximum span mismatch");
    } else {
        Require(std::holds_alternative<TapeExpansionFailure>(result) &&
                std::get<TapeExpansionFailure>(result).code == TapeExpansionError::BoundExceeded,
                "boundary wrong failure");
    }
    Unchanged(puzzle, machine, saved);
    std::printf("%s: %s after %zu allocations\n", name, success ? "accepted" : "BoundExceeded", allocations);
}
void RepeatingAllocations() {
    using A = opusoptima::core::AtomType;
    const io::Molecule raw{{{A::Salt, {0, 0}}, {A::Repeat, {1, 0}}},
                           {{1, {0, 0}, {1, 0}}}};
    const io::Molecule actual{{{A::Salt, {0, 0}}, {A::Salt, {1, 0}}},
                              {{1, {0, 0}, {1, 0}}}};
    const auto run = [&] { return CheckFiniteRepeatingGraph({raw, actual, {1, 0}, 2}); };
    const auto baseline = [&] { Measure measure; return run(); }();
    const auto count = trace.count;
    Require(std::get<FiniteRepeatingGraphChecked>(baseline).provenPrefix == 2,
            "repeating baseline failed");
    for (std::size_t ordinal = 1; ordinal <= count; ++ordinal) {
        auto result = [&] {
            Measure measure(ordinal);
            trace.persistent = true;
            return run();
        }();
        Require(std::holds_alternative<FiniteRepeatingGraphFailure>(result) &&
                    std::get<FiniteRepeatingGraphFailure>(result).code ==
                        RepeatingGraphFailureCode::ResourceUnavailable,
                "repeating allocation did not fail atomically");
        Require(trace.failures == 1, "repeating recovery allocated");
        Require(std::get<FiniteRepeatingGraphChecked>(run()).provenPrefix == 2,
                "repeating retry changed result");
    }
    std::printf("PASS: repeating graph %zu persistent allocation failure sites; retries unchanged\n", count);
}
} // namespace
int main() {
    RepeatingAllocations();
    io::PuzzleData puzzle;
    puzzle.version = 3;
    puzzle.name = "allocation-puzzle";
    puzzle.permissionBits = ~0ULL;
    puzzle.creatorSteamId = 123;
    puzzle.outputMultiplier = 6;
    std::size_t injected = 0;
    injected += Sweep("unsorted literal", puzzle, Machine({{'g', 2}, {'G', 0}}), "G g", 4);
    injected += Sweep("reset", puzzle, Machine({{'G', 0}, {'r', 1}, {'X', 2}}), "GrgR", 4);
    injected += Sweep("overlapping frozen C run", puzzle,
                      Machine({{'G', 0}, {'r', 1}, {'C', 2}, {'C', 3}}), "GrGGr", 5);
    injected += Sweep("two C source snapshots", puzzle,
                      Machine({{'G', 0}, {'C', 2}, {'g', 4}, {'C', 6}}), "G G g g", 6);
    injected += Sweep("empty C source", puzzle, Machine({{'C', 0}}), " ", 4);
    injected += Sweep("empty tape", puzzle, Machine({}), "", 0);
    Boundary("raw count 40002", puzzle, Machine(std::vector<io::ArmInstruction>(40002, {'G', 0})), 0, false);
    Boundary("raw position 20001", puzzle, Machine({{'G', 20000}, {'C', 20001}}), 0, false);
    Boundary("raw position -20001", puzzle, Machine({{'G', -20001}}), 0, false);
    Boundary("expanded position 20001", puzzle, Machine({{'r', 19998}, {'r', 19999}, {'X', 20000}}), 2, false);
    Boundary("maximum raw span", puzzle, Machine({{'X', -20000}, {'O', 20000}}), 4, true);
    std::printf("PASS: %zu actual bad_alloc injections; 6 fixtures; 5 resource boundaries; sizeof(origin)=%zu sizeof(instruction)=%zu\n",
                injected, sizeof(TapeOrigin), sizeof(io::ArmInstruction));
}
