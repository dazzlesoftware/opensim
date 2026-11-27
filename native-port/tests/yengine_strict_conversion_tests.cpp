// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/network/osd_json.hpp"
#include <algorithm>
#include <bit>
#include <map>
#include <charconv>
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
    check(bool(input), "Missing original strict-conversion fixture: " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::uint64_t original_bits(const network::Value &value) {
    const auto &text = value.get("bits").text;
    check(text.size() == 16, "Original double bits missing");
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result, 16);
    check(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size(), "Malformed original double bits");
    return result;
}

void compare_double(double actual, const network::Value &expected, const std::string &where) {
    const auto bits = std::bit_cast<std::uint64_t>(actual);
    const auto oracle = original_bits(expected);
    check(bits == oracle,
          where + ": IEEE754 bits differ; actual=" + std::to_string(bits) + " original=" + std::to_string(oracle));
}

void compare_value(const LslValue &actual, const network::Value &expected, const std::string &where) {
    const auto type = expected.get("type").text;
    const auto &value = expected.get("value");
    if (type == "integer") {
        const auto *integer = std::get_if<std::int32_t>(&actual);
        check(integer && *integer == network::osd_integer(value), where + ": original Int32 value/storage differs");
    } else if (type == "float") {
        const auto *number = std::get_if<double>(&actual);
        check(number != nullptr, where + ": original Double storage differs");
        compare_double(*number, expected, where);
    } else if (type == "vector") {
        const auto *vector = std::get_if<framework::Vector3d>(&actual);
        check(vector && value.array.size() == 3, where + ": original vector type/dimensions differ");
        compare_double(vector->x, value.array[0], where + ".x");
        compare_double(vector->y, value.array[1], where + ".y");
        compare_double(vector->z, value.array[2], where + ".z");
    } else if (type == "rotation") {
        const auto *rotation = std::get_if<LslRotation>(&actual);
        check(rotation && value.array.size() == 4, where + ": original rotation type/dimensions differ");
        compare_double(rotation->x, value.array[0], where + ".x");
        compare_double(rotation->y, value.array[1], where + ".y");
        compare_double(rotation->z, value.array[2], where + ".z");
        compare_double(rotation->w, value.array[3], where + ".w");
    } else
        throw std::runtime_error(where + ": unreviewed original result type " + type);
}

std::vector<std::string> globals(const std::string &source) {
    // This corpus declares only result/after globals before default; infer
    // storage order from the actual source rather than hard-code VM slots.
    const auto prefix = source.substr(0, source.find("default"));
    const std::regex declaration(R"(\b(?:integer|float|vector|rotation)\s+([A-Za-z_][A-Za-z_0-9]*)\s*;)");
    std::vector<std::string> names;
    for (auto it = std::sregex_iterator(prefix.begin(), prefix.end(), declaration); it != std::sregex_iterator(); ++it)
        names.push_back((*it)[1].str());
    return names;
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
    check(result.status == LslRunStatus::Faulted, "Original strict conversion failed but native execution completed");
    const auto type = original.get("type").text;
    check(type == "System.FormatException" || type == "System.OverflowException",
          "Unreviewed original conversion exception " + type);
    // The strict API must expose which original exception occurred. A generic
    // cast fault cannot prove Format and Overflow parity across platforms.
    const auto category = type.substr(type.find_last_of('.') + 1);
    check(result.error.starts_with(category + ": "), "Original " + category + " category differs: " + result.error);
}

void fixture(const std::filesystem::path &root, const network::Value &oracle) {
    const auto name = oracle.get("name").text;
    const auto source = read(root / name / "source.lsl");
    check(oracle.get("accepted").boolean, "Original strict source unexpectedly rejected");
    check(yengine_source_matches(source, oracle.get("source_hash").text), "Strict source provenance mismatch");
    const auto compiled = compile_lsl(source);
    std::string errors;
    for (const auto &error : compiled.diagnostics)
        errors += " (" + std::to_string(error.line) + "," + std::to_string(error.column) + ") " + error.message;
    check(bool(compiled), "Accepted original strict source failed native compile:" + errors);
    const auto &exception = oracle.get("actual_api_exception");
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
    LslInstance instance(compiled.program, {}, host);
    auto saved = instance.checkpoint();
    check(saved.has_value(), "Initial strict checkpoint unavailable");
    LslRunResult outcome;
    bool stopped = false;
    for (unsigned step = 0; step < 1000; ++step) {
        outcome = instance.run(1);
        if (auto checkpoint = instance.checkpoint())
            saved = *checkpoint;
        if (outcome.status == LslRunStatus::Faulted || outcome.status == LslRunStatus::Idle) {
            stopped = true;
            break;
        }
        check(outcome.status != LslRunStatus::Sleeping, "Pure strict conversion unexpectedly suspended");
    }
    check(stopped, "Strict conversion exceeded bounded source execution");
    check(sleeps == 0, "Strict conversion requested API sleep");
    if (exception.kind == network::Value::Kind::Map)
        check_fault(outcome, exception);
    else
        check(outcome.status == LslRunStatus::Idle, "Original successful conversion faulted: " + outcome.error);
    // A fault has no exportable checkpoint: the last successful instruction
    // boundary proves original pre-fault globals without inventing a result.
    compare_globals(*saved, source, oracle.get("final").get("globals"));
    if (exception.kind != network::Value::Kind::Map) {
        const auto names = globals(source);
        const auto found = std::find(names.begin(), names.end(), "result");
        check(found != names.end(), "Original successful result variable missing");
        compare_value(saved->globals[static_cast<std::size_t>(found - names.begin())], oracle.get("actual_api_return"),
                      "actual original method return");
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Original strict-conversion fixture directory required");
        const std::filesystem::path root = argv[1];
        const auto cases = network::parse_osd_json(read(root / "metadata.json"));
        check(cases.array.size() == 100, "Original strict-conversion case inventory changed");
        std::map<std::string, unsigned> methods;
        unsigned successes = 0, failures = 0;
        for (const auto &oracle : cases.array) {
            const auto name = oracle.get("name").text;
            try {
                fixture(root, oracle);
            } catch (const std::exception &error) {
                throw std::runtime_error(name + ": " + error.what());
            }
            ++methods[oracle.get("api_name").text];
            if (oracle.get("actual_api_exception").kind == network::Value::Kind::Map)
                ++failures;
            else
                ++successes;
        }
        check(methods["xmrString2Integer"] == 32 && methods["xmrString2Float"] == 37 &&
                  methods["xmrString2Vector"] == 16 && methods["xmrString2Rotation"] == 15,
              "Original conversion method scope changed");
        check(successes == 53 && failures == 47, "Original strict conversion return/exception scope changed");
        std::cout << "PASS: 100 original strict-conversion source cases (" << successes << " returns, " << failures
                  << " exceptions), exact IEEE754 bits\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
