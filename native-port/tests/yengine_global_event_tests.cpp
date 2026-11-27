// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/script/yengine_object_code.hpp"
#include "opensim/scene/object_asset_xml.hpp"
#include "opensim/framework/invariant_datetime_parse.hpp"
#include "opensim/network/osd_json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <stdexcept>

using namespace opensim;
using namespace framework;
using namespace script;

namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}

std::string read(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    check(bool(stream), "Missing original constructor fixture: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

auto bytes(const std::string &value) {
    return std::span(reinterpret_cast<const std::uint8_t *>(value.data()), value.size());
}

auto compile(const std::string &source) {
    const auto result = compile_lsl(source);
    std::string errors;
    for (const auto &error : result.diagnostics)
        errors += " (" + std::to_string(error.line) + "," + std::to_string(error.column) + ") " + error.message;
    check(bool(result), "Original constructor source did not compile:" + errors);
    return result.program;
}

LslCheckpoint checkpoint(LslInstance &instance) {
    const auto result = instance.checkpoint();
    check(bool(result), "Constructor checkpoint unavailable");
    return *result;
}

std::vector<std::string> global_names(std::string source, std::map<std::string, std::string> *types = nullptr) {
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
        R"(\b(integer|float|string|key|vector|rotation|list)\s+([A-Za-z_][A-Za-z_0-9]*)\s*(?:=|;))");
    std::vector<std::string> result;
    for (auto it = std::sregex_iterator(source.begin(), source.end(), declaration); it != std::sregex_iterator();
         ++it) {
        result.push_back((*it)[2].str());
        if (types)
            types->emplace((*it)[2].str(), (*it)[1].str());
    }
    return result;
}

void compare_value(const LslValue &actual, const network::Value &expected) {
    const auto value = unbox_lsl_value(actual);
    const auto type = expected.get("type").text;
    const auto &oracle = expected.get("value");
    const auto clr = expected.get("clr").text;
    if (clr.ends_with("+LSLInteger"))
        check(std::holds_alternative<LslBoxedInteger>(actual), "Original constructor lost boxed integer identity");
    if (clr.ends_with("+LSLString"))
        check(std::holds_alternative<LslBoxedString>(actual), "Original constructor lost boxed string identity");
    if (type == "integer")
        check(std::get<std::int32_t>(value) == network::osd_integer(oracle),
              "Constructor integer differs from original");
    else if (type == "string" || type == "key")
        check(std::get<std::string>(value) == oracle.text, "Constructor string differs from original");
    else if (type == "list") {
        const auto &list = std::get<std::shared_ptr<const LslList>>(value);
        check(list && list->items.size() == oracle.array.size(), "Constructor list length differs from original");
        for (std::size_t i = 0; i < list->items.size(); ++i)
            compare_value(list->items[i], oracle.array[i]);
    } else
        throw std::runtime_error("Unsupported completed constructor oracle type: " + type);
}

void compare_globals(const LslCheckpoint &saved, const std::string &source, const network::Value &expected,
                     bool partial = false) {
    std::map<std::string, std::string> types;
    const auto names = global_names(source, &types);
    check(names.size() == saved.globals.size() && expected.map.size() == names.size(),
          "Constructor global declaration/oracle count differs");
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto found = expected.map.find(names[i]);
        check(found != expected.map.end(), "Original constructor omitted global " + names[i]);
        if (partial && found->second.get("type").text == "null") {
            const auto &type = types.at(names[i]);
            if (type == "string" || type == "key") {
                check(std::holds_alternative<LslNullString>(saved.globals[i]) &&
                          !lsl_string_code_units(saved.globals[i]),
                      "Original uninitialized CLR String null was replaced by empty text: " + names[i]);
            } else {
                check(type == "list", "Unreviewed original partial-null global type: " + type);
                check(std::holds_alternative<LslNullList>(saved.globals[i]),
                      "Original uninitialized CLR List null was replaced by empty storage: " + names[i]);
            }
            continue;
        }
        try {
            compare_value(saved.globals[i], found->second);
        } catch (const std::exception &error) {
            throw std::runtime_error(names[i] + ": " + error.what());
        }
    }
}

void compare_state(const LslCheckpoint &saved, const network::Value &expected, const YEngineObjectCode &code) {
    const auto index = network::osd_integer(expected.get("state_index"));
    check(index >= 0 && static_cast<std::size_t>(index) < code.states.size() && saved.state == code.states[index] &&
              saved.global_initialization_pending == expected.get("initialize_globals").boolean,
          "Constructor state or pending-initialization phase differs from original");
}

void finish(LslInstance &instance, std::int64_t &clock) {
    for (unsigned step = 0; step < 10000; ++step) {
        const auto result = instance.run(1);
        check(result.status != LslRunStatus::Faulted, "Constructor continuation faulted: " + result.error);
        if (result.status == LslRunStatus::Sleeping) {
            const auto saved = checkpoint(instance);
            if (saved.sleep_remaining)
                clock += saved.sleep_remaining + 1;
            else if (saved.event_wait)
                throw std::runtime_error("Constructor continuation unexpectedly waits for another event");
        }
        if (result.status == LslRunStatus::Idle)
            return;
    }
    throw std::runtime_error("Constructor continuation exceeded its instruction bound");
}

std::int64_t original_deadline(const YEngineSnapshot &snapshot) {
    for (const auto &frame : snapshot.managed_frames) {
        if (frame.function != "xmrEventDequeue")
            continue;
        const auto &slots = std::get<YEngineManagedArray>(frame.slots->value);
        const auto &text = std::get<std::string>(slots.at(0)->value);
        InvariantDateTimeParseContext context;
        context.now = [] { return AssetDateTime(638000000000000000LL, 1); };
        context.two_digit_year_max = [] { return 2049; };
        context.local_offset_no_throw = [](auto) { return std::int64_t{0}; };
        context.utc_offset = [](auto) { return InvariantDateTimeParseContext::UtcOffset{}; };
        context.valid_leap_second = [](int, int, int, int, int) { return false; };
        AssetDateTime parsed;
        check(try_parse_invariant_datetime_utf8(text, parsed, context), "Original constructor deadline did not parse");
        return parsed.ticks();
    }
    return 638000000000000000LL;
}

void post_return(LslInstance &instance, const std::string &name, const network::Value &metadata) {
    if (name != "global_wait_return" && name != "global_wait_stop_start")
        return;
    const auto &result = metadata.get("final").get("globals").get("result").get("value").array;
    LslDetector detector;
    detector.name = name == "global_wait_return" ? metadata.get("final").get("globals").get("seen").get("value").text
                                                 : "stop/start arrival";
    check(instance.post_event({"touch_start", {network::osd_integer(result.at(1).get("value"))}, {detector}}),
          "Original constructor return event was not admitted");
}

void fixture(const std::filesystem::path &root, const network::Value &metadata) {
    const auto name = metadata.get("name").text;
    const auto directory = root / name;
    const auto source = read(directory / "source.lsl");
    const auto program = compile(source);
    const auto code_wire = read(directory / "original.yobj");
    const auto code = decode_yengine_object_code(bytes(code_wire));
    check(bool(code), "Original constructor object code did not decode");
    std::int64_t clock = 638000000000000000LL;
    unsigned writes = 0, sleeps = 0;
    std::string description;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.set_object_text = [&](auto, bool is_description, const std::string &text) {
        check(is_description, "Constructor fixture unexpectedly changed an object name");
        description = text;
        ++writes;
        return true;
    };
    host.request_sleep = [&](int) { ++sleeps; };
    LslContext context;
    context.object = Uuid::random();
    context.owner = Uuid::random();
    context.item = Uuid::random();
    if (!metadata.get("migrated").boolean) {
        LslInstance native(program, context, host);
        finish(native, clock);
        const bool followup = metadata.get("after_followup").kind == network::Value::Kind::Map;
        const auto &expected = metadata.get(followup ? "initial" : "final");
        compare_globals(checkpoint(native), source, expected.get("globals"));
        compare_state(checkpoint(native), expected, *code);
        if (followup) {
            const auto saved = decode_lsl_checkpoint(encode_lsl_checkpoint(checkpoint(native)));
            context.object_description = description;
            LslInstance resumed(program, context, host);
            check(saved && resumed.restore_checkpoint(*saved),
                  "Aborted constructor pending phase did not survive native restart");
            const auto writes_before_return = writes;
            for (auto *vm : {&native, &resumed}) {
                check(vm->post_event({"touch_start", {std::int32_t{1}}, {}}),
                      "Return-to-default event was not admitted");
                finish(*vm, clock);
                compare_globals(checkpoint(*vm), source, metadata.get("after_followup").get("globals"));
                compare_state(checkpoint(*vm), metadata.get("after_followup"), *code);
            }
            check(writes == writes_before_return,
                  "Returning to default replayed the completed external object-description guard");
        }
        return;
    }
    const auto wire = read(directory / "snapshot.bin");
    const auto raw = decode_yengine_snapshot(bytes(wire));
    check(raw && code, "Genuine original constructor capture did not decode");
    clock = original_deadline(*raw) + (name == "global_wait_expired" ? 10000000 : -30000000);
    const auto binary = std::make_shared<std::vector<std::uint8_t>>(wire.begin(), wire.end());
    const auto encoded = network::binary_base64(network::osd_from_binary(binary));
    const auto xml = "<ScriptState Engine=\"YEngine\" Asset=\"" + context.item.str() + "\" SourceHash=\"" +
                     metadata.get("source_hash").text + "\"><Running>True</Running><Snapshot>" + encoded +
                     "</Snapshot>" + read(directory / "event-queue.xml") + "</ScriptState>";
    const auto original = scene::read_yengine_state_xml(bytes(xml), context.item);
    check(original && original->snapshot, "Original constructor XML queue did not decode");
    LslInstance imported(program, context, host);
    check(imported.restore_yengine_snapshot(*original->snapshot, original->source_hash, source, *code),
          "Genuine original constructor continuation did not import");
    imported.restore_queued_events(original->events);
    compare_globals(checkpoint(imported), source, metadata.get("after_load").get("globals"), true);
    check(checkpoint(imported).global_initialization_pending.has_value(),
          "Imported constructor did not expose its explicit initialization phase");
    if (name == "global_wait_stop_start") {
        imported.running(false);
        const auto stopped = checkpoint(imported);
        check(!stopped.running && stopped.global_initialization_pending == true && stopped.queued_events.empty(),
              "Stop discarded constructor phase or retained queued events");
        compare_globals(stopped, source, metadata.get("after_stop").get("globals"), true);
        imported.running(true);
        compare_globals(checkpoint(imported), source, metadata.get("after_restart_wait").get("globals"), true);
    }
    const auto saved = checkpoint(imported);
    const auto native_wire = encode_lsl_checkpoint(saved);
    std::map<std::string, std::string> declared_types;
    global_names(source, &declared_types);
    bool original_string_null = false, original_list_null = false;
    for (const auto &[global, value] : metadata.get("after_load").get("globals").map) {
        const auto &type = declared_types.at(global);
        original_string_null |= value.get("type").text == "null" && (type == "string" || type == "key");
        original_list_null |= value.get("type").text == "null" && type == "list";
    }
    const auto expected_version = original_list_null ? 17 : original_string_null ? 16 : 12;
    check(native_wire.size() > 4 && native_wire[3] == expected_version,
          "Constructor checkpoint version differs from its original typed-null witness");
    const auto decoded = decode_lsl_checkpoint(native_wire);
    context.object_description = description;
    LslInstance fresh(program, context, host);
    check(decoded && fresh.restore_checkpoint(*decoded), "Constructor continuation did not survive a fresh native VM");
    for (auto *vm : {&imported, &fresh}) {
        post_return(*vm, name, metadata);
        finish(*vm, clock);
        compare_globals(checkpoint(*vm), source, metadata.get("final").get("globals"));
        compare_state(checkpoint(*vm), metadata.get("final"), *code);
    }
    check(writes == 0 && sleeps == 0,
          "Managed constructor import replayed completed object-description or sleep API effects");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 3, "Original global-event and Scene fixture directories required");
        const std::filesystem::path root = argv[1];
        const auto metadata = network::parse_osd_json(read(root / "metadata.json"));
        check(metadata.array.size() == 12, "Original constructor case count changed without coverage review");
        unsigned verified = 0, boundaries = 0;
        for (const auto &test : metadata.array) {
            const auto name = test.get("name").text;
            if (test.get("incomplete_scene_resource_boundary").boolean ||
                test.get("scheduler_guard_boundary").boolean) {
                check(name == "global_state_change_boundary" || name == "global_reentrant_entry_guard_boundary",
                      "Unexpected original environment boundary");
                ++boundaries;
                continue;
            }
            try {
                fixture(root, test);
            } catch (const std::exception &error) {
                throw std::runtime_error(name + ": " + error.what());
            }
            ++verified;
        }
        check(verified == 10 && boundaries == 2, "Constructor verified/pending scope changed unexpectedly");
        const std::filesystem::path scene_root = argv[2];
        const auto scene = network::parse_osd_json(read(scene_root / "metadata.json"));
        check(scene.array.size() == 6, "Original Scene case count changed without coverage review");
        for (const auto &test : scene.array) {
            check(!test.get("incomplete_scene_resource_boundary").boolean &&
                      !test.get("scheduler_guard_boundary").boolean,
                  "Actual Scene fixture unexpectedly retains an environment boundary");
            try {
                fixture(scene_root, test);
            } catch (const std::exception &error) {
                throw std::runtime_error("Scene/" + test.get("name").text + ": " + error.what());
            }
        }
        std::cout << "PASS: 16 original constructor/Scene flows; 2 baseline environment boundaries remain explicit\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
