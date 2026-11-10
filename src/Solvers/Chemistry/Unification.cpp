#include "Unification.hpp"
#include "FeedLayout.hpp"

#include <algorithm>
#include <array>
#include <numeric>
#include <stdexcept>

namespace opusoptima::solvers {
namespace {
void CheckDeadline(std::chrono::steady_clock::time_point deadline) {
    if (deadline != std::chrono::steady_clock::time_point::max() &&
        std::chrono::steady_clock::now() >= deadline)
        throw std::invalid_argument("unification deadline");
}

core::HexCoord Rotate(core::HexCoord c, int turns) {
    while (turns-- > 0)
        c = core::RotateClockwise(c);
    return c;
}
} // namespace

static io::SolutionData Compile(const io::PuzzleData& p, int batches,
                                std::chrono::steady_clock::time_point deadline,
                                std::vector<int>* productEnds = nullptr) {
    auto checkDeadline = [&] { CheckDeadline(deadline); };
    checkDeadline();
    constexpr auto required =
        (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 18) | (1ULL << 22) | (1ULL << 23);
    if (p.version != 3 || p.productionInfo || p.outputMultiplier < 1 || p.outputMultiplier > 8 ||
        (p.permissionBits & required) != required || p.outputs.size() != 1 ||
        p.outputs[0].atoms.size() != 1 || !p.outputs[0].bonds.empty() ||
        p.outputs[0].atoms[0].type != core::AtomType::Quintessence)
        throw std::invalid_argument(
            "unification requires a monatomic quintessence product and piston tracks");
    struct Slot {
        int input, atom, distance, extensions, type;
    };
    std::vector<Slot> slots;
    std::array<bool, 6> present{};
    int count = 0, salts = 0;
    for (const auto& input : p.inputs) {
        checkDeadline();
        if (input.atoms.empty()) throw std::invalid_argument("empty unification feed");
        count += static_cast<int>(input.atoms.size());
        for (const auto& a : input.atoms) {
            checkDeadline();
            const int type = static_cast<int>(a.type);
            if (type >= 1 && type <= 5) {
                present[type] = true;
                salts += type == 1;
            }
        }
        if (!input.bonds.empty() && !(p.permissionBits & (1ULL << 9)))
            throw std::invalid_argument("unification disassembly requires unbonding");
        for (const auto& b : input.bonds) {
            checkDeadline();
            const auto contains = [&](core::HexCoord cell) {
                return std::any_of(input.atoms.begin(), input.atoms.end(),
                                   [&](const auto& atom) {
                                       checkDeadline();
                                       return atom.position == cell;
                                   });
            };
            if (b.bits != 1 || core::Distance(b.from, b.to) != 1 || !contains(b.from) ||
                !contains(b.to))
                throw std::invalid_argument(
                    "unification requires ordinary adjacent bonds between feed atoms");
        }
        for (std::size_t i = 0; i < input.atoms.size(); ++i)
            for (std::size_t j = 0; j < i; ++j) {
                checkDeadline();
                if (input.atoms[i].position == input.atoms[j].position)
                    throw std::invalid_argument("overlapping unification feed atoms");
            }
    }
    int cardinalCount = 0;
    for (const auto& molecule : p.inputs) {
        checkDeadline();
        for (const auto& atom : molecule.atoms) {
            checkDeadline();
            cardinalCount +=
                atom.type >= core::AtomType::Salt && atom.type <= core::AtomType::Water;
        }
    }
    const bool impurities = cardinalCount != count;
    const bool selective =
        cardinalCount > 4 && present[2] && present[3] && present[4] && present[5];
    const bool normalize =
        !selective &&
        (count != 4 || std::any_of(p.inputs.begin(), p.inputs.end(), [&](const auto& input) {
             checkDeadline();
             return std::any_of(input.atoms.begin(), input.atoms.end(), [&](const auto& atom) {
                 checkDeadline();
                 if (atom.type == core::AtomType::Salt) return false;
                 int copies = 0;
                 for (const auto& molecule : p.inputs) {
                     checkDeadline();
                     for (const auto& other : molecule.atoms) {
                         checkDeadline();
                         copies += other.type == atom.type;
                     }
                 }
                 return copies > 1;
             });
         }));
    if (cardinalCount == 0 || count > 16)
        throw std::invalid_argument("unification feed bounds exceeded");
    if (impurities && !normalize)
        throw std::invalid_argument(
            "mixed unification feeds require normalized cardinal extraction");
    if (normalize) {
        if (!(p.permissionBits & (1ULL << 12)))
            throw std::invalid_argument("balanced unification requires calcification");
        present.fill(false);
        present[1] = true;
        salts = cardinalCount;
    }
    if (selective) {
        present[1] = false;
        salts = 0;
    }
    if (present[1] &&
        (p.permissionBits & ((1ULL << 13) | (1ULL << 28))) != ((1ULL << 13) | (1ULL << 28)))
        throw std::invalid_argument("salt unification requires duplication and a wheel");

    io::SolutionData result;
    result.puzzleName = p.name;
    result.solutionName = "Serial quintessence unification";
    auto object = [&](const char* type, core::HexCoord pos, int length, int rotation,
                      int index = 0) {
        checkDeadline();
        io::SolutionObject obj;
        obj.typeName = type;
        obj.position = pos;
        obj.bodyData = {length, rotation, index, 0, static_cast<int>(result.objects.size())};
        result.objects.push_back(obj);
        return static_cast<int>(result.objects.size()) - 1;
    };
    auto track = [&](const std::vector<core::HexCoord>& cells) {
        const int id = object("track", cells.front(), 1, 0);
        auto& data = result.objects[id].bodyData;
        data = {1, 0, 0, 0, static_cast<int>(cells.size())};
        for (auto c : cells) {
            checkDeadline();
            c = core::Subtract(c, cells.front());
            data.push_back(c.q);
            data.push_back(c.r);
        }
        data.push_back(0);
    };
    // Feed extraction uses the three-row high-return route of RowAssembly, based
    // on OpusSolver (third_party/OpusSolver-LICENSE.txt).
    const int feed = object("piston", {0, 3}, 1, 4);
    int reach = 0;
    for (int i = 0; i < static_cast<int>(p.inputs.size()); ++i) {
        checkDeadline();
        const auto& input = p.inputs[i];
        // Feed validation bounds this planner to sixteen atoms and twelve rotations.
        const auto layout = PlanFeedLayout(input);
        checkDeadline();
        int lo = 256, hi = -256, lowR = 256;
        for (auto c : layout.cells) {
            checkDeadline();
            lo = std::min(lo, c.q);
            hi = std::max(hi, c.q);
            lowR = std::min(lowR, c.r);
        }
        const int start = reach + 3;
        const core::HexCoord origin{-start - hi, 4 - lowR};
        object("input", origin, 1, layout.rotation, i);
        for (int j = 0; j < static_cast<int>(input.atoms.size()); ++j) {
            checkDeadline();
            const auto c = core::Add(origin, layout.cells[j]);
            slots.push_back({i, j, 5 - 2 * c.q, 6 - c.r, static_cast<int>(input.atoms[j].type)});
        }
        for (const auto& b : input.bonds) {
            checkDeadline();
            const auto a = core::Add(origin, Rotate(b.from, layout.rotation));
            const auto delta = Rotate(core::Subtract(b.to, b.from), layout.rotation);
            int rotation = 0;
            while (Rotate({1, 0}, rotation) != delta)
                ++rotation;
            object("unbonder", a, 1, rotation);
        }
        reach = start + hi - lo;
    }
    std::stable_sort(slots.begin(), slots.end(), [](const Slot& a, const Slot& b) {
        if (a.input != b.input) return a.input < b.input;
        return a.extensions != b.extensions ? a.extensions < b.extensions : a.distance < b.distance;
    });
    std::vector<core::HexCoord> feedRoute;
    for (int r = 3; r <= 8; ++r)
        feedRoute.push_back({0, r});
    for (int q = 1; q <= reach; ++q) {
        checkDeadline();
        feedRoute.push_back({-q, 8});
        feedRoute.push_back({-q, 7});
    }
    feedRoute.push_back({-reach - 1, 8});
    track(feedRoute);
    const int center = salts > 2 ? 35 : 20;
    if (normalize) object("glyph-calcification", {0, 0}, 1, 0);
    const int transport = object("piston", {0, -3}, 2, 1);
    std::vector<core::HexCoord> route{{0, -3}, {0, -4}};
    const int period = normalize ? cardinalCount / std::gcd(cardinalCount, 4) : 1;
    const int totalBatches = ((batches + period - 1) / period) * period;
    const int consumed =
        selective ? totalBatches * count : totalBatches * 4 / cardinalCount * count;
    const int storageCount = selective ? totalBatches * (count - 4) : consumed - totalBatches * 4;
    const int routeEnd = (selective || impurities) ? center + 15 + 2 * storageCount : center + 5;
    for (int q = 1; q <= routeEnd; ++q) {
        checkDeadline();
        route.push_back({q, -4});
    }
    for (int r = -3; r <= 4; ++r)
        route.push_back({routeEnd, r});
    for (int q = routeEnd - 1; q >= center - 1; --q) {
        checkDeadline();
        // Leave the output's vertical pickup rail disconnected from this rail
        // when the factory is nested inside a chemistry feed.
        if ((selective || impurities) && q == center + 6) {
            route.push_back({q + 1, 3});
            route.push_back({q + 1, 2});
            route.push_back({q, 2});
            route.push_back({q - 1, 2});
            route.push_back({q - 1, 3});
            --q;
        }
        route.push_back({q, 4});
    }
    track(route);
    object("glyph-unification", {center, 0}, 1, 0);
    const int output = object("arm1", {center + 3, 0}, 3, 3);
    object("out-std", core::Subtract({center + 6, 0}, p.outputs[0].atoms[0].position), 1, 0);
    std::array<int, 6> conversionQ{};
    int station = 7;
    for (int type = 2; type <= 5; ++type)
        if (!present[type]) {
            conversionQ[type] = station;
            // Orient the requested wheel atom downward, with the salt one cell farther out.
            const std::array<core::HexCoord, 6> wheel{{{}, {}, {-1, 1}, {1, -1}, {0, -1}, {0, 1}}};
            int rotation = 0;
            while (Rotate(wheel[type], rotation) != core::HexCoord{0, -1})
                ++rotation;
            object("baron", {station, 1}, 1, rotation);
            object("glyph-duplication", {station, 0}, 1, 4);
            station += 5;
        }
    int time = 0, position = 0;
    auto write = [&](int arm, char action) {
        checkDeadline();
        result.objects[arm].instructions.push_back({action, time++});
    };
    auto repeat = [&](int arm, char action, int n) {
        while (n-- > 0)
            write(arm, action);
    };
    auto move = [&](int target) {
        while (position < target) {
            write(transport, 'A');
            ++position;
        }
        while (position > target) {
            write(transport, 'a');
            --position;
        }
    };
    auto routeIndex = [&](core::HexCoord c) {
        return static_cast<int>(std::find_if(route.begin(), route.end(), [&](auto cell) {
            checkDeadline();
            return cell == c;
        }) - route.begin());
    };
    const std::array<core::HexCoord, 4> ports{
        {{center, 1}, {center - 1, 1}, {center, -1}, {center + 1, -1}}};
    int stored = 0, slotCursor = 0;
    for (int batch = 0; batch < totalBatches; ++batch) {
        checkDeadline();
        auto used = present;
        std::array<bool, 6> delivered{};
        int port = 0;
        for (int n = 0; n < (selective ? cardinalCount : 4) ||
                        (impurities && batch == totalBatches - 1 && slotCursor % count != 0);) {
            checkDeadline();
            const auto& slot = slots[slotCursor++ % count];
            repeat(feed, 'R', 3);
            repeat(feed, 'A', slot.distance - 1);
            repeat(feed, 'r', 3);
            write(feed, 'A');
            repeat(feed, 'E', slot.extensions);
            write(feed, 'G');
            repeat(feed, 'e', slot.extensions);
            write(feed, 'a');
            repeat(feed, 'R', 3);
            repeat(feed, 'a', slot.distance - 1);
            repeat(feed, 'r', 3);
            repeat(feed, 'E', 2);
            write(feed, 'g');
            repeat(feed, 'e', 2);
            write(transport, 'E');
            write(transport, 'G');
            repeat(transport, 'e', 2);
            repeat(transport, 'R', 3);
            if (slot.type < 1 || slot.type > 5) {
                move(routeIndex({center + 10 + 2 * stored++, 4}));
                repeat(transport, 'E', 2);
                write(transport, 'g');
                repeat(transport, 'e', 2);
                move(0);
                repeat(transport, 'R', 3);
                write(transport, 'E');
                continue;
            }
            ++n;
            if (normalize || (!selective && slot.type == 1)) {
                int type = 2;
                while (used[type])
                    ++type;
                used[type] = true;
                move(routeIndex({conversionQ[type], -4}));
                repeat(transport, 'R', 3);
                repeat(transport, 'E', 2);
                write(transport, 'g');
                write(transport, 'G');
                repeat(transport, 'e', 2);
                repeat(transport, 'R', 3);
            }
            if (selective && (slot.type == 1 || delivered[slot.type])) {
                move(routeIndex({center + 10 + 2 * stored++, 4}));
                repeat(transport, 'E', 2);
                write(transport, 'g');
                repeat(transport, 'e', 2);
                move(0);
                repeat(transport, 'R', 3);
                write(transport, 'E');
                continue;
            }
            delivered[slot.type] = true;
            const bool upper = port < 2;
            const auto base =
                core::Add(ports[port++], upper ? core::HexCoord{0, 3} : core::HexCoord{0, -3});
            move(routeIndex(base));
            if (!upper) repeat(transport, 'R', 3);
            repeat(transport, 'E', 2);
            write(transport, 'g');
            repeat(transport, 'e', 2);
            if (!upper) repeat(transport, 'R', 3);
            move(0);
            repeat(transport, 'R', 3);
            write(transport, 'E');
        }
        write(output, 'G');
        repeat(output, 'R', 3);
        write(output, 'g');
        repeat(output, 'R', 3);
        if (productEnds) productEnds->push_back(time);
    }
    result.metrics.cycles = time + 1;
    checkDeadline();
    return result;
}
io::SolutionData CompileUnificationBatch(const io::PuzzleData& puzzle) {
    return CompileUnificationBatch(puzzle, std::chrono::steady_clock::time_point::max());
}
io::SolutionData CompileUnificationBatch(const io::PuzzleData& puzzle,
                                         std::chrono::steady_clock::time_point deadline) {
    auto checkDeadline = [&] { CheckDeadline(deadline); };
    checkDeadline();
    std::vector<int> selected;
    io::SolutionData result;
    const auto search = [&](auto&& self, int first, std::size_t atoms) -> bool {
        checkDeadline();
        if (atoms == 4) {
            auto proxy = puzzle;
            proxy.inputs.clear();
            for (const int input : selected) {
                checkDeadline();
                proxy.inputs.push_back(puzzle.inputs[input]);
            }
            try {
                result = Compile(proxy, 1, deadline);
            } catch (const std::invalid_argument&) {
                checkDeadline();
                return false;
            }
            for (auto& part : result.objects) {
                checkDeadline();
                if (part.typeName == "input") part.bodyData[2] = selected[part.bodyData[2]];
            }
            checkDeadline();
            return true;
        }
        for (int i = first; i < static_cast<int>(puzzle.inputs.size()); ++i) {
            checkDeadline();
            const auto count = puzzle.inputs[i].atoms.size();
            if (!count || atoms + count > 4 ||
                std::any_of(puzzle.inputs[i].atoms.begin(), puzzle.inputs[i].atoms.end(),
                            [&](const auto& atom) {
                                checkDeadline();
                                return atom.type < core::AtomType::Salt ||
                                       atom.type > core::AtomType::Water;
                            }))
                continue;
            selected.push_back(i);
            if (self(self, i, atoms + count)) return true;
            selected.pop_back();
        }
        return false;
    };
    if (!search(search, 0, 0))
        throw std::invalid_argument(
            "single quintessence batch requires four compatible feed atoms");
    return result;
}
io::SolutionData CompileUnificationFiniteBatch(const io::PuzzleData& puzzle,
                                               std::vector<int>& productEnds, int products) {
    return CompileUnificationFiniteBatch(puzzle, productEnds, products,
                                         std::chrono::steady_clock::time_point::max());
}
io::SolutionData CompileUnificationFiniteBatch(const io::PuzzleData& puzzle,
                                               std::vector<int>& productEnds, int products,
                                               std::chrono::steady_clock::time_point deadline) {
    auto checkDeadline = [&] { CheckDeadline(deadline); };
    checkDeadline();
    if (products < 1) throw std::invalid_argument("unification requires a positive product count");
    productEnds.clear();
    if (products == 1) {
        try {
            return CompileUnificationBatch(puzzle, deadline);
        } catch (const std::invalid_argument&) {
            checkDeadline();
        }
    }
    std::array<bool, 6> present{};
    for (const auto& input : puzzle.inputs) {
        checkDeadline();
        for (const auto& atom : input.atoms) {
            checkDeadline();
            const int type = static_cast<int>(atom.type);
            if (type >= 2 && type <= 5) present[type] = true;
        }
    }
    const auto wheelPermissions = (1ULL << 12) | (1ULL << 13) | (1ULL << 28);
    if ((!present[2] || !present[3] || !present[4] || !present[5]) &&
        (puzzle.permissionBits & wheelPermissions) != wheelPermissions)
        throw std::invalid_argument(
            "finite unification requires cardinal feeds or calcification and wheel conversion");
    return Compile(puzzle, products, deadline, &productEnds);
}
io::SolutionData CompileUnification(const io::PuzzleData& puzzle) {
    return CompileUnification(puzzle, std::chrono::steady_clock::time_point::max());
}
io::SolutionData CompileUnification(const io::PuzzleData& puzzle,
                                    std::chrono::steady_clock::time_point deadline) {
    CheckDeadline(deadline);
    return Compile(puzzle, 6 * puzzle.outputMultiplier, deadline);
}
io::SolutionData CompileSaltQuintessenceBatch(const io::PuzzleData& puzzle, int saltInput) {
    return CompileSaltQuintessenceBatch(puzzle, saltInput,
                                        std::chrono::steady_clock::time_point::max());
}
io::SolutionData CompileSaltQuintessenceBatch(const io::PuzzleData& puzzle, int saltInput,
                                             std::chrono::steady_clock::time_point deadline) {
    CheckDeadline(deadline);
    if (saltInput < 0 || saltInput >= static_cast<int>(puzzle.inputs.size()) ||
        puzzle.inputs[saltInput].atoms.size() != 1 || !puzzle.inputs[saltInput].bonds.empty() ||
        puzzle.inputs[saltInput].atoms[0].type != core::AtomType::Salt)
        throw std::invalid_argument("quintessence batch requires a singleton salt feed");
    auto proxy = puzzle;
    proxy.inputs.assign(4, puzzle.inputs[saltInput]);
    proxy.outputs = {{{{core::AtomType::Quintessence, {0, 0}}}, {}}};
    auto result = Compile(proxy, 1, deadline);
    for (auto& o : result.objects) {
        CheckDeadline(deadline);
        if (o.typeName == "input") o.bodyData[2] = saltInput;
    }
    CheckDeadline(deadline);
    return result;
}
} // namespace opusoptima::solvers
