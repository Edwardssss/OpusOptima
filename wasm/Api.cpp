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
#include "EditorJson.hpp"
#include "PuzzleSpecs.hpp"
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


namespace {
void AppendMolecule(std::string& out, const opusoptima::io::Molecule& molecule) {
    out += "{\"atoms\":[";
    for (std::size_t i = 0; i < molecule.atoms.size(); ++i) {
        if (i) out += ',';
        const auto& atom = molecule.atoms[i];
        out += "{\"q\":" + std::to_string(atom.position.q) +
               ",\"r\":" + std::to_string(atom.position.r) + ",\"type\":" +
               std::to_string(static_cast<int>(atom.type)) + ",\"symbol\":" +
               Quote(std::string(1, opusoptima::core::AtomTypeSymbol(atom.type))) + "}";
    }
    out += "],\"bonds\":[";
    for (std::size_t i = 0; i < molecule.bonds.size(); ++i) {
        if (i) out += ',';
        const auto& bond = molecule.bonds[i];
        out += "{\"a\":[" + std::to_string(bond.from.q) + ',' + std::to_string(bond.from.r) +
               "],\"b\":[" + std::to_string(bond.to.q) + ',' + std::to_string(bond.to.r) +
               "],\"bits\":" + std::to_string(bond.bits) + "}";
    }
    out += "]}";
}

std::string DescribeJson(const std::uint8_t* puzzleBytes, int puzzleLength,
                         const std::uint8_t* solutionBytes, int solutionLength) {
    const auto puzzle = opusoptima::io::BinaryParser::ParsePuzzle(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(puzzleBytes),
                                   static_cast<std::size_t>(puzzleLength)));
    const auto solution = opusoptima::io::BinaryParser::ParseSolution(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(solutionBytes),
                                   static_cast<std::size_t>(solutionLength)));
    std::string out = "{\"puzzle\":{\"name\":" + Quote(puzzle.name) + ",\"inputs\":[";
    for (std::size_t i = 0; i < puzzle.inputs.size(); ++i) {
        if (i) out += ',';
        AppendMolecule(out, puzzle.inputs[i]);
    }
    out += "],\"outputs\":[";
    for (std::size_t i = 0; i < puzzle.outputs.size(); ++i) {
        if (i) out += ',';
        AppendMolecule(out, puzzle.outputs[i]);
    }
    out += "]},\"objects\":[";
    for (std::size_t i = 0; i < solution.objects.size(); ++i) {
        if (i) out += ',';
        const auto& object = solution.objects[i];
        out += "{\"type\":" + Quote(object.typeName) + ",\"q\":" +
               std::to_string(object.position.q) + ",\"r\":" +
               std::to_string(object.position.r) + ",\"body\":[";
        for (std::size_t j = 0; j < object.bodyData.size(); ++j) {
            if (j) out += ',';
            out += std::to_string(object.bodyData[j]);
        }
        out += "],\"tape\":[";
        for (std::size_t j = 0; j < object.instructions.size(); ++j) {
            if (j) out += ',';
            out += "[" + Quote(std::string(1, object.instructions[j].action)) + "," +
                   std::to_string(object.instructions[j].parameter) + "]";
        }
        out += "]}";
    }
    out += "]}";
    return out;
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

EMSCRIPTEN_KEEPALIVE char* opusoptima_describe(const std::uint8_t* puzzleBytes, int puzzleLength,
                                                const std::uint8_t* solutionBytes,
                                                int solutionLength) {
    if (!puzzleBytes || puzzleLength <= 0 || !solutionBytes || solutionLength <= 0)
        return Duplicate("{\"error\":\"invalid describe input\"}");
    try {
        return Duplicate(DescribeJson(puzzleBytes, puzzleLength, solutionBytes, solutionLength));
    } catch (const std::exception& exception) {
        return Duplicate(std::string("{\"error\":") + Quote(exception.what()) + "}");
    } catch (...) {
        return Duplicate("{\"error\":\"unknown describe failure\"}");
    }
}

EMSCRIPTEN_KEEPALIVE int opusoptima_make_puzzle(
    const char* name, int inputAtomCount, const int* inputAtoms, int inputBondCount,
    const int* inputBonds, int outputAtomCount, const int* outputAtoms, int outputBondCount,
    const int* outputBonds, int outputMultiplier, std::uint8_t** puzzle, int* puzzleLength,
    char** error) {
    if (puzzle) *puzzle = nullptr;
    if (puzzleLength) *puzzleLength = 0;
    if (error) *error = nullptr;
    const auto fail = [&](const char* detail) {
        if (error) *error = Duplicate(detail);
        return 0;
    };
    if (!name || !inputAtoms || !outputAtoms || inputAtomCount < 1 || outputAtomCount < 1 ||
        outputMultiplier < 1 || (inputBondCount && !inputBonds) ||
        (outputBondCount && !outputBonds))
        return fail("invalid puzzle editor arguments");
    try {
        opusoptima::io::PuzzleData data;
        data.version = 3;
        data.name = name;
        data.permissionBits = 0x17c7370fULL;
        data.outputMultiplier = outputMultiplier;
        const auto addAtoms = [](opusoptima::io::Molecule& molecule, const int* atoms, int count) {
            for (int i = 0; i < count; ++i) {
                const int type = atoms[i * 3];
                const int q = atoms[i * 3 + 1];
                const int r = atoms[i * 3 + 2];
                if (type < 1 || type > 16 || q < -128 || q > 127 || r < -128 || r > 127)
                    throw std::invalid_argument("invalid atom type or coordinate");
                molecule.atoms.push_back(
                    {static_cast<opusoptima::core::AtomType>(type), {q, r}});
            }
        };
        const auto addBonds = [](opusoptima::io::Molecule& molecule, const int* bonds, int count) {
            for (int i = 0; i < count; ++i) {
                const int bits = bonds[i * 5];
                const int aq = bonds[i * 5 + 1];
                const int ar = bonds[i * 5 + 2];
                const int bq = bonds[i * 5 + 3];
                const int br = bonds[i * 5 + 4];
                if (bits < 1 || bits > 255 || aq < -128 || aq > 127 || ar < -128 || ar > 127 ||
                    bq < -128 || bq > 127 || br < -128 || br > 127)
                    throw std::invalid_argument("invalid bond bits or coordinate");
                molecule.bonds.push_back({static_cast<std::uint8_t>(bits),
                                          {aq, ar},
                                          {bq, br}});
            }
        };
        opusoptima::io::Molecule input;
        opusoptima::io::Molecule output;
        addAtoms(input, inputAtoms, inputAtomCount);
        addBonds(input, inputBonds, inputBondCount);
        addAtoms(output, outputAtoms, outputAtomCount);
        addBonds(output, outputBonds, outputBondCount);
        data.inputs.push_back(std::move(input));
        data.outputs.push_back(std::move(output));
        const auto bytes = opusoptima::io::BinaryWriter::WritePuzzle(data);
        auto* result = static_cast<std::uint8_t*>(std::malloc(bytes.size()));
        if (!result) return fail("out of memory");
        std::memcpy(result, bytes.data(), bytes.size());
        *puzzle = result;
        *puzzleLength = static_cast<int>(bytes.size());
        return 1;
    } catch (const std::exception& exception) {
        if (error) *error = Duplicate(exception.what());
    } catch (...) {
        if (error) *error = Duplicate("unknown puzzle editor failure");
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE int opusoptima_make_puzzle_json(const char* json, std::uint8_t** puzzle,
                                                       int* puzzleLength, char** error) {
    if (puzzle) *puzzle = nullptr;
    if (puzzleLength) *puzzleLength = 0;
    if (error) *error = nullptr;
    if (!json) {
        if (error) *error = Duplicate("missing editor JSON");
        return 0;
    }
    std::string detail;
    const auto data = opusoptima::wasm::BuildEditorPuzzle(json, detail);
    if (!data) {
        if (error) *error = Duplicate(detail);
        return 0;
    }
    try {
        const auto bytes = opusoptima::io::BinaryWriter::WritePuzzle(*data);
        auto* result = static_cast<std::uint8_t*>(std::malloc(bytes.size()));
        if (!result) {
            if (error) *error = Duplicate("out of memory");
            return 0;
        }
        std::memcpy(result, bytes.data(), bytes.size());
        *puzzle = result;
        *puzzleLength = static_cast<int>(bytes.size());
        return 1;
    } catch (const std::exception& exception) {
        if (error) *error = Duplicate(exception.what());
    } catch (...) {
        if (error) *error = Duplicate("unknown puzzle writer failure");
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE void opusoptima_free(void* pointer) { std::free(pointer); }
}
