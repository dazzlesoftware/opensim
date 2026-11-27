// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/script/yengine_object_code.hpp"
#include "opensim/scene/object_asset_xml.hpp"
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
    check(bool(input), "Missing original truth-conversion fixture: " + path.string());
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

void fixture(const std::filesystem::path &root, const network::Value &oracle, bool rotation_extension = false) {
    const auto name = oracle.get("name").text;
    const auto source = read(root / name / "source.lsl");
    check(oracle.get("accepted").boolean, "Original truth-conversion source unexpectedly rejected");
    check(yengine_source_matches(source, oracle.get("source_hash").text),
          "Truth-conversion source provenance mismatch");
    const auto compiled = compile_lsl(source);
    std::string errors;
    for (const auto &error : compiled.diagnostics)
        errors += " (" + std::to_string(error.line) + "," + std::to_string(error.column) + ") " + error.message;
    check(bool(compiled), "Accepted original truth-conversion source failed native compile:" + errors);
    check(oracle.get("initial_exception").kind != network::Value::Kind::Map,
          "Original truth source unexpectedly faulted");
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
    check(saved.has_value(), "Initial truth-conversion checkpoint unavailable");
    LslRunResult outcome;
    bool stopped = false;
    for (unsigned step = 0; step < 1000; ++step) {
        outcome = instance.run(1);
        if (auto checkpoint = instance.checkpoint())
            saved = *checkpoint;
        if (outcome.status == LslRunStatus::BudgetExhausted || outcome.status == LslRunStatus::Sleeping) {
            const auto wire = encode_lsl_checkpoint(*saved);
            const auto decoded = decode_lsl_checkpoint(wire);
            LslInstance fresh(compiled.program, context, host);
            check(decoded && fresh.restore_checkpoint(*decoded),
                  "Internal truth continuation failed fresh-VM checkpoint restore");
            instance = std::move(fresh);
        }
        if (outcome.status == LslRunStatus::Faulted || outcome.status == LslRunStatus::Idle) {
            stopped = true;
            break;
        }
        if (outcome.status == LslRunStatus::Sleeping) {
            check(rotation_extension && oracle.get("suspended").boolean, "Unexpected truth API sleep");
            clock += 20000000;
        }
    }
    check(stopped, "Truth conversion exceeded bounded source execution");
    check(sleeps == (rotation_extension && oracle.get("suspended").boolean ? 1u : 0u), "Truth API sleep count differs");
    const auto &native_expected = rotation_extension && oracle.get("suspended").boolean
                                      ? oracle.get("live_resume").get("globals")
                                      : oracle.get("final").get("globals");
    check(outcome.status == LslRunStatus::Idle, "Original successful truth conversion faulted: " + outcome.error);
    compare_globals(*saved, source, native_expected);
    const auto wire = encode_lsl_checkpoint(*saved);
    const auto decoded = decode_lsl_checkpoint(wire);
    LslInstance restored(compiled.program, context, host);
    check(decoded && restored.restore_checkpoint(*decoded), "Final truth checkpoint failed fresh-VM restore");
    const auto restored_checkpoint = restored.checkpoint();
    check(restored_checkpoint.has_value(), "Restored truth checkpoint unavailable");
    compare_globals(*restored_checkpoint, source, native_expected);
    if (rotation_extension && oracle.get("suspended").boolean) {
        const auto directory = root / name;
        const auto raw_wire = read(directory / "snapshot.bin");
        const auto object_wire = read(directory / "original.yobj");
        const auto bytes = [](const std::string &value) {
            return std::span(reinterpret_cast<const std::uint8_t *>(value.data()), value.size());
        };
        const auto code = decode_yengine_object_code(bytes(object_wire));
        const auto raw = decode_yengine_snapshot(bytes(raw_wire));
        check(code && raw, "Original raw zero-rotation continuation failed decode");
        const auto binary = std::make_shared<std::vector<std::uint8_t>>(raw_wire.begin(), raw_wire.end());
        const auto encoded = network::binary_base64(network::osd_from_binary(binary));
        const auto xml = "<ScriptState Engine=\"YEngine\" Asset=\"" + context.item.str() + "\" SourceHash=\"" +
                         oracle.get("source_hash").text + "\"><Running>True</Running><Snapshot>" + encoded +
                         "</Snapshot>" + read(directory / "event-queue.xml") + "</ScriptState>";
        const auto original = scene::read_yengine_state_xml(bytes(xml), context.item);
        check(original && original->snapshot, "Original rotation XML snapshot/queue failed decode");
        LslInstance imported(compiled.program, context, host);
        check(imported.restore_yengine_snapshot(*original->snapshot, original->source_hash, source, *code),
              "Original sleeping rotation continuation did not import");
        imported.restore_queued_events(original->events);
        const auto sleeps_before = sleeps;
        bool complete = false;
        for (unsigned step = 0; step < 1000; ++step) {
            const auto result = imported.run(1);
            check(result.status != LslRunStatus::Faulted && result.status != LslRunStatus::Sleeping,
                  "Original post-sleep continuation faulted or replayed sleep: " + result.error);
            if (result.status == LslRunStatus::Idle) {
                complete = true;
                break;
            }
        }
        const auto imported_final = imported.checkpoint();
        check(complete && imported_final && sleeps == sleeps_before,
              "Original rotation continuation failed completion");
        // CLR Recv() constructs Quaternion and normalizes all-zero rotations.
        // Native checkpoint restoration above deliberately preserves raw bits.
        compare_globals(*imported_final, source, oracle.get("final").get("globals"));
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc >= 2 && argc <= 4,
              "Expected truth fixture and optional rotation-zero/arithmetic fixture directories");
        const std::filesystem::path root(argv[1]);
        const auto rows = network::parse_osd_json(read(root / "metadata.json"));
        check(rows.array.size() == 39, "Original truth source corpus count changed");
        std::map<std::string, std::size_t> kinds;
        for (const auto &row : rows.array) {
            ++kinds[row.get("kind").text];
            try {
                fixture(root, row);
            } catch (const std::exception &error) {
                throw std::runtime_error(row.get("name").text + ": " + error.what());
            }
        }
        check(kinds ==
                  std::map<std::string, std::size_t>{
                      {"Key", 10}, {"String", 8}, {"List", 7}, {"Rotation", 6}, {"Vector", 5}, {"Source", 3}},
              "Original truth source kind counts differ");
        const auto raw = network::parse_osd_json(read(root / "raw_controls.json"));
        check(raw.array.size() == 5, "Original raw control count changed");
        const std::vector<std::string> names{"key_null", "string_null", "list_null", "list_raw_boolean",
                                             "list_null_element"};
        for (std::size_t i = 0; i < names.size(); ++i)
            check(raw.array[i].get("name").text == names[i] && !raw.array[i].get("source_representable").boolean,
                  "Unreviewed raw truth control: not claimed as native source coverage");
        if (argc >= 3) {
            const std::filesystem::path rotations(argv[2]);
            const auto extended = network::parse_osd_json(read(rotations / "metadata.json"));
            check(extended.array.size() == 12, "Original rotation-zero corpus count differs");
            std::size_t sleeping = 0;
            for (const auto &row : extended.array) {
                sleeping += row.get("suspended").boolean;
                try {
                    fixture(rotations, row, true);
                } catch (const std::exception &error) {
                    throw std::runtime_error(row.get("name").text + ": " + error.what());
                }
            }
            check(sleeping == 2, "Original rotation-zero live/migration split count differs");
        }
        if (argc == 4) {
            const std::filesystem::path arithmetic(argv[3]);
            const auto rows = network::parse_osd_json(read(arithmetic / "metadata.json"));
            check(rows.array.size() == 5, "Original rotation arithmetic corpus count differs");
            for (const auto &row : rows.array) {
                check((row.get("kind").text.empty() || row.get("kind").text == "Source") &&
                          row.get("accepted").boolean && !row.get("suspended").boolean,
                      "Original rotation arithmetic case is not an accepted pure source case");
                try {
                    fixture(arithmetic, row);
                } catch (const std::exception &error) {
                    throw std::runtime_error(row.get("name").text + ": " + error.what());
                }
            }
        }
        std::cout << "PASS: 39 original truth source cases, every-instruction fresh VM checkpoint, exact typed/boxed "
                     "globals; "
                     "five source-unrepresentable raw CLR null/Boolean controls explicitly reference-only; supplied 12 "
                     "rotation-zero cases and 5 arithmetic cases verified separately\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
