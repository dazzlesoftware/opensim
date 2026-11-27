// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/script/yengine_object_code.hpp"
#include "opensim/scene/object_asset_xml.hpp"
#include "opensim/network/osd_json.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
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
    std::ifstream file(path, std::ios::binary);
    check(bool(file), "Missing event-dequeue fixture: " + path.string());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

auto bytes(const std::string &text) {
    return std::span(reinterpret_cast<const std::uint8_t *>(text.data()), text.size());
}

auto compile(const std::string &source) {
    const auto result = compile_lsl(source);
    std::string errors;
    for (const auto &error : result.diagnostics)
        errors += " (" + std::to_string(error.line) + "," + std::to_string(error.column) + ") " + error.message;
    check(bool(result), "Event-dequeue source did not compile:" + errors);
    return result.program;
}

LslContext context() {
    LslContext result;
    result.object = Uuid::random();
    result.owner = Uuid::random();
    result.item = Uuid::random();
    return result;
}

LslCheckpoint checkpoint(LslInstance &instance) {
    const auto result = instance.checkpoint();
    check(bool(result), "Event-dequeue checkpoint unavailable");
    return *result;
}

void step(LslInstance &instance) {
    const auto result = instance.run(1);
    check(result.status != LslRunStatus::Faulted, "Event-dequeue VM faulted: " + result.error);
}

template <class Predicate> void until(LslInstance &instance, Predicate predicate, const char *message) {
    for (unsigned count = 0; count < 2000; ++count) {
        if (predicate(checkpoint(instance)))
            return;
        step(instance);
    }
    throw std::runtime_error(message);
}

std::int32_t integer(const LslValue &value) {
    return std::get<std::int32_t>(unbox_lsl_value(value));
}

const LslList &list(const LslValue &value) {
    const auto result = std::get_if<std::shared_ptr<const LslList>>(&value);
    check(result && *result, "Expected an actual LSL list result");
    return **result;
}

const LslResumeFrameCheckpoint *captured_wait(const LslCheckpoint &state, unsigned ordinal = 0) {
    for (const auto &frame : state.resume_frames)
        if (frame.method == "xmrEventDequeue") {
            if (!ordinal)
                return &frame;
            --ordinal;
        }
    return nullptr;
}

bool captured_background(const LslCheckpoint &state, std::string_view root) {
    const auto *wait = captured_wait(state);
    return state.resume_entry_pending && state.event_contexts.empty() && !state.event_wait && state.active_event &&
           state.active_event->name == root && wait && wait->event_wait && wait->background_event;
}

LslEvent touch(std::string name, int value, const std::string &marker) {
    LslDetector detector;
    detector.name = marker;
    detector.key = Uuid::random();
    return {std::move(name), {std::int32_t{value}}, {detector}};
}

void compare_detector(const LslDetector &actual, const network::Value &expected) {
    check(actual.key.str() == expected.get("key").text && actual.owner.str() == expected.get("owner").text &&
              actual.group.str() == expected.get("group").text && actual.name == expected.get("name").text &&
              actual.type == network::osd_integer(expected.get("type")) &&
              actual.link_number == network::osd_integer(expected.get("link_number")) &&
              actual.touch_face == network::osd_integer(expected.get("touch_face")),
          "Imported background detector lost an original scalar/UUID field");
    const auto vector = [&](const Vector3d &value, const char *field) {
        const auto &components = expected.get(field).array;
        check(components.size() == 3 && value.x == network::osd_real(components[0]) &&
                  value.y == network::osd_real(components[1]) && value.z == network::osd_real(components[2]),
              std::string("Imported background detector differs from exact original component precision: ") + field);
    };
    vector(actual.grab, "grab");
    vector(actual.position, "position");
    vector(actual.velocity, "velocity");
    vector(actual.touch_st, "touch_st");
    vector(actual.touch_normal, "touch_normal");
    vector(actual.touch_binormal, "touch_binormal");
    vector(actual.touch_pos, "touch_pos");
    vector(actual.touch_uv, "touch_uv");
    const auto &rotation = expected.get("rotation").array;
    check(rotation.size() == 4 && actual.rotation.x == network::osd_real(rotation[0]) &&
              actual.rotation.y == network::osd_real(rotation[1]) &&
              actual.rotation.z == network::osd_real(rotation[2]) &&
              actual.rotation.w == network::osd_real(rotation[3]),
          "Imported background detector lost original rotation components");
}

void native_wait() {
    const auto program = compile(R"LSL(
        list result; integer phase; integer effects; string detected;
        float timeout(){effects++;return 5.0;}
        list wait(){return xmrEventDequeue(timeout(),2097152,0,2097152,0);}
        default {state_entry(){phase=1;result=wait();detected=llDetectedName(0);phase=2;}
          touch_start(integer n){} touch_end(integer n){} timer(){} }
    )LSL");
    std::int64_t clock = 638000000000000000LL;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    LslInstance live(program, context(), host);
    until(live, [](const auto &saved) { return saved.event_wait.has_value(); }, "Native wait did not suspend");
    auto saved = checkpoint(live);
    check(integer(saved.globals[1]) == 1 && integer(saved.globals[2]) == 1 && !saved.function.empty() &&
              !saved.call_stack.empty(),
          "Wait did not suspend its caller after evaluating arguments exactly once");
    check(live.post_event({"timer", {}, {}}), "Could not queue unmatched timer");
    check(live.post_event(touch("touch_end", 5, "unmatched marker")), "Could not queue unmatched touch");
    step(live);
    check(checkpoint(live).event_wait.has_value(), "Unmatched event woke a return-mask wait");
    const auto wire = encode_lsl_checkpoint(checkpoint(live));
    const auto decoded = decode_lsl_checkpoint(wire);
    LslInstance restored(program, context(), host);
    check(decoded && restored.restore_checkpoint(*decoded), "Running wait did not survive native codec/fresh VM");
    for (auto *instance : {&live, &restored}) {
        check(instance->post_event(touch("touch_start", 9, "returned marker")), "Could not queue matching touch");
        check(instance->post_event(touch("touch_start", 10, "later marker")), "Could not queue second matching touch");
        until(
            *instance, [](const auto &state) { return !state.event_wait && list(state.globals[0]).items.size() == 2; },
            "Matching event did not resume suspended expression");
        const auto result = checkpoint(*instance);
        const auto &items = list(result.globals[0]).items;
        check(std::holds_alternative<LslBoxedInteger>(items[0]) && integer(items[0]) == 21 && integer(items[1]) == 9 &&
                  result.queued_events.size() == 3 && result.queued_events[0].name == "timer" &&
                  result.queued_events[1].name == "touch_end" && result.queued_events[2].name == "touch_start" &&
                  integer(result.queued_events[2].arguments[0]) == 10 && result.active_event &&
                  result.active_event->name == "state_entry" && result.active_event->arguments.empty() &&
                  result.active_event->detectors.size() == 1 &&
                  result.active_event->detectors.front().name == "returned marker",
              "Return-mask wait lost original event, queue ordering or detector replacement semantics");
        until(
            *instance, [](const auto &state) { return integer(state.globals[1]) == 2; },
            "Caller did not continue after event return");
        const auto done = checkpoint(*instance);
        check(integer(done.globals[2]) == 1 && std::get<std::string>(done.globals[3]) == "returned marker",
              "Wait replayed argument effects or lost returned detector context");
    }
}

void masks_timeout_controls() {
    std::int64_t clock = 638000000000000000LL;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    const auto program = compile(R"LSL(list result;integer phase;default{
        state_entry(){phase=1;result=xmrEventDequeue(2.0,0,16,0,0);phase=2;}
        on_rez(integer n){} timer(){}})LSL");
    LslInstance instance(program, context(), host);
    until(instance, [](const auto &state) { return bool(state.event_wait); }, "Mask2 wait did not suspend");
    check(instance.post_event({"on_rez", {std::int32_t{12}}, {}}), "Could not queue upper-mask event");
    until(
        instance, [](const auto &state) { return list(state.globals[0]).items.size() == 2; },
        "Mask2 event not returned");
    check(integer(list(checkpoint(instance).globals[0]).items[0]) == 36,
          "Upper event mask used an incorrect event-code offset");

    const auto sleeper = compile(R"LSL(integer phase;list result;default{
        state_entry(){phase=1;result=xmrEventDequeue(2.0,0,0,0,0);phase=2;state go;}timer(){}}
        state go{state_entry(){phase=3;}})LSL");
    LslInstance timeout(sleeper, context(), host);
    until(timeout, [](const auto &state) { return bool(state.event_wait); }, "Zero-mask wait did not suspend");
    timeout.post_event({"timer", {}, {}});
    timeout.running(false);
    check(!checkpoint(timeout).running && checkpoint(timeout).queued_events.empty() && checkpoint(timeout).event_wait,
          "Stop did not clear queue while preserving suspended caller");
    clock += 30000000;
    step(timeout);
    check(integer(checkpoint(timeout).globals[0]) == 1, "Stopped wait executed after its deadline");
    timeout.running(true);
    until(
        timeout, [](const auto &state) { return integer(state.globals[0]) == 2; },
        "Start did not resume expired zero-mask wait");
    check(list(checkpoint(timeout).globals[1]).items.empty(), "Timed-out wait did not return an empty list");
    until(
        timeout, [](const auto &state) { return state.state == "go" && integer(state.globals[0]) == 3; },
        "Resumed wait did not permit an ordinary state transition");
    check(!checkpoint(timeout).event_wait, "State transition retained an old builtin wait continuation");
    timeout.reset();
    check(!checkpoint(timeout).event_wait, "Reset retained an old builtin wait continuation");

    const auto expired = compile(R"LSL(list result;default{state_entry(){
        result=xmrEventDequeue(-1.0,2097152,0,2097152,0);}touch_start(integer n){}})LSL");
    LslInstance eager(expired, context(), host);
    check(eager.post_event(touch("touch_start", 9, "eager marker")), "Could not queue initially matching event");
    until(
        eager,
        [](const auto &state) {
            return state.global_initialization_pending == false && list(state.globals[0]).items.size() == 2;
        },
        "Initial matching event did not take precedence over an already expired timeout");
}

void nested_wait_and_global_constructors() {
    const auto program = compile(R"LSL(list result;integer effects;integer phase;
        float timeout(){effects++;return 5.0;}
        default{state_entry(){phase=1;result=xmrEventDequeue(
            (float)llGetListLength(xmrEventDequeue(timeout(),2097152,0,0,0)),536870912,0,0,0);phase=2;}
        touch_start(integer n){} touch_end(integer n){}})LSL");
    std::int64_t clock = 638000000000000000LL;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    const auto identity = context();
    LslInstance inner(program, identity, host);
    until(
        inner, [](const auto &state) { return bool(state.event_wait); }, "Nested inner argument wait did not suspend");
    const auto first = checkpoint(inner);
    check(first.event_wait->return_mask1 == 2097152 && integer(first.globals[1]) == 1,
          "Nested expression suspended the wrong wait or replayed its timeout argument");
    const auto first_wire = decode_lsl_checkpoint(encode_lsl_checkpoint(first));
    LslInstance fresh_inner(program, identity, host);
    check(first_wire && fresh_inner.restore_checkpoint(*first_wire), "Nested inner wait did not survive restart");
    for (auto *vm : {&inner, &fresh_inner}) {
        vm->post_event(touch("touch_start", 7, "inner detector"));
        until(
            *vm, [](const auto &state) { return state.event_wait && state.event_wait->return_mask1 == 536870912; },
            "Completing inner argument did not suspend outer wait");
        const auto second = checkpoint(*vm);
        check(second.event_wait->expression != first.event_wait->expression &&
                  second.event_wait->deadline_utc_ticks == clock + 20000000 && integer(second.globals[1]) == 1,
              "Outer wait lost the inner result, selected the wrong deadline or replayed arguments");
        const auto second_wire = decode_lsl_checkpoint(encode_lsl_checkpoint(second));
        LslInstance fresh_outer(program, identity, host);
        check(second_wire && fresh_outer.restore_checkpoint(*second_wire), "Nested outer wait did not survive restart");
        for (auto *outer : {vm, &fresh_outer}) {
            outer->post_event(touch("touch_end", 9, "outer detector"));
            until(
                *outer, [](const auto &state) { return integer(state.globals[2]) == 2; },
                "Nested outer wait did not resume its caller");
            const auto done = checkpoint(*outer);
            const auto &items = list(done.globals[0]).items;
            check(items.size() == 2 && integer(items[0]) == 29 && integer(items[1]) == 9 &&
                      integer(done.globals[1]) == 1,
                  "Nested waits returned the wrong event or replayed the inner argument");
        }
    }
    // Original globals execute inside the real default-entry event context.
    // Both direct and nested-function waits retain that constructor phase.
    for (const auto *source :
         {"list pending=xmrEventDequeue(1.0,0,0,0,0);default{state_entry(){}}",
          "list pending=helper();list helper(){return xmrEventDequeue(1.0,0,0,0,0);}default{state_entry(){}}"}) {
        const auto global_program = compile(source);
        LslInstance initializing(global_program, identity, host);
        until(
            initializing, [](const auto &state) { return bool(state.event_wait); },
            "Global-initializer wait did not suspend in its entry event");
        const auto global_wait = checkpoint(initializing);
        check(global_wait.global_initialization_pending == true && global_wait.active_event &&
                  global_wait.active_event->name == "state_entry" && global_wait.global_initializer_cursor,
              "Global wait did not retain its constructor phase/cursor");
        const auto global_wire = decode_lsl_checkpoint(encode_lsl_checkpoint(global_wait));
        LslInstance resumed(global_program, identity, host);
        check(global_wire && resumed.restore_checkpoint(*global_wire), "Global wait did not survive native restart");
        clock += 20000000;
        for (auto *vm : {&initializing, &resumed}) {
            until(
                *vm, [](const auto &state) { return state.global_initialization_pending == false; },
                "Global wait did not finish initialization after its deadline");
            check(list(checkpoint(*vm).globals[0]).items.empty(), "Expired global wait returned nonempty data");
        }
    }
    auto invalid = first;
    invalid.global_initializer_cursor = invalid.globals.size();
    LslInstance target(program, identity, host);
    const auto untouched = encode_lsl_checkpoint(checkpoint(target));
    check(!target.restore_checkpoint(invalid) && encode_lsl_checkpoint(checkpoint(target)) == untouched,
          "Out-of-bounds constructor cursor was admitted or mutated the existing VM");
}

void for_update_wait() {
    const auto program = compile(R"LSL(list result;integer body;integer done;
        default{state_entry(){for(;body<1;result=xmrEventDequeue(5.0,2097152,0,0,0)){body++;}done=1;}
        touch_start(integer n){}})LSL");
    std::int64_t clock = 638000000000000000LL;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    const auto identity = context();
    LslInstance live(program, identity, host);
    until(live, [](const auto &state) { return bool(state.event_wait); }, "For-update wait did not suspend");
    const auto saved = checkpoint(live);
    check(integer(saved.globals[1]) == 1 && integer(saved.globals[2]) == 0,
          "For-update wait did not suspend after its loop body");
    const auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(saved));
    LslInstance restored(program, identity, host);
    check(decoded && restored.restore_checkpoint(*decoded), "For-update wait instruction did not survive restart");
    for (auto *vm : {&live, &restored}) {
        vm->post_event(touch("touch_start", 17, "loop detector"));
        until(
            *vm, [](const auto &state) { return integer(state.globals[2]) == 1; },
            "For-update wait did not finish its original loop");
        const auto done = checkpoint(*vm);
        const auto &items = list(done.globals[0]).items;
        check(integer(done.globals[1]) == 1 && items.size() == 2 && integer(items[0]) == 21 && integer(items[1]) == 17,
              "For-update continuation replayed its body or lost the returned event");
    }
}

void background_contexts() {
    const auto program = compile(R"LSL(list result;list nested;integer phase;integer calls;
        integer effects;integer arguments;string seen;
        integer helper(integer n){effects++;llSleep(0.2);return n+1;}
        default{state_entry(){}
          touch_end(integer n){phase=1;result=xmrEventDequeue(10.0,536870912,0,2097152,0);arguments+=n;phase=2;}
          touch_start(integer n){calls++;seen=llDetectedName(0);arguments=n;
            nested=xmrEventDequeue(5.0,0,16,0,0);effects+=helper(n);}
          on_rez(integer n){} timer(){}}
    )LSL");
    std::int64_t clock = 638000000000000000LL;
    unsigned sleep_calls = 0;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](int) { ++sleep_calls; };
    const auto identity = context();
    LslInstance live(program, identity, host);
    step(live);
    check(live.post_event(touch("touch_end", 4, "outer context")), "Could not queue outer event");
    until(
        live, [](const auto &state) { return state.event_wait && state.active_event->name == "touch_end"; },
        "Outer event did not wait for background delivery");
    check(live.post_event({"timer", {}, {}}), "Could not queue FIFO unmatched event");
    check(live.post_event(touch("touch_start", 7, "background context")), "Could not queue background event");
    until(
        live,
        [](const auto &state) {
            const auto *inner = captured_wait(state, 1);
            return captured_background(state, "touch_end") && inner && inner->event_wait &&
                   inner->event_wait->return_mask2 == 16;
        },
        "Background handler did not enter its own nested wait context");
    const auto nested = checkpoint(live);
    // Actual original nested_wait_background_sequence captures the root and
    // two handwritten dequeue frames at this real EventWait yield.
    const auto *outer_wait = captured_wait(nested);
    const auto *inner_wait = captured_wait(nested, 1);
    check(captured_background(nested, "touch_end") && nested.resume_frames.size() == 4 &&
              nested.resume_frames.front().method == "default touch_end" &&
              outer_wait->event_wait->return_mask1 == 536870912 &&
              outer_wait->event_wait->background_mask1 == 2097152 && inner_wait &&
              inner_wait->event_wait->return_mask2 == 16 && integer(nested.active_event->arguments.front()) == 4 &&
              nested.active_event->detectors.front().name == "outer context" &&
              outer_wait->background_event->name == "touch_start" &&
              outer_wait->background_event->arguments.size() == 1 &&
              outer_wait->background_event->detectors.size() == 1 &&
              integer(outer_wait->background_event->arguments.front()) == 7 &&
              outer_wait->background_event->detectors.front().name == "background context" &&
              integer(nested.globals[3]) == 1 && std::get<std::string>(nested.globals[6]) == "background context",
          "Background event did not preserve the original root, selected event and nested wait capture");
    const auto encoded = decode_lsl_checkpoint(encode_lsl_checkpoint(nested));
    LslInstance restored(program, identity, host);
    check(encoded && restored.restore_checkpoint(*encoded), "Nested background wait did not survive restart");
    LslInstance stopped(program, identity, host);
    check(stopped.restore_checkpoint(*encoded), "Could not restore Stop/Start background fixture");
    stopped.post_event({"on_rez", {std::int32_t{12}}, {}});
    stopped.running(false);
    check(captured_background(checkpoint(stopped), "touch_end") && captured_wait(checkpoint(stopped), 1) &&
              checkpoint(stopped).queued_events.empty(),
          "Stop lost suspended background contexts or retained queued events");
    step(stopped);
    check(integer(checkpoint(stopped).globals[4]) == 0, "Stopped background wait continued its helper");
    stopped.running(true);
    LslInstance reset(program, identity, host);
    check(reset.restore_checkpoint(*encoded), "Could not restore Reset background fixture");
    reset.reset();
    check(checkpoint(reset).event_contexts.empty() && !checkpoint(reset).event_wait &&
              checkpoint(reset).resume_frames.empty() && !checkpoint(reset).resume_entry_pending,
          "Reset retained a suspended background event context");
    for (auto *vm : {&live, &restored, &stopped}) {
        const std::size_t expected_unmatched = vm == &stopped ? 0 : 1;
        check(vm->post_event({"on_rez", {std::int32_t{11}}, {}}), "Could not queue nested-wait return event");
        until(
            *vm, [](const auto &state) { return state.sleep_remaining > 0; },
            "Background helper did not suspend in its ordinary sleep");
        const auto asleep = checkpoint(*vm);
        check(captured_background(asleep, "touch_end") && asleep.resume_frames.size() == 4 &&
                  asleep.resume_frames.back().method == "helper:0," && integer(asleep.globals[4]) == 1,
              "Background helper sleep lost caller context or replayed its prefix");
        const auto sleep_wire = decode_lsl_checkpoint(encode_lsl_checkpoint(asleep));
        LslInstance fresh_sleep(program, identity, host);
        check(sleep_wire && fresh_sleep.restore_checkpoint(*sleep_wire), "Sleeping background helper did not restore");
        clock += 3000000;
        const auto before_finish_sleeps = sleep_calls;
        for (auto *asleep_vm : {vm, &fresh_sleep}) {
            until(
                *asleep_vm,
                [](const auto &state) {
                    return state.event_wait && state.event_contexts.empty() && state.active_event->name == "touch_end";
                },
                "Background handler did not resume its suspended outer wait");
            const auto outer = checkpoint(*asleep_vm);
            std::string observed = std::string(vm == &stopped ? " stopped" : " running") +
                                   " phase=" + std::to_string(integer(outer.globals[2])) +
                                   " calls=" + std::to_string(integer(outer.globals[3])) +
                                   " effects=" + std::to_string(integer(outer.globals[4])) +
                                   " event=" + (outer.active_event ? outer.active_event->name : "none") + " arguments=";
            if (outer.active_event)
                for (const auto &argument : outer.active_event->arguments)
                    observed += std::holds_alternative<std::int32_t>(argument) ? std::to_string(integer(argument)) + ","
                                                                               : "noninteger,";
            observed += " detectors=";
            if (outer.active_event)
                for (const auto &detector : outer.active_event->detectors)
                    observed += detector.name + ",";
            observed += " queue=";
            for (const auto &event : outer.queued_events)
                observed += event.name + ",";
            check(integer(outer.globals[3]) == 1 && integer(outer.globals[4]) == 9 && outer.active_event &&
                      outer.active_event->detectors.size() == 1 && outer.active_event->arguments.size() == 1 &&
                      outer.active_event->detectors.front().name == "outer context" &&
                      integer(outer.active_event->arguments.front()) == 4 &&
                      outer.queued_events.size() == expected_unmatched &&
                      (!expected_unmatched || outer.queued_events.front().name == "timer"),
                  "Background return did not restore outer context or FIFO unmatched queue:" + observed);
            check(asleep_vm->post_event(touch("touch_end", 9, "return context")), "Could not queue outer result");
            until(
                *asleep_vm, [](const auto &state) { return integer(state.globals[2]) == 2; },
                "Outer event did not finish after background return");
            const auto done = checkpoint(*asleep_vm);
            check(integer(done.globals[5]) == 11 && list(done.globals[0]).items.size() == 2 &&
                      integer(list(done.globals[0]).items[0]) == 29 && integer(list(done.globals[0]).items[1]) == 9,
                  "Outer event arguments/result were replaced by its background handler");
        }
        check(sleep_calls == before_finish_sleeps, "Restoring sleeping background helper replayed llSleep");
    }
}

void background_state_and_missing_handler() {
    std::int64_t clock = 638000000000000000LL;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    const auto program = compile(R"LSL(integer phase;integer leaked;list pending;
        default{state_entry(){phase=1;pending=xmrEventDequeue(10.0,536870912,0,2097152,0);leaked=1;}
          state_exit(){phase=2;}touch_start(integer n){state other;}touch_end(integer n){}}
        state other{state_entry(){phase=3;}})LSL");
    LslInstance changing(program, context(), host);
    until(
        changing, [](const auto &state) { return bool(state.event_wait); },
        "State-changing outer wait did not suspend");
    changing.post_event(touch("touch_start", 1, "change marker"));
    until(
        changing, [](const auto &state) { return state.state == "other" && integer(state.globals[0]) == 3; },
        "Background state change did not unwind into the new state");
    const auto changed = checkpoint(changing);
    check(changed.event_contexts.empty() && !changed.event_wait && integer(changed.globals[1]) == 0,
          "Background state change returned to its old waiting caller");

    const auto missing = compile(R"LSL(integer phase;integer calls;list pending;
        default{state_entry(){phase=1;pending=xmrEventDequeue(10.0,536870912,0,2097152,0);phase=2;}
          touch_end(integer n){}}
        state other{touch_start(integer n){calls++;}})LSL");
    LslInstance absent(missing, context(), host);
    until(
        absent, [](const auto &state) { return bool(state.event_wait); }, "Missing-handler outer wait did not suspend");
    check(absent.post_event(touch("touch_start", 4, "ignored marker")), "Any-state declaration was not admitted");
    until(
        absent, [](const auto &state) { return state.queued_events.empty(); },
        "Background mask did not consume an event lacking a current-state handler");
    const auto ignored = checkpoint(absent);
    check(ignored.event_wait && ignored.event_contexts.empty() && ignored.active_event->name == "state_entry" &&
              ignored.active_event->detectors.empty() && integer(ignored.globals[1]) == 0,
          "Missing current-state handler changed event context or executed another state's handler");
}

void background_during_state_exit() {
    const auto program = compile(R"LSL(integer phase;integer entries;integer calls;list result;string observed;
        default{state_entry(){state other;}
          state_exit(){phase=1;result=xmrEventDequeue(10.0,536870912,0,2097152,0);phase=2;}
          touch_start(integer n){calls++;llSleep(0.2);}touch_end(integer n){}}
        state other{state_entry(){observed=llDetectedName(0);entries++;phase=3;}})LSL");
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](int) { ++sleeps; };
    const auto identity = context();
    LslInstance live(program, identity, host);
    until(
        live, [](const auto &state) { return state.event_wait && state.active_event->name == "state_exit"; },
        "Old state_exit did not suspend before its pending state transition");
    live.post_event(touch("touch_start", 7, "exit background"));
    until(
        live, [](const auto &state) { return captured_background(state, "state_exit") && state.sleep_remaining > 0; },
        "state_exit did not invoke a sleeping background handler");
    const auto saved = checkpoint(live);
    check(saved.next_state == "other" && saved.active_event->name == "state_exit" && saved.resume_frames.size() == 3 &&
              saved.resume_frames.front().method == "default state_exit" &&
              captured_wait(saved)->background_event->name == "touch_start",
          "Background handler lost the outer state_exit and its pending target state");
    const auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(saved));
    LslInstance restored(program, identity, host);
    check(decoded && restored.restore_checkpoint(*decoded),
          "Sleeping background state_exit context did not survive native restart");
    clock += 3000000;
    for (auto *vm : {&live, &restored}) {
        until(
            *vm,
            [](const auto &state) {
                return state.event_wait && state.event_contexts.empty() && state.active_event->name == "state_exit";
            },
            "Background completion did not return to its outer state_exit");
        vm->post_event(touch("touch_end", 9, "exit return"));
        until(
            *vm, [](const auto &state) { return state.state == "other" && integer(state.globals[0]) == 3; },
            "Completed state_exit did not enter its saved target state");
        const auto done = checkpoint(*vm);
        check(done.event_contexts.empty() && !done.event_wait && done.next_state.empty() &&
                  integer(done.globals[1]) == 1 && integer(done.globals[2]) == 1,
              "Background state_exit restart replayed a handler or lost its pending state transition");
        check(std::get<std::string>(done.globals[4]) == "exit return",
              "New state_entry lost the detector returned by its old state_exit wait");
    }
    check(sleeps == 1, "Restoring background state_exit replayed its completed llSleep call");
}

void background_state_change_detects() {
    const auto program = compile(R"LSL(integer phase;string exited;string entered;list result;
        default{state_entry(){}
          touch_end(integer n){result=xmrEventDequeue(10.0,0,16,2097152,0);}
          touch_start(integer n){state other;}on_rez(integer n){}
          state_exit(){exited=llDetectedName(0);}}
        state other{state_entry(){entered=llDetectedName(0);phase=1;}})LSL");
    std::int64_t clock = 638000000000000000LL;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    LslInstance live(program, context(), host);
    step(live);
    live.post_event(touch("touch_end", 4, "root state-change detector"));
    until(
        live, [](const auto &state) { return bool(state.event_wait); },
        "Detector state-change root handler did not wait");
    live.post_event(touch("touch_start", 7, "background state-change detector"));
    until(
        live, [](const auto &state) { return state.state == "other" && integer(state.globals[0]) == 1; },
        "Detector state-change background handler did not enter the target state");
    const auto done = checkpoint(live);
    check(std::get<std::string>(done.globals[1]) == "root state-change detector" &&
              std::get<std::string>(done.globals[2]) == "root state-change detector" && done.event_contexts.empty() &&
              !done.event_wait,
          "Background state change leaked its detectors into outer state_exit/new state_entry");
}

void background_replaces_pending_exit() {
    for (const auto *replacement : {"third", "default"}) {
        const std::string source =
            R"LSL(integer exits;integer initial;integer otherentries;integer thirdentries;list result;
            default{state_entry(){initial++;if(initial==1)state other;}
              state_exit(){exits++;result=xmrEventDequeue(10.0,536870912,0,2097152,0);}
              touch_start(integer n){state )LSL" +
            std::string(replacement) + R"LSL(;}touch_end(integer n){}}
            state other{state_entry(){otherentries++;}}
            state third{state_entry(){thirdentries++;}})LSL";
        std::int64_t clock = 638000000000000000LL;
        LslHost host;
        host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
        const auto program = compile(source);
        LslInstance live(program, context(), host);
        until(
            live, [](const auto &state) { return state.event_wait && state.active_event->name == "state_exit"; },
            "Pending state_exit replacement did not enter its wait");
        live.post_event(touch("touch_start", 7, "replace target"));
        until(
            live,
            [&](const auto &state) {
                return state.state == replacement &&
                       integer(state.globals[std::string(replacement) == "third" ? 3 : 1]) ==
                           (std::string(replacement) == "third" ? 1 : 2);
            },
            "Background state change did not commit replacement of the pending state_exit target");
        const auto done = checkpoint(live);
        check(integer(done.globals[0]) == 1 && integer(done.globals[2]) == 0 && done.event_contexts.empty() &&
                  !done.event_wait && done.next_state.empty(),
              "Background state change reran old state_exit, entered its abandoned target or retained old frames");
    }
}

void original_fixture(const std::filesystem::path &directory) {
    const auto source = read(directory / "source.lsl");
    const auto state_xml = read(directory / "state.xml");
    const auto metadata = network::parse_osd_json(read(directory / "metadata.json"));
    const auto object = decode_yengine_object_code(bytes(read(directory / "original.yobj")));
    const auto asset = Uuid("11111111-2222-3333-4444-555555555555");
    const auto original = scene::read_yengine_state_xml(bytes(state_xml), asset);
    check(original && original->snapshot && object && original->events.size() >= 1,
          "Original XML queue/detect envelope did not decode");
    const auto &frames = original->snapshot->managed_frames;
    const bool background = directory.filename().string().starts_with("background");
    const auto first_wait = std::find_if(frames.begin(), frames.end(),
                                         [](const auto &frame) { return frame.function == "xmrEventDequeue"; });
    check(first_wait != frames.end() && first_wait->call_no == (background ? 1 : 0) &&
              std::get<YEngineManagedArray>(first_wait->slots->value).size() == 9,
          "Genuine original handwritten wait frame has an unexpected contract");
    // Anchor UTC to the unchanged original deadline, rather than rewriting a
    // saved stack or relying on the date when these fixtures are run.
    const auto &deadline_value =
        metadata.get(background ? "original_outer_deadline_utc_ticks" : "original_deadline_utc_ticks");
    const auto deadline = deadline_value.kind == network::Value::Kind::Long
                              ? deadline_value.long_value
                              : static_cast<std::int64_t>(network::osd_real(deadline_value));
    std::int64_t clock = deadline + (directory.filename() == "precedence" || background ? -10000000 : 10000000);
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    const auto program = compile(source);
    unsigned sleep_calls = 0;
    host.request_sleep = [&](int) { ++sleep_calls; };
    LslInstance instance(program, context(), host);
    const auto initial = encode_lsl_checkpoint(checkpoint(instance));
    auto wrong_call = *original->snapshot;
    const auto wait_index = static_cast<std::size_t>(first_wait - frames.begin());
    wrong_call.managed_frames[wait_index].call_no = 99;
    check(!instance.restore_yengine_snapshot(wrong_call, original->source_hash, source, *object) &&
              encode_lsl_checkpoint(checkpoint(instance)) == initial,
          "Malformed handwritten call number was admitted or mutated the existing VM");
    auto wrong_mask = *original->snapshot;
    auto wrong_slots = std::make_shared<YEngineManagedValue>(*wrong_mask.managed_frames[wait_index].slots);
    auto &slot_values = std::get<YEngineManagedArray>(wrong_slots->value);
    auto combined = std::make_shared<YEngineManagedValue>(*slot_values[1]);
    combined->value = std::get<std::int32_t>(combined->value) | (1 << 2);
    slot_values[1] = combined;
    wrong_mask.managed_frames[wait_index].slots = wrong_slots;
    check(!instance.restore_yengine_snapshot(wrong_mask, original->source_hash, source, *object) &&
              encode_lsl_checkpoint(checkpoint(instance)) == initial,
          "Original return mask outside its saved union was admitted or mutated the existing VM");
    check(instance.restore_yengine_snapshot(*original->snapshot, original->source_hash, source, *object),
          "Verified original handwritten wait chain did not import atomically");
    instance.restore_queued_events(original->events);
    const auto waiting = checkpoint(instance);
    check(waiting.active_event && (!background || !waiting.event_contexts.empty()),
          "Imported wait did not expose its outer and current event contexts");
    const auto &outer = background ? waiting.event_contexts.front().active_event : *waiting.active_event;
    check((waiting.event_wait || !waiting.event_contexts.empty()) && outer.detectors.size() == 1 &&
              outer.detectors.front().name == "outer detect marker",
          "Original wait admission lost outer detector context");
    if (background) {
        check(waiting.active_event->name == "touch_start" && waiting.active_event->detectors.size() == 1,
              "Original background admission did not restore its active handler detector");
        compare_detector(waiting.active_event->detectors.front(), metadata.get("restored_background_detector"));
        check(network::osd_real(metadata.get("source_background_detector").get("touch_st").array[0]) !=
                  network::osd_real(metadata.get("restored_background_detector").get("touch_st").array[0]),
              "Original detector oracle did not exercise touch-vector float narrowing");
    }
    const auto wire = encode_lsl_checkpoint(waiting);
    const auto decoded = decode_lsl_checkpoint(wire);
    LslInstance restored(program, context(), host);
    check(decoded && restored.restore_checkpoint(*decoded), "Imported wait did not survive native restart");
    if (background)
        compare_detector(checkpoint(restored).active_event->detectors.front(),
                         metadata.get("restored_background_detector"));
    for (auto *vm : {&instance, &restored}) {
        const auto &expected = metadata.get("original_resume_result_items").array;
        until(
            *vm,
            [&](const auto &state) {
                return !state.event_wait && state.event_contexts.empty() &&
                       list(state.globals[0]).items.size() == expected.size();
            },
            "Original wait did not resume");
        const auto resumed = checkpoint(*vm);
        const auto &actual = list(resumed.globals[0]).items;
        check(actual.size() == expected.size(), "Original returned list element count differs");
        if (!actual.empty())
            check(std::holds_alternative<LslBoxedInteger>(actual[0]),
                  "Original dequeue event-code result lost boxed LSLInteger identity");
        for (std::size_t index = 0; index < expected.size(); ++index)
            check(integer(actual[index]) == std::stoi(expected[index].get("value").text),
                  "Original returned event code/argument differs");
        check(resumed.queued_events.size() == metadata.get("remaining_queue").array.size(),
              "Original wait consumed unmatched or expired queued events");
        for (std::size_t index = 0; index < resumed.queued_events.size(); ++index)
            check(resumed.queued_events[index].name == metadata.get("remaining_queue").array[index].text,
                  "Original wait reordered preserved queue entries");
        check(resumed.active_event && resumed.active_event->detectors.size() == 1 &&
                  resumed.active_event->detectors.front().name == metadata.get("detect_name_after_resume").text,
              "Original wait returned incorrect detector context");
        until(
            *vm, [](const auto &state) { return integer(state.globals[1]) == 11; },
            "Original caller did not reach its verified final counter");
        check(integer(checkpoint(*vm).globals[2]) == (background ? 7 : 0),
              "Return precedence incorrectly invoked a background handler");
    }
    check(sleep_calls == 0, "Importing an original post-API background frame replayed llSleep");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Original event-dequeue fixture directory required");
        native_wait();
        masks_timeout_controls();
        nested_wait_and_global_constructors();
        for_update_wait();
        background_contexts();
        background_state_and_missing_handler();
        background_during_state_exit();
        background_replaces_pending_exit();
        background_state_change_detects();
        for (const auto *name : {"timeout", "precedence", "expired_return", "background", "background_function",
                                 "background_sleep", "background_wait"}) {
            try {
                original_fixture(std::filesystem::path(argv[1]) / name);
            } catch (const std::exception &error) {
                throw std::runtime_error(std::string(name) + ": " + error.what());
            }
        }
        std::cout << "PASS: event dequeue waits preserve original queue, detector and continuation state\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
