#include <emscripten/emscripten.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <span>
#include <string>
#include <vector>

#include "BinaryParser.hpp"
#include "BinaryWriter.hpp"
#include "StandardSolver.hpp"

extern "C" {
#include "verifier.h"
}

namespace {
char* Duplicate(const std::string& text) {
    auto* result = static_cast<char*>(std::malloc(text.size() + 1));
    if (!result) return nullptr;
    std::memcpy(result, text.c_str(), text.size() + 1);
    return result;
}

std::string Quote(const std::string& text) {
    std::string out{"\""};
    for (const char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char escaped[7];
                    std::snprintf(escaped, sizeof(escaped), "\\u%04x",
                                  static_cast<unsigned char>(c));
                    out += escaped;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
    return out;
}

std::string VerifyJson(const std::uint8_t* puzzle, int puzzleLength, const std::uint8_t* solution,
                       int solutionLength) {
    void* verifier = verifier_create_from_bytes(
        reinterpret_cast<const char*>(puzzle), puzzleLength,
        reinterpret_cast<const char*>(solution), solutionLength);
    if (!verifier) return "{\"status\":\"unavailable\",\"detail\":\"verifier creation failed\"}";
    const char* error = verifier_error(verifier);
    if (error) {
        const std::string detail = error;
        verifier_destroy(verifier);
        return "{\"status\":\"failed\",\"detail\":" + Quote(detail) + "}";
    }
    const int cycles = verifier_evaluate_metric(verifier, "cycles");
    error = verifier_error(verifier);
    if (error || cycles < 0) {
        const std::string detail = error ? error : "cycles metric failed";
        verifier_destroy(verifier);
        return "{\"status\":\"failed\",\"detail\":" + Quote(detail) + "}";
    }
    const int cost = verifier_evaluate_metric(verifier, "cost");
    const int area = verifier_evaluate_metric(verifier, "area");
    const int instructions = verifier_evaluate_metric(verifier, "instructions");
    error = verifier_error(verifier);
    const std::string metricError = error ? error : "";
    verifier_destroy(verifier);
    if (!metricError.empty() || cost < 0 || area < 0 || instructions < 0)
        return "{\"status\":\"failed\",\"detail\":" +
               Quote(metricError.empty() ? "objective metric failed" : metricError) + "}";
    return "{\"status\":\"passed\",\"cycles\":" + std::to_string(cycles) +
           ",\"cost\":" + std::to_string(cost) + ",\"area\":" + std::to_string(area) +
           ",\"instructions\":" + std::to_string(instructions) + "}";
}
} // namespace

extern "C" {
// Returns 1 on success. On success *solution is a malloc'd byte buffer and
// *error is null; on failure *solution is null and *error is a malloc'd string.
EMSCRIPTEN_KEEPALIVE int opusoptima_solve(const std::uint8_t* puzzleBytes, int puzzleLength,
                                           std::uint8_t** solution, int* solutionLength,
                                           char** error) {
    if (solution) *solution = nullptr;
    if (solutionLength) *solutionLength = 0;
    if (error) *error = nullptr;
    if (!puzzleBytes || puzzleLength <= 0 || !solution || !solutionLength) {
        if (error) *error = Duplicate("invalid puzzle input");
        return 0;
    }
    try {
        const auto puzzle = opusoptima::io::BinaryParser::ParsePuzzle(
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(puzzleBytes),
                                       static_cast<std::size_t>(puzzleLength)));
        const auto candidate = opusoptima::solvers::standard::CompileUpstreamStandard(puzzle);
        if (!candidate) {
            if (error) *error = Duplicate("the Standard solver found no candidate");
            return 0;
        }
        const auto bytes = opusoptima::io::BinaryWriter::WriteSolution(*candidate);
        auto* output = static_cast<std::uint8_t*>(std::malloc(bytes.size()));
        if (!output) {
            if (error) *error = Duplicate("out of memory");
            return 0;
        }
        std::memcpy(output, bytes.data(), bytes.size());
        *solution = output;
        *solutionLength = static_cast<int>(bytes.size());
        return 1;
    } catch (const std::exception& exception) {
        if (error) *error = Duplicate(exception.what());
    } catch (...) {
        if (error) *error = Duplicate("unknown solver failure");
    }
    return 0;
}

// Returns a malloc'd JSON object; the caller owns it and releases it with
// opusoptima_free. This is an in-process omsim check, not the native isolated
// legacy-gate transaction.
EMSCRIPTEN_KEEPALIVE char* opusoptima_verify(const std::uint8_t* puzzleBytes, int puzzleLength,
                                              const std::uint8_t* solutionBytes,
                                              int solutionLength) {
    if (!puzzleBytes || puzzleLength <= 0 || !solutionBytes || solutionLength <= 0)
        return Duplicate("{\"status\":\"failed\",\"detail\":\"invalid verifier input\"}");
    return Duplicate(VerifyJson(puzzleBytes, puzzleLength, solutionBytes, solutionLength));
}

EMSCRIPTEN_KEEPALIVE void opusoptima_free(void* pointer) { std::free(pointer); }
}
