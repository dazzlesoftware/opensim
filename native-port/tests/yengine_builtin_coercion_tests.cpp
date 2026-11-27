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
    check(bool(input), "Missing original builtin-coercion fixture: " + path.string());
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
        throw std::runtime_error(where + ": Unreviewed original truth global type " + type + " CLR " + clr);
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

void fixture(const std::filesystem::path &root, const network::Value &oracle) {
    const auto name = oracle.get("name").text;
    const auto source = read(root / name / "source.lsl");
    const auto compiled = compile_lsl(source);
    if (!oracle.get("accepted").boolean) {
        check(framework::sha256_hex(source) == oracle.get("source_sha256").text,
              "Rejected original source provenance mismatch");
        check(!compiled && !compiled.diagnostics.empty(), "Original strict index/operation rejection was accepted");
        return;
    }
    check(yengine_source_matches(source, oracle.get("source_hash").text), "Original source hash mismatch");
    std::string errors;
    for (const auto &error : compiled.diagnostics)
        errors += " " + error.message;
    check(bool(compiled), "Accepted original builtin coercion source failed compile:" + errors);
    check(oracle.get("initial_exception").kind != network::Value::Kind::Map,
          "Original builtin source unexpectedly faulted");
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [] { return std::int64_t{638000000000000000LL}; };
    unsigned effects = 0;
    host.chat = [&](const LslChat &) { ++effects; };
    host.request_sleep = [&](int) { ++effects; };
    LslContext context;
    context.object = framework::Uuid::random();
    context.item = framework::Uuid::random();
    LslInstance instance(compiled.program, context, host);
    bool complete = false;
    for (unsigned step = 0; step < 1000; ++step) {
        const auto outcome = instance.run(1);
        check(outcome.status != LslRunStatus::Faulted && outcome.status != LslRunStatus::Sleeping,
              "Pure builtin coercion faulted/slept: " + outcome.error);
        const auto saved = instance.checkpoint();
        check(bool(saved), "Builtin coercion checkpoint unavailable");
        if (outcome.status == LslRunStatus::BudgetExhausted) {
            const auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(*saved));
            LslInstance fresh(compiled.program, context, host);
            check(decoded && fresh.restore_checkpoint(*decoded),
                  "Builtin coercion continuation fresh-VM restore rejected");
            instance = std::move(fresh);
        }
        if (outcome.status == LslRunStatus::Idle) {
            complete = true;
            break;
        }
    }
    const auto saved = instance.checkpoint();
    check(complete && saved && effects == 0, "Builtin coercion did not complete purely");
    compare_globals(*saved, source, oracle.get("final").get("globals"));
    const auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(*saved));
    LslInstance final(compiled.program, context, host);
    check(decoded && final.restore_checkpoint(*decoded), "Final builtin coercion checkpoint failed restore");
    const auto restored = final.checkpoint();
    check(bool(restored), "Final restored builtin checkpoint unavailable");
    compare_globals(*restored, source, oracle.get("final").get("globals"));
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected original builtin coercion fixture directory");
        const std::filesystem::path root(argv[1]);
        const auto rows = network::parse_osd_json(read(root / "metadata.json"));
        check(rows.array.size() == 67, "Original builtin coercion corpus count changed");
        std::size_t accepted = 0, rejected = 0, strings = 0, lists = 0;
        for (const auto &row : rows.array) {
            const auto name = row.get("name").text;
            (row.get("accepted").boolean ? accepted : rejected)++;
            if (name.starts_with("string_"))
                ++strings;
            if (name.starts_with("list_"))
                ++lists;
            try {
                fixture(root, row);
            } catch (const std::exception &error) {
                throw std::runtime_error(name + ": " + error.what());
            }
        }
        check(accepted == 59 && rejected == 8 && strings == 35 && lists == 24,
              "Original builtin coercion coverage counts differ");
        std::cout
            << "PASS: 67 original builtin coercion cases (35 string,24 list accepted;8 strict integer-index/operation "
               "rejects), exact typed results across every-instruction fresh-VM checkpoints\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
