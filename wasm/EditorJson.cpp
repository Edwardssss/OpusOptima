#include "EditorJson.hpp"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "BinaryWriter.hpp"
#include "PuzzleSpecs.hpp"

namespace opusoptima::wasm {
namespace {
using io::Molecule;
using io::MoleculeAtom;
using io::MoleculeBond;
using io::ProductionChamber;
using io::ProductionConduit;
using io::ProductionInfo;
using io::ProductionVial;
using io::PuzzleData;

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type{Type::Null};
    bool boolean{};
    std::string text;
    std::vector<Value> array;
    std::map<std::string, Value> object;
};

class Parser {
    public:
    explicit Parser(std::string_view input) : input_(input) {}

    Value Parse() {
        Skip();
        Value value = ParseValue();
        Skip();
        if (position_ != input_.size()) Fail("trailing JSON data");
        return value;
    }

    private:
    void Skip() {
        while (position_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[position_])))
            ++position_;
    }
    [[noreturn]] void Fail(const std::string& message) const {
        throw std::runtime_error("JSON at byte " + std::to_string(position_) + ": " + message);
    }
    char Peek() const { return position_ < input_.size() ? input_[position_] : '\0'; }
    void Expect(char character) {
        if (Peek() != character) Fail(std::string("expected '") + character + "'");
        ++position_;
    }
    Value ParseValue() {
        Skip();
        switch (Peek()) {
            case '{': return ParseObject();
            case '[': return ParseArray();
            case '"': {
                Value value;
                value.type = Value::Type::String;
                value.text = ParseString();
                return value;
            }
            case 't': return ParseLiteral("true", true);
            case 'f': return ParseLiteral("false", false);
            case 'n': return ParseNull();
            default: return ParseNumber();
        }
    }
    Value ParseLiteral(const char* literal, bool boolean) {
        for (const char* p = literal; *p; ++p) {
            if (Peek() != *p) Fail("invalid literal");
            ++position_;
        }
        Value value;
        value.type = Value::Type::Bool;
        value.boolean = boolean;
        return value;
    }
    Value ParseNull() {
        ParseLiteral("null", false);
        return {};
    }
    Value ParseNumber() {
        const std::size_t start = position_;
        if (Peek() == '-') ++position_;
        while (std::isdigit(static_cast<unsigned char>(Peek()))) ++position_;
        if (Peek() == '.') {
            ++position_;
            while (std::isdigit(static_cast<unsigned char>(Peek()))) ++position_;
        }
        if (Peek() == 'e' || Peek() == 'E') {
            ++position_;
            if (Peek() == '+' || Peek() == '-') ++position_;
            while (std::isdigit(static_cast<unsigned char>(Peek()))) ++position_;
        }
        if (start == position_) Fail("expected value");
        Value value;
        value.type = Value::Type::Number;
        value.text = std::string(input_.substr(start, position_ - start));
        return value;
    }
    std::string ParseString() {
        Expect('"');
        std::string result;
        while (true) {
            if (position_ >= input_.size()) Fail("unterminated string");
            const char c = input_[position_++];
            if (c == '"') return result;
            if (c != '\\') {
                result.push_back(c);
                continue;
            }
            if (position_ >= input_.size()) Fail("unterminated escape");
            const char escaped = input_[position_++];
            switch (escaped) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'u': {
                    if (position_ + 4 > input_.size()) Fail("short unicode escape");
                    unsigned codepoint = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char hex = input_[position_++];
                        codepoint <<= 4;
                        if (hex >= '0' && hex <= '9') codepoint |= static_cast<unsigned>(hex - '0');
                        else if (hex >= 'a' && hex <= 'f') codepoint |= static_cast<unsigned>(hex - 'a' + 10);
                        else if (hex >= 'A' && hex <= 'F') codepoint |= static_cast<unsigned>(hex - 'A' + 10);
                        else Fail("invalid unicode escape");
                    }
                    if (codepoint < 0x80) result.push_back(static_cast<char>(codepoint));
                    else if (codepoint < 0x800) {
                        result.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
                        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
                    } else {
                        result.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
                        result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
                        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
                    }
                    break;
                }
                default: Fail("invalid escape");
            }
        }
    }
    Value ParseArray() {
        Expect('[');
        Value value;
        value.type = Value::Type::Array;
        Skip();
        if (Peek() == ']') { ++position_; return value; }
        while (true) {
            value.array.push_back(ParseValue());
            Skip();
            if (Peek() == ']') { ++position_; return value; }
            Expect(',');
        }
    }
    Value ParseObject() {
        Expect('{');
        Value value;
        value.type = Value::Type::Object;
        Skip();
        if (Peek() == '}') { ++position_; return value; }
        while (true) {
            Skip();
            const std::string key = ParseString();
            Skip();
            Expect(':');
            value.object.emplace(key, ParseValue());
            Skip();
            if (Peek() == '}') { ++position_; return value; }
            Expect(',');
        }
    }

    std::string_view input_;
    std::size_t position_{};
};

const Value* Member(const Value& value, const std::string& key) {
    if (value.type != Value::Type::Object) return nullptr;
    const auto it = value.object.find(key);
    return it == value.object.end() ? nullptr : &it->second;
}

const Value& Required(const Value& value, const char* key, const char* what) {
    const auto* member = Member(value, key);
    if (!member) throw std::runtime_error(std::string(what) + " is required");
    return *member;
}

std::int64_t Integer(const Value& value, const char* what) {
    if (value.type != Value::Type::Number) throw std::runtime_error(std::string(what) + " must be a number");
    try {
        std::size_t used = 0;
        const auto result = std::stoll(value.text, &used, 10);
        if (used != value.text.size()) throw std::runtime_error("invalid number");
        return result;
    } catch (const std::exception&) {
        throw std::runtime_error(std::string("invalid ") + what);
    }
}

std::uint64_t Unsigned(const Value& value, const char* what) {
    if (value.type == Value::Type::Number) {
        const auto result = Integer(value, what);
        if (result < 0) throw std::runtime_error(std::string(what) + " must be non-negative");
        return static_cast<std::uint64_t>(result);
    }
    if (value.type == Value::Type::String) {
        char* end = nullptr;
        const auto result = std::strtoull(value.text.c_str(), &end, 0);
        if (!end || *end != '\0') throw std::runtime_error(std::string("invalid ") + what);
        return result;
    }
    throw std::runtime_error(std::string(what) + " must be a number or string");
}

bool Boolean(const Value& value, bool fallback) {
    return value.type == Value::Type::Bool ? value.boolean : fallback;
}

std::string String(const Value& value, const char* what) {
    if (value.type != Value::Type::String) throw std::runtime_error(std::string(what) + " must be a string");
    return value.text;
}

std::vector<core::HexCoord> ReadPath(const Value& value) {
    if (value.type != Value::Type::Array) throw std::runtime_error("conduit path must be an array");
    std::vector<core::HexCoord> path;
    for (const auto& entry : value.array) {
        if (entry.type != Value::Type::Array || entry.array.size() != 2)
            throw std::runtime_error("path entries must be [q,r]");
        path.push_back({static_cast<int>(Integer(entry.array[0], "path q")),
                        static_cast<int>(Integer(entry.array[1], "path r"))});
    }
    return path;
}

Molecule ReadMolecule(const Value& value) {
    if (value.type != Value::Type::Object) throw std::runtime_error("molecule must be an object");
    Molecule molecule;
    const auto* atoms = Member(value, "atoms");
    if (!atoms || atoms->type != Value::Type::Array) throw std::runtime_error("molecule.atoms must be an array");
    for (const auto& atom : atoms->array) {
        if (atom.type != Value::Type::Object) throw std::runtime_error("atom must be an object");
        const auto* type = Member(atom, "type");
        if (!type) throw std::runtime_error("atom.type is required");
        const int id = static_cast<int>(Integer(*type, "atom.type"));
        if (id < 1 || id > 16) throw std::runtime_error("atom.type must be 1..16");
        molecule.atoms.push_back(
            {static_cast<core::AtomType>(id),
             {static_cast<int>(Integer(Required(atom, "q", "atom.q"), "atom.q")),
              static_cast<int>(Integer(Required(atom, "r", "atom.r"), "atom.r"))}});
    }
    if (const auto* bonds = Member(value, "bonds")) {
        if (bonds->type != Value::Type::Array) throw std::runtime_error("molecule.bonds must be an array");
        for (const auto& bond : bonds->array) {
            if (bond.type != Value::Type::Object) throw std::runtime_error("bond must be an object");
            const auto* bits = Member(bond, "bits");
            const auto* from = Member(bond, "from");
            const auto* to = Member(bond, "to");
            if (!bits || !from || !to || from->type != Value::Type::Array || to->type != Value::Type::Array ||
                from->array.size() != 2 || to->array.size() != 2)
                throw std::runtime_error("bond requires bits and two [q,r] endpoints");
            molecule.bonds.push_back(
                {static_cast<std::uint8_t>(Integer(*bits, "bond.bits")),
                 {static_cast<int>(Integer(from->array[0], "bond.from q")),
                  static_cast<int>(Integer(from->array[1], "bond.from r"))},
                 {static_cast<int>(Integer(to->array[0], "bond.to q")),
                  static_cast<int>(Integer(to->array[1], "bond.to r"))}});
        }
    }
    return molecule;
}

std::vector<Molecule> ReadMolecules(const Value& root, const char* key, bool required) {
    const auto* value = Member(root, key);
    if (!value) {
        if (required) throw std::runtime_error(std::string(key) + " is required");
        return {};
    }
    if (value->type != Value::Type::Array) throw std::runtime_error(std::string(key) + " must be an array");
    std::vector<Molecule> molecules;
    for (const auto& entry : value->array) molecules.push_back(ReadMolecule(entry));
    return molecules;
}
} // namespace

std::optional<PuzzleData> BuildEditorPuzzle(std::string_view json, std::string& error) {
    try {
        const Value root = Parser(json).Parse();
        if (root.type != Value::Type::Object) throw std::runtime_error("root must be an object");
        PuzzleData puzzle;
        puzzle.version = 3;
        puzzle.name = Member(root, "name") ? String(*Member(root, "name"), "name") : "Browser puzzle";
        puzzle.outputMultiplier = 1;
        if (const auto* multiplier = Member(root, "outputMultiplier"))
            puzzle.outputMultiplier = static_cast<int>(Integer(*multiplier, "outputMultiplier"));
        puzzle.permissionBits = (1ULL << 0) | (1ULL << 3) | (1ULL << 9) | (1ULL << 22) | (1ULL << 23);
        if (const auto* permissions = Member(root, "permissionBits"))
            puzzle.permissionBits = Unsigned(*permissions, "permissionBits");
        if (const auto* creator = Member(root, "creatorSteamId"))
            puzzle.creatorSteamId = Unsigned(*creator, "creatorSteamId");
        puzzle.inputs = ReadMolecules(root, "inputs", true);
        puzzle.outputs = ReadMolecules(root, "outputs", true);
        if (puzzle.inputs.empty() || puzzle.outputs.empty())
            throw std::runtime_error("at least one input and output molecule is required");
        if (puzzle.outputMultiplier < 1) throw std::runtime_error("outputMultiplier must be positive");
        if (const auto* production = Member(root, "production")) {
            if (production->type != Value::Type::Object)
                throw std::runtime_error("production must be an object");
            ProductionInfo info;
            info.shrinkLeft = Boolean(*Member(*production, "shrinkLeft"), false);
            info.shrinkRight = Boolean(*Member(*production, "shrinkRight"), false);
            info.isolation = Boolean(*Member(*production, "isolation"), false);
            if (const auto* chambers = Member(*production, "chambers")) {
                if (chambers->type != Value::Type::Array) throw std::runtime_error("chambers must be an array");
                for (const auto& entry : chambers->array) {
                    if (entry.type != Value::Type::Object) throw std::runtime_error("chamber must be an object");
                    ProductionChamber chamber;
                    chamber.position = {
                        static_cast<int>(Integer(Required(entry, "q", "chamber.q"), "chamber.q")),
                        static_cast<int>(Integer(Required(entry, "r", "chamber.r"), "chamber.r"))};
                    chamber.chamberType = Member(entry, "type") ? String(*Member(entry, "type"), "chamber.type") : "Large";
                    info.chambers.push_back(std::move(chamber));
                }
            }
            if (const auto* conduits = Member(*production, "conduits")) {
                if (conduits->type != Value::Type::Array) throw std::runtime_error("conduits must be an array");
                for (const auto& entry : conduits->array) {
                    if (entry.type != Value::Type::Object) throw std::runtime_error("conduit must be an object");
                    const auto* a = Member(entry, "a");
                    const auto* b = Member(entry, "b");
                    if (!a || !b || a->type != Value::Type::Array || b->type != Value::Type::Array ||
                        a->array.size() != 2 || b->array.size() != 2)
                        throw std::runtime_error("conduit requires a and b [q,r]");
                    ProductionConduit conduit;
                    conduit.positionA = {static_cast<int>(Integer(a->array[0], "conduit.a q")),
                                         static_cast<int>(Integer(a->array[1], "conduit.a r"))};
                    conduit.positionB = {static_cast<int>(Integer(b->array[0], "conduit.b q")),
                                         static_cast<int>(Integer(b->array[1], "conduit.b r"))};
                    if (const auto* path = Member(entry, "path")) conduit.path = ReadPath(*path);
                    info.conduits.push_back(std::move(conduit));
                }
            }
            if (const auto* vials = Member(*production, "vials")) {
                if (vials->type != Value::Type::Array) throw std::runtime_error("vials must be an array");
                for (const auto& entry : vials->array) {
                    if (entry.type != Value::Type::Object) throw std::runtime_error("vial must be an object");
                    ProductionVial vial;
                    vial.position = {
                        static_cast<int>(Integer(Required(entry, "q", "vial.q"), "vial.q")),
                        static_cast<int>(Integer(Required(entry, "r", "vial.r"), "vial.r"))};
                    vial.top = Boolean(*Member(entry, "top"), false);
                    vial.count = static_cast<int>(Integer(*Member(entry, "count"), "vial.count"));
                    info.vials.push_back(vial);
                }
            }
            puzzle.productionInfo = std::move(info);
        }
        return puzzle;
    } catch (const std::exception& exception) {
        error = exception.what();
        return std::nullopt;
    }
}
} // namespace opusoptima::wasm
