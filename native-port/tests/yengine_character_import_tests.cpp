// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/sha256.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/script/yengine_object_code.hpp"
#include <span>
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
    } else if ((type == "other" || type == "string") && clr == "System.String") {
        const auto *value = std::get_if<std::string>(&actual);
        check(value && *value == (type == "string" ? expected.get("value").text : expected.get("text").text),
              where + ": String differs");
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
          "Character import global count differs");
    for (std::size_t i = 0; i < names.size(); ++i)
        compare(checkpoint.globals[i], expected.get(names[i]), names[i]);
}

std::span<const std::uint8_t> bytes(const std::string &data) {
    return {reinterpret_cast<const std::uint8_t *>(data.data()), data.size()};
}

void fixture(const std::filesystem::path &folder, bool character) {
    const auto oracle = network::parse_osd_json(read(folder / "metadata.json"));
    const auto source = read(folder / "source.lsl");
    check(oracle.get("accepted").boolean && oracle.get("suspended").boolean && oracle.get("migrated").boolean &&
              oracle.get("initial_exception").kind != network::Value::Kind::Map &&
              oracle.get("resume_exception").kind != network::Value::Kind::Map,
          "Actual original capture/migration did not succeed");
    for (const auto *name : {"source.lsl", "snapshot.bin", "original.yobj", "original.il.txt"})
        check(framework::sha256_hex(read(folder / name)) == oracle.get("sha256").get(name).text,
              std::string("Original import provenance differs: ") + name);
    check(yengine_source_matches(source, oracle.get("source_hash").text), "Original import source hash differs");
    check(oracle.get("initial").get("queue").array.empty(), "Fixture has external queue data needing import");
    const auto object_bytes = read(folder / "original.yobj");
    const auto snapshot_bytes = read(folder / "snapshot.bin");
    const auto code = decode_yengine_object_code(bytes(object_bytes));
    const auto snapshot = decode_yengine_snapshot(bytes(snapshot_bytes));
    check(code.has_value() && snapshot.has_value(), "Actual original bytes did not decode");
    const auto compiled = compile_lsl(source);
    std::string errors;
    for (const auto &error : compiled.diagnostics)
        errors += " " + error.message;
    check(bool(compiled), "Actual original import source failed native compile:" + errors);
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0, description_writes = 0;
    std::string description = oracle.get("initial").get("object_description").text;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](int) { ++sleeps; };
    host.set_object_text = [&](auto, bool is_description, const std::string &text) {
        check(is_description, "Original import unexpectedly changed object name");
        description = text;
        ++description_writes;
        return true;
    };
    LslContext context;
    context.object = framework::Uuid::random();
    context.item = framework::Uuid::random();
    context.owner = framework::Uuid::random();
    context.object_description = description;
    auto instance = std::make_unique<LslInstance>(compiled.program, context, host);
    check(instance->restore_yengine_snapshot(*snapshot, oracle.get("source_hash").text, source, *code),
          "Genuine original character/switch capture failed native managed import");
    auto checkpoint = instance->checkpoint();
    check(checkpoint.has_value(), "Imported original state has no native checkpoint");
    compare_globals(*checkpoint, source, oracle.get("after_load").get("globals"));
    for (unsigned step = 0; step < 3000; ++step) {
        const auto result = instance->run(1);
        check(result.status != LslRunStatus::Faulted, "Imported original continuation faulted: " + result.error);
        check(result.status != LslRunStatus::Sleeping, "Imported continuation replayed completed original llSleep");
        checkpoint = instance->checkpoint();
        check(checkpoint.has_value(), "Imported continuation checkpoint unavailable");
        const auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(*checkpoint));
        check(decoded.has_value(), "Imported continuation native checkpoint codec failed");
        auto fresh = std::make_unique<LslInstance>(compiled.program, context, host);
        check(fresh->restore_checkpoint(*decoded), "Fresh VM rejected imported continuation checkpoint");
        instance = std::move(fresh);
        if (result.status == LslRunStatus::Idle) {
            compare_globals(*checkpoint, source, oracle.get("final").get("globals"));
            check(sleeps == 0, "Original completed API call replayed during native managed resume");
            check(description_writes == (character ? 1u : 0u), "Original external prefix replayed");
            if (character)
                check(description == oracle.get("final").get("object_description").text,
                      "Original character final object description differs");
            check(instance->run(1).status == LslRunStatus::Idle, "Imported completed entry replayed");
            compare_globals(*instance->checkpoint(), source, oracle.get("final").get("globals"));
            return;
        }
    }
    throw std::runtime_error("Original continuation exceeded bounded execution");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 3, "Original character process and switch sleep fixture directories required");
        fixture(std::filesystem::path(argv[1]) / "process_character_restart", true);
        fixture(std::filesystem::path(argv[2]) / "sleep_integer", false);
        fixture(std::filesystem::path(argv[2]) / "sleep_string", false);
        std::cout << "Three genuine original captures imported and resumed without replay.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
