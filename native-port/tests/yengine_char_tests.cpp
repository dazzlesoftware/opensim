// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/sha256.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/script/lsl_runtime.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <regex>
#include <stdexcept>

using namespace opensim;
using namespace script;

namespace {
void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}

std::string read(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    check(bool(stream), "Missing original character evidence: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::vector<std::string> globals(std::string source) {
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
    std::vector<std::string> names;
    const std::regex declaration(R"(\b(?:char|integer|float|string|key|list)\s+([A-Za-z_][A-Za-z_0-9]*)\s*(?:=|;))");
    for (auto it = std::sregex_iterator(source.begin(), source.end(), declaration); it != std::sregex_iterator(); ++it)
        names.push_back((*it)[1].str());
    return names;
}

void compare(const LslValue &actual, const network::Value &expected, const std::string &where) {
    const auto type = expected.get("type").text;
    const auto clr = expected.get("clr").text;
    if (type == "char") {
        const auto *value = std::get_if<LslCharacter>(&actual);
        check(value && std::uint32_t(value->value) == network::osd_integer(expected.get("utf16")),
              where + ": CLR UTF16 character unit differs");
    } else if (type == "integer") {
        const auto *value = std::get_if<std::int32_t>(&actual);
        check(value && *value == network::osd_integer(expected.get("value")), where + ": Int32 differs");
    } else if (type == "other" && clr == "System.String") {
        const auto *value = std::get_if<std::string>(&actual);
        check(value && *value == expected.get("text").text, where + ": String differs");
    } else if (type == "other" && clr.ends_with("+LSLInteger")) {
        const auto *value = std::get_if<LslBoxedInteger>(&actual);
        check(value && std::to_string(value->value) == expected.get("text").text,
              where + ": LSLInteger boxing differs");
    } else if (type == "list") {
        const auto *value = std::get_if<std::shared_ptr<const LslList>>(&actual);
        const auto &entries = expected.get("entries").array;
        check(value && *value && (*value)->items.size() == entries.size(), where + ": List length differs");
        for (std::size_t i = 0; i < entries.size(); ++i)
            compare((*value)->items[i], entries[i], where + "[" + std::to_string(i) + "]");
    } else if (type == "other" && clr.ends_with("+list")) {
        // First oracle captured original list.ToString, not its element graph.
        // The separate expression corpus additionally proves exact CLR boxing.
        const auto *value = std::get_if<std::shared_ptr<const LslList>>(&actual);
        check(value && *value, where + ": List storage differs");
        std::string text;
        for (const auto &item : (*value)->items) {
            const auto *integer = std::get_if<LslBoxedInteger>(&item);
            check(integer != nullptr, where + ": Unreviewed list item in original text-only reference");
            text += std::to_string(integer->value);
        }
        check(text == expected.get("text").text, where + ": Original list text differs");
    } else
        throw std::runtime_error(where + ": Unreviewed original character value " + type + "/" + clr);
}

void compare_globals(const LslCheckpoint &checkpoint, const std::string &source, const network::Value &expected) {
    const auto names = globals(source);
    check(names.size() == checkpoint.globals.size() && names.size() == expected.map.size(),
          "Character global count differs");
    for (std::size_t i = 0; i < names.size(); ++i)
        compare(checkpoint.globals[i], expected.get(names[i]), names[i]);
}

void fixture(const std::filesystem::path &root, const network::Value &oracle) {
    const auto path = root / oracle.get("name").text;
    const auto source = read(path / "source.lsl");
    check(source == oracle.get("source").text, "Original source record differs");
    check(framework::sha256_hex(source) == oracle.get("sha256").get("source.lsl").text, "Original source SHA differs");
    const auto token_hash = read(path / "source_hash.txt");
    if (!token_hash.empty())
        check(yengine_source_matches(source, token_hash), "Original source-token hash differs");
    const auto compiled = compile_lsl(source);
    if (!oracle.get("accepted").boolean) {
        check(!compiled, "Original rejected character source unexpectedly accepted");
        return;
    }
    check(framework::sha256_hex(read(path / "original.yobj")) == oracle.get("sha256").get("original.yobj").text,
          "Original object provenance differs");
    check(framework::sha256_hex(read(path / "original.il.txt")) == oracle.get("sha256").get("original.il.txt").text,
          "Original IL provenance differs");
    std::string errors;
    for (const auto &error : compiled.diagnostics)
        errors += " " + error.message;
    check(bool(compiled), "Accepted original character source failed native compile:" + errors);
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [] { return std::int64_t{638000000000000000LL}; };
    auto instance = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
    auto saved = instance->checkpoint();
    check(saved.has_value(), "Initial character checkpoint unavailable");
    const bool expected_fault = oracle.get("exceptionType").kind == network::Value::Kind::String;
    for (unsigned step = 0; step < 2000; ++step) {
        const auto outcome = instance->run(1);
        if (outcome.status == LslRunStatus::Faulted) {
            check(expected_fault, "Character execution unexpectedly faulted: " + outcome.error);
            check(oracle.get("exception").text == "Unknown type in List.Size: System.Char" &&
                      outcome.error.find("Unknown type in List.Size: System.Char") != std::string::npos,
                  "Original char-list runtime exception differs: " + outcome.error);
            compare_globals(*saved, source, oracle.get("globals"));
            return;
        }
        check(outcome.status != LslRunStatus::Sleeping, "Pure character source unexpectedly slept");
        saved = instance->checkpoint();
        check(saved.has_value(), "Character checkpoint unavailable");
        const auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(*saved));
        check(decoded.has_value(), "Character checkpoint codec rejected source state");
        auto next = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
        check(next->restore_checkpoint(*decoded), "Fresh VM rejected character state");
        instance = std::move(next);
        if (outcome.status == LslRunStatus::Idle) {
            check(!expected_fault, "Original char-list runtime fault incorrectly completed");
            compare_globals(*saved, source, oracle.get("globals"));
            check(instance->run(1).status == LslRunStatus::Idle, "Completed character entry replayed");
            compare_globals(*instance->checkpoint(), source, oracle.get("globals"));
            return;
        }
    }
    throw std::runtime_error("Character source exceeded bounded execution");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 4, "Original character baseline/expression/dynamic fixture directories required");
        const std::size_t counts[] = {56, 32, 15};
        std::size_t accepted = 0, rejected = 0, faults = 0;
        std::string failures;
        for (int group = 0; group < 3; ++group) {
            const std::filesystem::path root = argv[group + 1];
            const auto cases = network::parse_osd_json(read(root / "metadata.json"));
            check(cases.array.size() == counts[group], "Original character corpus inventory changed");
            for (const auto &oracle : cases.array) {
                try {
                    fixture(root, oracle);
                } catch (const std::exception &error) {
                    failures += root.filename().string() + "/" + oracle.get("name").text + ": " + error.what() + "\n";
                }
                if (oracle.get("accepted").boolean) {
                    ++accepted;
                    if (oracle.get("exceptionType").kind == network::Value::Kind::String)
                        ++faults;
                } else
                    ++rejected;
            }
        }
        check(accepted == 76 && rejected == 27 && faults == 2, "Original character outcome coverage changed");
        check(failures.empty(), failures);
        std::cout
            << "103 original character sources passed (74 successful, 2 runtime faults, 27 compile rejections).\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
