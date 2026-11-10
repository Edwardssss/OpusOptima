#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#ifdef _WIN32
#    include <process.h>
#else
#    include <unistd.h>
#endif

namespace opusoptima::test {

[[nodiscard]] inline int ProcessId() {
#ifdef _WIN32
    return _getpid();
#else
    return getpid();
#endif
}

// Constant numbers used in test payload construction
inline constexpr int kTestSolutionTag = 7;  // .solution format tag
inline constexpr int kTestCycles = 100;     // dummy cycle count
inline constexpr int kTestCost = 200;       // dummy cost
inline constexpr int kTestArea = 300;       // dummy area
inline constexpr int kTestInstrCount = 400; // dummy instruction count

inline constexpr int kTestClaimedObjects = 5; // for overflow test
inline constexpr int kTestGrabParam = 42;     // dummy arm parameter
inline constexpr int kTestPipeMarker = 100;   // pipe body marker (can be 100/101/102)

/// Helper to assemble little-endian binary payloads for parser unit tests.
class BufferBuilder {
    public:
    void PushU8(std::uint8_t value) {
        buf_.push_back(static_cast<std::byte>(value));
    }

    void PushI32(std::int32_t value) {
        for (int shift = 0; shift < 4; ++shift) {
            buf_.push_back(static_cast<std::byte>((value >> (kBitsPerByte * shift)) & kByteMask));
        }
    }

    void PushU64(std::uint64_t value) {
        for (int shift = 0; shift < kU64ByteCount; ++shift) {
            buf_.push_back(static_cast<std::byte>((value >> (kBitsPerByte * shift)) & kByteMask));
        }
    }

    void PushVStr(std::string_view string) {
        PushU8(static_cast<std::uint8_t>(string.size()));
        for (char chr : string) {
            PushU8(static_cast<std::uint8_t>(chr));
        }
    }

    std::vector<std::byte>& Buffer() {
        return buf_;
    }
    [[nodiscard]] const std::vector<std::byte>& Buffer() const {
        return buf_;
    }

    [[nodiscard]] std::span<const std::byte> Span() const {
        return {buf_.data(), buf_.size()};
    }

    private:
    static constexpr int kBitsPerByte = 8;
    static constexpr int kU64ByteCount = 8;
    static constexpr int kByteMask = 0xFF;

    std::vector<std::byte> buf_;
};

// Pre-built payloads for common test cases

/// Minimal .puzzle: version=3, one input Salt @(0,0), one output Salt @(0,0).
inline std::vector<std::byte> BuildMinimalPuzzle(std::string_view name = "test") {
    BufferBuilder builder;
    builder.PushI32(3);     // version
    builder.PushVStr(name); // puzzle name
    builder.PushU64(0);     // creatorSteamId
    builder.PushU64(0);     // permissionBits

    builder.PushI32(1); // 1 input molecule
    builder.PushI32(1); //   1 atom
    builder.PushU8(1);  //     type = Salt
    builder.PushU8(0);  //     q = 0
    builder.PushU8(0);  //     r = 0
    builder.PushI32(0); //   0 bonds

    builder.PushI32(1); // 1 output molecule
    builder.PushI32(1); //   1 atom
    builder.PushU8(1);  //     type = Salt
    builder.PushU8(0);  //     q = 0
    builder.PushU8(0);  //     r = 0
    builder.PushI32(0); //   0 bonds

    builder.PushI32(1); // outputMultiplier
    builder.PushU8(0);  // hasProduction = false

    return builder.Buffer();
}

/// Minimal .solution: format_tag=7, zero objects, preset metrics.
inline std::vector<std::byte> BuildMinimalSolution(std::string_view name = "test") {
    BufferBuilder builder;
    builder.PushI32(kTestSolutionTag); // format_tag
    builder.PushVStr(name);            // puzzle_name
    builder.PushVStr("test");          // solution_name
    builder.PushI32(4);                // metrics_count = 4
    builder.PushI32(0);
    builder.PushI32(kTestCycles); //   type=0 cycles
    builder.PushI32(1);
    builder.PushI32(kTestCost); //   type=1 cost
    builder.PushI32(2);
    builder.PushI32(kTestArea); //   type=2 area
    builder.PushI32(3);
    builder.PushI32(kTestInstrCount); //   type=3 instructions
    builder.PushI32(0);               // 0 objects

    return builder.Buffer();
}

} // namespace opusoptima::test
