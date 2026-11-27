// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/sha256.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/script/lsl_runtime.hpp"
#include <bit>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <stdexcept>

namespace utf16_test {
using namespace opensim;
using namespace opensim::script;

inline void check(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

inline std::string read(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    check(bool(input), "Missing original UTF16 proof: " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

inline std::string global_declaration_source(std::string source) {
    unsigned depth = 0;
    char quote = 0;
    bool escaped = false;
    for (char &c : source) {
        const char original = c;
        if (quote) {
            c = ' ';
            if (escaped)
                escaped = false;
            else if (original == '\\')
                escaped = true;
            else if (original == quote)
                quote = 0;
        } else if (original == '"' || original == '\'') {
            quote = original;
            c = ' ';
        } else if (original == '{') {
            ++depth;
            c = ' ';
        } else if (original == '}') {
            --depth;
            c = ' ';
        } else if (depth)
            c = ' ';
    }
    return source;
}

inline std::vector<std::string> global_names(std::string source) {
    source = global_declaration_source(std::move(source));
    std::vector<std::string> names;
    const std::regex declaration(R"(\b(?:char|integer|float|string|key|list)\s+([A-Za-z_][A-Za-z_0-9]*)\s*(?:=|;))");
    for (auto it = std::sregex_iterator(source.begin(), source.end(), declaration); it != std::sregex_iterator(); ++it)
        names.push_back((*it)[1].str());
    return names;
}

inline std::set<std::string> declared_string_globals(std::string source) {
    source = global_declaration_source(std::move(source));
    const std::regex declaration(R"(\b(?:string|key)\s+([A-Za-z_][A-Za-z_0-9]*)\s*(?:=|;))");
    std::set<std::string> names;
    for (auto it = std::sregex_iterator(source.begin(), source.end(), declaration); it != std::sregex_iterator(); ++it)
        names.insert((*it)[1].str());
    return names;
}

inline void compare_value(const LslValue &actual, const network::Value &expected, const std::string &where,
                          bool raw_string_context = false) {
    const auto type = expected.get("type").text;
    if (type == "null") {
        check(raw_string_context,
              where + ": untyped original null lacks a declared String/Key context; object null remains unsupported");
        check(std::holds_alternative<LslNullString>(actual) && !lsl_string_code_units(actual),
              where + ": original raw CLR String null identity differs");
    } else if (type == "integer") {
        const auto *integer = std::get_if<std::int32_t>(&actual);
        if (expected.get("clr").text.ends_with("+LSLInteger")) {
            const auto *boxed = std::get_if<LslBoxedInteger>(&actual);
            check(boxed != nullptr, where + ": original LSLInteger boxing differs");
            integer = &boxed->value;
        }
        check(integer && *integer == network::osd_integer(expected.get("value")),
              where + ": original UTF16 length/index result differs");
    } else if (type == "float") {
        const auto clr = expected.get("clr").text;
        const double *number = std::get_if<double>(&actual);
        if (clr.ends_with("+LSLFloat")) {
            const auto *boxed = std::get_if<LslBoxedFloat>(&actual);
            check(boxed != nullptr, where + ": original LSLFloat boxing differs");
            number = &boxed->value;
        }
        check(number && std::bit_cast<std::uint64_t>(*number) ==
                            std::bit_cast<std::uint64_t>(network::osd_real(expected.get("value"))),
              where + ": original Float bits differ");
    } else if (type == "vector" || type == "rotation") {
        std::vector<double> components;
        if (type == "vector") {
            const auto *value = std::get_if<framework::Vector3d>(&actual);
            check(value != nullptr, where + ": Vector differs");
            components = {value->x, value->y, value->z};
        } else {
            const auto *value = std::get_if<LslRotation>(&actual);
            check(value != nullptr, where + ": Rotation differs");
            components = {value->x, value->y, value->z, value->w};
        }
        const auto &expected_components = expected.get("value").array;
        check(components.size() == expected_components.size(), where + ": component count differs");
        for (std::size_t i = 0; i < components.size(); ++i)
            check(std::bit_cast<std::uint64_t>(components[i]) ==
                      std::bit_cast<std::uint64_t>(network::osd_real(expected_components[i])),
                  where + ": component bits differ");
    } else if (type == "string") {
        const auto clr = expected.get("clr").text;
        const bool boxed = clr == "OpenSim.Region.ScriptEngine.Shared.LSL_Types+LSLString";
        check(boxed || clr == "System.String", where + ": Unreviewed original string identity");
        const auto &captured_units = expected.get("utf16");
        if (captured_units.kind != network::Value::Kind::Array) {
            check(boxed && (expected.get("value").kind == network::Value::Kind::Undefined ||
                            expected.get("value").kind == network::Value::Kind::NullReference),
                  where + ": malformed original nullable string wrapper");
            check(std::holds_alternative<LslBoxedNullString>(actual) && !lsl_string_code_units(actual),
                  where + ": SDK LSLString wrapper lost its null inner value");
            return;
        }
        check(boxed ? (std::holds_alternative<LslBoxedString>(actual) ||
                       std::holds_alternative<LslBoxedUtf16String>(actual))
                    : (std::holds_alternative<std::string>(actual) || std::holds_alternative<LslUtf16String>(actual)),
              where + ": raw/boxed CLR string identity differs");
        const auto units = lsl_string_code_units(actual);
        check(units.has_value(), where + ": native value is not a string");
        const auto &original_units = expected.get("utf16");
        check(original_units.kind == network::Value::Kind::Array && units->size() == original_units.array.size(),
              where + ": original CLR string code-unit count differs");
        for (std::size_t j = 0; j < units->size(); ++j) {
            const auto unit = network::osd_integer(original_units.array[j]);
            check(unit >= 0 && unit <= 65535 && std::uint32_t((*units)[j]) == std::uint32_t(unit),
                  where + ": CLR UTF16 code unit " + std::to_string(j) + " differs");
        }
        // JSON text can contain U+FFFD for an actual lone surrogate. Only
        // captured CLR units are expected values; display text is unused.
    } else if (type == "list") {
        const auto *list = std::get_if<std::shared_ptr<const LslList>>(&actual);
        const auto &items = expected.get("value").array;
        check(list && *list && (*list)->items.size() == items.size(), where + ": list length differs");
        for (std::size_t i = 0; i < items.size(); ++i)
            compare_value((*list)->items[i], items[i], where + "[" + std::to_string(i) + "]");
    } else
        throw std::runtime_error(where + ": Unreviewed UTF16 original value type " + type);
}

inline void compare_globals(const LslCheckpoint &checkpoint, const std::vector<std::string> &names,
                            const network::Value &original, const std::set<std::string> &string_contexts = {}) {
    check(names.size() == checkpoint.globals.size() && names.size() == original.map.size(),
          "UTF16 original/global declaration count differs");
    for (std::size_t i = 0; i < names.size(); ++i)
        compare_value(checkpoint.globals[i], original.get(names[i]), names[i], string_contexts.contains(names[i]));
}

inline unsigned string_identity(const LslValue &value) {
    if (std::holds_alternative<LslNullString>(value))
        return 4;
    if (std::holds_alternative<LslBoxedNullString>(value))
        return 5;
    if (std::holds_alternative<LslBoxedNullKey>(value))
        return 6;
    if (std::holds_alternative<std::string>(value) || std::holds_alternative<LslUtf16String>(value))
        return 1;
    if (std::holds_alternative<LslBoxedString>(value) || std::holds_alternative<LslBoxedUtf16String>(value))
        return 2;
    if (std::holds_alternative<LslBoxedKey>(value) || std::holds_alternative<LslBoxedUtf16Key>(value))
        return 3;
    return 0;
}

inline void compare_checkpoint_strings(const LslValue &before, const LslValue &after) {
    check(lsl_string_code_units(before) == lsl_string_code_units(after),
          "Checkpoint codec changed a string's exact UTF16 units");
    check(string_identity(before) == string_identity(after), "Checkpoint codec changed raw/string/key boxing");
    if (const auto *list = std::get_if<std::shared_ptr<const LslList>>(&before)) {
        const auto *restored = std::get_if<std::shared_ptr<const LslList>>(&after);
        check(list && *list && restored && *restored && (*list)->items.size() == (*restored)->items.size(),
              "Checkpoint codec changed list structure");
        for (std::size_t i = 0; i < (*list)->items.size(); ++i)
            compare_checkpoint_strings((*list)->items[i], (*restored)->items[i]);
    }
}

} // namespace utf16_test
