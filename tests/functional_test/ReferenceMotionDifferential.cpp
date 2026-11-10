#include "Verification/ReferenceMotion.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>

extern "C" void reference_motion_oracle(const int*, float, float*);
extern "C" void reference_snap_oracle(float, float, int*);
extern "C" int reference_raster_oracle(float, float, float, int*);

int main() {
    namespace rm = opusoptima::verification::reference_motion;
    std::uint32_t seed = 0x88d12d8;
    auto next = [&]() { seed = seed * 1664525u + 1013904223u; return seed; };
    std::size_t compared = 0;
    const std::array<float, 9> times{0.f, 0x1p-22f, .125f, .25f, .5f, .75f,
                                     .875f, 0x1.fffffep-1f, 1.f};
    constexpr int randomSamples = 20000;
    constexpr int structuredSamples = 4 * 11 * 3 * 6;
    constexpr int kinematicSamples = 4 * 6 * 2;
    std::size_t rejectedSingularEndpoints = 0;
    std::size_t checkedKinematicEndpoints = 0;
    for (int sample = 0; sample < randomSamples + structuredSamples + kinematicSamples; ++sample) {
        rm::Motion m;
        m.mechanism = static_cast<rm::Mechanism>(sample % 4);
        auto coordinate = [&]() { return static_cast<int>(next() % 2097153u) - 1048576; };
        m.base = {coordinate(), coordinate()};
        m.grabberOffset = {1 + static_cast<int>(next() % 8u), 0};
        m.absoluteGrabPosition = {coordinate(), coordinate()};
        m.atom = {coordinate(), coordinate()};
        m.baseRotation = static_cast<int>(next() % 11u) - 5;
        m.isPiston = sample % 3 == 0 || m.mechanism == rm::Mechanism::Piston;
        if (m.mechanism == rm::Mechanism::Swing || m.mechanism == rm::Mechanism::Pivot)
            m.rotation = static_cast<int>(next() % 3u) - 1;
        if (m.mechanism == rm::Mechanism::Track)
            m.translation = {coordinate(), coordinate()};
        if (m.mechanism == rm::Mechanism::Piston)
            m.pistonExtension = static_cast<int>(next() % 3u) - 1;
        if (sample >= randomSamples && sample < randomSamples + structuredSamples) {
            int index = sample - randomSamples;
            constexpr std::array<rm::Hex, 6> directions{{{2, 0}, {0, 2}, {-2, 2},
                                                        {-2, 0}, {0, -2}, {2, -2}}};
            const auto direction = directions[index % 6];
            index /= 6;
            const int change = index % 3 - 1;
            index /= 3;
            m = {};
            m.baseRotation = index % 11 - 5;
            index /= 11;
            m.mechanism = static_cast<rm::Mechanism>(index);
            m.base = {-2, 1};
            m.grabberOffset = direction;
            m.absoluteGrabPosition = {1, -2};
            m.atom = {1 - direction.u, -2 - direction.v};
            m.isPiston = true;
            if (m.mechanism == rm::Mechanism::Swing || m.mechanism == rm::Mechanism::Pivot)
                m.rotation = change;
            if (m.mechanism == rm::Mechanism::Track)
                m.translation = {change * direction.u, change * direction.v};
            if (m.mechanism == rm::Mechanism::Piston)
                m.pistonExtension = change;
        }
        if (sample >= randomSamples + structuredSamples) {
            int index = sample - randomSamples - structuredSamples;
            const int change = index % 2 == 0 ? -1 : 1;
            index /= 2;
            const int direction = index % 6;
            index /= 6;
            constexpr std::array<rm::Hex, 6> axes{{{1, 0}, {0, 1}, {-1, 1},
                                                  {-1, 0}, {0, -1}, {1, -1}}};
            const auto axis = axes[direction];
            auto rotate = [](rm::Hex p, int turn) -> rm::Hex {
                return turn == 1 ? rm::Hex{-p.v, p.u + p.v}
                                 : rm::Hex{p.u + p.v, -p.u};
            };
            auto move = [&](rm::Hex p, rm::Hex origin, int turn, rm::Hex translation) {
                rm::Hex relative{p.u - origin.u, p.v - origin.v};
                if (turn != 0) relative = rotate(relative, turn);
                return rm::Hex{origin.u + relative.u + translation.u,
                               origin.v + relative.v + translation.v};
            };
            m = {};
            m.mechanism = static_cast<rm::Mechanism>(index);
            m.isPiston = true;
            m.base = {-2, 1};
            m.baseRotation = direction;
            m.grabberOffset = {2, 0};
            m.absoluteGrabPosition = {m.base.u + 2 * axis.u, m.base.v + 2 * axis.v};
            m.atom = {m.absoluteGrabPosition.u + 1, m.absoluteGrabPosition.v - 1};
            const auto origin = m.mechanism == rm::Mechanism::Swing ? m.base : m.absoluteGrabPosition;
            if (m.mechanism == rm::Mechanism::Swing || m.mechanism == rm::Mechanism::Pivot)
                m.rotation = change;
            if (m.mechanism == rm::Mechanism::Track || m.mechanism == rm::Mechanism::Piston)
                m.translation = {change * axis.u, change * axis.v};
            m.atom = move(m.atom, origin, m.rotation, m.translation);
            m.absoluteGrabPosition = move(m.absoluteGrabPosition, origin, m.rotation, m.translation);
            if (m.mechanism == rm::Mechanism::Track)
                m.base = move(m.base, origin, 0, m.translation);
            if (m.mechanism == rm::Mechanism::Piston) {
                m.pistonExtension = change;
                m.grabberOffset.u += change;
            }
            if (m.mechanism == rm::Mechanism::Swing)
                m.baseRotation = (m.baseRotation + change) % 6;

            // Derive endpoint expectations from direction indices, not the move helper.
            const auto endAxis = axes[(direction + change + 6) % 6];
            rm::Hex expectedBase{-2, 1};
            rm::Hex expectedGrab{-2 + 2 * axis.u, 1 + 2 * axis.v};
            rm::Hex relative{1, -1};
            int expectedLength = 2;
            int expectedDirection = direction;
            if (m.mechanism == rm::Mechanism::Swing) {
                expectedGrab = {-2 + 2 * endAxis.u, 1 + 2 * endAxis.v};
                expectedDirection = (direction + change + 6) % 6;
            }
            if (m.mechanism == rm::Mechanism::Swing || m.mechanism == rm::Mechanism::Pivot)
                relative = change == 1 ? rm::Hex{1, 0} : rm::Hex{0, -1};
            if (m.mechanism == rm::Mechanism::Track) {
                expectedBase.u += change * axis.u;
                expectedBase.v += change * axis.v;
            }
            if (m.mechanism == rm::Mechanism::Track || m.mechanism == rm::Mechanism::Piston) {
                expectedGrab.u += change * axis.u;
                expectedGrab.v += change * axis.v;
            }
            if (m.mechanism == rm::Mechanism::Piston) expectedLength += change;
            const auto finalAxis = axes[expectedDirection];
            auto equal = [](rm::Hex a, rm::Hex b) { return a.u == b.u && a.v == b.v; };
            if (!equal(m.base, expectedBase) || !equal(m.absoluteGrabPosition, expectedGrab) ||
                !equal(m.atom, {expectedGrab.u + relative.u, expectedGrab.v + relative.v}) ||
                !equal(m.grabberOffset, {expectedLength, 0}) ||
                (m.baseRotation + 6) % 6 != expectedDirection ||
                expectedGrab.u - expectedBase.u != expectedLength * finalAxis.u ||
                expectedGrab.v - expectedBase.v != expectedLength * finalAxis.v) {
                std::cerr << "Invalid kinematic endpoint fixture at sample " << sample << '\n';
                return 1;
            }
            ++checkedKinematicEndpoints;
        }
        const std::array<int, 15> input{static_cast<int>(m.mechanism), m.isPiston,
            m.base.u, m.base.v, m.grabberOffset.u, m.grabberOffset.v,
            m.absoluteGrabPosition.u, m.absoluteGrabPosition.v, m.atom.u, m.atom.v,
            m.translation.u, m.translation.v, m.baseRotation, m.rotation, m.pistonExtension};
        for (float t : times) {
            const auto actual = rm::AtomAt(m, t);
            if (!actual) {
                if (m.isPiston && m.grabberOffset.u == 1 && m.grabberOffset.v == 0 &&
                    m.pistonExtension == 1 && t == 0.f) {
                    ++rejectedSingularEndpoints;
                    continue;
                }
                std::cerr << "Unexpected model rejection at sample " << sample << '\n';
                return 1;
            }
            std::array<float, 2> expected{};
            reference_motion_oracle(input.data(), t, expected.data());
            if (std::bit_cast<std::uint32_t>(actual->x) != std::bit_cast<std::uint32_t>(expected[0]) ||
                std::bit_cast<std::uint32_t>(actual->y) != std::bit_cast<std::uint32_t>(expected[1])) {
                std::cerr << "Bit mismatch at sample " << sample << " time " << t << '\n';
                return 1;
            }
            ++compared;
        }
    }
    if (compared != 187349 || rejectedSingularEndpoints != 211 ||
        compared + rejectedSingularEndpoints != (randomSamples + structuredSamples + kinematicSamples) * times.size()) {
        std::cerr << "Unexpected differential coverage counts\n";
        return 1;
    }
    if (checkedKinematicEndpoints != kinematicSamples) {
        std::cerr << "Incomplete kinematic endpoint coverage\n";
        return 1;
    }
    constexpr std::array<rm::Point, 9> snapBoundaries{{
        {0.f, 0.f}, {0.f, 71.f}, {41.f, 35.5f}, {-41.f, 35.5f},
        {0.f, -71.f}, {-41.f, -35.5f}, {41.f, -35.5f},
        {43.999f, 0.f}, {-43.999f, 0.f}
    }};
    const int structuredSnapSamples = 12 + static_cast<int>(snapBoundaries.size()) * 9;
    std::size_t snapped = 0;
    for (int sample = 0; sample < 30000 + structuredSnapSamples; ++sample) {
        rm::Point point{
            static_cast<float>(static_cast<int>(next() % 160000001u) - 80000000) / 8.f,
            static_cast<float>(static_cast<int>(next() % 160000001u) - 80000000) / 8.f};
        if (sample < 12) {
            const float x = sample < 6 ? 41.f : 44.f;
            const int offset = sample % 3 - 1;
            point = {offset == 0 ? x : std::nextafter(x, offset < 0 ? -INFINITY : INFINITY), 0.f};
            if (sample % 6 >= 3) point.x = -point.x;
        } else if (sample < structuredSnapSamples) {
            const int index = sample - 12;
            point = snapBoundaries[index / 9];
            const int dx = index % 3 - 1;
            const int dy = index / 3 % 3 - 1;
            if (dx != 0) point.x = std::nextafter(point.x, dx < 0 ? -INFINITY : INFINITY);
            if (dy != 0) point.y = std::nextafter(point.y, dy < 0 ? -INFINITY : INFINITY);
        }
        int expected[2];
        reference_snap_oracle(point.x, point.y, expected);
        const auto actual = rm::SnapToHex(point);
        if (!actual || actual->u != expected[0] || actual->v != expected[1]) {
            std::cerr << "Snap differential failed at sample " << sample << '\n';
            return 1;
        }
        for (float radius : {15.f, 20.f, 29.f}) {
            int expectedCells[14];
            const int count = reference_raster_oracle(point.x, point.y, radius, expectedCells);
            rm::Hex cells[7];
            const auto actualCount = rm::BoardAreaCells(point, radius, cells);
            if (!actualCount || *actualCount != static_cast<unsigned>(count)) {
                std::cerr << "Raster count differential failed at sample " << sample << '\n';
                return 1;
            }
            for (int i = 0; i < count; ++i) {
                if (cells[i].u != expectedCells[2 * i] || cells[i].v != expectedCells[2 * i + 1]) {
                    std::cerr << "Raster order differential failed at sample " << sample << '\n';
                    return 1;
                }
            }
        }
        ++snapped;
    }
    std::cout << "Extracted C raster differential: " << snapped * 3 << " comparisons passed\n";
    std::cout << "Extracted C snap differential: " << snapped << " comparisons passed\n";
    std::cout << "Kinematic endpoint invariants: " << checkedKinematicEndpoints << " passed\n";
    std::cout << "ReferenceMotion extracted C differential: " << compared << " bitwise point comparisons passed; "
              << rejectedSingularEndpoints << " expected singular endpoints rejected\n";
}
