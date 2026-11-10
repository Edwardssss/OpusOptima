#include "Disassembly.hpp"
#include "FragmentCustody.hpp"
#include "HeldArmMotion.hpp"
#include "InventoryIR.hpp"
#include "PhysicalInventory.hpp"

#include <algorithm>
#include <array>
#include <numeric>
#include <stdexcept>

namespace opusoptima::solvers {
void ValidateSpecialRules(const io::PuzzleData& puzzle) {
    if (!puzzle.productionInfo) return;
    if (!puzzle.productionInfo->conduits.empty())
        throw std::invalid_argument("unsupported special rules: production conduits require "
                                    "chamber-aware transport; this is not a chemistry limitation");
    throw std::invalid_argument(
        "unsupported special rules: production chambers, isolation, shrink and vial constraints "
        "require a production compiler; this is not a chemistry limitation");
}

io::SolutionData CompileIntactFeed(const io::PuzzleData& p) {
    ValidateSpecialRules(p);
    constexpr auto permissions = (1ULL << 0) | (1ULL << 3) | (1ULL << 22) | (1ULL << 23);
    if (p.version != 3 || p.inputs.size() != 1 || p.outputs.size() != 1 ||
        p.outputMultiplier <= 0 || (p.permissionBits & permissions) != permissions)
        throw std::invalid_argument("intact feed transport requires one finite input/output and "
                                    "arm, track and grab permissions");
    const auto& input = p.inputs[0];
    const auto& output = p.outputs[0];
    if (input.atoms.empty() || input.atoms.size() > 64 ||
        input.atoms.size() != output.atoms.size() || input.bonds.size() != output.bonds.size())
        throw std::invalid_argument("intact feed transport requires congruent molecules");
    auto valid = [](const io::Molecule& m) {
        for (std::size_t i = 0; i < m.atoms.size(); ++i) {
            const auto& a = m.atoms[i];
            if (!core::AtomTypeFromId(static_cast<std::uint8_t>(a.type)) ||
                a.type == core::AtomType::Repeat || a.position.q < -128 || a.position.q > 127 ||
                a.position.r < -128 || a.position.r > 127)
                return false;
            for (std::size_t j = 0; j < i; ++j)
                if (m.atoms[j].position == a.position) return false;
        }
        std::vector<bool> reached(m.atoms.size());
        reached[0] = true;
        for (std::size_t k = 0; k < m.atoms.size(); ++k)
            for (const auto& b : m.bonds) {
                // Existing colored bonds can join any atom types; transport neither
                // creates them nor requires a triplex glyph.
                if (core::Distance(b.from, b.to) != 1 ||
                    (b.bits != 1 && (b.bits <= 0 || (b.bits & ~14) != 0)))
                    return false;
                auto index = [&](core::HexCoord c) {
                    return std::find_if(m.atoms.begin(), m.atoms.end(),
                                        [&](const auto& a) { return a.position == c; }) -
                           m.atoms.begin();
                };
                const auto a = index(b.from), z = index(b.to);
                if (a == m.atoms.size() || z == m.atoms.size()) return false;
                if (reached[a] || reached[z]) reached[a] = reached[z] = true;
            }
        for (std::size_t i = 0; i < m.bonds.size(); ++i)
            for (std::size_t j = 0; j < i; ++j) {
                const auto& a = m.bonds[i];
                const auto& b = m.bonds[j];
                if ((a.from == b.from && a.to == b.to) || (a.from == b.to && a.to == b.from))
                    return false;
            }
        return std::all_of(reached.begin(), reached.end(), [](bool b) { return b; });
    };
    if (!valid(input) || !valid(output)) throw std::invalid_argument("invalid intact molecule");
    auto top = input.atoms[0].position;
    int low = top.q, high = top.q;
    for (const auto& a : input.atoms) {
        if (a.position.r > top.r) top = a.position;
        low = std::min(low, a.position.q);
        high = std::max(high, a.position.q);
    }
    try {
        ExecutionIR program;
        auto state = ExecutionIR::Initial(p);
        program.Append(p, state, {ExecutionIR::Kind::Acquire, 0, {}});
        program.Append(p, state, {ExecutionIR::Kind::Relocate, 0, {1}, 0, 0, {high - low + 3, 0}});
        program.Append(p, state, {ExecutionIR::Kind::Output, 0, {1}});
        return CompileTranslationIR(p, program);
    } catch (const ExecutionIR::Error& error) {
        throw std::invalid_argument(error.what());
    }
}

io::SolutionData CompileDisassemblyIR(const io::PuzzleData& p, const ExecutionIR& program,
                                      const std::vector<std::size_t>& outputCounts) {
    using K = ExecutionIR::Kind;
    ValidateSpecialRules(p);
    if (!(p.permissionBits & (1ULL << 9)) || p.inputs.size() != 1 || p.outputs.empty() ||
        program.steps.empty())
        throw ExecutionIR::Unsupported(
            "disassembly requires one feed, outputs and unbonder permission");
    const auto final = program.Replay(p);
    const auto requiredOutputs =
        outputCounts.empty() ? std::vector<std::size_t>(p.outputs.size(), 1) : outputCounts;
    if (requiredOutputs.size() != p.outputs.size() || requiredOutputs.front() == 0 ||
        std::any_of(requiredOutputs.begin(), requiredOutputs.end(),
                    [&](auto count) { return count != requiredOutputs.front(); }) ||
        (6ULL * p.outputMultiplier) % requiredOutputs.front() != 0)
        throw ExecutionIR::Unsupported("disassembly requires a balanced dividing output period");
    ExecutionIR::RequireComplete(final, requiredOutputs);
    const auto firstCut =
        std::find_if(program.steps.begin(), program.steps.end(),
                     [](const auto& step) { return step.operation.kind == K::Unbond; });
    if (firstCut == program.steps.end())
        throw ExecutionIR::Unsupported("disassembly requires explicit bond cuts");
    auto transportPuzzle = p;
    transportPuzzle.outputs = transportPuzzle.inputs;
    ExecutionIR transport;
    auto state = ExecutionIR::Initial(transportPuzzle);
    for (auto step = program.steps.begin(); step != firstCut; ++step)
        transport.Append(transportPuzzle, state, step->operation);
    transport.Append(transportPuzzle, state, {K::Output, 0, {1}});
    auto result = CompileTranslationIR(transportPuzzle, transport);
    result.objects.pop_back();
    result.solutionName = "IR fragment disassembly";
    const bool inventoryTransport =
        std::any_of(firstCut, program.steps.end(), [](const auto& step) {
            return step.operation.kind == K::Relocate || step.operation.kind == K::Store ||
                   step.operation.kind == K::Retrieve;
        });
    bool outputStarted = false;
    for (auto step = firstCut; step != program.steps.end(); ++step) {
        const auto& op = step->operation;
        if (op.kind == K::Unbond && (!outputStarted || inventoryTransport)) {
            const auto a = step->preconditions.atoms.at(op.atoms.at(0)).position;
            const auto b = step->preconditions.atoms.at(op.atoms.at(1)).position;
            const auto edge = ExecutionIR::Edge{std::min(op.atoms[0], op.atoms[1]),
                                                std::max(op.atoms[0], op.atoms[1])};
            if (step->preconditions.bonds.at(edge) != op.bits)
                throw ExecutionIR::Unsupported("physical unbonder removes the entire bond mask");
            int rotation = 0;
            auto direction = core::HexCoord{1, 0};
            while (rotation < 6 && core::Add(a, direction) != b) {
                direction = core::RotateClockwise(direction);
                ++rotation;
            }
            if (rotation == 6) throw ExecutionIR::Error("nonadjacent cut endpoints");
            result.objects.push_back({"unbonder", a, {}, {1, rotation, 0, 0, 0}});
        } else if (op.kind == K::Output) {
            outputStarted = true;
            const auto& molecule = p.outputs.at(op.index);
            const auto& stock = step->preconditions;
            const auto fragment = ExecutionIR::Fragment(stock, op.atoms.at(0));
            int turns = -1;
            core::HexCoord offset;
            for (int n = 0; n < 6 && turns < 0; ++n) {
                auto rotate = [n](core::HexCoord c) {
                    for (int i = 0; i < n; ++i)
                        c = core::RotateClockwise(c);
                    return c;
                };
                for (const auto id : fragment) {
                    const auto delta = core::Subtract(stock.atoms.at(id).position,
                                                      rotate(molecule.atoms.front().position));
                    std::map<ExecutionIR::AtomId, core::HexCoord> positions;
                    bool match = true;
                    for (const auto& atom : molecule.atoms) {
                        const auto pos = core::Add(rotate(atom.position), delta);
                        const auto found =
                            std::find_if(fragment.begin(), fragment.end(), [&](auto candidate) {
                                const auto& actual = stock.atoms.at(candidate);
                                return actual.type == atom.type && actual.position == pos;
                            });
                        if (found == fragment.end()) {
                            match = false;
                            break;
                        }
                        positions[*found] = atom.position;
                    }
                    for (const auto& [edge, bits] : stock.bonds) {
                        if (!positions.contains(edge.a) || !positions.contains(edge.b)) continue;
                        if (std::none_of(molecule.bonds.begin(), molecule.bonds.end(),
                                         [&](const auto& bond) {
                                             return bond.bits == bits &&
                                                    ((bond.from == positions.at(edge.a) &&
                                                      bond.to == positions.at(edge.b)) ||
                                                     (bond.to == positions.at(edge.a) &&
                                                      bond.from == positions.at(edge.b)));
                                         }))
                            match = false;
                    }
                    if (match) {
                        turns = n;
                        offset = delta;
                        break;
                    }
                }
            }
            if (turns < 0) throw ExecutionIR::Error("cannot place fragment output");
            const auto existing =
                std::find_if(result.objects.begin(), result.objects.end(), [&](const auto& object) {
                    return object.typeName == "out-std" &&
                           object.bodyData.at(2) == static_cast<int>(op.index);
                });
            if (existing == result.objects.end())
                result.objects.push_back(
                    {"out-std", offset, {}, {1, turns, static_cast<int>(op.index), 0, 0}});
            else if (existing->position != offset || existing->bodyData.at(1) != turns)
                throw ExecutionIR::LayoutFailure("repeated fragments require a shared output pose");
        } else if (op.kind == K::Discard) {
            outputStarted = true;
            const auto position = step->preconditions.atoms.at(op.atoms.at(0)).position;
            if (std::none_of(result.objects.begin(), result.objects.end(), [&](const auto& object) {
                    return object.typeName == "glyph-disposal" && object.position == position;
                }))
                result.objects.push_back({"glyph-disposal", position, {}, {1, 0, 0, 0, 0}});
        } else if (op.kind != K::Relocate && op.kind != K::Store && op.kind != K::Retrieve) {
            throw ExecutionIR::Unsupported("disassembly inventory does not lower this operation");
        }
    }
    if (inventoryTransport) {
        auto input = p.inputs.front();
        const auto& acquire = program.steps.front().operation;
        const core::HexFrame inputFrame{acquire.translation, acquire.rotation};
        for (auto& atom : input.atoms)
            atom.position = inputFrame.ToWorld(atom.position);
        for (auto& bond : input.bonds) {
            bond.from = inputFrame.ToWorld(bond.from);
            bond.to = inputFrame.ToWorld(bond.to);
        }
        auto top = input.atoms.front().position;
        int low = top.q, high = low;
        for (const auto& atom : input.atoms) {
            if (atom.position.r > top.r) top = atom.position;
            low = std::min(low, atom.position.q);
            high = std::max(high, atom.position.q);
        }
        int end = top.q;
        int maxLength = (p.permissionBits & (1ULL << 2)) ? 3 : 1;
        for (const auto& step : program.steps) {
            if (step.operation.kind == K::Relocate &&
                (step.operation.rotation != 0 || step.operation.translation.r != 0 ||
                 step.operation.translation.q == 0))
                throw ExecutionIR::Unsupported(
                    "inventory corridor requires nonzero horizontal translations");
            for (const auto& [id, atom] : step.effects.atoms) {
                if (atom.position.r > top.r || atom.position.q < low ||
                    atom.position.q > top.q + 4096 || atom.position.q < -128 ||
                    atom.position.q > 127 || atom.position.r < -128 || atom.position.r > 127)
                    throw ExecutionIR::Unsupported(
                        "inventory corridor exceeds its geometric bounds");
                end = std::max(end, atom.position.q);
                maxLength = std::max(maxLength, top.r + 1 - atom.position.r);
            }
        }
        if (maxLength > 3 || (maxLength > 1 && !(p.permissionBits & (1ULL << 2))))
            throw ExecutionIR::Unsupported("inventory corridor requires an available piston reach");
        const auto base = core::Add(top, {0, 1});
        if (base.q < -128 || base.q > 127 || base.r < -128 || base.r > 127)
            throw ExecutionIR::Unsupported("inventory corridor arm base exceeds coordinate bounds");
        std::vector<core::HexCoord> rail;
        for (int q = top.q; q <= end; ++q)
            rail.push_back({q, base.r});
        auto tip = [&](const HeldArmState& arm) {
            return core::Add(rail.at(arm.trackIndex), {0, -arm.length});
        };
        auto sameWorld = [](auto a, auto b) {
            a.stored.clear();
            b.stored.clear();
            return a == b;
        };
        auto safeGlyphs = [&](const ExecutionIR::State& world, bool held, core::HexCoord contact,
                              const ExecutionIR::Step* release) {
            std::vector<ExecutionIR::AtomId> heldFragment;
            if (held) {
                const auto grip =
                    std::find_if(world.atoms.begin(), world.atoms.end(), [&](const auto& entry) {
                        return entry.second.position == contact;
                    });
                if (grip == world.atoms.end()) return false;
                heldFragment = ExecutionIR::Fragment(world, grip->first);
            }
            for (const auto& step : program.steps) {
                const auto& op = step.operation;
                if (op.kind == K::Unbond) {
                    const auto a = step.preconditions.atoms.at(op.atoms[0]).position;
                    const auto b = step.preconditions.atoms.at(op.atoms[1]).position;
                    for (const auto& [edge, bits] : world.bonds) {
                        const auto x = world.atoms.at(edge.a).position;
                        const auto y = world.atoms.at(edge.b).position;
                        if ((x == a && y == b) || (x == b && y == a)) {
                            // Co-located unbonders activate together before sequential IR cuts.
                            auto actual = world;
                            auto expected = step.preconditions;
                            for (const auto& cut : program.steps) {
                                if (cut.operation.kind != K::Unbond) continue;
                                const auto first = cut.operation.atoms[0],
                                           second = cut.operation.atoms[1];
                                if (!world.atoms.contains(first) || !world.atoms.contains(second) ||
                                    world.atoms.at(first) != cut.preconditions.atoms.at(first) ||
                                    world.atoms.at(second) != cut.preconditions.atoms.at(second))
                                    continue;
                                const ExecutionIR::Edge cutEdge{std::min(first, second),
                                                                std::max(first, second)};
                                actual.bonds.erase(cutEdge);
                                expected.bonds.erase(cutEdge);
                            }
                            if (!sameWorld(actual, expected)) return false;
                        }
                    }
                } else if (op.kind == K::Discard) {
                    const auto position = step.preconditions.atoms.at(op.atoms[0]).position;
                    const auto occupant = std::find_if(
                        world.atoms.begin(), world.atoms.end(),
                        [&](const auto& entry) { return entry.second.position == position; });
                    if (occupant != world.atoms.end() &&
                        std::find(heldFragment.begin(), heldFragment.end(), occupant->first) ==
                            heldFragment.end() &&
                        (!release || release->operation.kind != K::Discard ||
                         !sameWorld(world, release->preconditions)))
                        return false;
                } else if (op.kind == K::Output) {
                    const auto fragment = ExecutionIR::Fragment(step.preconditions, op.atoms[0]);
                    bool occupied = true;
                    bool gripped = false;
                    for (const auto id : fragment) {
                        const auto position = step.preconditions.atoms.at(id).position;
                        const auto found = std::find_if(
                            world.atoms.begin(), world.atoms.end(),
                            [&](const auto& entry) { return entry.second.position == position; });
                        occupied &= found != world.atoms.end();
                        if (found != world.atoms.end())
                            gripped |= std::find(heldFragment.begin(), heldFragment.end(),
                                                 found->first) != heldFragment.end();
                    }
                    if (occupied && (!held || !gripped) &&
                        (!release || release->operation.kind != K::Output ||
                         release->operation.index != op.index ||
                         !sameWorld(world, release->preconditions)))
                        return false;
                }
            }
            return true;
        };
        auto inventory = LowerInventoryIR(
            p, program, rail, {0, 1, 4, 0},
            [&](const auto& world, const auto& from, const auto& to, char opcode, bool holding) {
                if (from.direction != 4 || to.direction != 4 ||
                    (opcode != 'A' && opcode != 'a' && opcode != 'G' && opcode != 'g' &&
                     opcode != 'E' && opcode != 'e'))
                    return false;
                const auto a = tip(from), b = tip(to);
                auto next = world;
                std::vector<ExecutionIR::AtomId> moving;
                if (holding) {
                    const auto grip =
                        std::find_if(world.atoms.begin(), world.atoms.end(),
                                     [&](const auto& entry) { return entry.second.position == a; });
                    if (grip == world.atoms.end()) return false;
                    moving = ExecutionIR::Fragment(world, grip->first);
                    for (auto id : moving)
                        next.atoms.at(id).position =
                            core::Add(world.atoms.at(id).position, core::Subtract(b, a));
                }
                for (const auto& [id, atom] : world.atoms) {
                    if (std::find(moving.begin(), moving.end(), id) != moving.end()) continue;
                    if ((opcode == 'E' || opcode == 'e') && a != b &&
                        core::Distance(a, atom.position) + core::Distance(atom.position, b) ==
                            core::Distance(a, b))
                        return false;
                    for (auto moved : moving)
                        if (next.atoms.at(moved).position == atom.position) return false;
                }
                // Once the input has cleared, its replacement remains present on
                // every subsequent cycle, including the empty return journey.
                for (auto id : moving)
                    for (const auto& atom : input.atoms)
                        if (world.atoms.at(id).position.q > high &&
                            next.atoms.at(id).position == atom.position)
                            return false;
                const ExecutionIR::Step* release = nullptr;
                if (opcode == 'g') {
                    for (const auto& planned : program.steps)
                        if ((planned.operation.kind == K::Output ||
                             planned.operation.kind == K::Discard) &&
                            sameWorld(next, planned.preconditions) &&
                            std::any_of(
                                next.atoms.begin(), next.atoms.end(), [&](const auto& entry) {
                                    const auto fragment =
                                        ExecutionIR::Fragment(next, planned.operation.atoms[0]);
                                    return entry.second.position == b &&
                                           std::find(fragment.begin(), fragment.end(),
                                                     entry.first) != fragment.end();
                                })) {
                            release = &planned;
                            break;
                        }
                }
                return safeGlyphs(next, opcode != 'g' && (holding || opcode == 'G'), b, release);
            },
            [&](const auto& step, const auto& arm) {
                if (!safeGlyphs(step.effects,
                                step.operation.kind != K::Output &&
                                    step.operation.kind != K::Discard &&
                                    step.operation.kind != K::Acquire,
                                tip(arm), nullptr))
                    return false;
                return std::any_of(
                    program.steps.begin(), program.steps.end(), [&](const auto& planned) {
                        return step.operation.kind == planned.operation.kind &&
                               step.operation.index == planned.operation.index &&
                               sameWorld(step.preconditions, planned.preconditions) &&
                               sameWorld(step.effects, planned.effects);
                    });
            },
            maxLength, 20000, requiredOutputs);
        auto& arm = result.objects[0];
        arm.typeName = maxLength > 1 ? "piston" : "arm1";
        arm.instructions.clear();
        int time = 0;
        for (char opcode : inventory.Tape())
            arm.instructions.push_back({opcode, time++});
        arm.bodyData[3] = time;
        auto& track = result.objects[1];
        track.bodyData = {1, 0, 0, 0, static_cast<int>(rail.size())};
        for (const auto pos : rail) {
            track.bodyData.push_back(pos.q - base.q);
            track.bodyData.push_back(0);
        }
        track.bodyData.push_back(0);
        return result;
    }
    // Glyphs are active throughout transport, not only at their IR operation.
    // Conservatively require the complete workstation footprint to stay clear
    // until the last translation tick.
    const auto& delivered = firstCut->preconditions;
    const int distance =
        delivered.atoms.at(1).position.q - program.steps.front().effects.atoms.at(1).position.q;
    std::vector<core::HexCoord> workstation;
    for (const auto& step : program.steps) {
        const auto& op = step.operation;
        if (op.kind == K::Unbond) {
            workstation.push_back(step.preconditions.atoms.at(op.atoms[0]).position);
            workstation.push_back(step.preconditions.atoms.at(op.atoms[1]).position);
        } else if (op.kind == K::Discard) {
            workstation.push_back(step.preconditions.atoms.at(op.atoms[0]).position);
        } else if (op.kind == K::Output) {
            for (auto id : ExecutionIR::Fragment(step.preconditions, op.atoms[0]))
                workstation.push_back(step.preconditions.atoms.at(id).position);
        }
    }
    for (int remaining = 1; remaining <= distance; ++remaining)
        for (const auto& [id, atom] : delivered.atoms)
            if (std::find(workstation.begin(), workstation.end(),
                          core::Subtract(atom.position, {remaining, 0})) != workstation.end())
                throw ExecutionIR::Unsupported(
                    "workstation overlaps material before terminal delivery");
    return result;
}

std::optional<io::SolutionData> CompileAcceptedFragmentFeed(
    const io::PuzzleData& p, const std::function<bool(const io::SolutionData&)>& accept) {
    ValidateSpecialRules(p);
    constexpr auto permissions =
        (1ULL << 0) | (1ULL << 3) | (1ULL << 9) | (1ULL << 22) | (1ULL << 23);
    if (p.version != 3 || p.outputMultiplier <= 0 ||
        (p.permissionBits & permissions) != permissions || p.inputs.size() != 1 ||
        p.outputs.empty() || p.outputs.size() > 64 || p.inputs[0].atoms.empty() ||
        p.inputs[0].atoms.size() > 64 || p.inputs[0].bonds.empty() || p.inputs[0].bonds.size() > 12)
        throw std::invalid_argument(
            "fragment-feed cut search requires one bonded feed and at most 4096 cut subsets");
    using K = ExecutionIR::Kind;
    const auto& input = p.inputs[0];
    auto remainingAtoms = input.atoms;
    for (const auto& output : p.outputs)
        for (const auto& atom : output.atoms) {
            const auto available =
                std::find_if(remainingAtoms.begin(), remainingAtoms.end(),
                             [&](const auto& feedAtom) { return feedAtom.type == atom.type; });
            if (available == remainingAtoms.end())
                throw std::invalid_argument(
                    "fragment-feed cuts cannot synthesize missing output elements or atoms");
            remainingAtoms.erase(available);
        }
    auto signature = [](const io::Molecule& molecule) {
        std::vector<int> best;
        for (int rotation = 0; rotation < 6; ++rotation) {
            auto rotated = molecule;
            auto turn = [&](core::HexCoord cell) {
                for (int i = 0; i < rotation; ++i)
                    cell = core::RotateClockwise(cell);
                return cell;
            };
            for (auto& atom : rotated.atoms)
                atom.position = turn(atom.position);
            auto lessCell = [](core::HexCoord a, core::HexCoord b) {
                return a.q != b.q ? a.q < b.q : a.r < b.r;
            };
            const auto origin = std::min_element(rotated.atoms.begin(), rotated.atoms.end(),
                                                 [&](const auto& a, const auto& b) {
                                                     return lessCell(a.position, b.position);
                                                 })
                                    ->position;
            for (auto& atom : rotated.atoms)
                atom.position = core::Subtract(atom.position, origin);
            std::sort(
                rotated.atoms.begin(), rotated.atoms.end(),
                [&](const auto& a, const auto& b) { return lessCell(a.position, b.position); });
            std::vector<int> key{static_cast<int>(rotated.atoms.size()),
                                 static_cast<int>(rotated.bonds.size())};
            for (const auto& atom : rotated.atoms) {
                key.push_back(atom.position.q);
                key.push_back(atom.position.r);
                key.push_back(static_cast<int>(atom.type));
            }
            std::vector<std::array<int, 5>> edges;
            for (const auto& bond : rotated.bonds) {
                auto a = core::Subtract(turn(bond.from), origin);
                auto b = core::Subtract(turn(bond.to), origin);
                if (lessCell(b, a)) std::swap(a, b);
                edges.push_back({a.q, a.r, b.q, b.r, bond.bits});
            }
            std::sort(edges.begin(), edges.end());
            for (const auto& edge : edges)
                key.insert(key.end(), edge.begin(), edge.end());
            if (best.empty() || key < best) best = std::move(key);
        }
        return best;
    };
    // Cutting preserves each component's geometry, elements and complete bond masks.
    (void)ExecutionIR::Initial(p);
    std::vector<std::vector<int>> outputSignatures;
    for (const auto& output : p.outputs)
        outputSignatures.push_back(signature(output));
    std::vector<std::pair<std::size_t, std::size_t>> endpoints;
    for (const auto& bond : input.bonds) {
        auto index = [&](core::HexCoord position) {
            return static_cast<std::size_t>(
                std::find_if(input.atoms.begin(), input.atoms.end(),
                             [&](const auto& atom) { return atom.position == position; }) -
                input.atoms.begin());
        };
        endpoints.push_back({index(bond.from), index(bond.to)});
    }
    auto matchesOutputs = [&](unsigned mask, std::size_t period) {
        std::vector<std::size_t> component(input.atoms.size());
        std::iota(component.begin(), component.end(), 0);
        for (std::size_t i = 0; i < input.bonds.size(); ++i)
            if (!(mask & (1U << i))) {
                const auto a = component[endpoints[i].first], b = component[endpoints[i].second];
                if (a != b)
                    for (auto& label : component)
                        if (label == b) label = a;
            }
        std::vector<std::vector<int>> fragments;
        for (std::size_t label = 0; label < input.atoms.size(); ++label) {
            io::Molecule fragment;
            for (std::size_t i = 0; i < input.atoms.size(); ++i)
                if (component[i] == label) fragment.atoms.push_back(input.atoms[i]);
            if (fragment.atoms.empty()) continue;
            for (std::size_t i = 0; i < input.bonds.size(); ++i)
                if (!(mask & (1U << i)) && component[endpoints[i].first] == label)
                    fragment.bonds.push_back(input.bonds[i]);
            fragments.push_back(signature(fragment));
        }
        for (std::size_t copy = 0; copy < period; ++copy) {
            for (const auto& output : outputSignatures) {
                const auto found = std::find(fragments.begin(), fragments.end(), output);
                if (found == fragments.end()) return false;
                fragments.erase(found);
            }
        }
        return fragments.empty() || (p.permissionBits & (1ULL << 17));
    };
    std::size_t outputAtoms = 0;
    for (const auto& output : p.outputs)
        outputAtoms += output.atoms.size();
    if (outputAtoms == 0) return std::nullopt;
    for (std::size_t period = input.atoms.size() / outputAtoms; period > 0; --period) {
        if ((6ULL * p.outputMultiplier) % period != 0 ||
            (input.atoms.size() != period * outputAtoms && !(p.permissionBits & (1ULL << 17))))
            continue;
        std::vector<unsigned> matchingMasks;
        for (unsigned mask = 1; mask < (1U << input.bonds.size()); ++mask)
            if (matchesOutputs(mask, period)) matchingMasks.push_back(mask);
        for (int rotation = 0; rotation < 6; ++rotation) {
            const core::HexFrame frame{{0, 0}, rotation};
            int low = frame.ToWorld(input.atoms[0].position).q, high = low;
            for (const auto& atom : input.atoms) {
                const auto cell = frame.ToWorld(atom.position);
                low = std::min(low, cell.q);
                high = std::max(high, cell.q);
            }
            for (int transportFragments = 0; transportFragments < 2; ++transportFragments) {
                for (const auto mask : matchingMasks) {
                    for (int clearance = 0; clearance < 3; ++clearance) {
                        std::optional<io::SolutionData> candidate;
                        try {
                            ExecutionIR program;
                            auto state = ExecutionIR::Initial(p);
                            program.Append(p, state, {K::Acquire, 0, {}, 0, rotation});
                            program.Append(
                                p, state,
                                {K::Relocate, 0, {1}, 0, 0, {high - low + 3 + clearance, 0}});
                            for (std::size_t i = 0; i < input.bonds.size(); ++i)
                                if (mask & (1U << i)) {
                                    const auto& bond = input.bonds[i];
                                    auto id = [&](core::HexCoord pos) -> ExecutionIR::AtomId {
                                        return 1 +
                                               (std::find_if(input.atoms.begin(), input.atoms.end(),
                                                             [&](const auto& atom) {
                                                                 return atom.position == pos;
                                                             }) -
                                                input.atoms.begin());
                                    };
                                    program.Append(
                                        p, state,
                                        {K::Unbond, 0, {id(bond.from), id(bond.to)}, bond.bits});
                                }
                            std::vector<std::optional<core::HexCoord>> outputAnchors(
                                p.outputs.size());
                            for (std::size_t copy = 0; copy < period; ++copy) {
                                for (std::size_t output = 0; output < p.outputs.size(); ++output) {
                                    bool matched = false;
                                    std::vector<std::pair<ExecutionIR::AtomId, ExecutionIR::Atom>>
                                        available;
                                    for (const auto& entry : state.atoms)
                                        available.push_back(entry);
                                    std::sort(available.begin(), available.end(),
                                              [](const auto& a, const auto& b) {
                                                  return a.second.position.q > b.second.position.q;
                                              });
                                    for (const auto& [id, atom] : available) {
                                        try {
                                            auto trial = program;
                                            auto trialState = state;
                                            const auto fragment =
                                                ExecutionIR::Fragment(trialState, id);
                                            auto fragmentAnchor = [&] {
                                                auto anchor =
                                                    trialState.atoms.at(fragment.front()).position;
                                                for (const auto member : fragment) {
                                                    const auto cell =
                                                        trialState.atoms.at(member).position;
                                                    if (std::tie(cell.q, cell.r) <
                                                        std::tie(anchor.q, anchor.r))
                                                        anchor = cell;
                                                }
                                                return anchor;
                                            };
                                            if (outputAnchors[output]) {
                                                const auto anchor = fragmentAnchor();
                                                const core::HexCoord delta{
                                                    outputAnchors[output]->q - anchor.q,
                                                    outputAnchors[output]->r - anchor.r};
                                                if (delta != core::HexCoord{})
                                                    trial.Append(
                                                        p, trialState,
                                                        {K::Relocate, 0, {id}, 0, 0, delta});
                                            } else if (transportFragments)
                                                trial.Append(p, trialState,
                                                             {K::Relocate,
                                                              0,
                                                              {id},
                                                              0,
                                                              0,
                                                              {static_cast<int>((output + 1) *
                                                                                (high - low + 3)),
                                                               0}});
                                            const auto anchor = fragmentAnchor();
                                            trial.Append(p, trialState, {K::Output, output, {id}});
                                            outputAnchors[output] = anchor;
                                            program = std::move(trial);
                                            state = std::move(trialState);
                                            matched = true;
                                            break;
                                        } catch (const ExecutionIR::Error&) {
                                        }
                                    }
                                    if (!matched)
                                        throw ExecutionIR::Unsupported(
                                            "no matching output fragment");
                                }
                            }
                            while (!state.atoms.empty()) {
                                if (!state.bonds.empty()) {
                                    const auto [edge, bits] = *state.bonds.begin();
                                    program.Append(p, state,
                                                   {K::Unbond, 0, {edge.a, edge.b}, bits});
                                    continue;
                                }
                                const auto rightmost = std::max_element(
                                    state.atoms.begin(), state.atoms.end(),
                                    [](const auto& a, const auto& b) {
                                        return a.second.position.q < b.second.position.q;
                                    });
                                const auto id = rightmost->first;
                                const auto position = rightmost->second.position;
                                int destination = high + 20;
                                for (const auto& step : program.steps)
                                    for (const auto& [member, atom] : step.effects.atoms)
                                        destination = std::max(destination, atom.position.q + 3);
                                program.Append(
                                    p, state,
                                    {K::Relocate, 0, {id}, 0, 0, {destination - position.q, 0}});
                                program.Append(p, state, {K::Discard, 0, {id}});
                            }
                            candidate = CompileDisassemblyIR(
                                p, program, std::vector<std::size_t>(p.outputs.size(), period));
                        } catch (const ExecutionIR::Error&) {
                        }
                        if (candidate && accept(*candidate)) return candidate;
                    }
                }
            }
        }
    }
    return std::nullopt;
}
io::SolutionData CompileFragmentFeed(const io::PuzzleData& p) {
    auto result = CompileAcceptedFragmentFeed(p, [](const io::SolutionData&) { return true; });
    if (result) return std::move(*result);
    throw std::invalid_argument("fragment-feed cut search found no supported terminal workstation");
}

io::SolutionData CompileTranslationIR(const io::PuzzleData& p, const ExecutionIR& program) {
    ValidateSpecialRules(p);
    using K = ExecutionIR::Kind;
    constexpr auto permissions = (1ULL << 0) | (1ULL << 3) | (1ULL << 22) | (1ULL << 23);
    if (p.version != 3 || p.outputMultiplier <= 0 ||
        (p.permissionBits & permissions) != permissions || p.inputs.size() != 1 ||
        p.outputs.size() != 1 || program.steps.size() < 3 ||
        program.steps.front().operation.kind != K::Acquire ||
        program.steps.back().operation.kind != K::Output)
        throw ExecutionIR::Unsupported(
            "translation compiler requires an Acquire/transport/Output transaction");
    int distance = 0;
    for (std::size_t i = 1; i + 1 < program.steps.size(); ++i) {
        const auto& op = program.steps[i].operation;
        if (op.kind == K::Relocate) {
            if (op.rotation != 0 || op.translation.r != 0 || op.translation.q <= 0 ||
                op.translation.q > 4096 - distance)
                throw ExecutionIR::Unsupported("translation compiler requires bounded positive-q "
                                               "translation without rotation");
            distance += op.translation.q;
        } else if (op.kind != K::Store && op.kind != K::Retrieve) {
            throw ExecutionIR::Unsupported("translation compiler does not lower this operation");
        }
    }
    auto input = p.inputs[0];
    const auto& acquire = program.steps.front().operation;
    const core::HexFrame inputFrame{acquire.translation, acquire.rotation};
    for (auto& atom : input.atoms)
        atom.position = inputFrame.ToWorld(atom.position);
    for (auto& bond : input.bonds) {
        bond.from = inputFrame.ToWorld(bond.from);
        bond.to = inputFrame.ToWorld(bond.to);
    }
    const auto& output = p.outputs[0];
    if (input.atoms.empty() || input.atoms.size() > 64)
        throw ExecutionIR::Unsupported(
            "translation compiler supports one connected molecule of at most 64 atoms");
    for (const auto* molecule : {&p.inputs[0], &output})
        for (const auto& atom : molecule->atoms)
            if (atom.position.q < -128 || atom.position.q > 127 || atom.position.r < -128 ||
                atom.position.r > 127)
                throw ExecutionIR::Unsupported("coordinates exceed serialized molecule range");
    const auto final = program.Replay(p);
    ExecutionIR::RequireComplete(final, std::vector<std::size_t>(p.outputs.size(), 1));
    auto rotate = [](core::HexCoord c, int n) {
        while (n--)
            c = core::RotateClockwise(c);
        return c;
    };
    int turns = -1;
    core::HexCoord offset;
    for (int n = 0; n < 6 && turns == -1; ++n)
        for (const auto& anchor : input.atoms) {
            const auto delta = core::Subtract(anchor.position, rotate(output.atoms[0].position, n));
            const bool atoms =
                std::all_of(output.atoms.begin(), output.atoms.end(), [&](const auto& a) {
                    return std::any_of(input.atoms.begin(), input.atoms.end(), [&](const auto& b) {
                        return a.type == b.type &&
                               core::Add(rotate(a.position, n), delta) == b.position;
                    });
                });
            const bool bonds =
                std::all_of(output.bonds.begin(), output.bonds.end(), [&](const auto& a) {
                    const auto x = core::Add(rotate(a.from, n), delta),
                               y = core::Add(rotate(a.to, n), delta);
                    return std::any_of(input.bonds.begin(), input.bonds.end(), [&](const auto& b) {
                        return a.bits == b.bits &&
                               ((x == b.from && y == b.to) || (x == b.to && y == b.from));
                    });
                });
            if (atoms && bonds) {
                turns = n;
                offset = delta;
                break;
            }
        }
    if (turns < 0)
        throw std::invalid_argument("intact feed transport cannot change chemistry or bonds");
    auto top = input.atoms[0].position;
    int low = top.q, high = top.q;
    for (const auto& a : input.atoms) {
        if (a.position.r > top.r) top = a.position;
        low = std::min(low, a.position.q);
        high = std::max(high, a.position.q);
    }
    // The arm base and track sit above every atom; translating past the full
    // q extent separates the product from both the input and its next spawn.
    if (distance < high - low + 3 || distance > 4096)
        throw ExecutionIR::Unsupported(
            "translation corridor is not separated or exceeds compiler budget");
    io::SolutionData result;
    result.puzzleName = p.name;
    result.solutionName = "Intact wide-feed transport";
    const auto base = core::Add(top, {0, 1});
    io::SolutionObject arm;
    arm.typeName = "arm1";
    arm.position = base;
    arm.bodyData = {1, 4, 0, 2 * distance + 2, 0};
    HeldArmMotionProblem motion;
    for (int i = 0; i <= distance; ++i)
        motion.track.push_back(core::Add(base, {i, 0}));
    for (const auto& atom : input.atoms)
        motion.fragment.push_back(core::Subtract(atom.position, top));
    motion.start = {0, 1, 4, 0};
    motion.goal = {core::Add(top, {distance, 0}), 0};
    motion.pivot = false;
    // This corridor admits translation only: the arm stays above all atoms,
    // and the transported molecule is the only moving material.
    motion.executable = [](const auto&, const auto&, char instruction) {
        return instruction == 'A' || instruction == 'a';
    };
    const auto route = RouteHeldArm(motion);
    if (route.status != MotionStatus::Found)
        throw std::invalid_argument("intact feed arm routing failed");
    int travelled = 0;
    for (std::size_t i = 1; i + 1 < program.steps.size(); ++i) {
        const auto& op = program.steps[i].operation;
        if (op.kind == K::Relocate) travelled += op.translation.q;
        if (op.kind == K::Store && (travelled < high - low + 3 || travelled == distance))
            throw ExecutionIR::Unsupported(
                "parking requires clearance from input and output glyphs");
    }
    auto inventory = LowerInventoryIR(
        p, program, motion.track, motion.start,
        [](const auto&, const auto& from, const auto& to, char op, bool) {
            return (op == 'A' || op == 'a' || op == 'G' || op == 'g') && from.direction == 4 &&
                   to.direction == 4 && from.length == 1 && to.length == 1;
        },
        [&](const auto& step, const auto&) {
            if (step.operation.kind == K::Acquire)
                return step.preconditions == program.steps.front().preconditions &&
                       step.effects == program.steps.front().effects && step.operation.index == 0;
            auto expected = program.steps.back().preconditions;
            expected.stored.clear();
            return step.operation.kind == K::Output && step.preconditions == expected &&
                   step.effects == program.steps.back().effects;
        });
    int time = 0;
    for (char op : inventory.Tape())
        arm.instructions.push_back({op, time++});
    arm.bodyData[3] = time;
    result.objects.push_back(arm);
    io::SolutionObject track;
    track.typeName = "track";
    track.position = base;
    track.bodyData = {1, 0, 0, 0, distance + 1};
    for (int i = 0; i <= distance; ++i) {
        track.bodyData.push_back(i);
        track.bodyData.push_back(0);
    }
    track.bodyData.push_back(0);
    result.objects.push_back(track);
    result.objects.push_back({"input", inputFrame.origin, {}, {1, inputFrame.Turns(), 0, 0, 0}});
    result.objects.push_back(
        {"out-std", core::Add(offset, {distance, 0}), {}, {1, turns, 0, 0, 0}});
    return result;
}
} // namespace opusoptima::solvers
