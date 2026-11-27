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
    const auto value = instance.checkpoint();
    check(value.has_value(), "Old-local checkpoint unavailable");
    return *value;
}

void counters(const LslInstance &instance, const LslCheckpoint &saved, const network::Value &original,
              const std::string &where) {
    check(saved.arrays_heap_usage && saved.locals_heap_usage &&
              *saved.arrays_heap_usage == network::osd_integer(original.get("arrays_heap_used")) &&
              *saved.locals_heap_usage == network::osd_integer(original.get("locals_heap_used")),
          where + ": global/local ledger differs");
    check(instance.heap_bytes_used() == network::osd_integer(original.get("heap_used")), where + ": heap API differs");
    check(saved.globals.size() == 6, where + ": global count differs");
    const std::array<std::string_view, 4> names{"before", "resumed", "cleared", "marker"};
    for (std::size_t i = 0; i < names.size(); ++i)
        check(std::get<std::int32_t>(saved.globals[i + 2]) ==
                  network::osd_integer(original.get("globals").get(std::string(names[i])).get("value")),
              where + ": source progress differs");
}

void aliases(const LslCheckpoint &saved, bool raw, std::int32_t charge) {
    const auto value = std::get<Class>(saved.globals[0]);
    check(value && value->descriptor && value->descriptor->canonical_name == "integer[]" &&
              value->arrays_heap_usage == 16 && std::get<std::int32_t>(value->instance_arrays[3][0]) == 3,
          "Restored global class differs");
    const auto backing = std::get<Array>(value->instance_arrays[5][0]);
    check(backing && backing->kind == LslFixedArrayKind::Integer && lsl_fixed_array_size(backing) == 3,
          "Restored backing differs");
    unsigned class_aliases = 0, raw_aliases = 0;
    for (const auto &local : saved.locals) {
        if (const auto c = std::get_if<Class>(&local); c && *c == value)
            ++class_aliases;
        if (const auto a = std::get_if<Array>(&local); a && *a == backing)
            ++raw_aliases;
    }
    check(class_aliases == 1 && raw_aliases == unsigned(raw), "Within-checkpoint class/raw backing aliases lost");
    std::int32_t charges = 0;
    for (const auto &entry : saved.local_object_heap_usage)
        if (entry)
            charges += *entry;
    check(charges == charge, "Restored per-slot tracker charges differ from original Size(payload)");
    check(saved.local_heap_base && *saved.local_heap_base == 0, "Captured handler entry heap base changed");
}

void mode_case(const std::filesystem::path &folder, const network::Value &metadata, const network::Value &observation,
               std::int32_t charge) {
    const auto source = read(folder / "source.lsl");
    const auto compiled = compile_lsl(source);
    check(bool(compiled), "Original old-local source rejected");
    const auto object = decode_yengine_object_code(bytes(read(folder / "original.yobj")));
    const auto snapshot = decode_yengine_snapshot(bytes(read(folder / "snapshot.bin")));
    check(object && snapshot && object->source_hash == metadata.get("source_hash").text,
          "Original old-local context rejected");
    const auto mode = observation.get("mode").text;
    const bool same = mode == "same_paused";
    check(same || mode == "fresh_allocated", "Unknown original import mode");
    const bool raw = metadata.get("name").text == "local_raw_array" || metadata.get("name").text == "local_combined";
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.heap_limit_bytes = 100000000;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](std::int32_t) { ++sleeps; };
    LslInstance instance(compiled.program, {}, host);
    if (same) {
        const auto prepared = instance.run(10000);
        check(prepared.status == LslRunStatus::Sleeping && prepared.error.empty() && sleeps == 1,
              "Native populated target did not pause at source Sleep");
    }
    const auto before = checkpoint(instance);
    counters(instance, before, observation.get("before"), "before " + mode);
    const auto prior_sleeps = sleeps;
    check(instance.restore_yengine_snapshot(*snapshot, metadata.get("source_hash").text, source, *object),
          "Original old-local import rejected");
    const auto imported = checkpoint(instance);
    counters(instance, imported, observation.get("after"), "after " + mode);
    aliases(imported, raw, charge);
    check(sleeps == prior_sleeps, "Migration replayed source Sleep");
    clock += imported.sleep_remaining + 1;
    bool saw_clear = false, completed = false;
    const auto expected_clear = 16 - (same ? 0 : charge);
    for (unsigned step = 0; step < 10000; ++step) {
        const auto result = instance.run(1);
        check(result.error.empty() && result.status != LslRunStatus::Faulted && result.status != LslRunStatus::Sleeping,
              "Old-local resume faulted or replayed Sleep: " + result.error);
        const auto saved = checkpoint(instance);
        if (std::get<std::int32_t>(saved.globals[4]) == expected_clear &&
            std::get<std::int32_t>(saved.globals[5]) == 0) {
            check(saved.locals_heap_usage && *saved.locals_heap_usage == (same ? 0 : -charge) &&
                      instance.heap_bytes_used() == expected_clear,
                  "Counter after clearing restored locals differs");
            saw_clear = true;
        }
        if (result.status == LslRunStatus::Idle) {
            completed = true;
            break;
        }
    }
    check(saw_clear && completed && sleeps == prior_sleeps, "Missing local-clear boundary or repeated source prefix");
    const auto done = checkpoint(instance);
    counters(instance, done, observation.get("completed"), "completed " + mode);
    check(done.locals_heap_usage && *done.locals_heap_usage == 0 && instance.heap_bytes_used() == 16,
          "Normal return did not restore local entry heap base");
    check(instance.run(10000).status == LslRunStatus::Idle && sleeps == prior_sleeps,
          "Completed old-local event replayed");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected original old-local heap fixture directory");
        const std::filesystem::path root = argv[1];
        const auto text = read(root / "verification.json");
        check(framework::sha256_hex(text) == "aa07e33ebc42a3810853c6b5831e7f24e5ae0715c22d1c318159966e2d6ca55e",
              "Original old-local proof changed");
        const auto proof = network::parse_osd_json(text);
        check(proof.get("passed").boolean && !proof.get("nativeParityVerified").boolean,
              "Original old-local proof scope changed");
        check(framework::sha256_hex(read(root / "Reference.cs")) == proof.get("generatorSha256").text &&
                  framework::sha256_hex(read(root / "verify.py")) == proof.get("verifierSha256").text &&
                  framework::sha256_hex(read(root / "decode.py")) == proof.get("decoderSha256").text,
              "Original old-local provenance changed");
        unsigned checked = 0;
        std::vector<std::string> failures;
        for (const auto platform : {"windows", "linux"}) {
            const auto &cases = proof.get("platforms").get(platform).get("cases").array;
            check(cases.size() == 4, "Original old-local case count changed");
            for (const auto &row : cases) {
                const auto folder = root / platform / row.get("name").text;
                const auto metadata_text = read(folder / "metadata.json");
                const auto observations_text = read(folder / "old-local-observations.json");
                check(framework::sha256_hex(metadata_text) == row.get("metadataSha256").text &&
                          framework::sha256_hex(observations_text) == row.get("observationsSha256").text &&
                          framework::sha256_hex(read(folder / "source.lsl")) == row.get("sourceSha256").text &&
                          framework::sha256_hex(read(folder / "snapshot.bin")) == row.get("snapshotSha256").text &&
                          framework::sha256_hex(read(folder / "original.yobj")) == row.get("originalObjectSha256").text,
                      "Original old-local capture changed");
                const auto metadata = network::parse_osd_json(metadata_text);
                const auto observations = network::parse_osd_json(observations_text);
                check(observations.array.size() == 2, "Original old-local target mode count changed");
                for (const auto &observation : observations.array) {
                    const auto label =
                        std::string(platform) + "/" + row.get("name").text + "/" + observation.get("mode").text;
                    try {
                        mode_case(folder, metadata, observation, network::osd_integer(row.get("charge")));
                        ++checked;
                    } catch (const std::exception &e) {
                        failures.push_back(label + ": " + e.what());
                    }
                }
            }
        }
        for (const auto &failure : failures)
            std::cerr << failure << "\n";
        check(failures.empty() && checked == 16, "Old-local heap import regression checks failed");
        std::cout << "PASS 16 original old-local import/clear/return checks\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}
