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
    const auto result = instance.checkpoint();
    check(result.has_value(), "Missing event-argument heap checkpoint");
    return *result;
}

void compare_class(const LslCheckpoint &saved, bool empty, std::int32_t own, bool active) {
    check(saved.globals.size() == 2, "Unexpected event-argument fixture globals");
    const auto instance = std::get<Class>(saved.globals[0]);
    check(instance && instance->descriptor && instance->descriptor->canonical_name == "integer[]" &&
              instance->arrays_heap_usage == own,
          "Class type/own ledger differs");
    check(instance->instance_arrays[3].size() == 1 &&
              std::get<std::int32_t>(instance->instance_arrays[3][0]) == (empty ? 0 : 3),
          "Class dimension differs");
    check(instance->instance_arrays[5].size() == 1, "Class Object lane differs");
    if (empty)
        check(std::holds_alternative<LslNullObject>(instance->instance_arrays[5][0]),
              "Migrated empty backing is not null");
    else {
        auto array = std::get<Array>(instance->instance_arrays[5][0]);
        check(array && array->kind == LslFixedArrayKind::Integer && lsl_fixed_array_size(array) == 3,
              "Populated backing differs");
        for (std::size_t i = 0; i < 3; ++i)
            check(std::get<std::int32_t>(lsl_fixed_array_get(array, i)) == 0, "Backing cell differs");
    }
    if (active) {
        // Both roots must be observed from this one checkpoint graph: separate
        // calls intentionally detach mutable storage and have different pointers.
        check(saved.active_event && saved.active_event->arguments.size() == 1 &&
                  std::get<Class>(saved.active_event->arguments[0]) == instance,
              "Backend-injected argument/global class alias lost");
    }
}

void run_case(const std::filesystem::path &folder, const network::Value &proof) {
    const auto source = read(folder / "source.lsl");
    const auto compiled = compile_lsl(source);
    check(bool(compiled), "Original source rejected");
    const auto metadata = network::parse_osd_json(read(folder / "metadata.json"));
    const auto object = decode_yengine_object_code(bytes(read(folder / "original.yobj")));
    const auto snapshot = decode_yengine_snapshot(bytes(read(folder / "snapshot.bin")));
    check(object && snapshot && object->source_hash == metadata.get("source_hash").text, "Original context rejected");
    const auto limit = network::osd_integer(proof.get("targetLimit"));
    const auto own = network::osd_integer(proof.get("ownHeap"));
    const bool expected = proof.get("externalMigrationAccepted").boolean;
    const bool empty = proof.get("name").text.starts_with("empty");
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.heap_limit_bytes = limit;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](std::int32_t) { ++sleeps; };
    LslInstance instance(compiled.program, {}, host);
    const auto before = encode_lsl_checkpoint(checkpoint(instance));
    check(instance.heap_bytes_used() == 4, "Fresh allocated global ledger differs from original");
    const bool admitted =
        instance.restore_yengine_snapshot(*snapshot, metadata.get("source_hash").text, source, *object);
    check(admitted == expected, "Original event-argument heap threshold differs at " + proof.get("name").text);
    if (!expected) {
        // Admission outcome and native atomicity are checked. Original failed
        // MigrateIn partial storage mutations remain outside this bounded test.
        check(encode_lsl_checkpoint(checkpoint(instance)) == before && sleeps == 0,
              "Rejected import mutated VM/replayed Sleep");
        return;
    }
    // Derived controls change only the decoded outer argument container. They
    // are not additional original captures: CLR covariance is covered separately.
    for (const auto container_type : {"sc", "in", "st"}) {
        auto derived = *snapshot;
        auto arguments = std::make_shared<YEngineManagedValue>(*derived.managed_event_arguments);
        arguments->element_type = container_type;
        derived.managed_event_arguments = arguments;
        std::int64_t control_clock = 638000000000000000LL;
        unsigned control_sleeps = 0;
        LslHost control_host;
        control_host.heap_limit_bytes = limit;
        control_host.clock_ticks = control_host.monotonic_ticks = [&] { return control_clock; };
        control_host.request_sleep = [&](std::int32_t) { ++control_sleeps; };
        LslInstance control(compiled.program, {}, control_host);
        const auto control_before = encode_lsl_checkpoint(checkpoint(control));
        const bool control_admitted =
            control.restore_yengine_snapshot(derived, metadata.get("source_hash").text, source, *object);
        const bool covariant = std::string_view(container_type) == "sc";
        check(control_admitted == covariant, "Derived argument-container covariance/content admission differs");
        if (!covariant) {
            check(encode_lsl_checkpoint(checkpoint(control)) == control_before && control_sleeps == 0,
                  "Rejected derived argument container mutated native state");
            continue;
        }
        const auto control_imported = checkpoint(control);
        compare_class(control_imported, empty, own, true);
        control_clock += control_imported.sleep_remaining + 1;
        const auto control_result = control.run(10000);
        check(control_result.status == LslRunStatus::Idle && control_result.error.empty() && control_sleeps == 0 &&
                  std::get<std::int32_t>(checkpoint(control).globals[1]) == 1,
              "Derived covariant class argument failed to resume or replayed Sleep");
    }
    const auto imported = checkpoint(instance);
    compare_class(imported, empty, own, true);
    check(std::get<std::int32_t>(imported.globals[1]) == 0 && instance.heap_bytes_used() == 4 && sleeps == 0,
          "Imported state prefix/heap differs");
    clock += imported.sleep_remaining + 1;
    const auto result = instance.run(10000);
    check(result.status == LslRunStatus::Idle && result.error.empty() && sleeps == 0,
          "Imported event replayed Sleep or failed: " + result.error);
    const auto completed = checkpoint(instance);
    compare_class(completed, empty, own, false);
    check(std::get<std::int32_t>(completed.globals[1]) == 1 && instance.heap_bytes_used() == 4,
          "Imported event did not finish with correct heap");
    check(instance.run(10000).status == LslRunStatus::Idle && sleeps == 0, "Completed event replayed");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected original event-argument class heap fixture directory");
        const std::filesystem::path root = argv[1];
        const auto proof_text = read(root / "verification.json");
        check(framework::sha256_hex(proof_text) == "c9fdfc4ecded8c50ca51ffd994d475464eb1b2158053436efc08b7249d4b65bd",
              "Original event-argument proof changed");
        const auto proof = network::parse_osd_json(proof_text);
        check(proof.get("passed").boolean && !proof.get("nativeParityVerified").boolean,
              "Original proof scope changed");
        check(framework::sha256_hex(read(root / "Reference.cs")) == proof.get("generatorSha256").text &&
                  framework::sha256_hex(read(root / "verify.py")) == proof.get("verifierSha256").text &&
                  framework::sha256_hex(read(root / "decode.py")) == proof.get("decoderSha256").text,
              "Fixture provenance changed");
        unsigned count = 0;
        std::vector<std::string> failures;
        for (const auto platform : {"windows", "linux"}) {
            const auto &cases = proof.get("platforms").get(platform).get("cases").array;
            check(cases.size() == 4, "Original case count changed");
            for (const auto &row : cases) {
                const auto folder = root / platform / row.get("name").text;
                check(framework::sha256_hex(read(folder / "metadata.json")) == row.get("metadataSha256").text &&
                          framework::sha256_hex(read(folder / "source.lsl")) == row.get("sourceSha256").text &&
                          framework::sha256_hex(read(folder / "snapshot.bin")) == row.get("snapshotSha256").text,
                      "Original capture changed");
                const auto metadata = network::parse_osd_json(read(folder / "metadata.json"));
                check(framework::sha256_hex(read(folder / "original.yobj")) ==
                          metadata.get("sha256").get("original.yobj").text,
                      "Original object changed");
                try {
                    run_case(folder, row);
                    ++count;
                } catch (const std::exception &e) {
                    failures.push_back(std::string(platform) + "/" + row.get("name").text + ": " + e.what());
                }
            }
        }
        for (const auto &failure : failures)
            std::cerr << failure << "\n";
        check(failures.empty() && count == 8, "Event-argument class heap checks failed");
        std::cout << "PASS 8 original backend-injected class event argument heap checks; rejected partial-state parity "
                     "pending\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}
