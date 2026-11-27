// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/osd_json.hpp"
#include "opensim/script/lsl_runtime.hpp"
#include <bit>
#include <charconv>
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
    std::ifstream input(path, std::ios::binary);
    check(bool(input), "Missing original numeric hash evidence: " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::uint64_t bits(const network::Value &value, std::size_t width) {
    const auto text = value.get("bits").text;
    check(text.size() == width, "Missing original numeric input bits");
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result, 16);
    check(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size(), "Malformed original numeric bits");
    return result;
}

LslCheckpoint execute(const std::string &source) {
    const auto compiled = compile_lsl(source);
    std::string diagnostics;
    for (const auto &error : compiled.diagnostics)
        diagnostics += " " + error.message;
    check(bool(compiled), "Accepted original hash source failed compile:" + diagnostics);
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [] { return std::int64_t{638000000000000000LL}; };
    auto instance = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
    for (unsigned step = 0; step < 1000; ++step) {
        const auto outcome = instance->run(1);
        check(outcome.status != LslRunStatus::Faulted, "Numeric hash fault: " + outcome.error);
        check(outcome.status != LslRunStatus::Sleeping, "Pure numeric hash unexpectedly slept");
        const auto current = instance->checkpoint();
        check(current.has_value(), "Numeric hash checkpoint unavailable");
        const auto restored = decode_lsl_checkpoint(encode_lsl_checkpoint(*current));
        check(restored.has_value(), "Numeric hash checkpoint codec rejected state");
        auto next = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
        check(next->restore_checkpoint(*restored), "Fresh numeric hash VM rejected checkpoint");
        instance = std::move(next);
        if (outcome.status == LslRunStatus::Idle) {
            check(instance->run(1).status == LslRunStatus::Idle, "Completed hash script replayed entry");
            return *instance->checkpoint();
        }
    }
    throw std::runtime_error("Numeric hash exceeded bounded execution");
}

void fixture(const std::filesystem::path &root, const network::Value &oracle) {
    const auto source = read(root / oracle.get("name").text / "source.lsl");
    check(oracle.get("accepted").boolean && oracle.get("initial_exception").kind != network::Value::Kind::Map &&
              oracle.get("direct_exception").kind != network::Value::Kind::Map,
          "Original numeric hash did not succeed");
    check(yengine_source_matches(source, oracle.get("source_hash").text), "Original hash source mismatch");
    const auto kind = oracle.get("kind").text;
    check(kind == "Integer" || kind == "Float", "Unreviewed original overload");
    const auto &calls = oracle.get("original_hash_call_lines").array;
    check(calls.size() == 1 &&
              calls[0].text.find(kind == "Integer" ? "xmrHashCode(Int32)" : "xmrHashCode(Double)") != std::string::npos,
          "Original IL does not prove expected overload");
    const auto final = execute(source);
    const auto prefix = source.substr(0, source.find("default"));
    const std::regex declarations(R"(\b(?:integer|float)\s+([A-Za-z_][A-Za-z_0-9]*)\s*(?:=[^;]*)?;)");
    std::size_t index = 0;
    for (auto it = std::sregex_iterator(prefix.begin(), prefix.end(), declarations); it != std::sregex_iterator();
         ++it) {
        check(index < final.globals.size(), "Native hash globals missing");
        const auto name = (*it)[1].str();
        const auto &expected = oracle.get("final").get("globals").get(name);
        if (expected.get("type").text == "integer") {
            const auto *actual = std::get_if<std::int32_t>(&final.globals[index]);
            check(actual && *actual == network::osd_integer(expected.get("value")), name + ": original Int32 differs");
            if (name == "result")
                check(*actual == network::osd_integer(oracle.get("direct_result").get("value")),
                      "Compiled source differs from original direct overload result");
        } else {
            const auto *actual = std::get_if<double>(&final.globals[index]);
            // General global snapshots carry JSON numbers; the dedicated
            // direct_input evidence below independently records IEEE bits.
            check(actual && *actual == network::osd_real(expected.get("value")), "Original Double global differs");
        }
        ++index;
    }
    check(index == final.globals.size(), "Unexpected native hash globals");
    // Independently expose the exact source argument, preserving initialization
    // and casts. This verifies IEEE input bits before a hash could hide them.
    const std::string marker = "result=xmrHashCode(";
    const auto begin = source.find(marker);
    const auto end = source.find(");after=", begin);
    check(begin != std::string::npos && end != std::string::npos, "Unreviewed original hash source shape");
    const auto argument = source.substr(begin + marker.size(), end - begin - marker.size());
    auto input_source = prefix;
    const auto declaration = input_source.find("integer result;");
    check(declaration != std::string::npos, "Original result declaration missing");
    input_source.replace(declaration, 15, kind == "Integer" ? "integer result;" : "float result;");
    input_source += "default{state_entry(){result=" + argument + ";after=1;}}";
    const auto input = execute(input_source);
    const auto result_index = final.globals.size() - 2;
    const auto &expected_input = oracle.get("direct_input");
    if (kind == "Integer") {
        const auto *actual = std::get_if<std::int32_t>(&input.globals[result_index]);
        check(actual && std::bit_cast<std::uint32_t>(*actual) == bits(expected_input, 8),
              "Original Int32 argument bits differ");
    } else {
        const auto *actual = std::get_if<double>(&input.globals[result_index]);
        check(actual && std::bit_cast<std::uint64_t>(*actual) == bits(expected_input, 16),
              "Original Double argument bits differ");
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Original numeric hash fixture directory required");
        const std::filesystem::path root = argv[1];
        const auto cases = network::parse_osd_json(read(root / "metadata.json"));
        check(cases.array.size() == 46, "Original hash source inventory changed");
        unsigned integers = 0, floats = 0;
        for (const auto &oracle : cases.array) {
            try {
                fixture(root, oracle);
            } catch (const std::exception &error) {
                throw std::runtime_error(oracle.get("name").text + ": " + error.what());
            }
            (oracle.get("kind").text == "Integer" ? integers : floats)++;
        }
        check(integers == 18 && floats == 28, "Original numeric overload coverage changed");
        // Reference-only arbitrary NaN payloads have no established source
        // construction/public numeric helper. Do not manufacture VM inputs or
        // claim these six controls are native execution coverage.
        const auto controls = network::parse_osd_json(read(root / "raw_nan_controls.json"));
        check(controls.array.size() == 6, "Original raw NaN control inventory changed");
        for (const auto &control : controls.array) {
            check(!control.get("source_expressible").boolean, "Raw NaN control mislabeled as LSL source");
            const auto raw = bits(control.get("input"), 16);
            check((raw & 0x7ff0000000000000ULL) == 0x7ff0000000000000ULL && (raw & 0xfffffffffffffULL),
                  "Original raw control is not NaN");
            check(network::osd_integer(control.get("result").get("value")) == 2146435072,
                  "Original CLR NaN hash control changed");
        }
        // String and Key overloads have their own genuine original corpus in
        // yengine_string_hash_tests. These remaining Object overloads have not
        // established a native contract yet.
        for (const auto &argument : {"[1]", "<1,2,3>", "<0,0,0,1>"}) {
            const auto compiled = compile_lsl(std::string("integer result;default{state_entry(){result=xmrHashCode(") +
                                              argument + ");}}");
            check(!compiled, "Unsupported nonnumeric hash overload accepted before its contract");
        }
        std::cout << "46 original numeric hash sources passed; six CLR-only NaN controls retained as reference-only.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
