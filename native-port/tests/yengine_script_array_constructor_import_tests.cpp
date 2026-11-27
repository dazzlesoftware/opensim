// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include "opensim/script/lsl_script_class.hpp"
#include "opensim/script/yengine_object_code.hpp"
#include <iostream>

using namespace utf16_test;

namespace {
using Class = std::shared_ptr<const LslScriptClass>;
using Array = std::shared_ptr<const LslFixedArray>;

std::span<const std::uint8_t> bytes(const std::string &value) {
    return {reinterpret_cast<const std::uint8_t *>(value.data()), value.size()};
}

LslCheckpoint checkpoint(const LslInstance &instance) {
    const auto saved = instance.checkpoint();
    check(saved.has_value(), "Constructor argument checkpoint missing");
    return *saved;
}

void shape(const Class &value) {
    check(value && value->descriptor && value->descriptor->canonical_name == "integer[]" &&
              value->descriptor->element_type == "integer" && value->descriptor->array_rank == 1 &&
              value->arrays_heap_usage == 20 && value->instance_arrays[3].size() == 1 &&
              std::get<std::int32_t>(value->instance_arrays[3][0]) == 4 && value->instance_arrays[5].size() == 1,
          "Retained constructor wrapper shape/OWN heap ledger differs");
    const auto *backing = std::get_if<Array>(&value->instance_arrays[5][0]);
    check(backing && *backing && (*backing)->kind == LslFixedArrayKind::Integer && lsl_fixed_array_size(*backing) == 4,
          "Retained constructor backing differs");
    for (std::size_t i = 0; i < 4; ++i)
        check(std::get<std::int32_t>(lsl_fixed_array_get(*backing, i)) == 0,
              "Retained constructor backing cell changed");
}

void class_values(const std::vector<LslValue> &values, std::set<Class> &classes) {
    for (const auto &value : values)
        if (const auto *object = std::get_if<Class>(&value); object && *object)
            classes.insert(*object);
}

void class_evaluations(const std::vector<LslEvaluationCheckpoint> &values, std::set<Class> &classes) {
    for (const auto &entry : values)
        if (const auto *object = std::get_if<Class>(&entry.value); object && *object)
            classes.insert(*object);
}

std::set<Class> retained_classes(const LslCheckpoint &saved) {
    std::set<Class> classes;
    class_values(saved.locals, classes);
    class_evaluations(saved.evaluations, classes);
    for (const auto &frame : saved.call_stack) {
        class_values(frame.locals, classes);
        class_evaluations(frame.evaluations, classes);
    }
    for (const auto &frame : saved.resume_frames) {
        class_values(frame.locals, classes);
        class_evaluations(frame.evaluations, classes);
    }
    return classes;
}

void compare_globals(const LslInstance &instance, const LslCheckpoint &saved, const network::Value &stage,
                     bool completed) {
    constexpr std::array<std::string_view, 6> names{"input", "pressure", "result", "before", "during", "after"};
    check(saved.globals.size() == names.size() && stage.get("globals").map.size() == names.size(),
          "Constructor argument global inventory differs");
    for (std::size_t i = 2; i < names.size(); ++i)
        check(std::get<std::int32_t>(saved.globals[i]) ==
                  network::osd_integer(stage.get("globals").get(std::string(names[i])).get("value")),
              "Constructor argument source result differs: " + std::string(names[i]));
    check(std::holds_alternative<LslNullObject>(saved.globals[1]), "Basic constructor case unexpectedly has pressure");
    if (completed)
        shape(std::get<Class>(saved.globals[0]));
    else
        check(std::holds_alternative<LslNullObject>(saved.globals[0]),
              "Retained constructor argument reached input before its sleeping next argument returned");
    check(saved.arrays_heap_usage && saved.locals_heap_usage &&
              *saved.arrays_heap_usage == network::osd_integer(stage.get("arrays_heap_used")) &&
              *saved.locals_heap_usage == network::osd_integer(stage.get("locals_heap_used")) &&
              instance.heap_bytes_used() == network::osd_integer(stage.get("heap_used")),
          "Constructor argument global/local heap accounting differs");
}

void case_test(const std::filesystem::path &folder, const network::Value &proof_row) {
    const auto source = read(folder / "source.lsl");
    const auto metadata_text = read(folder / "metadata.json");
    const auto wire = read(folder / "snapshot.bin");
    const auto object_wire = read(folder / "original.yobj");
    check(framework::sha256_hex(source) == proof_row.get("sourceSha256").text &&
              framework::sha256_hex(metadata_text) == proof_row.get("metadataSha256").text &&
              framework::sha256_hex(wire) == proof_row.get("snapshotSha256").text &&
              framework::sha256_hex(object_wire) == proof_row.get("originalObjectSha256").text &&
              framework::sha256_hex(read(folder / "retained-constructor-capture.json")) ==
                  proof_row.get("captureSha256").text &&
              framework::sha256_hex(read(folder / "retained-constructor-live.json")) ==
                  proof_row.get("liveSha256").text,
          "Original retained constructor fixture artifact changed");
    const auto row = network::parse_osd_json(metadata_text);
    const auto observation = network::parse_osd_json(read(folder / "retained-constructor-capture.json"));
    check(row.get("accepted").boolean && row.get("suspended").boolean &&
              observation.get("retained").array.size() == 1 && proof_row.get("livePreservedReference").boolean,
          "Original retained constructor capture/live proof scope differs");
    const auto object = decode_yengine_object_code(bytes(object_wire));
    const auto saved = decode_yengine_snapshot(bytes(wire));
    const auto compiled = compile_lsl(source);
    check(object && saved && bool(compiled) && object->source_hash == row.get("source_hash").text,
          "Original retained constructor object/snapshot/source rejected");
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.heap_limit_bytes = network::osd_integer(observation.get("heap_limit"));
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](std::int32_t) { ++sleeps; };
    LslInstance instance(compiled.program, {}, host);
    const auto before = encode_lsl_checkpoint(checkpoint(instance));
    const bool accepted = instance.restore_yengine_snapshot(*saved, row.get("source_hash").text, source, *object);
    if (!proof_row.get("externalMigrationAccepted").boolean) {
        check(host.heap_limit_bytes == 128 && network::osd_integer(observation.get("global_heap")) == 128 &&
                  proof_row.get("externalFault").get("type").text ==
                      "OpenSim.Region.ScriptEngine.Yengine.OutOfHeapException" &&
                  proof_row.get("externalFault").get("message").text == "(OWNER)oldtotal=128, newtotal=148, limit=128",
              "Original constructor pressure threshold/fault changed");
        check(!accepted && encode_lsl_checkpoint(checkpoint(instance)) == before && sleeps == 0,
              "Frame-only constructor pressure import was accepted or changed the live native VM");
        return;
    }
    check(accepted, "Actual original retained constructor argument migration rejected");
    const auto capture = checkpoint(instance);
    compare_globals(instance, capture, row.get("after_load"), false);
    const auto retained = retained_classes(capture);
    check(retained.size() == 1, "Imported constructor argument is absent or split across frame roots");
    shape(*retained.begin());
    clock += capture.sleep_remaining + 1;
    bool completed = false, observed_alias = false;
    for (unsigned step = 0; step < 10000; ++step) {
        const auto result = instance.run(1);
        check(result.error.empty() && result.status != LslRunStatus::Faulted && result.status != LslRunStatus::Sleeping,
              "Retained constructor resume faulted or replayed Sleep: " + result.error);
        const auto current = checkpoint(instance);
        if (const auto *input = std::get_if<Class>(&current.globals[0]); input && *input) {
            // checkpoint() detaches mutable graphs. Compare aliases WITHIN this
            // checkpoint while the retained caller evaluation is still visible,
            // rather than comparing copied pointers across different snapshots.
            std::set<Class> caller_values;
            for (const auto &frame : current.call_stack)
                class_evaluations(frame.evaluations, caller_values);
            for (const auto &frame : current.resume_frames)
                class_evaluations(frame.evaluations, caller_values);
            if (caller_values.contains(*input))
                observed_alias = true;
        }
        if (result.status == LslRunStatus::Idle) {
            compare_globals(instance, current, row.get("final"), true);
            completed = true;
            break;
        }
    }
    check(completed && observed_alias && sleeps == 0,
          "Constructor argument did not retain its caller reference through Use, or source Sleep replayed");
    check(instance.run(1).status == LslRunStatus::Idle && sleeps == 0, "Constructor source entry replayed");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected retained constructor fixture directory");
        const std::filesystem::path root = argv[1];
        const auto proof_text = read(root / "verification.json");
        check(framework::sha256_hex(proof_text) == "e06d442a19a1a65b6932308965b4aba3b9c83ef3a2a511e060dc6ab90da4faaa",
              "Original retained constructor proof changed");
        const auto proof = network::parse_osd_json(proof_text);
        check(proof.get("passed").boolean && !proof.get("nativeParityVerified").boolean &&
                  framework::sha256_hex(read(root / "Reference.cs")) == proof.get("generatorSha256").text &&
                  framework::sha256_hex(read(root / "decode.py")) == proof.get("decoderSha256").text &&
                  framework::sha256_hex(read(root / "verify.py")) == proof.get("verifierSha256").text,
              "Original retained constructor generator/decoder/verifier changed");
        unsigned passed = 0;
        std::vector<std::string> failures;
        for (const auto platform : {"windows", "linux"}) {
            const auto &platform_proof = proof.get("platforms").get(platform);
            check(framework::sha256_hex(read(root / platform / "metadata.json")) ==
                      platform_proof.get("metadataSha256").text,
                  "Original constructor platform metadata changed");
            check(platform_proof.get("cases").array.size() == 2, "Original constructor case inventory differs");
            for (const auto &row : platform_proof.get("cases").array) {
                const auto label = std::string(platform) + "/" + row.get("name").text;
                try {
                    case_test(root / platform / row.get("name").text, row);
                    ++passed;
                    std::cout << "PASS " << label << '\n';
                } catch (const std::exception &error) {
                    failures.push_back(label + ": " + error.what());
                }
            }
        }
        collect_lsl_fixed_arrays();
        collect_yengine_managed_cycles();
        std::cout << "Verified " << passed << "/4 original retained-constructor imports/admission controls.\n";
        for (const auto &failure : failures)
            std::cerr << "FAIL " << failure << '\n';
        return passed == 4 && failures.empty() ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
