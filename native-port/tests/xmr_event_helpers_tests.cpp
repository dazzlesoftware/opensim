// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/network/osd_json.hpp"
#include "event_helper_source_oracles.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <set>
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
    std::ifstream input(path, std::ios::binary);
    check(bool(input), "Missing original event-helper oracle: " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

auto compile(const std::string &source) {
    const auto result = compile_lsl(source);
    std::string errors;
    for (const auto &error : result.diagnostics)
        errors += " (" + std::to_string(error.line) + "," + std::to_string(error.column) + ") " + error.message;
    check(bool(result), "Event-helper source did not compile:" + errors);
    return result.program;
}

LslContext context() {
    LslContext value;
    value.object = Uuid::random();
    value.owner = Uuid::random();
    value.item = Uuid::random();
    return value;
}

LslCheckpoint snapshot(LslInstance &instance) {
    const auto result = instance.checkpoint();
    check(bool(result), "Event-helper native checkpoint unavailable");
    return *result;
}

template <class Predicate> void until(LslInstance &instance, Predicate predicate, const char *message) {
    for (unsigned step = 0; step < 10000; ++step) {
        if (predicate(snapshot(instance)))
            return;
        const auto progress = instance.run(1);
        check(progress.status != LslRunStatus::Faulted, "Event-helper VM faulted: " + progress.error);
    }
    throw std::runtime_error(message);
}

std::int32_t integer(const LslValue &value) {
    return std::get<std::int32_t>(unbox_lsl_value(value));
}

const LslList &list(const LslValue &value) {
    const auto result = std::get_if<std::shared_ptr<const LslList>>(&value);
    check(result && *result, "Event helper returned a non-list value");
    return **result;
}

void compare(const LslValue &boxed, const network::Value &expected, bool strict_boxing = true) {
    const auto actual = unbox_lsl_value(boxed);
    const auto type = expected.get("type").text;
    const auto &value = expected.get("value");
    const auto clr = expected.get("clr").text;
    if (strict_boxing && clr.ends_with("+LSLInteger"))
        check(std::holds_alternative<LslBoxedInteger>(boxed), "Original helper lost boxed LSLInteger identity");
    if (strict_boxing && clr.ends_with("+LSLFloat"))
        check(std::holds_alternative<LslBoxedFloat>(boxed), "Original helper lost boxed LSLFloat identity");
    if (strict_boxing && clr.ends_with("+LSLString"))
        check(std::holds_alternative<LslBoxedString>(boxed), "Original helper lost boxed LSLString identity");
    if (strict_boxing && clr.ends_with("+key"))
        check(std::holds_alternative<LslBoxedKey>(boxed), "Original helper lost boxed key identity");
    if (strict_boxing && clr == "System.Int32")
        check(std::holds_alternative<std::int32_t>(boxed), "Original helper lost raw CLR Int32 identity");
    if (strict_boxing && clr == "System.Double")
        check(std::holds_alternative<double>(boxed), "Original helper lost raw CLR Double identity");
    if (strict_boxing && clr == "System.String")
        check(std::holds_alternative<std::string>(boxed), "Original helper lost raw CLR String identity");
    if (type == "integer") {
        const auto item = std::get_if<std::int32_t>(&actual);
        check(item && *item == network::osd_integer(value), "Original helper integer value/type differs");
    } else if (type == "float") {
        const auto item = std::get_if<double>(&actual);
        check(item && *item == network::osd_real(value), "Original helper float precision/type differs");
    } else if (type == "string" || type == "key") {
        const auto item = std::get_if<std::string>(&actual);
        check(item && *item == value.text, "Original helper string/key contents differ");
    } else if (type == "vector") {
        const auto item = std::get_if<Vector3d>(&actual);
        check(item && value.array.size() == 3 && item->x == network::osd_real(value.array[0]) &&
                  item->y == network::osd_real(value.array[1]) && item->z == network::osd_real(value.array[2]),
              "Original helper vector precision/type differs");
    } else if (type == "rotation") {
        const auto item = std::get_if<LslRotation>(&actual);
        check(item && value.array.size() == 4 && item->x == network::osd_real(value.array[0]) &&
                  item->y == network::osd_real(value.array[1]) && item->z == network::osd_real(value.array[2]) &&
                  item->w == network::osd_real(value.array[3]),
              "Original helper rotation precision/type differs");
    } else if (type == "list") {
        const auto &items = list(actual).items;
        check(items.size() == value.array.size(), "Original helper list length differs");
        for (std::size_t index = 0; index < items.size(); ++index)
            compare(items[index], value.array[index], strict_boxing);
    } else {
        throw std::runtime_error("Unsupported script-visible original oracle type: " + type);
    }
}

std::optional<LslValue> original_value(const network::Value &node) {
    const auto type = node.get("type").text;
    const auto clr = node.get("clr").text;
    const auto &value = node.get("value");
    if (type == "integer") {
        const auto number = network::osd_integer(value);
        return clr.ends_with("+LSLInteger") ? LslValue(LslBoxedInteger{number}) : LslValue(number);
    }
    if (type == "float") {
        const auto number = network::osd_real(value);
        return clr.ends_with("+LSLFloat") ? LslValue(LslBoxedFloat{number}) : LslValue(number);
    }
    if (type == "key")
        return LslBoxedKey{value.text};
    if (type == "string")
        return clr.ends_with("+LSLString") ? LslValue(LslBoxedString{value.text}) : LslValue(value.text);
    if (type == "vector")
        return Vector3d{network::osd_real(value.array.at(0)), network::osd_real(value.array.at(1)),
                        network::osd_real(value.array.at(2))};
    if (type == "rotation")
        return LslRotation{network::osd_real(value.array.at(0)), network::osd_real(value.array.at(1)),
                           network::osd_real(value.array.at(2)), network::osd_real(value.array.at(3))};
    if (type == "list") {
        auto result = std::make_shared<LslList>();
        for (const auto &element : value.array) {
            auto item = original_value(element);
            if (!item)
                return {};
            result->items.push_back(std::move(*item));
        }
        return LslValue(std::shared_ptr<const LslList>(result));
    }
    // CLR null and Boolean have no corresponding public native LSL value yet.
    return {};
}

std::optional<std::string> literal(const network::Value &node) {
    const auto type = node.get("type").text;
    const auto &value = node.get("value");
    if (type == "integer")
        return std::to_string(network::osd_integer(value));
    if (type == "string" || type == "key")
        return std::string(type == "key" ? "(key)" : "") + network::osd_json(network::Value(value.text));
    const auto number = [](const network::Value &component) {
        std::ostringstream text;
        text << std::setprecision(17) << network::osd_real(component);
        return text.str();
    };
    if (type == "float")
        return "(float)(" + number(value) + ")";
    if (type == "vector" || type == "rotation") {
        std::string text = "<";
        for (const auto &component : value.array) {
            if (text.size() > 1)
                text += ",";
            text += number(component);
        }
        return text + ">";
    }
    if (type == "list") {
        std::string text = "[";
        for (const auto &element : value.array) {
            const auto item = literal(element);
            if (!item)
                return {};
            if (text.size() > 1)
                text += ",";
            text += *item;
        }
        return text + "]";
    }
    // CLR null/Boolean are covered by the unchanged original oracle but have
    // no distinct literal in the supported LSL value model.
    return {};
}

std::vector<LslDetector> original_detectors(const network::Value &oracle);

void original_load_cases(const network::Value &oracle) {
    unsigned covered = 0;
    const std::set<std::string> unrepresentable{"LoadDets_version_SystemBool", "LoadDets_version_null",
                                                "LoadDets_null_uuid"};
    std::set<std::string> omitted;
    for (const auto &test : oracle.get("cases").array) {
        const auto name = test.get("name").text;
        if (!name.starts_with("LoadDets_"))
            continue;
        const auto input = original_value(test.get("input"));
        if (!input) {
            check(unrepresentable.contains(name), "Unexpected unrepresentable original load case: " + name);
            omitted.insert(name);
            continue;
        }
        try {
            auto detectors = original_detectors(oracle);
            bool threw = false;
            try {
                detectors = load_lsl_detector_values(list(*input).items);
            } catch (const std::exception &) {
                threw = true;
            }
            const bool expected_error = test.get("exception").kind == network::Value::Kind::Map;
            check(threw == expected_error, "Production detector load exception outcome differs from original");
            auto after = std::make_shared<LslList>();
            after->items = save_lsl_detector_values(detectors);
            compare(LslValue(std::shared_ptr<const LslList>(after)), test.get("after"));
            ++covered;
        } catch (const std::exception &error) {
            throw std::runtime_error(name + ": " + error.what());
        }
    }
    check(covered == 31 && omitted == unrepresentable,
          "Original detector load coverage changed without an explicit coverage audit");
}

void original_scalar_cast_controls(const network::Value &oracle) {
    const auto baseline = original_value(oracle.get("rich").get("save_result"));
    check(bool(baseline), "Original rich detector baseline is not representable");
    const std::set<std::string> unrepresentable{"ListInt_SystemBool", "ListInt_null", "ListStr_SystemBool",
                                                "ListStr_null"};
    std::set<std::string> omitted;
    unsigned covered = 0;
    for (const auto &test : oracle.get("cases").array) {
        const auto name = test.get("name").text;
        const bool integer_cast = name.starts_with("ListInt_");
        if (!integer_cast && !name.starts_with("ListStr_"))
            continue;
        const auto value = original_value(test.get("input"));
        if (!value) {
            check(unrepresentable.contains(name), "Unexpected unrepresentable original scalar case: " + name);
            omitted.insert(name);
            continue;
        }
        // The production detector decoder invokes the same strict ListInt and
        // ListStr operations for LinkNum and Name respectively.
        auto fields = list(*baseline).items;
        fields[integer_cast ? 3 : 5] = *value;
        bool threw = false;
        std::vector<LslDetector> loaded;
        try {
            loaded = load_lsl_detector_values(fields);
        } catch (const std::exception &) {
            threw = true;
        }
        check(threw == (test.get("exception").kind == network::Value::Kind::Map),
              "Original scalar cast outcome differs in production detector field: " + name);
        if (!threw) {
            check(!loaded.empty(), "Successful scalar cast did not produce its detector");
            compare(integer_cast ? LslValue(loaded[0].link_number) : LslValue(loaded[0].name), test.get("result"));
        }
        ++covered;
    }
    check(covered == 18 && omitted == unrepresentable,
          "Original scalar-control coverage changed without an explicit coverage audit");
}

void rich_detect_capture_restart(const network::Value &oracle) {
    const auto &rich = oracle.get("rich");
    const auto program =
        compile("list before;list returned;list after;integer effects;integer phase;"
                "list input(){effects++;return " +
                rich.get("rich_list_source").text +
                ";}"
                "default{state_entry(){xmrEventLoadDets(input());before=xmrEventSaveDets();"
                "xmrEventEnqueue([21,7]);xmrEventLoadDets([1]);returned=xmrEventDequeue(0.0,2097152,0,0,0);"
                "after=xmrEventSaveDets();phase=1;llSleep(5.0);phase=2;}touch_start(integer n){}}");
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](int) { ++sleeps; };
    const auto identity = context();
    LslInstance live(program, identity, host);
    until(
        live, [](const auto &state) { return state.sleep_remaining > 0; },
        "Rich detector helper sequence did not sleep");
    const auto saved = snapshot(live);
    compare(saved.globals[0], rich.get("load_save_roundtrip"));
    compare(saved.globals[2], rich.get("load_save_roundtrip"));
    check(list(saved.globals[1]).items.size() == 2 && integer(list(saved.globals[1]).items[1]) == 7 &&
              integer(saved.globals[3]) == 1 && sleeps == 1,
          "Enqueued detector capture followed later LoadDets replacement or replayed its argument");
    const auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(saved));
    LslInstance restored(program, identity, host);
    check(decoded && restored.restore_checkpoint(*decoded), "Rich detector helpers did not survive native restart");
    clock += 60000000;
    for (auto *vm : {&live, &restored}) {
        until(
            *vm, [](const auto &state) { return integer(state.globals[4]) == 2; },
            "Rich helper caller did not resume after native restart");
        const auto done = snapshot(*vm);
        compare(done.globals[0], rich.get("load_save_roundtrip"));
        compare(done.globals[2], rich.get("load_save_roundtrip"));
        check(integer(done.globals[3]) == 1, "Rich helper restart replayed its argument");
    }
    check(sleeps == 1, "Rich helper restart replayed a completed sleep");
}

std::vector<LslDetector> original_detectors(const network::Value &oracle) {
    std::vector<LslDetector> detectors;
    for (const auto &value : oracle.get("rich").get("detectors").array) {
        const auto vector = [&](const char *field) {
            const auto &array = value.get(field).array;
            check(array.size() == 3, "Original detector vector oracle is malformed");
            return Vector3d{network::osd_real(array[0]), network::osd_real(array[1]), network::osd_real(array[2])};
        };
        LslDetector detector;
        detector.key = Uuid(value.get("key").text);
        detector.owner = Uuid(value.get("owner").text);
        detector.group = Uuid(value.get("group").text);
        detector.name = value.get("name").text;
        detector.link_number = network::osd_integer(value.get("link_number"));
        detector.type = network::osd_integer(value.get("type"));
        detector.grab = vector("grab");
        detector.position = vector("position");
        detector.velocity = vector("velocity");
        detector.touch_st = vector("touch_st");
        detector.touch_normal = vector("touch_normal");
        detector.touch_binormal = vector("touch_binormal");
        detector.touch_pos = vector("touch_pos");
        detector.touch_uv = vector("touch_uv");
        detector.touch_face = network::osd_integer(value.get("touch_face"));
        const auto &rotation = value.get("rotation").array;
        check(rotation.size() == 4, "Original detector rotation oracle is malformed");
        detector.rotation = {network::osd_real(rotation[0]), network::osd_real(rotation[1]),
                             network::osd_real(rotation[2]), network::osd_real(rotation[3])};
        detectors.push_back(std::move(detector));
    }
    return detectors;
}

void original_save_and_load_precision(const network::Value &oracle) {
    const auto program = compile(R"LSL(list before;list after;integer phase;
        default{state_entry(){}touch_start(integer n){before=xmrEventSaveDets();xmrEventLoadDets(before);
            after=xmrEventSaveDets();phase=1;}})LSL");
    auto detectors = original_detectors(oracle);
    LslInstance instance(program, context(), {});
    check(instance.post_event({"touch_start", {std::int32_t{1}}, std::move(detectors)}),
          "Could not post actual original rich detector inputs");
    until(
        instance, [](const auto &state) { return integer(state.globals[2]) == 1; },
        "Original rich detector Save/Load script did not finish");
    const auto saved = snapshot(instance);
    compare(saved.globals[0], oracle.get("rich").get("save_result"));
    compare(saved.globals[1], oracle.get("rich").get("load_save_roundtrip"));
}

void original_enqueue_cases(const network::Value &oracle) {
    unsigned covered = 0;
    const std::set<std::string> unrepresentable{"Enqueue_null_argument", "Enqueue_code_SystemBool",
                                                "Enqueue_code_null"};
    const std::set<std::string> separate{"Enqueue_per_event_cap", "Enqueue_shallow_alias_and_detect_capture"};
    std::set<std::string> omitted, separately_recorded;
    const auto program = compile(R"LSL(list returned;integer phase;default{
        state_entry(){}touch_start(integer n){phase=1;returned=xmrEventDequeue(600.0,-1,-1,0,0);phase=2;}
        timer(){}attach(key avatar){}}state other{changed(integer flags){}})LSL");
    for (const auto &test : oracle.get("cases").array) {
        const auto name = test.get("name").text;
        if (!name.starts_with("Enqueue_"))
            continue;
        if (test.get("input").kind != network::Value::Kind::Map) {
            check(separate.contains(name), "Unexpected original enqueue case without typed input: " + name);
            separately_recorded.insert(name);
            continue;
        }
        const auto input = original_value(test.get("input"));
        if (!input) {
            check(unrepresentable.contains(name), "Unexpected unrepresentable original enqueue case: " + name);
            omitted.insert(name);
            continue;
        }
        try {
            std::int64_t clock = 638000000000000000LL;
            LslHost host;
            host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
            const auto identity = context();
            LslInstance instance(program, identity, host);
            check(instance.post_event({"touch_start", {std::int32_t{1}}, original_detectors(oracle)}),
                  "Could not enter real detector-bearing handler for direct enqueue");
            until(
                instance, [](const auto &saved) { return bool(saved.event_wait); },
                "Direct enqueue fixture did not pause inside its actual handler");
            if (name.find("minimum_delay") != std::string::npos)
                instance.restore_script_clock(std::int64_t{0}, 1.0);
            bool threw = false;
            try {
                instance.enqueue_script_event_list(list(*input));
            } catch (const std::exception &) {
                threw = true;
            }
            const bool expected_error = test.get("exception").kind == network::Value::Kind::Map;
            check(threw == expected_error, "Production enqueue exception outcome differs from original");
            const auto saved = snapshot(instance);
            const auto &queue = test.get("queue").array;
            check(saved.queued_events.size() == queue.size(), "Production enqueue admission differs from original");
            for (std::size_t event = 0; event < queue.size(); ++event) {
                check(saved.queued_events[event].name == queue[event].get("name").text &&
                          saved.queued_events[event].arguments.size() == queue[event].get("arguments").array.size(),
                      "Production enqueue identity/argument count differs from original");
                for (std::size_t argument = 0; argument < saved.queued_events[event].arguments.size(); ++argument)
                    compare(saved.queued_events[event].arguments[argument],
                            queue[event].get("arguments").array[argument]);
                auto captures = std::make_shared<LslList>();
                captures->items = save_lsl_detector_values(saved.queued_events[event].detectors);
                compare(LslValue(std::shared_ptr<const LslList>(captures)), oracle.get("rich").get("save_result"));
            }
            if (!expected_error) {
                const auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(saved));
                LslInstance restored(program, identity, host);
                check(decoded && restored.restore_checkpoint(*decoded),
                      "Production raw argument payload did not survive native checkpoint");
                if (queue.empty())
                    clock += 6010000000LL;
                for (auto *vm : {&instance, &restored}) {
                    until(
                        *vm, [](const auto &state) { return integer(state.globals[1]) == 2; },
                        "Production-enqueued event did not return through dequeue after restart");
                    if (test.get("returned").kind == network::Value::Kind::Map)
                        compare(snapshot(*vm).globals[0], test.get("returned"));
                    else if (queue.empty())
                        check(list(snapshot(*vm).globals[0]).items.empty(), "Dropped event unexpectedly returned data");
                }
            }
            ++covered;
        } catch (const std::exception &error) {
            throw std::runtime_error(name + ": " + error.what());
        }
    }
    check(covered == 25 && omitted == unrepresentable && separately_recorded == separate,
          "Original enqueue coverage changed without an explicit coverage audit");
}

void dispatch_prefix_rules() {
    for (const auto *arguments : {"", ",\"custom\"", ",7,\"extra\""}) {
        const auto program = compile("integer received;integer phase;default{state_entry(){xmrEventEnqueue([21" +
                                     std::string(arguments) + "]);}touch_start(integer n){received=n;phase=1;}}");
        LslInstance instance(program, context(), {});
        const auto result = instance.run(10000);
        const bool surplus = std::string(arguments).find("extra") != std::string::npos;
        check((result.status == LslRunStatus::Faulted) == !surplus,
              "Helper event dispatch did not preserve original required-prefix argument checks");
        if (surplus) {
            const auto saved = snapshot(instance);
            check(integer(saved.globals[0]) == 7 && integer(saved.globals[1]) == 1,
                  "Surplus helper event arguments prevented valid handler dispatch");
        }
    }
}

void boxed_changed_minimum_delay(const network::Value &oracle) {
    unsigned covered = 0;
    for (const auto &test : oracle.get("cases").array) {
        const auto name = test.get("name").text;
        if (name != "Enqueue_changed_minimum_delay_LSLInteger" &&
            name != "Enqueue_changed_minimum_delay_zeroLSLInteger")
            continue;
        check(test.get("exception").get("type").text == "System.InvalidCastException",
              "Original boxed changed/minimum-delay oracle no longer establishes strict CLR cast failure");
        const auto input = literal(test.get("input"));
        check(bool(input), "Original boxed changed event has no script-visible input literal");
        const auto program = compile("default{state_entry(){llMinEventDelay(1.0);xmrEventEnqueue(" + *input +
                                     ");}}state other{changed(integer flags){}}");
        LslInstance instance(program, context(), {});
        check(instance.run(10000).status == LslRunStatus::Faulted,
              "Actual boxed LSL changed literal bypassed original minimum-delay raw Int32 cast failure: " + name);
        ++covered;
    }
    check(covered == 2, "Required boxed changed-event minimum-delay oracle cases were not exercised");
    // The original raw System.Int32 controls remain in the reference metadata;
    // an ordinary LSL list literal produces LSLInteger rather than that CLR box.
}

void any_state_queue_cap_restart() {
    const auto program = compile(R"LSL(integer phase;integer effects;
        integer argument(integer n){effects++;return n;}
        default{state_entry(){integer n;for(n=0;n<70;n++)xmrEventEnqueue([21,argument(n)]);
            xmrEventEnqueue([2]);xmrEventEnqueue([2]);phase=1;llSleep(5.0);phase=2;}timer(){}}
        state other{touch_start(integer n){}}
    )LSL");
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](int) { ++sleeps; };
    const auto identity = context();
    LslInstance live(program, identity, host);
    until(live, [](const auto &saved) { return saved.sleep_remaining > 0; }, "Queued helper script did not sleep");
    const auto saved = snapshot(live);
    check(saved.queued_events.size() == 65 && integer(saved.globals[0]) == 1 && integer(saved.globals[1]) == 70 &&
              sleeps == 1,
          "Helper enqueue did not preserve any-state admission, per-event bounds or evaluated arguments");
    for (std::size_t index = 0; index < 64; ++index)
        check(saved.queued_events[index].name == "touch_start" &&
                  std::holds_alternative<LslBoxedInteger>(saved.queued_events[index].arguments.front()) &&
                  integer(saved.queued_events[index].arguments.front()) == static_cast<std::int32_t>(index),
              "Helper enqueue changed FIFO order before reaching its per-name limit");
    check(saved.queued_events.back().name == "timer", "Helper enqueue allowed duplicate timer events");
    const auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(saved));
    LslInstance restored(program, identity, host);
    check(decoded && restored.restore_checkpoint(*decoded), "Queued helper events did not survive native restart");
    clock += 60000000;
    for (auto *vm : {&live, &restored}) {
        until(
            *vm, [](const auto &state) { return integer(state.globals[0]) == 2; },
            "Helper caller did not resume after native restart");
        check(integer(snapshot(*vm).globals[1]) == 70, "Native restart replayed helper queue arguments");
    }
    check(sleeps == 1, "Native restart replayed the helper caller's completed sleep");
}

void helpers_in_background_checkpoint() {
    const auto program =
        compile(R"LSL(integer phase;integer effects;list saved;list nested;list result;string observed;
        default{state_entry(){result=xmrEventDequeue(20.0,536870912,0,2097152,0);phase=2;}
          touch_start(integer n){effects++;saved=xmrEventSaveDets();xmrEventLoadDets([1]);
            xmrEventEnqueue([36,9]);nested=xmrEventDequeue(0.0,0,16,0,0);
            xmrEventLoadDets(saved);observed=llDetectedName(0);phase=1;llSleep(5.0);}
          touch_end(integer n){}on_rez(integer n){}}
    )LSL");
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](int) { ++sleeps; };
    const auto identity = context();
    LslInstance live(program, identity, host);
    until(live, [](const auto &state) { return bool(state.event_wait); }, "Helper outer wait did not suspend");
    LslDetector detector;
    detector.name = "helper background marker";
    check(live.post_event({"touch_start", {std::int32_t{7}}, {detector}}), "Could not queue helper background event");
    until(live, [](const auto &state) { return state.sleep_remaining > 0; }, "Helper background handler did not sleep");
    const auto saved = snapshot(live);
    // Genuine helper_background_sequence original capture: entry, handwritten
    // background dequeue, touch_start post-Sleep frame. Globals remain unchanged.
    check(saved.event_contexts.empty() && saved.resume_entry_pending && saved.active_event &&
              saved.active_event->name == "state_entry" && saved.resume_frames.size() == 3 &&
              saved.resume_frames[0].method == "default state_entry" &&
              saved.resume_frames[1].method == "xmrEventDequeue" && saved.resume_frames[1].event_wait &&
              saved.resume_frames[1].event_wait->return_mask1 == 536870912 &&
              saved.resume_frames[1].event_wait->background_mask1 == 2097152 &&
              saved.resume_frames[1].background_event &&
              saved.resume_frames[1].background_event->name == "touch_start" &&
              saved.resume_frames[1].background_event->detectors.size() == 1 &&
              saved.resume_frames[1].background_event->detectors.front().name == "helper background marker" &&
              saved.resume_frames[2].method == "default touch_start" && integer(saved.globals[1]) == 1 &&
              list(saved.globals[2]).items.size() == 17 && list(saved.globals[3]).items.size() == 2 &&
              integer(list(saved.globals[3]).items[0]) == 36 && integer(list(saved.globals[3]).items[1]) == 9 &&
              std::get<std::string>(saved.globals[5]) == "helper background marker" && sleeps == 1,
          "Background helper sequence lost detector/list values or repeated an API prefix");
    const auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(saved));
    LslInstance restored(program, identity, host);
    check(decoded && restored.restore_checkpoint(*decoded), "Helper background checkpoint did not restore");
    clock += 60000000;
    for (auto *vm : {&live, &restored}) {
        until(
            *vm, [](const auto &state) { return state.event_wait && state.event_contexts.empty(); },
            "Helper background did not return to its outer wait");
        check(vm->post_event({"touch_end", {std::int32_t{11}}, {}}), "Could not queue helper outer return event");
        until(
            *vm, [](const auto &state) { return integer(state.globals[0]) == 2; },
            "Helper outer caller did not resume");
        const auto done = snapshot(*vm);
        check(integer(done.globals[1]) == 1 && list(done.globals[4]).items.size() == 2 &&
                  integer(list(done.globals[4]).items[1]) == 11,
              "Helper background restart replayed its prefix or lost its outer result");
    }
    check(sleeps == 1, "Restoring helper background context replayed its sleep");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Original event-helper fixture directory required");
        const auto oracle = network::parse_osd_json(read(std::filesystem::path(argv[1]) / "metadata.json"));
        check(oracle.get("cases").array.size() == 86,
              "Original helper row count changed without an explicit handled/excluded coverage audit");
        event_helper_reference::verify_sources(
            oracle, std::filesystem::path(argv[1]),
            [](const LslValue &actual, const network::Value &expected) { compare(actual, expected); });
        original_load_cases(oracle);
        original_scalar_cast_controls(oracle);
        original_save_and_load_precision(oracle);
        rich_detect_capture_restart(oracle);
        original_enqueue_cases(oracle);
        dispatch_prefix_rules();
        boxed_changed_minimum_delay(oracle);
        any_state_queue_cap_restart();
        helpers_in_background_checkpoint();
        std::cout << "PASS: 74 typed backend rows and 14 source cases; 10 CLR Boolean/null rows explicitly excluded; "
                     "cap control verified; external alias-mutation control remains reference-only; "
                     "native detector replacement, sleep and background restart checks passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
