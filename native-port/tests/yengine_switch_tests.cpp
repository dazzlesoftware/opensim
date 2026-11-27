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
    check(bool(stream), "Missing original switch evidence: " + path.string());
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
              where + ": CLR UTF16 switch unit differs");
    } else if (type == "integer") {
        const auto *value = std::get_if<std::int32_t>(&actual);
        check(value && *value == network::osd_integer(expected.get("value")), where + ": Int32 differs");
    } else if (type == "other" && clr == "System.String") {
        const auto *value = std::get_if<std::string>(&actual);
        check(value &&
                  *value == (expected.get("text").kind == network::Value::Kind::String ? expected.get("text").text
                                                                                       : expected.get("value").text),
              where + ": String differs");
    } else
        throw std::runtime_error(where + ": Unreviewed original switch value " + type + "/" + clr);
}

void compare_globals(const LslCheckpoint &checkpoint, const std::string &source, const network::Value &expected) {
    const auto names = globals(source);
    check(names.size() == checkpoint.globals.size() && names.size() == expected.map.size(),
          "Switch global count differs");
    for (std::size_t i = 0; i < names.size(); ++i)
        compare(checkpoint.globals[i], expected.get(names[i]), names[i]);
}

void fixture(const std::filesystem::path &root, const network::Value &oracle, bool sleeping) {
    const auto folder = root / oracle.get("name").text;
    const auto source = read(folder / "source.lsl");
    check(framework::sha256_hex(source) == oracle.get("sha256").get("source.lsl").text,
          "Original switch source SHA differs");
    const auto token_hash = sleeping ? oracle.get("source_hash").text : read(folder / "source_hash.txt");
    if (!token_hash.empty())
        check(yengine_source_matches(source, token_hash), "Original switch token hash differs");
    const auto compiled = compile_lsl(source);
    if (!oracle.get("accepted").boolean) {
        check(!compiled, "Original rejected switch source unexpectedly accepted");
        return;
    }
    for (const auto *name : {"original.yobj", "original.il.txt"})
        check(framework::sha256_hex(read(folder / name)) == oracle.get("sha256").get(name).text,
              std::string("Original switch artifact provenance differs: ") + name);
    std::string diagnostics;
    for (const auto &error : compiled.diagnostics)
        diagnostics += " " + error.message;
    check(bool(compiled), "Accepted original switch source failed native compile:" + diagnostics);
    if (sleeping)
        check(oracle.get("suspended").boolean && oracle.get("migrated").boolean &&
                  oracle.get("initial_exception").kind != network::Value::Kind::Map &&
                  oracle.get("resume_exception").kind != network::Value::Kind::Map,
              "Original switch sleep/migration proof did not succeed");
    else
        check(oracle.get("exceptionType").kind != network::Value::Kind::String,
              "Original pure switch proof unexpectedly faulted");
    std::int64_t clock = 638000000000000000LL;
    unsigned sleep_requests = 0;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](int) { ++sleep_requests; };
    auto instance = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
    bool slept = false;
    for (unsigned step = 0; step < 3000; ++step) {
        const auto result = instance->run(1);
        check(result.status != LslRunStatus::Faulted, "Switch source faulted: " + result.error);
        const auto current = instance->checkpoint();
        check(current.has_value(), "Switch checkpoint unavailable");
        const auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(*current));
        check(decoded.has_value(), "Switch checkpoint codec rejected source state");
        auto fresh = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
        check(fresh->restore_checkpoint(*decoded), "Fresh VM rejected switch state");
        instance = std::move(fresh);
        if (result.status == LslRunStatus::Sleeping) {
            check(sleeping && !slept, "Switch requested an unexpected/replayed sleep");
            slept = true;
            compare_globals(*current, source, oracle.get("initial").get("globals"));
            clock += current->sleep_remaining + 1;
        }
        if (result.status == LslRunStatus::Idle) {
            compare_globals(*current, source, sleeping ? oracle.get("final").get("globals") : oracle.get("globals"));
            check(slept == sleeping && sleep_requests == (sleeping ? 1u : 0u),
                  "Switch sleep entry point/replay count differs");
            check(instance->run(1).status == LslRunStatus::Idle, "Completed switch entry replayed");
            compare_globals(*instance->checkpoint(), source,
                            sleeping ? oracle.get("final").get("globals") : oracle.get("globals"));
            return;
        }
    }
    throw std::runtime_error("Switch source exceeded bounded execution");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 4, "Original switch pure/sleep/boolean fixture directories required");
        const std::size_t counts[] = {36, 2, 4};
        unsigned accepted = 0, rejected = 0;
        for (int group = 0; group < 3; ++group) {
            const std::filesystem::path root = argv[group + 1];
            const auto cases = network::parse_osd_json(read(root / "metadata.json"));
            check(cases.array.size() == counts[group], "Original switch case inventory changed");
            for (const auto &oracle : cases.array) {
                try {
                    fixture(root, oracle, group == 1);
                } catch (const std::exception &error) {
                    throw std::runtime_error(root.filename().string() + "/" + oracle.get("name").text + ": " +
                                             error.what());
                }
                if (oracle.get("accepted").boolean)
                    ++accepted;
                else
                    ++rejected;
            }
        }
        check(accepted == 25 && rejected == 17, "Original switch acceptance inventory changed");
        std::cout
            << "42 original switch sources passed (25 executed including two real sleep references; 17 rejected).\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
