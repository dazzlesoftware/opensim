// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/framework/sha256.hpp"
#include "opensim/network/osd_json.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
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
    check(bool(input), "Missing original mixed-operator fixture: " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void compare_value(const LslValue &actual, const network::Value &expected, const std::string &where) {
    const auto &clr = expected.get("clr").text;
    if (clr == "OpenSim.Region.ScriptEngine.Shared.LSL_Types+LSLInteger") {
        const auto *value = std::get_if<LslBoxedInteger>(&actual);
        check(value && value->value == network::osd_integer(expected.get("value")),
              where + ": boxed LSLInteger differs");
        return;
    }
    if (clr == "OpenSim.Region.ScriptEngine.Shared.LSL_Types+LSLString") {
        const auto *value = std::get_if<LslBoxedString>(&actual);
        check(value && value->value == expected.get("value").text, where + ": boxed LSLString differs");
        return;
    }
    if (expected.get("type").text == "float") {
        const double *value = nullptr;
        if (clr == "OpenSim.Region.ScriptEngine.Shared.LSL_Types+LSLFloat") {
            const auto *boxed = std::get_if<LslBoxedFloat>(&actual);
            check(boxed != nullptr, where + ": Boxed LSLFloat storage differs");
            value = &boxed->value;
        } else
            value = std::get_if<double>(&actual);
        const auto &text = expected.get("bits").text;
        std::uint64_t bits{};
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), bits, 16);
        check(value && text.size() == 16 && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
                  std::bit_cast<std::uint64_t>(*value) == bits,
              where + ": Raw/boxed Float bits differ");
        return;
    }
    if (expected.get("type").text == "key") {
        const auto *value = std::get_if<LslBoxedKey>(&actual);
        check(value && value->value == expected.get("value").text, where + ": Raw backend boxed key differs");
        return;
    }
    const auto type = expected.get("type").text;
    if (type == "string") {
        const auto *value = std::get_if<std::string>(&actual);
        check(value && *value == expected.get("value").text, where + ": String storage/value differs");
    } else if (type == "integer") {
        const auto *value = std::get_if<std::int32_t>(&actual);
        check(value && *value == network::osd_integer(expected.get("value")), where + ": Int32 storage/value differs");
    } else if (type == "list") {
        const auto *value = std::get_if<std::shared_ptr<const LslList>>(&actual);
        check(value && *value && (*value)->items.size() == expected.get("value").array.size(),
              where + ": List differs");
        for (std::size_t i = 0; i < (*value)->items.size(); ++i)
            compare_value((*value)->items[i], expected.get("value").array[i], where + "[" + std::to_string(i) + "]");
    } else if (type == "vector" || type == "rotation") {
        std::vector<double> components;
        if (type == "vector") {
            const auto *v = std::get_if<framework::Vector3d>(&actual);
            check(v != nullptr, where + ": Vector storage differs");
            components = {v->x, v->y, v->z};
        } else {
            const auto *v = std::get_if<LslRotation>(&actual);
            check(v != nullptr, where + ": Rotation storage differs");
            components = {v->x, v->y, v->z, v->w};
        }
        check(components.size() == expected.get("value").array.size(), where + ": Component count differs");
        for (std::size_t i = 0; i < components.size(); ++i) {
            const auto &text = expected.get("value").array[i].get("bits").text;
            std::uint64_t bits{};
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), bits, 16);
            check(text.size() == 16 && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
                      std::bit_cast<std::uint64_t>(components[i]) == bits,
                  where + ": IEEE component " + std::to_string(i) + " bits differ; actual=" +
                      std::to_string(std::bit_cast<std::uint64_t>(components[i])) + " original=" + text);
        }
    } else
        throw std::runtime_error(where + ": Unreviewed original mixed-operator global type " + type + " CLR " + clr);
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

LslCheckpoint snapshot(LslInstance &instance) {
    const auto saved = instance.checkpoint();
    check(saved.has_value(), "Mixed-operator checkpoint unavailable");
    return *saved;
}

void fixture(const std::filesystem::path &root, const network::Value &oracle) {
    const auto name = oracle.get("name").text;
    const auto source = read(root / name / "source.lsl");
    check(oracle.get("case_definition").get("source").text == source, "Original operator case/source mismatch");
    const bool accepted = oracle.get("accepted").boolean;
    const auto outcome = oracle.get("compiler_outcome").text;
    const auto digest = accepted ? oracle.get("sha256").get("source.lsl").text : oracle.get("source_sha256").text;
    check(framework::sha256_hex(source) == digest, "Original operator source SHA256 mismatch");
    check(yengine_source_matches(source, oracle.get("source_hash").text),
          "Original operator compiler source hash mismatch");
    const auto compiled = compile_lsl(source);
    if (!accepted) {
        check(outcome == "diagnostic_rejection" || outcome == "compiler_internal_failure",
              "Unreviewed original compilation failure category");
        if (outcome == "compiler_internal_failure") {
            check(oracle.get("diagnostics").array.empty() &&
                      oracle.get("compile_exception").get("type").text == "System.Exception" &&
                      oracle.get("compile_exception").get("message").text.starts_with("stack has "),
                  "Original internal-compiler control changed");
        } else
            check(!oracle.get("diagnostics").array.empty(), "Original rejection has no diagnostic provenance");
        // No runtime result is invented for original compiler failures. Native
        // may reject those inputs cleanly rather than reproduce a CLR crash.
        check(!compiled && !compiled.diagnostics.empty(),
              "Native accepted an original rejected/internal-failure source");
        return;
    }
    std::string errors;
    for (const auto &error : compiled.diagnostics)
        errors += " (" + std::to_string(error.line) + "," + std::to_string(error.column) + ") " + error.message;
    check(outcome == "accepted" && bool(compiled),
          "Original successful operator source failed native compile:" + errors);
    check(oracle.get("initial_exception").kind != network::Value::Kind::Map && !oracle.get("suspended").boolean,
          "Unreviewed original runtime outcome");
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](int) { ++sleeps; };
    LslContext context;
    context.object = framework::Uuid::random();
    context.item = framework::Uuid::random();
    LslInstance instance(compiled.program, context, host);
    bool completed = false;
    for (unsigned step = 0; step < 10000; ++step) {
        const auto run = instance.run(1);
        check(run.status != LslRunStatus::Faulted, "Original successful operator source faulted: " + run.error);
        check(run.status != LslRunStatus::Sleeping, "Pure operator expression unexpectedly suspended");
        const auto current = snapshot(instance);
        // Every internal CPU budget boundary is restarted in a fresh VM. This
        // proves actual expression journals/call frames preserve RTL effects,
        // deferred reads and boxed/nested list values without replay.
        const auto wire = encode_lsl_checkpoint(current);
        const auto decoded = decode_lsl_checkpoint(wire);
        LslInstance restored(compiled.program, context, host);
        check(decoded && restored.restore_checkpoint(*decoded),
              "Mixed-operator continuation failed native checkpoint restore");
        instance = std::move(restored);
        if (run.status == LslRunStatus::Idle) {
            compare_globals(snapshot(instance), source, oracle.get("final").get("globals"));
            const auto idle = instance.run(1);
            check(idle.status == LslRunStatus::Idle, "Completed operator checkpoint replayed source");
            compare_globals(snapshot(instance), source, oracle.get("final").get("globals"));
            completed = true;
            break;
        }
    }
    check(completed && sleeps == 0, "Operator source did not finish within its pure instruction bound");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2 || argc == 3,
              "Original mixed-operator directory and optional deferred-read directory required");
        const std::filesystem::path root = argv[1];
        const auto cases = network::parse_osd_json(read(root / "metadata.json"));
        check(cases.array.size() == 208, "Original mixed-operator inventory changed");
        std::map<std::string, unsigned> outcomes, groups;
        for (const auto &oracle : cases.array) {
            const auto name = oracle.get("name").text;
            try {
                fixture(root, oracle);
            } catch (const std::exception &error) {
                throw std::runtime_error(name + ": " + error.what());
            }
            ++outcomes[oracle.get("compiler_outcome").text];
            ++groups[oracle.get("case_definition").get("coverage_group").text];
        }
        check(outcomes["accepted"] == 57 && outcomes["diagnostic_rejection"] == 143 &&
                  outcomes["compiler_internal_failure"] == 8,
              "Original operator outcome scope changed");
        check(groups["binary_matrix"] == 168 && groups["compound_matrix"] == 14 && groups["return"] == 4 &&
                  groups["order"] == 8 && groups["deferred"] == 2 && groups["ordinal"] == 12,
              "Original mixed-operator feature scope changed");
        unsigned additional_deferred = 0;
        if (argc == 3) {
            const std::filesystem::path deferred_root = argv[2];
            const auto deferred = network::parse_osd_json(read(deferred_root / "metadata.json"));
            check(deferred.array.size() == 4, "Original right/left deferred-read inventory changed");
            for (const auto &oracle : deferred.array) {
                check(oracle.get("accepted").boolean && oracle.get("compiler_outcome").text == "accepted",
                      "Deferred-read source did not compile in original oracle");
                try {
                    fixture(deferred_root, oracle);
                } catch (const std::exception &error) {
                    throw std::runtime_error("deferred/" + oracle.get("name").text + ": " + error.what());
                }
                ++additional_deferred;
            }
        }
        std::cout
            << "PASS: 57 original operator executions, 143 diagnostic rejects, 8 separately scoped original "
               "compiler-failure controls; "
            << additional_deferred
            << " additional deferred-read executions; exact boxing/ordinal/effects with budget checkpoint restart\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
