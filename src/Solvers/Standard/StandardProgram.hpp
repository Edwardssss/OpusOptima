#pragma once

// C++ port of upstream OpusSolver's ProgramWriter/ProgramBuilder
// (gtw123/OpusSolver@fdedd3f, MIT). Fragments carry their own local time; the
// builder shifts them so instructions on the same arm never overlap while
// independent arms run concurrently.

#include <cstddef>
#include <map>
#include <optional>
#include <vector>

#include "StandardModel.hpp"

namespace opusoptima::solvers::standard {

enum class Instr {
    None,
    Wait,
    PivotCCW,
    Extend,
    PivotCW,
    Drop,
    MoveNeg,
    RotateCCW,
    Retract,
    RotateCW,
    Grab,
    MovePos,
    PeriodOverride,
    Reset,
    Repeat
};

class Program {
    public:
    std::map<Arm*, std::vector<Instr>> instructions;
    int currentTime{};

    [[nodiscard]] std::vector<Instr>& GetArmInstructions(Arm* arm) { return instructions[arm]; }
    [[nodiscard]] const std::vector<Instr>& GetArmInstructions(Arm* arm) const {
        static const std::vector<Instr> empty;
        const auto it = instructions.find(arm);
        return it == instructions.end() ? empty : it->second;
    }
    void AddInstructions(const std::vector<Arm*>& arms, const std::vector<Instr>& list,
                         bool updateTime);
    void AdjustTime(int delta);
};

class ProgramWriter {
    public:
    ProgramWriter() { NewFragment(); }

    [[nodiscard]] const std::vector<Program>& Fragments() const { return fragments_; }

    void NewFragment() {
        fragments_.emplace_back();
        current_ = fragments_.size() - 1;
    }
    Program* GetLastFragmentForArm(Arm* arm);

    void Write(Arm* arm, Instr instruction, bool updateTime = true) {
        Write(std::vector<Arm*>{arm}, std::vector<Instr>{instruction}, updateTime);
    }
    void Write(Arm* arm, const std::vector<Instr>& list, bool updateTime = true) {
        Write(std::vector<Arm*>{arm}, list, updateTime);
    }
    void Write(const std::vector<Arm*>& arms, Instr instruction, bool updateTime = true) {
        Write(arms, std::vector<Instr>{instruction}, updateTime);
    }
    void Write(const std::vector<Arm*>& arms, const std::vector<Instr>& list,
               bool updateTime = true) {
        fragments_[current_].AddInstructions(arms, list, updateTime);
    }
    void WriteGrabResetAction(Arm* arm, Instr instruction, bool updateTime = true) {
        WriteGrabResetAction(std::vector<Arm*>{arm}, std::vector<Instr>{instruction}, updateTime);
    }
    void WriteGrabResetAction(Arm* arm, const std::vector<Instr>& list,
                              bool updateTime = true) {
        WriteGrabResetAction(std::vector<Arm*>{arm}, list, updateTime);
    }
    void WriteGrabResetAction(const std::vector<Arm*>& arms, Instr instruction,
                              bool updateTime = true) {
        WriteGrabResetAction(arms, std::vector<Instr>{instruction}, updateTime);
    }
    void WriteGrabResetAction(const std::vector<Arm*>& arms, const std::vector<Instr>& list,
                              bool updateTime = true) {
        std::vector<Instr> all{Instr::Grab};
        all.insert(all.end(), list.begin(), list.end());
        all.push_back(Instr::Reset);
        Write(arms, all, updateTime);
        if (updateTime) AdjustTime(-1);
    }
    void AdjustTime(int delta) { fragments_[current_].AdjustTime(delta); }

    /// Merges another writer's fragment into the current fragment at the current
    /// time, matching upstream `ProgramWriter.AppendFragment`.
    void AppendFragment(const Program& fragment, bool updateTime = true) {
        for (const auto& [arm, instructions] : fragment.instructions)
            fragments_[current_].AddInstructions({arm}, instructions, false);
        if (updateTime) fragments_[current_].AdjustTime(fragment.currentTime);
    }

    private:
    std::vector<Program> fragments_;
    std::size_t current_{};
};

class ProgramBuilder {
    public:
    explicit ProgramBuilder(const std::vector<Program>& fragments) : fragments_(fragments) {}
    [[nodiscard]] Program Build();

    private:
    void AddFragment(const Program& fragment);
    [[nodiscard]] int CalculateTimeShift(const Program& fragment) const;
    [[nodiscard]] int CalculateResetTime(const Program& fragment, Arm* arm, int index) const;
    static int CalculateResetTime(const std::vector<Instr>& list, int initialExtension);
    void AddRepeats();
    void AddPeriodOverride();

    const std::vector<Program>& fragments_;
    Program program_;
};

[[nodiscard]] char InstructionCode(Instr instruction);

} // namespace opusoptima::solvers::standard
