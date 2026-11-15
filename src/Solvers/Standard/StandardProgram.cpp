#include "StandardProgram.hpp"

// Ported from gtw123/OpusSolver@fdedd3f01a238b941ceba8bace7ea35cbd3b8c41 (MIT).
// See docs/analysis/UPSTREAM_STANDARD_PORT_20261115.md for the module map.


#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace opusoptima::solvers::standard {

namespace {

void EnsureSize(std::vector<Instr>& list, std::size_t size) {
    if (list.size() < size) list.resize(size, Instr::None);
}

} // namespace

void Program::AddInstructions(const std::vector<Arm*>& arms, const std::vector<Instr>& list,
                              bool updateTime) {
    for (Arm* arm : arms) {
        int time = currentTime;
        auto& armInstructions = GetArmInstructions(arm);
        EnsureSize(armInstructions,
                   static_cast<std::size_t>(time) + list.size());
        for (Instr instruction : list) {
            armInstructions[static_cast<std::size_t>(time++)] = instruction;
        }
    }
    if (updateTime) currentTime += static_cast<int>(list.size());
}

void Program::AdjustTime(int delta) {
    currentTime += delta;
    if (currentTime < 0) {
        const std::size_t padding = static_cast<std::size_t>(-currentTime);
        for (auto& [arm, list] : instructions) {
            list.insert(list.begin(), padding, Instr::None);
        }
        currentTime = 0;
    }
}

Program* ProgramWriter::GetLastFragmentForArm(Arm* arm) {
    for (auto it = fragments_.rbegin(); it != fragments_.rend(); ++it) {
        if (it->instructions.contains(arm)) return &*it;
    }
    return nullptr;
}

Program ProgramBuilder::Build() {
    for (auto it = fragments_.rbegin(); it != fragments_.rend(); ++it) AddFragment(*it);
    AddPeriodOverride();
    AddRepeats();
    return program_;
}

void ProgramBuilder::AddFragment(const Program& fragment) {
    const int timeShift = CalculateTimeShift(fragment);
    if (timeShift > 0) {
        const std::size_t padding = static_cast<std::size_t>(timeShift);
        for (auto& [arm, list] : program_.instructions) {
            list.insert(list.begin(), padding, Instr::None);
        }
    }

    const std::size_t startTime =
        timeShift < 0 ? static_cast<std::size_t>(-timeShift) : std::size_t{0};
    for (const auto& [arm, fragmentInstructions] : fragment.instructions) {
        if (arm->Removed() || arm->Parent() == nullptr) continue;
        auto& programInstructions = program_.GetArmInstructions(arm);
        EnsureSize(programInstructions, startTime);
        for (std::size_t i = 0; i < fragmentInstructions.size(); ++i) {
            const std::size_t time = startTime + i;
            if (time < programInstructions.size())
                programInstructions[time] = fragmentInstructions[i];
            else
                programInstructions.push_back(fragmentInstructions[i]);
        }
    }
}

int ProgramBuilder::CalculateTimeShift(const Program& fragment) const {
    std::optional<int> maxTimeShift;
    for (const auto& [arm, fragmentInstructions] : fragment.instructions) {
        int lastIndex = -1;
        for (int i = static_cast<int>(fragmentInstructions.size()) - 1; i >= 0; --i) {
            if (fragmentInstructions[static_cast<std::size_t>(i)] != Instr::None) {
                lastIndex = i;
                break;
            }
        }
        if (lastIndex < 0) continue;
        if (fragmentInstructions[static_cast<std::size_t>(lastIndex)] == Instr::Reset) {
            lastIndex += CalculateResetTime(fragment, arm, lastIndex) - 1;
        }

        int firstIndex = -1;
        const auto it = program_.instructions.find(arm);
        if (it != program_.instructions.end()) {
            for (std::size_t i = 0; i < it->second.size(); ++i) {
                if (it->second[i] != Instr::None) {
                    firstIndex = static_cast<int>(i);
                    break;
                }
            }
        }
        if (firstIndex >= 0) {
            const int shift = lastIndex + 1 - firstIndex;
            maxTimeShift = maxTimeShift ? std::max(*maxTimeShift, shift) : shift;
        }
    }
    return maxTimeShift.value_or(0);
}

int ProgramBuilder::CalculateResetTime(const Program& fragment, Arm* arm, int index) const {
    std::vector<Instr> instructions;
    for (const Program& prior : fragments_) {
        if (&prior == &fragment) break;
        const auto it = prior.instructions.find(arm);
        if (it != prior.instructions.end())
            instructions.insert(instructions.end(), it->second.begin(), it->second.end());
    }
    const auto fragmentIt = fragment.instructions.find(arm);
    if (fragmentIt != fragment.instructions.end()) {
        const auto end = std::min<std::size_t>(fragmentIt->second.size(),
                                               static_cast<std::size_t>(index));
        instructions.insert(instructions.end(), fragmentIt->second.begin(),
                            fragmentIt->second.begin() + static_cast<std::ptrdiff_t>(end));
    }

    std::size_t startIndex = 0;
    for (std::size_t i = 0; i < instructions.size(); ++i)
        if (instructions[i] == Instr::Reset) startIndex = i + 1;
    return CalculateResetTime(
        std::vector<Instr>(instructions.begin() + static_cast<std::ptrdiff_t>(startIndex),
                           instructions.end()),
        arm->extension);
}

int ProgramBuilder::CalculateResetTime(const std::vector<Instr>& list, int initialExtension) {
    bool isGrabbing = false;
    int extension = initialExtension;
    int deltaRotation = 0;
    int deltaPosition = 0;
    for (Instr instruction : list) {
        switch (instruction) {
        case Instr::Grab: isGrabbing = true; break;
        case Instr::Drop: isGrabbing = false; break;
        case Instr::RotateCW: deltaRotation = (deltaRotation + 5) % 6; break;
        case Instr::RotateCCW: deltaRotation = (deltaRotation + 1) % 6; break;
        case Instr::Extend: extension = std::min(extension + 1, 3); break;
        case Instr::Retract: extension = std::max(extension - 1, 0); break;
        case Instr::MovePos: ++deltaPosition; break;
        case Instr::MoveNeg: --deltaPosition; break;
        case Instr::None:
        case Instr::Wait:
        case Instr::PivotCW:
        case Instr::PivotCCW: break;
        default: throw std::invalid_argument("unexpected instruction in reset time");
        }
    }
    int resetTime = isGrabbing ? 1 : 0;
    resetTime += std::abs(extension - initialExtension);
    resetTime += 3 - std::abs(deltaRotation - 3);
    resetTime += std::abs(deltaPosition);
    return std::max(resetTime, 1);
}

void ProgramBuilder::AddRepeats() {
    for (auto& [arm, instructions] : program_.instructions) {
        (void)arm;
        int start = -1;
        for (std::size_t i = 0; i < instructions.size(); ++i) {
            if (instructions[i] != Instr::None && instructions[i] != Instr::Wait) {
                start = static_cast<int>(i);
                break;
            }
        }
        if (start < 0) continue;
        int end = -1;
        for (std::size_t i = static_cast<std::size_t>(start); i < instructions.size(); ++i) {
            if (instructions[i] == Instr::Reset) {
                end = static_cast<int>(i);
                break;
            }
        }
        if (end < 0) continue;
        const std::vector<Instr> sequence(
            instructions.begin() + start,
            instructions.begin() + static_cast<std::ptrdiff_t>(end) + 1);
        const std::size_t startIndex = static_cast<std::size_t>(end) + 1;
        for (std::size_t i = startIndex; i < instructions.size(); ++i) {
            if (instructions[i] == Instr::None) continue;
            if (i + sequence.size() > instructions.size()) return;
            bool matches = true;
            for (std::size_t j = 0; j < sequence.size(); ++j) {
                if (instructions[i + j] != sequence[j]) {
                    matches = false;
                    break;
                }
            }
            if (!matches) return;
            instructions[i] = Instr::Repeat;
            for (std::size_t j = 1; j < sequence.size(); ++j) instructions[i + j] = Instr::None;
            i += sequence.size();
        }
    }
}

void ProgramBuilder::AddPeriodOverride() {
    int maxLength = -1;
    std::vector<Instr>* maxInstructions = nullptr;
    for (auto& [arm, instructions] : program_.instructions) {
        (void)arm;
        int lastIndex = -1;
        for (int i = static_cast<int>(instructions.size()) - 1; i >= 0; --i) {
            if (instructions[static_cast<std::size_t>(i)] != Instr::None) {
                lastIndex = i;
                break;
            }
        }
        if (lastIndex < 0 || instructions[static_cast<std::size_t>(lastIndex)] != Instr::Wait)
            continue;
        int firstIndex = -1;
        for (std::size_t i = 0; i < instructions.size(); ++i) {
            if (instructions[i] != Instr::None) {
                firstIndex = static_cast<int>(i);
                break;
            }
        }
        const int length = lastIndex - firstIndex;
        if (maxLength < 0 || length > maxLength) {
            maxLength = length;
            maxInstructions = &instructions;
        }
    }
    if (!maxInstructions) return;
    int lastIndex = -1;
    for (int i = static_cast<int>(maxInstructions->size()) - 1; i >= 0; --i) {
        if ((*maxInstructions)[static_cast<std::size_t>(i)] != Instr::None) {
            lastIndex = i;
            break;
        }
    }
    if (lastIndex >= 0)
        (*maxInstructions)[static_cast<std::size_t>(lastIndex)] = Instr::PeriodOverride;
}

char InstructionCode(Instr instruction) {
    switch (instruction) {
    case Instr::PivotCCW: return 'p';
    case Instr::Extend: return 'E';
    case Instr::PivotCW: return 'P';
    case Instr::Drop: return 'g';
    case Instr::MoveNeg: return 'a';
    case Instr::RotateCCW: return 'r';
    case Instr::Retract: return 'e';
    case Instr::RotateCW: return 'R';
    case Instr::Grab: return 'G';
    case Instr::MovePos: return 'A';
    case Instr::PeriodOverride: return 'O';
    case Instr::Reset: return 'X';
    case Instr::Repeat: return 'C';
    default: throw std::invalid_argument("instruction has no code");
    }
}

} // namespace opusoptima::solvers::standard
