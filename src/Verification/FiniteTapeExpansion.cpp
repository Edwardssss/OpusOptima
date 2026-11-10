#include "FiniteTapeExpansion.hpp"
#include "IndependentCapabilities.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <string_view>

namespace opusoptima::verification {
namespace {
using Error = TapeExpansionError;
[[noreturn]] void Fail(Error code, std::optional<std::size_t> ordinal = {},
                       std::optional<std::int64_t> slot = {}) {
    throw TapeExpansionFailure{code, ordinal, slot};
}
int Permission(char action) {
    switch (action) {
    case 'G':
        return 23;
    case 'g':
    case 'R':
    case 'r':
        return 22;
    case 'P':
    case 'p':
        return 26;
    case 'O':
    case 'C':
        return 25;
    case 'X':
        return 24;
    case ' ':
    case 'B':
        return -1;
    default:
        return -2;
    }
}
bool KnownPart(std::string_view name) {
    constexpr std::string_view names[]{"input",
                                       "out-std",
                                       "pipe",
                                       "bonder",
                                       "unbonder",
                                       "bonder-speed",
                                       "bonder-prisma",
                                       "glyph-calcification",
                                       "glyph-duplication",
                                       "glyph-projection",
                                       "glyph-purification",
                                       "glyph-life-and-death",
                                       "glyph-disposal",
                                       "glyph-unification",
                                       "glyph-dispersion"};
    return std::find(std::begin(names), std::end(names), name) != std::end(names);
}
struct Slot {
    char action{' '};
    bool written{};
    TapeOrigin origin;
};
ExpandedFiniteTape Expand(const io::PuzzleData& puzzle, const io::SolutionData& solution,
                          std::optional<std::chrono::steady_clock::time_point> deadline) {
    const auto budget = [&] {
        if (deadline && std::chrono::steady_clock::now() > *deadline)
            Fail(Error::ResourceUnavailable);
    };
    ExpandedFiniteTape out;
    std::size_t arms = 0;
    for (std::size_t i = 0; i < solution.objects.size(); ++i) {
        const auto& part = solution.objects[i];
        if (part.bodyData.size() < 5 || part.bodyData[1] < 0 || part.bodyData[1] > 5)
            Fail(Error::Malformed);
        if (part.typeName == "arm1") {
            if (part.bodyData[0] < 1 || part.bodyData[0] > finiteOrdinaryProfile.maxArmLength)
                Fail(Error::Malformed);
            out.armObject = i;
            ++arms;
        }
        if (part.typeName == "pipe" &&
            (part.bodyData.size() < 7 || part.bodyData[6] < 1 ||
             static_cast<std::uint64_t>(part.bodyData[6]) > (part.bodyData.size() - 7) / 2))
            Fail(Error::Malformed);
    }
    if (!arms) Fail(Error::Malformed);
    if (arms != 1) Fail(Error::OutsideDomain);
    for (const auto& part : solution.objects) {
        if (part.typeName == "arm1") continue;
        if (!KnownPart(part.typeName) || !part.instructions.empty() ||
            (part.typeName == "pipe" && !puzzle.productionInfo))
            Fail(Error::OutsideDomain);
    }
    for (const auto* molecules : {&puzzle.inputs, &puzzle.outputs})
        for (const auto& molecule : *molecules)
            for (const auto& atom : molecule.atoms)
                if (atom.type == core::AtomType::Repeat) Fail(Error::OutsideDomain);
    const auto& raw = solution.objects[out.armObject].instructions;
    const auto bound = static_cast<std::int64_t>(finiteOrdinaryProfile.maxTapePosition);
    const bool copy =
        std::any_of(raw.begin(), raw.end(), [](auto inst) { return inst.action == 'C'; });
    if (copy && std::any_of(raw.begin(), raw.end(), [](auto inst) { return inst.parameter < 0; }))
        Fail(Error::OutsideDomain);
    if (solution.objects.size() > finiteOrdinaryProfile.maxParts || raw.size() > 2 * bound + 1)
        Fail(Error::BoundExceeded);
    for (std::size_t i = 0; i < raw.size(); ++i)
        if (raw[i].parameter < -bound || raw[i].parameter > bound) Fail(Error::BoundExceeded, i);
    std::vector<std::size_t> order(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(),
              [&](auto a, auto b) { return raw[a].parameter < raw[b].parameter; });
    for (std::size_t j = 1; j < order.size(); ++j)
        if (raw[order[j - 1]].parameter == raw[order[j]].parameter)
            Fail(Error::Malformed, order[j]);
    const auto permitted = [&](char action, std::optional<std::size_t> ordinal) {
        const int bit = Permission(action);
        if (bit == -2 || (bit >= 0 && !(puzzle.permissionBits & (1ULL << bit))))
            Fail(Error::OutsideDomain, ordinal);
    };
    if (!(puzzle.permissionBits & 1)) Fail(Error::OutsideDomain);
    for (auto ordinal : order)
        permitted(raw[ordinal].action, ordinal);
    out.rawEntries = static_cast<std::int64_t>(raw.size());
    budget();
    if (raw.empty()) return out;
    const std::int64_t start = raw[order.front()].parameter;
    out.rawStart = static_cast<std::int32_t>(start);
    out.rawSpan = static_cast<std::int64_t>(raw[order.back()].parameter) - start + 1;
    const std::int64_t capacity = 2 * out.rawSpan;
    if (static_cast<std::uint64_t>(capacity) > std::vector<Slot>().max_size())
        Fail(Error::BoundExceeded);
    std::vector<Slot> tape(static_cast<std::size_t>(capacity));
    std::int64_t length = out.rawSpan, lastEnd = 0, lastRepeat = 0, resetFrom = 0;
    const auto checkEnd = [&](std::int64_t pos, std::int64_t count, std::size_t ordinal) {
        if (pos < 0 || count < 0 || pos > capacity || count > capacity - pos)
            Fail(Error::BoundExceeded, ordinal, pos);
    };
    const auto set = [&](std::int64_t pos, char action, TapeOrigin origin, std::size_t ordinal) {
        checkEnd(pos, 1, ordinal);
        if (tape[pos].written) Fail(Error::Conflict, ordinal, pos);
        permitted(action, ordinal);
        tape[pos] = {action, true, origin};
    };
    for (std::size_t j = 0; j < order.size(); ++j) {
        budget();
        const auto ordinal = order[j];
        const auto inst = raw[ordinal];
        std::int64_t n = static_cast<std::int64_t>(inst.parameter) - start;
        TapeOrigin origin;
        origin.rawOrdinal = ordinal;
        origin.rawIndex = inst.parameter;
        if (inst.action == 'C') {
            const auto count = std::max<std::int64_t>(0, lastEnd - lastRepeat);
            checkEnd(lastRepeat, count, ordinal);
            // Freeze once per run: overlapping destinations never become a source.
            const std::vector<Slot> source(tape.begin() + lastRepeat,
                                           tape.begin() + lastRepeat + count);
            for (const auto& slot : source) {
                budget();
                if (slot.action == 'B') Fail(Error::OutsideDomain, ordinal, n);
            }
            do {
                budget();
                const auto siteOrdinal = order[j];
                const auto site = raw[siteOrdinal].parameter;
                n = static_cast<std::int64_t>(site) - start;
                if (lastEnd > n) Fail(Error::Conflict, siteOrdinal, n);
                checkEnd(n, count, siteOrdinal);
                for (std::int64_t k = 0; k < count; ++k) {
                    budget();
                    auto slot = source[k];
                    slot.origin.kind = TapeOrigin::Kind::Copy;
                    slot.origin.copySite = site;
                    slot.origin.sourceSlot = lastRepeat + k;
                    tape[n + k] = slot;
                }
                length = std::max(length, n + count);
                ++j;
            } while (j < order.size() && raw[order[j]].action == 'C');
            if (j < order.size()) lastRepeat = resetFrom = raw[order[j]].parameter - start;
            --j;
        } else if (inst.action == 'X') {
            bool grab = false;
            std::int64_t rotation = 0;
            for (auto k = resetFrom; k < n; ++k) {
                budget();
                checkEnd(k, 1, ordinal);
                switch (tape[k].action) {
                case 'G':
                    grab = true;
                    break;
                case 'g':
                    grab = false;
                    break;
                case 'r':
                    ++rotation;
                    break;
                case 'R':
                    --rotation;
                    break;
                case 'P':
                case 'p':
                case 'B':
                case 'O':
                case ' ':
                    break;
                default:
                    Fail(Error::UnknownState, ordinal, k);
                }
            }
            while (rotation > 3) {
                budget();
                rotation -= 6;
            }
            while (rotation < -3) {
                budget();
                rotation += 6;
            }
            origin.kind = TapeOrigin::Kind::Reset;
            std::uint8_t step = 0;
            const auto emit = [&](char action) {
                origin.resetStep = step++;
                set(n, action, origin, ordinal);
                ++n;
            };
            if (grab) emit('g');
            while (rotation > 0) {
                emit('R');
                --rotation;
            }
            while (rotation < 0) {
                emit('r');
                ++rotation;
            }
            if (!step) emit(' ');
            resetFrom = lastEnd = n;
            length = std::max(length, n);
        } else {
            origin.kind = TapeOrigin::Kind::Raw;
            set(n, inst.action, origin, ordinal);
            if (inst.action == 'B' && !out.haltSlot) out.haltSlot = n;
            lastEnd = n + 1;
        }
    }
    if (start + length - 1 > bound || start < -bound ||
        start + length - 1 > std::numeric_limits<std::int32_t>::max())
        Fail(Error::BoundExceeded, {}, length - 1);
    if (static_cast<std::uint64_t>(length) > out.slots.max_size() ||
        static_cast<std::uint64_t>(length) > out.origins.max_size())
        Fail(Error::BoundExceeded);
    out.expandedLength = length;
    out.slots.reserve(static_cast<std::size_t>(length));
    out.origins.reserve(static_cast<std::size_t>(length));
    for (std::int64_t k = 0; k < length; ++k) {
        budget();
        const auto& slot = tape[k];
        out.slots.push_back({slot.action, static_cast<std::int32_t>(start + k)});
        out.origins.push_back(slot.origin);
        if (slot.action == 'G' || slot.action == 'g' || slot.action == 'r' || slot.action == 'R' ||
            slot.action == 'P' || slot.action == 'p')
            ++out.staticInstructions;
    }
    budget();
    return out;
}
} // namespace
TapeExpansionResult ExpandFiniteArm1Tape(
    const io::PuzzleData& puzzle, const io::SolutionData& solution,
    std::optional<std::chrono::steady_clock::time_point> deadline) {
    try {
        return Expand(puzzle, solution, deadline);
    } catch (const TapeExpansionFailure& failure) {
        return failure;
    } catch (const std::bad_alloc&) {
        return TapeExpansionFailure{Error::ResourceUnavailable, {}, {}};
    } catch (const std::length_error&) {
        return TapeExpansionFailure{Error::ResourceUnavailable, {}, {}};
    }
}
} // namespace opusoptima::verification
