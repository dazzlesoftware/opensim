// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/network/osd_json.hpp"
#include <algorithm>
#include <map>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
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
    check(bool(input), "Missing original numeric-format fixture: " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void compare_value(const LslValue &actual, const network::Value &expected, const std::string &where) {
    const auto type = expected.get("type").text;
    if (type == "string") {
        const auto *value = std::get_if<std::string>(&actual);
        check(value != nullptr, where + ": plain original String storage changed");
        check(*value == expected.get("value").text,
              where + ": exact formatted UTF8 differs; actual=" + *value + " original=" + expected.get("value").text);
    } else if (type == "integer") {
        const auto *value = std::get_if<std::int32_t>(&actual);
        check(value && *value == network::osd_integer(expected.get("value")),
              where + ": original Int32/counter differs");
    } else
        throw std::runtime_error(where + ": unreviewed original formatter global type " + type);
}

std::vector<std::string> globals(std::string source) {
    // Ignore function/event bodies and quoted text rather than treating a
    // function's local marker as an additional global declaration.
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (char &ch : source) {
        const char original = ch;
        if (quoted) {
            ch = ' ';
            if (escaped)
                escaped = false;
            else if (original == '\\')
                escaped = true;
            else if (original == '"')
                quoted = false;
            continue;
        }
        if (original == '"') {
            quoted = true;
            ch = ' ';
        } else if (original == '{') {
            ++depth;
            ch = ' ';
        } else if (original == '}') {
            --depth;
            ch = ' ';
        } else if (depth)
            ch = ' ';
    }
    const std::regex declaration(
        R"(\b(?:integer|float|string|key|vector|rotation|list)\s+([A-Za-z_][A-Za-z_0-9]*)\s*(?:=|;))");
    std::vector<std::string> result;
    for (auto it = std::sregex_iterator(source.begin(), source.end(), declaration); it != std::sregex_iterator(); ++it)
        result.push_back((*it)[1].str());
    return result;
}

void compare_globals(const LslCheckpoint &checkpoint, const std::string &source, const network::Value &expected) {
    const auto names = globals(source);
    check(names.size() == checkpoint.globals.size() && names.size() == expected.map.size(),
          "Original global declaration count differs");
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto found = expected.map.find(names[i]);
        check(found != expected.map.end(), "Original global omitted " + names[i]);
        compare_value(checkpoint.globals[i], found->second, names[i]);
    }
}

void check_fault(const LslRunResult &result, const network::Value &original) {
    check(result.status == LslRunStatus::Faulted, "Original numeric formatting failed but native execution completed");
    const auto type = original.get("type").text;
    check(type == "System.FormatException", "Unreviewed original conversion exception " + type);
    // The formatting API must expose which original exception occurred. A generic
    // cast fault cannot prove Format and Overflow parity across platforms.
    const auto category = type.substr(type.find_last_of('.') + 1);
    check(result.error.starts_with(category + ": "), "Original " + category + " category differs: " + result.error);
}

void fixture(const std::filesystem::path &root, const network::Value &oracle) {
    const auto name = oracle.get("name").text;
    const auto source = read(root / name / "source.lsl");
    check(oracle.get("accepted").boolean, "Original numeric-format source unexpectedly rejected");
    check(yengine_source_matches(source, oracle.get("source_hash").text), "Numeric-format source provenance mismatch");
    const auto compiled = compile_lsl(source);
    std::string errors;
    for (const auto &error : compiled.diagnostics)
        errors += " (" + std::to_string(error.line) + "," + std::to_string(error.column) + ") " + error.message;
    check(bool(compiled), "Accepted original numeric-format source failed native compile:" + errors);
    const auto &exception = oracle.get("direct_exception");
    const auto &source_exception = oracle.get("initial_exception");
    check((exception.kind == network::Value::Kind::Map) == (source_exception.kind == network::Value::Kind::Map),
          "Direct/generated original outcome mismatch");
    if (exception.kind == network::Value::Kind::Map)
        check(exception.get("type").text == source_exception.get("type").text,
              "Direct/generated original exception mismatch");
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](int) { ++sleeps; };
    LslContext context;
    context.object = framework::Uuid::random();
    context.item = framework::Uuid::random();
    LslInstance instance(compiled.program, context, host);
    auto saved = instance.checkpoint();
    check(saved.has_value(), "Initial numeric-format checkpoint unavailable");
    LslRunResult outcome;
    bool stopped = false;
    for (unsigned step = 0; step < 1000; ++step) {
        outcome = instance.run(1);
        if (auto checkpoint = instance.checkpoint())
            saved = *checkpoint;
        if (outcome.status == LslRunStatus::BudgetExhausted) {
            const auto wire = encode_lsl_checkpoint(*saved);
            const auto decoded = decode_lsl_checkpoint(wire);
            LslInstance fresh(compiled.program, context, host);
            check(decoded && fresh.restore_checkpoint(*decoded),
                  "Internal formatting continuation failed fresh-VM checkpoint restore");
            instance = std::move(fresh);
        }
        if (outcome.status == LslRunStatus::Faulted || outcome.status == LslRunStatus::Idle) {
            stopped = true;
            break;
        }
        check(outcome.status != LslRunStatus::Sleeping, "Pure numeric formatting unexpectedly suspended");
    }
    check(stopped, "Numeric formatting exceeded bounded source execution");
    check(sleeps == 0, "Numeric formatting requested API sleep");
    if (exception.kind == network::Value::Kind::Map)
        check_fault(outcome, exception);
    else
        check(outcome.status == LslRunStatus::Idle, "Original successful conversion faulted: " + outcome.error);
    // A fault has no exportable checkpoint: the last successful instruction
    // boundary proves original pre-fault globals without inventing a result.
    compare_globals(*saved, source, oracle.get("final").get("globals"));
    const auto wire = encode_lsl_checkpoint(*saved);
    const auto decoded = decode_lsl_checkpoint(wire);
    LslInstance restored(compiled.program, context, host);
    check(decoded && restored.restore_checkpoint(*decoded), "Final formatter checkpoint failed fresh-VM restore");
    const auto restored_checkpoint = restored.checkpoint();
    check(restored_checkpoint.has_value(), "Restored formatter checkpoint unavailable");
    compare_globals(*restored_checkpoint, source, oracle.get("final").get("globals"));
    if (exception.kind != network::Value::Kind::Map && oracle.get("kind").text != "Source") {
        const auto names = globals(source);
        const auto found = std::find(names.begin(), names.end(), "result");
        check(found != names.end(), "Original successful result variable missing");
        compare_value(saved->globals[static_cast<std::size_t>(found - names.begin())], oracle.get("direct_result"),
                      "actual original method return");
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2 || argc == 4, "Expected baseline and optional extended/zero-section fixture directories");
        const std::filesystem::path root = argv[1];
        const auto cases = network::parse_osd_json(read(root / "metadata.json"));
        check(cases.array.size() == 489, "Original formatter case inventory changed");
        std::map<std::string, unsigned> kinds;
        unsigned successes = 0, failures = 0;
        for (const auto &oracle : cases.array) {
            const auto name = oracle.get("name").text;
            check(oracle.get("production_culture").text.empty(), "Original formatter culture is not invariant");
            try {
                fixture(root, oracle);
            } catch (const std::exception &error) {
                throw std::runtime_error(name + ": " + error.what());
            }
            ++kinds[oracle.get("kind").text];
            if (oracle.get("direct_exception").kind == network::Value::Kind::Map)
                ++failures;
            else
                ++successes;
        }
        check(kinds["Integer"] == 141 && kinds["Float"] == 287 && kinds["Vector"] == 30 && kinds["Rotation"] == 30 &&
                  kinds["Source"] == 1,
              "Original numeric formatting method scope changed");
        check(successes == 472 && failures == 17, "Original formatting return/failure coverage changed");
        unsigned extended = 0;
        for (int argument = 2; argument < argc; ++argument) {
            const std::filesystem::path extra_root = argv[argument];
            const auto extra = network::parse_osd_json(read(extra_root / "metadata.json"));
            check(extra.array.size() == (argument == 2 ? 15 : 5), "Original formatter extension inventory changed");
            for (const auto &oracle : extra.array) {
                check(oracle.get("production_culture").text.empty(), "Extended formatter culture is not invariant");
                check(oracle.get("direct_exception").kind != network::Value::Kind::Map &&
                          oracle.get("kind").text != "Source",
                      "Extended formatter outcome scope changed");
                try {
                    fixture(extra_root, oracle);
                } catch (const std::exception &error) {
                    throw std::runtime_error(extra_root.filename().string() + "/" + oracle.get("name").text + ": " +
                                             error.what());
                }
                ++extended;
            }
        }
        check(extended == (argc == 4 ? 20 : 0), "Original formatter extension coverage changed");
        std::cout << "PASS: 489 original formatter sources (471 helper returns, 17 FormatException outcomes, one "
                     "argument-order case), "
                  << extended
                  << " additional original edge cases, exact strings and "
                     "instruction-budget/final checkpoint restore\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
