// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include "opensim/script/lsl_script_class.hpp"
#include "opensim/script/yengine_object_code.hpp"
#include <array>
#include <iostream>

using namespace utf16_test;

namespace {
using Class = std::shared_ptr<const LslScriptClass>;
using Array = std::shared_ptr<const LslFixedArray>;
constexpr std::array<std::int32_t, 8> limits{0, 3, 4, 15, 16, 19, 20, 100000000};

std::span<const std::uint8_t> bytes(const std::string &value) {
    return {reinterpret_cast<const std::uint8_t *>(value.data()), value.size()};
}

LslCheckpoint checkpoint(const LslInstance &instance) {
    const auto result = instance.checkpoint();
    check(result.has_value(), "Class restore heap checkpoint unavailable");
    return *result;
}

void compare(const LslInstance &instance, const network::Value &stage, const network::Value &observed,
             std::int32_t limit, const std::string &where) {
    const auto saved = checkpoint(instance);
    check(saved.globals.size() == 3 && stage.get("globals").map.size() == 3,
          where + ": original global inventory differs");
    check(saved.arrays_heap_usage && saved.locals_heap_usage &&
              *saved.arrays_heap_usage == network::osd_integer(stage.get("arrays_heap_used")) &&
              *saved.locals_heap_usage == network::osd_integer(stage.get("locals_heap_used")),
          where + ": global/local heap ledger differs");
    check(instance.heap_bytes_used() == network::osd_integer(stage.get("heap_used")) &&
              instance.heap_bytes_used() == network::osd_integer(observed.get("heapUsed")) &&
              limit - instance.heap_bytes_used() == network::osd_integer(observed.get("heapLeft")),
          where + ": script heap used/left differs at configured limit");
    check(std::get<std::int32_t>(saved.globals[2]) ==
              network::osd_integer(stage.get("globals").get("marker").get("value")),
          where + ": marker changed (source prefix may have replayed)");
    const auto input = std::get<Class>(saved.globals[0]);
    const auto second = std::get<Class>(saved.globals[1]);
    check(input && second && input != second && input->descriptor && second->descriptor &&
              same_lsl_script_class_type(*input->descriptor, *second->descriptor),
          where + ": distinct original wrappers merged or lost their shared generated type");
    const auto &classes = observed.get("classes").array;
    check(classes.size() == 2, where + ": original class observation count differs");
    std::array<Array, 2> backing;
    for (std::size_t index = 0; index < 2; ++index) {
        const auto &actual = index == 0 ? input : second;
        const auto &expected = stage.get("globals").get(index == 0 ? "input" : "second");
        const auto &summary = classes[index];
        check(actual->descriptor->canonical_name == expected.get("script_type").text &&
                  actual->descriptor->element_type == expected.get("element_type").text &&
                  actual->descriptor->array_rank == network::osd_integer(expected.get("rank")) &&
                  actual->arrays_heap_usage == network::osd_integer(expected.get("arrays_heap_use")) &&
                  actual->arrays_heap_usage == network::osd_integer(summary.get("ownUsage")),
              where + ": generated class type or OWN ledger differs");
        const auto &dimensions = expected.get("integers").array;
        check(actual->instance_arrays[3].size() == dimensions.size() &&
                  summary.get("dimensions").array.size() == dimensions.size(),
              where + ": dimension inventory differs");
        for (std::size_t dimension = 0; dimension < dimensions.size(); ++dimension)
            check(std::get<std::int32_t>(actual->instance_arrays[3][dimension]) ==
                          network::osd_integer(dimensions[dimension]) &&
                      network::osd_integer(summary.get("dimensions").array[dimension]) ==
                          network::osd_integer(dimensions[dimension]),
                  where + ": public dimension differs");
        check(actual->instance_arrays[5].size() == 1 && expected.get("objects").array.size() == 1,
              where + ": backing field inventory differs");
        const auto &expected_backing = expected.get("objects").array[0];
        const auto &actual_backing = actual->instance_arrays[5][0];
        if (expected_backing.get("type").text == "null") {
            check(std::holds_alternative<LslNullObject>(actual_backing),
                  where + ": externally restored empty backing failed to normalize null");
        } else {
            const auto *array = std::get_if<Array>(&actual_backing);
            check(array && *array && (*array)->kind == LslFixedArrayKind::Integer,
                  where + ": restored raw backing type differs");
            backing[index] = *array;
            const auto &cells = expected_backing.get("items").array;
            check(lsl_fixed_array_size(*array) == cells.size(), where + ": backing cell count differs");
            for (std::size_t cell = 0; cell < cells.size(); ++cell)
                check(std::get<std::int32_t>(lsl_fixed_array_get(*array, cell)) ==
                          network::osd_integer(cells[cell].get("value")),
                      where + ": backing cell differs");
        }
    }
    check(!backing[0] || !backing[1] || backing[0] != backing[1], where + ": distinct original backing arrays merged");
}

void limit_case(const std::filesystem::path &folder, const network::Value &metadata, const network::Value &observation,
                std::int32_t limit) {
    const auto source = read(folder / "source.lsl");
    const auto compiled = compile_lsl(source);
    check(bool(compiled), "Original class restore heap source rejected");
    const auto object = decode_yengine_object_code(bytes(read(folder / "original.yobj")));
    const auto saved = decode_yengine_snapshot(bytes(read(folder / "snapshot.bin")));
    check(object && saved && object->source_hash == metadata.get("source_hash").text,
          "Original class restore heap object/snapshot context rejected");
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.heap_limit_bytes = limit;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](std::int32_t) { ++sleeps; };
    LslInstance instance(compiled.program, {}, host);
    const auto before = checkpoint(instance);
    check(before.globals.size() == 3 && std::holds_alternative<LslNullObject>(before.globals[0]) &&
              std::holds_alternative<LslNullObject>(before.globals[1]) &&
              std::get<std::int32_t>(before.globals[2]) == 0 &&
              instance.heap_bytes_used() == network::osd_integer(observation.get("before").get("heapUsed")),
          "Native fresh-allocated preparation differs from original");
    const auto native_before = encode_lsl_checkpoint(before);
    const auto admitted = instance.restore_yengine_snapshot(*saved, metadata.get("source_hash").text, source, *object);
    const bool expected = observation.get("error").kind != network::Value::Kind::Map;
    check(admitted == expected,
          "Near-limit original class restore admission differs at limit " + std::to_string(limit));
    if (!expected) {
        // The original failed MigrateIn mutates partially cleared storage. The
        // native Boolean admission boundary is atomic. This test intentionally
        // verifies admission outcome and native atomicity, NOT failed-state parity.
        check(encode_lsl_checkpoint(checkpoint(instance)) == native_before && sleeps == 0,
              "Rejected near-limit import changed the native VM or replayed source Sleep");
        std::cout << "REJECT limit=" << limit << " (native atomic rejection; original partial-state parity pending)\n";
        return;
    }
    compare(instance, metadata.get("after_load"), observation.get("after"), limit, "near-limit original import");
    clock += checkpoint(instance).sleep_remaining + 1;
    const auto result = instance.run(10000);
    check(result.status == LslRunStatus::Idle && result.error.empty() && sleeps == 0,
          "Near-limit original import failed to resume or replayed Sleep: " + result.error);
    compare(instance, metadata.get("final"), observation.get("after"), limit, "near-limit original resume");
    check(instance.run(10000).status == LslRunStatus::Idle && sleeps == 0,
          "Near-limit original import replayed the completed entry");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected original class restore heap fixture directory");
        const std::filesystem::path root = argv[1];
        const auto proof_text = read(root / "verification.json");
        check(framework::sha256_hex(proof_text) == "7d428bda5da80f46b8e8c3d5bde6a9ab3f5c9c54e7cfb52d4fbef41e4fa2866d",
              "Original class restore heap proof changed");
        const auto proof = network::parse_osd_json(proof_text);
        check(proof.get("passed").boolean && !proof.get("nativeParityVerified").boolean &&
                  framework::sha256_hex(read(root / "Reference.cs")) == proof.get("generatorSha256").text &&
                  framework::sha256_hex(read(root / "verify.py")) == proof.get("verifierSha256").text,
              "Original class restore heap evidence scope/provenance changed");
        std::vector<std::string> failures;
        unsigned checked = 0;
        for (const auto platform : {"windows", "linux"}) {
            const auto &cases = proof.get("platforms").get(platform).get("cases").array;
            check(cases.size() == 2, "Original class restore heap case inventory differs");
            for (const auto &row : cases) {
                const auto folder = root / (std::string(platform) + "-fields") / row.get("name").text;
                const auto metadata_text = read(folder / "metadata.json");
                const auto observations_text = read(folder / "restore-heap-observations.json");
                check(framework::sha256_hex(metadata_text) == row.get("metadataSha256").text &&
                          framework::sha256_hex(observations_text) == row.get("observationsSha256").text &&
                          framework::sha256_hex(read(folder / "source.lsl")) == row.get("sourceSha256").text &&
                          framework::sha256_hex(read(folder / "snapshot.bin")) == row.get("snapshotSha256").text,
                      "Original class restore heap fixture artifact changed");
                const auto metadata = network::parse_osd_json(metadata_text);
                check(metadata.get("accepted").boolean && metadata.get("suspended").boolean &&
                          metadata.get("migrated").boolean &&
                          framework::sha256_hex(read(folder / "original.yobj")) ==
                              metadata.get("sha256").get("original.yobj").text,
                      "Original heap capture/object provenance changed");
                const auto observations = network::parse_osd_json(observations_text);
                check(observations.array.size() == 24, "Original class restore heap observation inventory differs");
                for (const auto limit : limits) {
                    const auto label = std::string(platform) + "/" + row.get("name").text + "/" + std::to_string(limit);
                    try {
                        const network::Value *observation = nullptr;
                        for (const auto &candidate : observations.array)
                            if (candidate.get("mode").text == "fresh_allocated" &&
                                network::osd_integer(candidate.get("limit")) == limit) {
                                check(!observation, "Duplicate original fresh-allocated observation");
                                observation = &candidate;
                            }
                        check(observation, "Missing original fresh-allocated observation");
                        limit_case(folder, metadata, *observation, limit);
                        ++checked;
                        std::cout << "PASS " << label << '\n';
                    } catch (const std::exception &error) {
                        failures.push_back(label + ": " + error.what());
                    }
                }
            }
        }
        collect_lsl_fixed_arrays();
        collect_yengine_managed_cycles();
        std::cout << "Checked " << checked
                  << "/32 fresh-allocated near-limit imports. Original failed partial-state parity remains pending.\n";
        for (const auto &failure : failures)
            std::cerr << "FAIL " << failure << '\n';
        return checked == 32 && failures.empty() ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
