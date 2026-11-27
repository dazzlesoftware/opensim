// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include "opensim/script/yengine_object_code.hpp"
#include "opensim/scene/object_asset_xml.hpp"
#include <algorithm>
#include <bit>
#include <iostream>
#include <memory>
using namespace utf16_test;

namespace {
std::span<const std::uint8_t> bytes(const std::string &s) {
    return {reinterpret_cast<const std::uint8_t *>(s.data()), s.size()};
}

std::string bits(double n) {
    constexpr char hex[] = "0123456789abcdef";
    auto nbits = std::bit_cast<std::uint64_t>(n);
    std::string result(16, '0');
    for (unsigned i = 0; i < 16; ++i) {
        result[15 - i] = hex[nbits & 15];
        nbits >>= 4;
    }
    return result;
}

void value(const LslValue &actual, const network::Value &expected, const std::string &where) {
    const auto type = expected.get("type").text;
    if (type == "list") {
        const auto *list = std::get_if<std::shared_ptr<const LslList>>(&actual);
        check(list && *list && (*list)->items.size() == expected.get("value").array.size(),
              where + ": list structure differs (variant=" + std::to_string(actual.index()) +
                  ", actual size=" + std::to_string(list && *list ? (*list)->items.size() : 0) +
                  ", expected size=" + std::to_string(expected.get("value").array.size()) + ")");
        for (std::size_t i = 0; i < (*list)->items.size(); ++i)
            value((*list)->items[i], expected.get("value").array[i], where + "[" + std::to_string(i) + "]");
    } else if (type == "vector" || type == "rotation") {
        std::vector<double> components;
        if (type == "vector") {
            const auto *v = std::get_if<framework::Vector3d>(&actual);
            check(v != nullptr, where + ": vector type differs");
            components = {v->x, v->y, v->z};
        } else {
            const auto *v = std::get_if<LslRotation>(&actual);
            check(v != nullptr, where + ": rotation type differs");
            components = {v->x, v->y, v->z, v->w};
        }
        const auto &wanted = expected.get("value").array;
        check(wanted.size() == components.size(), where + ": component count differs");
        for (std::size_t i = 0; i < wanted.size(); ++i)
            check(bits(components[i]) == wanted[i].get("bits").text,
                  where + ": exact component bits differ at" + std::to_string(i) + " actual=" + bits(components[i]) +
                      " expected=" + wanted[i].get("bits").text);
    } else
        compare_value(actual, expected, where, type == "string" && expected.get("clr").text == "System.String");
}

void globals(const LslCheckpoint &saved, const std::string &source, const network::Value &expected) {
    const auto names = global_names(source);
    check(names.size() == saved.globals.size() && names.size() == expected.map.size(),
          "BG identity global count differs");
    for (std::size_t i = 0; i < names.size(); ++i)
        value(saved.globals[i], expected.get(names[i]), names[i]);
}

LslCheckpoint checkpoint(LslInstance &vm) {
    auto saved = vm.checkpoint();
    check(bool(saved), "BG identity checkpoint missing");
    return *saved;
}

void roundtrip(std::unique_ptr<LslInstance> &vm, const std::shared_ptr<const LslProgram> &program,
               const LslContext &context, const LslHost &host) {
    const auto wire = encode_lsl_checkpoint(checkpoint(*vm));
    const auto decoded = decode_lsl_checkpoint(wire);
    check(decoded && encode_lsl_checkpoint(*decoded) == wire, "Budget-only BG checkpoint changed bytes");
    auto fresh = std::make_unique<LslInstance>(program, context, host);
    check(fresh->restore_checkpoint(*decoded), "Fresh VM rejected BG identity checkpoint");
    vm = std::move(fresh);
}

void fixture(const std::filesystem::path &root, const network::Value &oracle) {
    const auto folder = root / oracle.get("name").text;
    const auto source = read(folder / "source.lsl");
    check(oracle.get("accepted").boolean && oracle.get("suspended").boolean && oracle.get("migrated").boolean &&
              yengine_source_matches(source, oracle.get("source_hash").text),
          "Original BG identity source/outcome differs");
    for (const auto *name :
         {"source.lsl", "original.yobj", "original.il.txt", "snapshot.bin", "event-queue.xml", "detect-array.xml"})
        check(framework::sha256_hex(read(folder / name)) == oracle.get("sha256").get(name).text,
              "Original BG identity artifact hash differs");
    for (const auto *name :
         {"initial_exception", "production_write_exception", "live_resume_exception", "resume_exception"})
        check(oracle.get(name).kind != network::Value::Kind::Map, "Original BG identity reference failed");
    const auto compiled = compile_lsl(source);
    std::string errors;
    for (const auto &d : compiled.diagnostics)
        errors += " " + d.message;
    check(bool(compiled), "Original BG identity source failed compile:" + errors);
    std::int64_t clock = 638000000000000000LL;
    for (const auto &frame : oracle.get("initial").get("frames").array)
        if (frame.get("function").text == "xmrEventDequeue") {
            const auto &deadline = frame.get("wait_deadline_utc_ticks");
            clock =
                (deadline.kind == network::Value::Kind::Long ? deadline.long_value
                                                             : static_cast<std::int64_t>(network::osd_real(deadline))) -
                3000000000LL;
            break;
        }
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    unsigned sleeps = 0;
    host.request_sleep = [&](int ms) {
        check(ms == 1000, "BG identity sleep duration differs");
        ++sleeps;
    };
    LslContext context;
    context.object = framework::Uuid::random();
    context.item = framework::Uuid::random();
    auto native = std::make_unique<LslInstance>(compiled.program, context, host);
    bool suspended = false;
    bool checked_legacy_source_yield = false;
    for (unsigned i = 0; i < 6000; ++i) {
        const auto result = native->run(1);
        check(result.status != LslRunStatus::Faulted, "BG identity native prefix faulted: " + result.error);
        const auto budget_state = checkpoint(*native);
        std::size_t selected_index = 0;
        for (const auto &outer : budget_state.event_contexts) {
            if (outer.kind != LslEventContextKind::DequeueBackground)
                continue;
            check(outer.invoked_event.has_value(), "BG budget state lost the selected event identity");
            std::vector<const network::Value *> original_waits;
            for (const auto &frame : oracle.get("initial").get("frames").array)
                if (frame.get("function").text == "xmrEventDequeue")
                    original_waits.push_back(&frame);
            check(selected_index < original_waits.size(), "Native BG chain has no original selected-event witness");
            const auto &slot = original_waits[selected_index++]->get("slot_values").array.at(7);
            auto expected_selected = slot;
            expected_selected.map["type"] = network::Value("list");
            auto selected_values =
                std::make_shared<const LslList>(LslList{save_lsl_detector_values(outer.invoked_event->detectors)});
            value(LslValue{selected_values}, expected_selected, "selected BG budget detector");
        }
        if (!budget_state.event_contexts.empty()) {
            auto legacy = budget_state;
            bool removed_identity = false;
            for (auto &outer : legacy.event_contexts)
                if (outer.kind == LslEventContextKind::DequeueBackground) {
                    outer.invoked_event.reset();
                    removed_identity = true;
                }
            if (removed_identity) {
                auto legacy_host = host;
                legacy_host.request_sleep = [](int) {};
                LslInstance old_native(compiled.program, context, legacy_host);
                check(old_native.restore_checkpoint(legacy), "Legacy BG checkpoint reader was rejected");
                const auto old_live = encode_lsl_checkpoint(checkpoint(old_native));
                const auto old_effects = sleeps;
                const auto preserved = old_native.persistent_checkpoint();
                check(preserved && encode_lsl_checkpoint(*preserved) == old_live &&
                          encode_lsl_checkpoint(checkpoint(old_native)) == old_live && sleeps == old_effects,
                      "Legacy BG state was dropped, guessed or mutated at the persistence boundary");
                if (!checked_legacy_source_yield) {
                    bool old_suspended = false;
                    for (unsigned step = 0; step < 2000; ++step) {
                        const auto progress = old_native.run(1);
                        check(progress.status != LslRunStatus::Faulted,
                              "Legacy BG source continuation faulted: " + progress.error);
                        if (progress.status == LslRunStatus::Sleeping) {
                            old_suspended = true;
                            break;
                        }
                        check(progress.status == LslRunStatus::BudgetExhausted,
                              "Legacy BG source continuation did not reach its real yield");
                    }
                    const auto old_yield = checkpoint(old_native);
                    check(old_suspended && !old_yield.event_contexts.empty() &&
                              std::ranges::any_of(old_yield.event_contexts,
                                                  [](const auto &outer) {
                                                      return outer.kind == LslEventContextKind::DequeueBackground &&
                                                             !outer.invoked_event;
                                                  }) &&
                              old_yield.resume_frames.empty(),
                          "Legacy real source yield fabricated a selected BG frame or dropped its raw context");
                    const auto old_projection = old_native.persistent_checkpoint();
                    check(old_projection && encode_lsl_checkpoint(*old_projection) == encode_lsl_checkpoint(old_yield),
                          "Legacy source-yield state was omitted or guessed during persistence");
                    checked_legacy_source_yield = true;
                }
                // The old wire never stored the selected event separately. This
                // assertion preserves data and makes no recovered-identity claim.
            }
        }
        const auto before_projection = encode_lsl_checkpoint(budget_state);
        const auto effect_count = sleeps;
        const auto projected = native->persistent_checkpoint();
        check(projected && projected->event_contexts.empty() &&
                  encode_lsl_checkpoint(checkpoint(*native)) == before_projection && sleeps == effect_count,
              "Persistent source-frame projection retained raw contexts, changed the live VM or invoked a host API");
        roundtrip(native, compiled.program, context, host);
        if (result.status == LslRunStatus::Sleeping) {
            suspended = true;
            break;
        }
        check(result.status == LslRunStatus::BudgetExhausted, "BG identity failed to reach original real-source yield");
    }
    check(suspended && sleeps == 1, "BG identity sleep replayed or disappeared");
    check(checked_legacy_source_yield, "Legacy BG real-source-yield coverage was not reached");
    const auto saved = checkpoint(*native);
    globals(saved, source, oracle.get("initial").get("globals"));
    check(saved.resume_entry_pending && !saved.resume_frames.empty(),
          "Real BG source yield did not capture original frame chain");
    std::vector<std::unique_ptr<LslInstance>> instances;
    instances.push_back(std::move(native));
    const auto projected = instances.front()->persistent_checkpoint();
    check(bool(projected), "Persistent BG source-frame projection missing");
    const auto persistent = decode_lsl_checkpoint(encode_persistent_lsl_checkpoint(*projected));
    auto transferred = std::make_unique<LslInstance>(compiled.program, context, host);
    check(persistent && transferred->restore_checkpoint(*persistent), "Persistent BG identity state did not restore");
    globals(checkpoint(*transferred), source, oracle.get("after_load").get("globals"));
    instances.push_back(std::move(transferred));
    const auto raw = read(folder / "snapshot.bin");
    const auto xml =
        "<ScriptState Engine=\"YEngine\" Asset=\"" + context.item.str() + "\" SourceHash=\"" +
        oracle.get("source_hash").text + "\"><Running>True</Running><Snapshot>" +
        network::binary_base64(
            network::osd_from_binary(std::make_shared<std::vector<std::uint8_t>>(raw.begin(), raw.end()))) +
        "</Snapshot>" + read(folder / "detect-array.xml") + read(folder / "event-queue.xml") + "</ScriptState>";
    const auto original = scene::read_yengine_state_xml(bytes(xml), context.item);
    const auto object = decode_yengine_object_code(bytes(read(folder / "original.yobj")));
    auto imported = std::make_unique<LslInstance>(compiled.program, context, host);
    check(original && original->snapshot && object &&
              imported->restore_yengine_snapshot(*original->snapshot, original->source_hash, source, *object),
          "Genuine original BG identity import rejected");
    imported->restore_queued_events(original->events);
    instances.push_back(std::move(imported));
    clock += 20000000;
    for (std::size_t path = 0; path < instances.size(); ++path) {
        bool done = false;
        for (unsigned i = 0; i < 6000; ++i) {
            const auto result = instances[path]->run(1);
            check(result.status != LslRunStatus::Faulted,
                  "BG identity resume fault path" + std::to_string(path) + ": " + result.error);
            roundtrip(instances[path], compiled.program, context, host);
            if (result.status == LslRunStatus::Sleeping)
                clock += 20000000;
            else if (result.status == LslRunStatus::Idle) {
                done = true;
                break;
            } else
                check(result.status == LslRunStatus::BudgetExhausted, "Unexpected BG identity resume status");
        }
        check(done, "BG identity resume exceeded bound");
        const auto &expected = oracle.get(path == 0 ? "live_resume" : "final");
        try {
            globals(checkpoint(*instances[path]), source, expected.get("globals"));
        } catch (const std::exception &error) {
            throw std::runtime_error("resume path " + std::to_string(path) + ": " + error.what());
        }
        const auto effects = sleeps;
        check(instances[path]->run(1).status == LslRunStatus::Idle && sleeps == effects,
              "Completed BG identity state replayedAPI");
    }
    check(sleeps == 1, "BG source/import replayed completed Sleep API");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Original BG detector identity fixture required");
        const std::filesystem::path root = argv[1];
        const auto rows = network::parse_osd_json(read(root / "metadata.json"));
        check(rows.array.size() == 7, "Original BG identity inventory changed");
        for (const auto &row : rows.array)
            try {
                fixture(root, row);
            } catch (const std::exception &e) {
                throw std::runtime_error(row.get("name").text + ": " + e.what());
            }
        std::cout << "PASS:7 original BG detector cases,live/internal/persistent/original-import differential "
                     "paths,exact component bits and source replay counters\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
