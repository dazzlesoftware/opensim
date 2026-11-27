// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/script/yengine_object_code.hpp"
#include "opensim/scene/object_asset_xml.hpp"
#include "opensim/framework/invariant_datetime_parse.hpp"
#include "opensim/network/osd_json.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
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
    check(bool(stream), "Missing original direct-handler fixture: " + path.string());
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
    check(bool(result), "Original direct-handler source did not compile:" + errors);
    return result.program;
}

LslCheckpoint checkpoint(LslInstance &instance) {
    const auto result = instance.checkpoint();
    check(bool(result), "Direct-handler checkpoint unavailable");
    return *result;
}

std::vector<std::string> global_names(std::string source) {
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

void compare_value(const LslValue &actual, const network::Value &expected) {
    const auto value = unbox_lsl_value(actual);
    const auto type = expected.get("type").text;
    const auto &oracle = expected.get("value");
    const auto clr = expected.get("clr").text;
    if (clr.ends_with("+LSLInteger"))
        check(std::holds_alternative<LslBoxedInteger>(actual), "Original direct-handler lost boxed integer identity");
    if (clr.ends_with("+LSLString"))
        check(std::holds_alternative<LslBoxedString>(actual), "Original direct-handler lost boxed string identity");
    if (clr.ends_with("+LSLFloat"))
        check(std::holds_alternative<LslBoxedFloat>(actual), "Original direct-handler lost boxed float identity");
    if (clr.ends_with("+key"))
        check(std::holds_alternative<LslBoxedKey>(actual), "Original direct-handler lost true CLR key identity");
    if (clr == "System.Int32")
        check(std::holds_alternative<std::int32_t>(actual), "Original raw integer was reboxed");
    if (clr == "System.Double")
        check(std::holds_alternative<double>(actual), "Original raw double was reboxed");
    if (clr == "System.String")
        check(std::holds_alternative<std::string>(actual), "Original raw string was reboxed");
    if (type == "integer")
        check(std::get<std::int32_t>(value) == network::osd_integer(oracle),
              "Direct-handler integer differs from original: actual=" + std::to_string(std::get<std::int32_t>(value)) +
                  " expected=" + std::to_string(network::osd_integer(oracle)));
    else if (type == "string" || type == "key")
        check(std::get<std::string>(value) == oracle.text, "Direct-handler string differs from original");
    else if (type == "list") {
        const auto &list = std::get<std::shared_ptr<const LslList>>(value);
        check(list && list->items.size() == oracle.array.size(),
              "Direct-handler list length differs from original: actual=" +
                  std::to_string(list ? list->items.size() : 0) + " expected=" + std::to_string(oracle.array.size()));
        for (std::size_t i = 0; i < list->items.size(); ++i)
            compare_value(list->items[i], oracle.array[i]);
    } else if (type == "vector") {
        const auto v = std::get<Vector3d>(value);
        check(v.x == network::osd_real(oracle.array.at(0)) && v.y == network::osd_real(oracle.array.at(1)) &&
                  v.z == network::osd_real(oracle.array.at(2)),
              "Vector differs from original");
    } else if (type == "rotation") {
        const auto v = std::get<LslRotation>(value);
        check(v.x == network::osd_real(oracle.array.at(0)) && v.y == network::osd_real(oracle.array.at(1)) &&
                  v.z == network::osd_real(oracle.array.at(2)) && v.w == network::osd_real(oracle.array.at(3)),
              "Rotation differs from original");
    } else if (type == "float")
        check(std::get<double>(value) == network::osd_real(oracle), "Float differs from original");
    else
        throw std::runtime_error("Unsupported direct-handler oracle type: " + type);
}

void compare_globals(const LslCheckpoint &saved, const std::string &source, const network::Value &expected,
                     bool partial = false, std::string_view phase = {}) {
    const auto names = global_names(source);
    check(names.size() == saved.globals.size() && expected.map.size() == names.size(),
          "Direct-handler global declaration/oracle count differs");
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto found = expected.map.find(names[i]);
        check(found != expected.map.end(), "Original direct-handler omitted global " + names[i]);
        // Original not-yet-initialized CLR reference globals may be null.
        // Native typed storage is allocated first; compare written prefix only.
        if (partial && found->second.get("type").text == "null")
            continue;
        try {
            compare_value(saved.globals[i], found->second);
        } catch (const std::exception &error) {
            throw std::runtime_error(std::string(phase) + "/" + names[i] + ": " + error.what());
        }
    }
}

void compare_state(const LslCheckpoint &saved, const network::Value &expected, const YEngineObjectCode &code) {
    const auto index = network::osd_integer(expected.get("state_index"));
    check(index >= 0 && static_cast<std::size_t>(index) < code.states.size() && saved.state == code.states[index] &&
              saved.global_initialization_pending == expected.get("initialize_globals").boolean,
          "Direct-handler state or pending-initialization phase differs from original");
}

Vector3d vector(const network::Value &v) {
    check(v.array.size() == 3, "Original detector vector is malformed");
    return {network::osd_real(v.array[0]), network::osd_real(v.array[1]), network::osd_real(v.array[2])};
}

std::vector<LslDetector> detectors(const network::Value &values) {
    std::vector<LslDetector> result;
    for (const auto &v : values.array) {
        LslDetector d;
        d.key = Uuid(v.get("key").text);
        d.owner = Uuid(v.get("owner").text);
        d.group = Uuid(v.get("group").text);
        d.name = v.get("name").text;
        d.link_number = network::osd_integer(v.get("link_number"));
        d.type = network::osd_integer(v.get("type"));
        d.touch_face = network::osd_integer(v.get("touch_face"));
        d.grab = vector(v.get("grab"));
        d.position = vector(v.get("position"));
        d.velocity = vector(v.get("velocity"));
        d.touch_pos = vector(v.get("touch_pos"));
        d.touch_normal = vector(v.get("touch_normal"));
        d.touch_binormal = vector(v.get("touch_binormal"));
        d.touch_st = vector(v.get("touch_st"));
        d.touch_uv = vector(v.get("touch_uv"));
        const auto &r = v.get("rotation").array;
        check(r.size() == 4, "Original detector rotation malformed");
        d.rotation = {network::osd_real(r[0]), network::osd_real(r[1]), network::osd_real(r[2]),
                      network::osd_real(r[3])};
        result.push_back(d);
    }
    return result;
}

void compare_detectors(const std::vector<LslDetector> &actual, const network::Value &expected) {
    const auto oracle = detectors(expected);
    // The production serialization includes all sixteen detector fields, including float-narrowed touch vectors.
    const auto a = save_lsl_detector_values(actual), b = save_lsl_detector_values(oracle);
    LslCheckpoint left, right;
    left.globals = a;
    right.globals = b;
    check(encode_lsl_checkpoint(left) == encode_lsl_checkpoint(right),
          "Direct-handler detector state differs from original");
}

void compare_queue(const LslCheckpoint &actual, const network::Value &expected) {
    check(actual.queued_events.size() == expected.array.size(), "Direct call consumed or manufactured a queued event");
    for (std::size_t i = 0; i < actual.queued_events.size(); ++i) {
        const auto &a = actual.queued_events[i];
        const auto &e = expected.array[i];
        check(a.name == e.get("name").text && a.arguments.size() == e.get("arguments").array.size(),
              "Queued event identity changed");
        for (std::size_t j = 0; j < a.arguments.size(); ++j)
            compare_value(a.arguments[j], e.get("arguments").array[j]);
        compare_detectors(a.detectors, e.get("detectors"));
    }
}

struct Outcome {
    LslRunResult run;
    LslCheckpoint last;
};

Outcome run_until_boundary(LslInstance &vm, std::int64_t &clock, bool stop_on_sleep,
                           const std::function<void(LslInstance &)> &on_budget) {
    auto last = checkpoint(vm);
    for (unsigned n = 0; n < 20000; ++n) {
        auto run = vm.run(1);
        // A zero-budget settle may finish an already-executed root handler,
        // but cannot execute the next queued event. The original oracle calls
        // one CallSEH, so independently queued events must remain queued.
        if (run.status != LslRunStatus::Faulted && run.status != LslRunStatus::Sleeping) {
            const auto settled = vm.run(0);
            if (settled.status == LslRunStatus::Faulted)
                run = settled;
            else if (auto saved = vm.checkpoint(); saved && !saved->active_event && saved->function.empty())
                run.status = LslRunStatus::Idle;
        }
        if (auto cp = vm.checkpoint())
            last = *cp;
        if (run.status == LslRunStatus::BudgetExhausted)
            on_budget(vm);
        if (run.status == LslRunStatus::Faulted || run.status == LslRunStatus::Idle)
            return {run, last};
        if (run.status == LslRunStatus::Sleeping) {
            if (stop_on_sleep)
                return {run, last};
            check(!last.event_wait, "Unexpected pending return event in finite original direct-handler fixture");
            clock += last.sleep_remaining + 1;
        }
    }
    throw std::runtime_error("Direct-handler execution exceeded bounded source steps");
}

void expected_fault(const LslRunResult &run, const network::Value &exception) {
    check(run.status == LslRunStatus::Faulted, "Original direct-handler exception did not fault native execution");
    const auto type = exception.get("type").text;
    if (exception.get("message").text.starts_with("frame mismatch"))
        check(run.error.find("frame mismatch") != std::string::npos, "Original frame-mismatch outcome changed");
    else if (type.ends_with("InvalidCastException"))
        check(run.error.find("cast") != std::string::npos, "Original invalid-cast category changed");
    else if (type.ends_with("FormatException"))
        check(run.error.find("format") != std::string::npos || run.error.find("Format") != std::string::npos,
              "Original format category changed");
    else if (type.ends_with("IndexOutOfRangeException"))
        check(run.error.find("range") != std::string::npos || run.error.find("bounds") != std::string::npos,
              "Original index exception category changed");
    else
        throw std::runtime_error("Unreviewed original direct-handler exception: " + type);
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

void fixture(const std::filesystem::path &root, const network::Value &m, const std::vector<LslDetector> &seed) {
    const auto name = m.get("name").text;
    const auto directory = root / name;
    const auto source = read(directory / "source.lsl");
    const auto object_bytes = read(directory / "original.yobj");
    const auto object = decode_yengine_object_code(bytes(object_bytes));
    check(object.has_value(), "Original direct-handler object metadata failed to decode");
    check(yengine_source_matches(source, m.get("source_hash").text),
          "Original direct-handler source provenance mismatch");
    const auto program = compile(source);
    std::int64_t clock = 638000000000000000LL;
    LslHost host;
    host.clock_ticks = [&] { return clock; };
    host.monotonic_ticks = [&] { return clock; };
    std::string description;
    host.set_object_text = [&](Uuid, bool is_description, const std::string &value) {
        check(is_description, "Fixture unexpectedly changed object name");
        description = value;
        return true;
    };
    LslContext context;
    context.object = Uuid::random();
    context.owner = Uuid::random();
    context.item = Uuid::random();
    LslInstance vm(program, context, host);
    auto initial = checkpoint(vm);
    check(initial.queued_events.size() == 1 && initial.queued_events[0].name == "state_entry",
          "Fresh VM lost constructor entry");
    initial.queued_events[0].detectors = seed;
    check(vm.restore_checkpoint(initial), "Initial authentic detector context rejected");
    const auto internal_roundtrip = [&](LslInstance &instance) {
        const auto wire = encode_lsl_checkpoint(checkpoint(instance));
        const auto decoded = decode_lsl_checkpoint(wire);
        LslInstance restored(program, context, host);
        check(decoded && restored.restore_checkpoint(*decoded),
              "Internal instruction-budget pause did not restore transient direct-handler state");
        instance = std::move(restored);
    };
    const bool captured = m.get("suspended").boolean;
    auto outcome = run_until_boundary(vm, clock, captured, internal_roundtrip);
    if (m.get("prefix_resume").kind == network::Value::Kind::Map) {
        check(outcome.run.status == LslRunStatus::Sleeping, "Completed-prefix API did not yield first");
        clock += outcome.last.sleep_remaining + 1;
        outcome = run_until_boundary(vm, clock, true, internal_roundtrip);
    }
    if (!captured) {
        const auto &fault = m.get("initial_exception");
        if (fault.kind == network::Value::Kind::Map)
            expected_fault(outcome.run, fault);
        else
            check(outcome.run.status == LslRunStatus::Idle, "Synchronous direct-handler source did not finish");
        compare_globals(outcome.last, source, m.get("final").get("globals"), false, "native-source execution");
        if (fault.kind != network::Value::Kind::Map) {
            compare_state(outcome.last, m.get("final"), *object);
            compare_queue(outcome.last, m.get("final").get("queue"));
            // Complete calls leave no active event: the detector persistence is also observed through exact source globals.
        }
        return;
    }
    check(outcome.run.status == LslRunStatus::Sleeping, "Original real-yield direct-handler source did not suspend");
    compare_globals(outcome.last, source, m.get("initial").get("globals"), false, "native-source initial real yield");
    const auto wire = encode_lsl_checkpoint(outcome.last);
    const auto cp = decode_lsl_checkpoint(wire);
    LslInstance fresh(program, context, host);
    check(cp && fresh.restore_checkpoint(*cp),
          "Yielded direct-handler state did not survive native checkpoint roundtrip");
    const auto &expected = m.get("final");
    const auto &failure = m.get("resume_exception");
    const auto resume_and_compare = [&](LslInstance &instance, std::string_view phase) {
        const auto &cycles = m.get("migrated_cycles");
        if (cycles.kind == network::Value::Kind::Array && !cycles.array.empty()) {
            for (const auto &cycle : cycles.array) {
                const auto prior = checkpoint(instance);
                clock += prior.sleep_remaining + 1;
                const auto resumed = run_until_boundary(instance, clock, true, internal_roundtrip);
                const auto &state = cycle.get("state");
                const auto &error = cycle.get("exception");
                if (error.kind == network::Value::Kind::Map)
                    expected_fault(resumed.run, error);
                else
                    check(resumed.run.status ==
                              (state.get("frames").array.empty() ? LslRunStatus::Idle : LslRunStatus::Sleeping),
                          "Original repeated scheduler slice outcome differs");
                compare_globals(resumed.last, source, state.get("globals"), false, phase);
            }
        } else {
            // Managed capture resumes beyond its completed original sleep and
            // has no native sleep remainder. Advance only this instance's own
            // deadline, never the unrelated native-source control's duration.
            clock += checkpoint(instance).sleep_remaining + 1;
            const auto resumed = run_until_boundary(instance, clock, false, internal_roundtrip);
            if (failure.kind == network::Value::Kind::Map)
                expected_fault(resumed.run, failure);
            else
                check(resumed.run.status == LslRunStatus::Idle,
                      "Real-yield direct-handler continuation did not complete");
            compare_globals(resumed.last, source, expected.get("globals"), false, phase);
        }
    };
    resume_and_compare(vm, "native-live real-yield resume");
    resume_and_compare(fresh, "native-codec fresh-VM resume");
    // Independently exercise the genuine original object/capture/XML admission path. Never fabricate managed frame IDs.
    const auto raw_bytes = read(directory / "snapshot.bin");
    const auto raw = decode_yengine_snapshot(bytes(raw_bytes));
    check(raw.has_value(), "Original direct-handler capture did not decode");
    clock = original_deadline(*raw) + (name == "wait" ? 1 : -10000000);
    const auto binary = std::make_shared<std::vector<std::uint8_t>>(raw_bytes.begin(), raw_bytes.end());
    const auto encoded = network::binary_base64(network::osd_from_binary(binary));
    const auto xml = "<ScriptState Engine=\"YEngine\" Asset=\"" + context.item.str() + "\" SourceHash=\"" +
                     m.get("source_hash").text + "\"><Running>True</Running><Snapshot>" + encoded + "</Snapshot>" +
                     read(directory / "event-queue.xml") + "</ScriptState>";
    const auto managed = scene::read_yengine_state_xml(bytes(xml), context.item);
    check(managed && managed->snapshot, "Original direct-handler XML admission failed");
    LslInstance imported(program, context, host);
    const bool accepted = imported.restore_yengine_snapshot(*managed->snapshot, managed->source_hash, source, *object);
    check(accepted, "Original MigrateIn accepted this frame chain; native import must accept it before execution");
    imported.restore_queued_events(managed->events);
    compare_globals(checkpoint(imported), source, m.get("after_load").get("globals"), true,
                    "original managed import before execution");
    resume_and_compare(imported, "original managed import resume");
}

void timeslice_fixture(const std::filesystem::path &root, const network::Value &m,
                       const std::vector<LslDetector> &seed) {
    const auto directory = root / m.get("name").text;
    const auto source = read(directory / "source.lsl");
    const auto program = compile(source);
    const auto object_bytes = read(directory / "original.yobj");
    const auto object = decode_yengine_object_code(bytes(object_bytes));
    check(object && yengine_source_matches(source, m.get("source_hash").text),
          "Timeslice source/object provenance mismatch");
    check(m.get("forced_cutoff_count").integer == 1 && m.get("cutoff_observed_count").integer == 8,
          "Original forced cutoff observation changed");
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](int) { ++sleeps; };
    LslContext context;
    context.object = Uuid::random();
    context.owner = Uuid::random();
    context.item = Uuid::random();
    LslInstance vm(program, context, host);
    auto initial = checkpoint(vm);
    initial.queued_events.at(0).detectors = seed;
    const auto &queued = m.get("initial").get("queue").array;
    check(queued.size() == 1 && queued[0].get("name").text == "touch_end", "Original independent event changed");
    initial.queued_events.push_back(
        {"touch_end",
         {LslBoxedInteger{network::osd_integer(queued[0].get("arguments").array.at(0).get("value"))}},
         detectors(queued[0].get("detectors"))});
    check(vm.restore_checkpoint(initial), "Timeslice initial context rejected");
    const auto names = global_names(source);
    const auto count = std::ranges::find(names, "count");
    check(count != names.end(), "Timeslice counter missing");
    const auto slot = static_cast<std::size_t>(count - names.begin());
    bool reached = false;
    for (unsigned i = 0; i < 1000; ++i) {
        const auto run = vm.run(1);
        check(run.status != LslRunStatus::Faulted && run.status != LslRunStatus::Sleeping,
              "No-sleep timeslice source faulted/slept before cutoff");
        const auto saved = checkpoint(vm);
        if (std::get<std::int32_t>(saved.globals.at(slot)) == 8) {
            reached = true;
            break;
        }
        // Internal CPU budget boundaries retain the ordinary direct caller.
        const auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(saved));
        LslInstance fresh(program, context, host);
        check(decoded && fresh.restore_checkpoint(*decoded), "Timeslice internal-budget continuation failed roundtrip");
        vm = std::move(fresh);
    }
    check(reached, "Source did not reach the original NORMAL loop-check counter");
    clock += 610000; // .NET tick units: advance61ms once, without sleeping.
    const auto before_cutoff = checkpoint(vm);
    check(before_cutoff.execution_slice_elapsed_ticks == 610000,
          "Internal instruction budgets reset the elapsed source quantum");
    const auto copied = decode_lsl_checkpoint(encode_lsl_checkpoint(before_cutoff));
    LslInstance before_fresh(program, context, host);
    check(copied && before_fresh.restore_checkpoint(*copied), "Pending61ms source slice did not restore");
    const auto &failure = m.get("resume_exception");
    const auto resume = [&](LslInstance &instance, std::string_view phase, bool managed = false) {
        const auto outcome = run_until_boundary(instance, clock, false, [](LslInstance &) {});
        if (failure.kind == network::Value::Kind::Map)
            expected_fault(outcome.run, failure);
        else
            check(outcome.run.status == LslRunStatus::Idle, "Real quantum continuation did not finish");
        compare_globals(outcome.last, source, m.get("final").get("globals"), false, phase);
        // Native codec keeps rich detector fields; original EventQueue XML
        // omits touch data, as the separate actual migrated oracle records.
        compare_queue(outcome.last, m.get(managed ? "final" : "live_resume").get("queue"));
    };
    for (auto *instance : {&vm, &before_fresh}) {
        const auto cutoff = instance->run(1000);
        check(cutoff.status == LslRunStatus::BudgetExhausted,
              "Elapsed61ms did not produce a real source quantum cutoff");
        const auto saved = checkpoint(*instance);
        check(!saved.execution_slice_elapsed_ticks, "Actual quantum cutoff retained the expired source slice");
        compare_globals(saved, source, m.get("initial").get("globals"), false, "actual native quantum cutoff");
        compare_queue(saved, m.get("initial").get("queue"));
        check(saved.active_event.has_value(), "Quantum cutoff discarded called event");
        compare_detectors(saved.active_event->detectors, m.get("initial").get("detectors"));
        const auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(saved));
        LslInstance fresh(program, context, host);
        check(decoded && fresh.restore_checkpoint(*decoded), "Captured real quantum failed fresh VM restore");
        resume(*instance, "native real quantum resume");
        resume(fresh, "native quantum codec fresh-VM resume");
    }
    const auto raw_bytes = read(directory / "snapshot.bin");
    const auto raw = decode_yengine_snapshot(bytes(raw_bytes));
    check(raw.has_value(), "Original quantum capture did not decode");
    const auto binary = std::make_shared<std::vector<std::uint8_t>>(raw_bytes.begin(), raw_bytes.end());
    const auto encoded = network::binary_base64(network::osd_from_binary(binary));
    const auto xml = "<ScriptState Engine=\"YEngine\" Asset=\"" + context.item.str() + "\" SourceHash=\"" +
                     m.get("source_hash").text + "\"><Running>True</Running><Snapshot>" + encoded + "</Snapshot>" +
                     read(directory / "event-queue.xml") + "</ScriptState>";
    const auto original = scene::read_yengine_state_xml(bytes(xml), context.item);
    check(original && original->snapshot, "Original quantum XML failed");
    LslInstance imported(program, context, host);
    check(imported.restore_yengine_snapshot(*original->snapshot, original->source_hash, source, *object),
          "Original MigrateIn-accepted quantum frame chain rejected");
    imported.restore_queued_events(original->events);
    resume(imported, "original quantum managed import resume", true);
    check(sleeps == 0, "Real source quantum incorrectly requested API sleep");
}

} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 6,
              "Original baseline, extended, scheduler, composition and timeslice fixture directories required");
        const std::filesystem::path baseline = argv[1], extended = argv[2], scheduler = argv[3], composition = argv[4],
                                    timeslice = argv[5];
        const auto scheduler_case =
            network::parse_osd_json(read(scheduler / "same_handler_recursive" / "metadata.json"));
        check(scheduler_case.get("migrated_cycles").array.size() == 3, "Original repeated-sleep evidence changed");
        const auto normal = network::parse_osd_json(read(baseline / "normal" / "metadata.json"));
        const auto seed = detectors(normal.get("final").get("detectors"));
        unsigned verified = 0, controls = 0;
        for (const auto &[root, count] :
             {std::pair{baseline, 23u}, std::pair{extended, 7u}, std::pair{composition, 6u}}) {
            const auto cases = network::parse_osd_json(read(root / "metadata.json"));
            check(cases.array.size() == count, "Original direct-handler case inventory changed");
            for (const auto &m : cases.array) {
                const auto name = m.get("name").text;
                // This explicit original control suppresses mandatory sleep suspension; it is not a production scheduler oracle.
                // Reset is recorded at CallSEH before the original engine ResetLocked handler, so it is not a completed-reset oracle.
                if (name == "sleep_uninterrupted" || m.get("direct_reset_scheduler_boundary").boolean) {
                    ++controls;
                    continue;
                }
                try {
                    if (name == "same_handler_recursive") {
                        // The old one-shot control disabled subsequent sleep captures and reached306.
                        // Actual base CheckRunWork/ResumeEx instead yields6,9,12 in three bounded slices.
                        check(read(root / name / "source.lsl") == read(scheduler / name / "source.lsl"),
                              "Real scheduler substitution changed source");
                        fixture(scheduler, scheduler_case, seed);
                    } else
                        fixture(root, m, seed);
                } catch (const std::exception &e) {
                    throw std::runtime_error(name + ": " + e.what());
                }
                ++verified;
            }
        }
        const auto quantum = network::parse_osd_json(read(timeslice / "metadata.json"));
        check(quantum.array.size() == 2, "Original quantum case inventory changed");
        for (const auto &m : quantum.array) {
            try {
                timeslice_fixture(timeslice, m, seed);
            } catch (const std::exception &error) {
                throw std::runtime_error(m.get("name").text + ": " + error.what());
            }
            ++verified;
        }
        check(verified == 36 && controls == 2, "Direct-handler verified/control scope changed");
        std::cout
            << "PASS: 36 original direct-handler source/capture/composition/quantum outcomes; 2 explicit scheduler "
               "controls excluded\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
