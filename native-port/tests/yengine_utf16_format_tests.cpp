// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/sha256.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/script/lsl_runtime.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <regex>
#include <set>
#include <stdexcept>

using namespace opensim;
using namespace script;

namespace {
void check(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::string read(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    check(bool(input), "Missing original UTF16 proof: " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::vector<std::string> global_names(const std::string &source) {
    // These actual sources have scalar/list globals; the one helper has no
    // local declarations. Parameters are excluded by the declaration suffix.
    const auto prefix = source.substr(0, source.find("default"));
    const std::regex declaration(R"(\b(?:char|integer|string|list)\s+([A-Za-z_][A-Za-z_0-9]*)\s*(?:=|;))");
    std::vector<std::string> names;
    for (auto it = std::sregex_iterator(prefix.begin(), prefix.end(), declaration); it != std::sregex_iterator(); ++it)
        names.push_back((*it)[1].str());
    return names;
}

void compare_value(const LslValue &actual, const network::Value &expected, const std::string &where) {
    const auto type = expected.get("type").text;
    if (type == "char") {
        const auto *character = std::get_if<LslCharacter>(&actual);
        check(character && std::uint32_t(character->value) == network::osd_integer(expected.get("utf16")),
              where + ": original format character unit differs");
    } else if (type == "integer") {
        const auto *integer = std::get_if<std::int32_t>(&actual);
        check(integer && *integer == network::osd_integer(expected.get("value")),
              where + ": original UTF16 length/index result differs");
    } else if (type == "string") {
        const auto clr = expected.get("clr").text;
        const bool boxed = clr == "OpenSim.Region.ScriptEngine.Shared.LSL_Types+LSLString";
        check(boxed || clr == "System.String", where + ": Unreviewed original string identity");
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

void compare_globals(const LslCheckpoint &checkpoint, const std::vector<std::string> &names,
                     const network::Value &original) {
    check(names.size() == checkpoint.globals.size() && names.size() == original.map.size(),
          "UTF16 original/global declaration count differs");
    for (std::size_t i = 0; i < names.size(); ++i)
        compare_value(checkpoint.globals[i], original.get(names[i]), names[i]);
}

unsigned string_identity(const LslValue &value) {
    if (std::holds_alternative<std::string>(value) || std::holds_alternative<LslUtf16String>(value))
        return 1;
    if (std::holds_alternative<LslBoxedString>(value) || std::holds_alternative<LslBoxedUtf16String>(value))
        return 2;
    if (std::holds_alternative<LslBoxedKey>(value) || std::holds_alternative<LslBoxedUtf16Key>(value))
        return 3;
    return 0;
}

void compare_checkpoint_strings(const LslValue &before, const LslValue &after) {
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

void fixture(const std::filesystem::path &root, const network::Value &oracle) {
    const auto folder = root / oracle.get("name").text;
    const auto source = read(folder / "source.lsl");
    check(oracle.get("accepted").boolean && oracle.get("initial_exception").kind != network::Value::Kind::Map,
          "Original UTF16 proof did not execute successfully");
    check(framework::sha256_hex(source) == oracle.get("sha256").get("source.lsl").text &&
              yengine_source_matches(source, oracle.get("source_hash").text),
          "Actual UTF16 source provenance differs");
    for (const auto *name : {"original.yobj", "original.il.txt"})
        check(framework::sha256_hex(read(folder / name)) == oracle.get("sha256").get(name).text,
              std::string("Original UTF16 compiler artifact differs: ") + name);
    const auto compiled = compile_lsl(source);
    std::string errors;
    for (const auto &error : compiled.diagnostics)
        errors += " " + error.message;
    check(bool(compiled), "Actual original UTF16 source failed native compile:" + errors);
    const auto names = global_names(source);
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [] { return std::int64_t{638000000000000000LL}; };
    auto instance = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
    for (unsigned step = 0; step < 2000; ++step) {
        const auto result = instance->run(1);
        check(result.status != LslRunStatus::Faulted, "UTF16 source faulted: " + result.error);
        check(result.status != LslRunStatus::Sleeping, "Pure UTF16 source unexpectedly slept");
        const auto current = instance->checkpoint();
        check(current.has_value(), "UTF16 checkpoint unavailable");
        const auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(*current));
        check(decoded.has_value(), "UTF16 checkpoint codec rejected source state");
        check(decoded->globals.size() == current->globals.size(), "UTF16 checkpoint global count changed");
        for (std::size_t i = 0; i < current->globals.size(); ++i)
            compare_checkpoint_strings(current->globals[i], decoded->globals[i]);
        auto fresh = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
        check(fresh->restore_checkpoint(*decoded), "Fresh VM rejected UTF16 checkpoint");
        instance = std::move(fresh);
        if (result.status == LslRunStatus::Idle) {
            compare_globals(*current, names, oracle.get("final").get("globals"));
            check(instance->run(1).status == LslRunStatus::Idle, "Completed UTF16 source replayed entry");
            compare_globals(*instance->checkpoint(), names, oracle.get("final").get("globals"));
            return;
        }
    }
    throw std::runtime_error("UTF16 proof exceeded bounded execution");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Original UTF16 numeric-format fixture directory required");
        const std::filesystem::path root = argv[1];
        const auto cases = network::parse_osd_json(read(root / "metadata.json"));
        check(cases.array.size() == 31, "Original UTF16 format inventory changed");
        std::vector<std::string> failures;
        for (const auto &oracle : cases.array) {
            try {
                fixture(root, oracle);
            } catch (const std::exception &error) {
                failures.push_back(oracle.get("name").text + ": " + error.what());
            }
        }
        if (!failures.empty()) {
            for (const auto &failure : failures)
                std::cerr << failure << '\n';
            throw std::runtime_error(std::to_string(failures.size()) + " of 31 original UTF16 format cases failed");
        }
        std::cout << "31 actual original UTF16 format sources passed exact units and checkpoint checks.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
