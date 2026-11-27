// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
// Compares original admission and successful old-global ClearOldArrays residuals.
// Original partial-failure fields remain pending; admission covers every original case.
// Preparation uses real source checkpoints without native ledger fabrication.
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

LslCheckpoint snapshot(const LslInstance &instance) {
    const auto result = instance.checkpoint();
    check(result.has_value(), "Old-global-clear checkpoint missing");
    return *result;
}

void compare(const LslInstance &instance, const network::Value &state, bool list_case) {
    const auto saved = snapshot(instance);
    check(saved.globals.size() == 3 && saved.arrays_heap_usage && saved.locals_heap_usage &&
              *saved.arrays_heap_usage == network::osd_integer(state.get("globalUsage")) &&
              *saved.locals_heap_usage == network::osd_integer(state.get("localsUsage")) &&
              instance.heap_bytes_used() == network::osd_integer(state.get("heapUsed")) &&
              network::osd_integer(state.get("heapLimit")) - instance.heap_bytes_used() ==
                  network::osd_integer(state.get("heapLeft")),
          "Original old-clear global/local ledger differs");
    check(std::get<std::int32_t>(saved.globals[2]) == 7 && state.get("globalIntegers").array.size() == 1 &&
              network::osd_integer(state.get("globalIntegers").array[0]) == 7,
          "Original marker changed or source prefix replayed");
    const auto wrapper = std::get<Class>(saved.globals[0]);
    check(wrapper && wrapper->descriptor && wrapper->descriptor->canonical_name == "integer[]" &&
              wrapper->arrays_heap_usage == 16 &&
              wrapper->arrays_heap_usage == network::osd_integer(state.get("classes").array.at(0).get("ownUsage")) &&
              wrapper->instance_arrays[3].size() == 1 && wrapper->instance_arrays[5].size() == 1 &&
              std::get<std::int32_t>(wrapper->instance_arrays[3][0]) == 3,
          "Original generated class shape/OWN ledger differs");
    const auto backing = std::get<Array>(wrapper->instance_arrays[5][0]);
    check(backing && backing->kind == LslFixedArrayKind::Integer && lsl_fixed_array_size(backing) == 3,
          "Original generated backing shape differs");
    for (std::size_t i = 0; i < 3; ++i)
        check(std::get<std::int32_t>(lsl_fixed_array_get(backing, i)) == 0, "Backing data changed");
    if (list_case) {
        const auto list = std::get<std::shared_ptr<const LslList>>(saved.globals[1]);
        check(list && list->items.size() == 1 && lsl_string_code_units(list->items[0]) == u"x" &&
                  network::osd_integer(state.get("lists").array.at(0).get("size")) == 10,
              "Source-created original List contents/size differ");
    } else {
        const auto array = std::get<Array>(saved.globals[1]);
        check(array && array->kind == LslFixedArrayKind::Object && lsl_fixed_array_size(array) == 1 &&
                  lsl_string_code_units(lsl_fixed_array_get(array, 0)) == u"abc" &&
                  network::osd_integer(state.get("objects").array.at(0).get("size")) == 6,
              "Source-mutated original Object[] contents/size differ");
    }
}

void resume_copy(const LslInstance &instance, std::shared_ptr<const LslProgram> program, const LslHost &host,
                 std::int64_t &clock, unsigned &sleeps, const network::Value &expected, bool list_case) {
    const auto saved = snapshot(instance);
    LslInstance resumed(std::move(program), {}, host);
    check(resumed.restore_checkpoint(saved), "Accepted old-clear checkpoint clone rejected");
    clock += saved.sleep_remaining + 1;
    const auto result = resumed.run(10000);
    check(result.status == LslRunStatus::Idle && result.error.empty() && sleeps == 0,
          "Accepted old-clear import failed to resume or replayed Sleep: " + result.error);
    compare(resumed, expected, list_case);
    check(resumed.run(1).status == LslRunStatus::Idle && sleeps == 0, "Source entry replayed");
}

struct Counts {
    unsigned checked = 0, accepted = 0, rejected = 0, below_old = 0, lowered_below_old = 0;
};

void corpus_case(const std::filesystem::path &folder, const network::Value &proof, Counts &counts) {
    const auto source = read(folder / "source.lsl");
    const auto metadata_text = read(folder / "metadata.json");
    const auto observations_text = read(folder / "restore-heap-observations.json");
    const auto wire = read(folder / "snapshot.bin");
    const auto object_wire = read(folder / "original.yobj");
    check(framework::sha256_hex(source) == proof.get("sourceSha256").text &&
              framework::sha256_hex(metadata_text) == proof.get("metadataSha256").text &&
              framework::sha256_hex(observations_text) == proof.get("observationsSha256").text &&
              framework::sha256_hex(wire) == proof.get("snapshotSha256").text &&
              framework::sha256_hex(object_wire) == proof.get("objectSha256").text,
          "Original old-clear fixture artifact changed");
    const auto metadata = network::parse_osd_json(metadata_text);
    const auto observations = network::parse_osd_json(observations_text);
    const auto compiled = compile_lsl(source);
    const auto object = decode_yengine_object_code(bytes(object_wire));
    const auto original = decode_yengine_snapshot(bytes(wire));
    check(compiled && object && original && observations.array.size() == 45,
          "Original source/object/snapshot/observation inventory rejected");
    const bool list_case = folder.filename() == "list_residual";
    std::int64_t clock = 638000000000000000LL;
    unsigned source_sleeps = 0;
    LslHost source_host;
    source_host.heap_limit_bytes = 100000000;
    source_host.clock_ticks = source_host.monotonic_ticks = [&] { return clock; };
    source_host.request_sleep = [&](std::int32_t) { ++source_sleeps; };
    LslInstance source_vm(compiled.program, {}, source_host);
    bool paused = false;
    for (unsigned i = 0; i < 10000; ++i) {
        const auto result = source_vm.run(1);
        check(result.error.empty(), "Source old-clear preparation faulted: " + result.error);
        if (result.status == LslRunStatus::Sleeping) {
            paused = true;
            break;
        }
    }
    check(paused && source_sleeps == 1, "Source old-clear preparation did not pause exactly once");
    const auto seed = snapshot(source_vm); // Real source-produced stale ledger/fields.
    unsigned sleeps = 0;
    std::unique_ptr<LslInstance> repeated;
    for (const auto &row : observations.array) {
        const auto mode = row.get("mode").text;
        const auto limit = network::osd_integer(row.get("limit"));
        const auto old_global = network::osd_integer(row.get("before").get("globalUsage"));
        if (old_global > limit) {
            ++counts.below_old;
            if (mode != "fresh_allocated")
                ++counts.lowered_below_old;
        }
        LslHost host;
        host.heap_limit_bytes = limit;
        host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
        host.request_sleep = [&](std::int32_t) { ++sleeps; };
        std::unique_ptr<LslInstance> target;
        if (mode == "same_instance_repeat") {
            if (!repeated) {
                auto preparation_host = host;
                preparation_host.heap_limit_bytes = 100000000;
                repeated = std::make_unique<LslInstance>(compiled.program, LslContext{}, preparation_host);
                check(repeated->restore_checkpoint(seed), "Real source checkpoint repeat seeding rejected");
            }
        } else {
            check(mode == "fresh_allocated" || mode == "same_instance", "Unsupported original preparation mode");
            auto preparation_host = host;
            if (mode == "same_instance")
                preparation_host.heap_limit_bytes = 100000000;
            target = std::make_unique<LslInstance>(compiled.program, LslContext{}, preparation_host);
            if (mode == "same_instance")
                check(target->restore_checkpoint(seed), "Real source checkpoint high-limit seeding rejected");
        }
        auto &instance = target ? *target : *repeated;
        // Lower a real VM's configured budget after source-produced state was
        // seeded at its actual generous capture budget. This changes no fields
        // or ledgers, including when the old usage now exceeds the limit.
        if (mode != "fresh_allocated")
            instance.set_heap_limit_bytes(limit);
        check(instance.heap_bytes_used() == old_global, "Prepared old global ledger differs from original");
        if (mode != "fresh_allocated")
            compare(instance, row.get("before"), list_case);
        else {
            const auto fresh = snapshot(instance);
            check(std::holds_alternative<LslNullObject>(fresh.globals.at(0)) &&
                      std::get<std::int32_t>(fresh.globals.at(2)) == 0,
                  "Fresh allocation unexpectedly ran source initialization");
        }
        const auto before = encode_lsl_checkpoint(snapshot(instance));
        const bool admitted =
            instance.restore_yengine_snapshot(*original, metadata.get("source_hash").text, source, *object);
        const bool expected = row.get("error").kind != network::Value::Kind::Map;
        check(admitted == expected, folder.filename().string() + "/" + mode + "/limit" + std::to_string(limit) +
                                        ": original residual heap admission differs");
        if (!expected) {
            ++counts.rejected;
            // Admission and native atomic rejection are tested. Original
            // partially cleared failure fields are evidence-only, not parity.
            check(encode_lsl_checkpoint(snapshot(instance)) == before && sleeps == 0,
                  "Rejected old-clear import mutated native state or replayed Sleep");
        } else {
            ++counts.accepted;
            compare(instance, row.get("after"), list_case);
            resume_copy(instance, compiled.program, host, clock, sleeps, row.get("after"), list_case);
        }
        ++counts.checked;
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected original old-global-clear fixture directory");
        const std::filesystem::path root = argv[1];
        const auto proof_text = read(root / "verification.json");
        check(framework::sha256_hex(proof_text) == "ce2d1e929c8927e589423909ff19f672b478f1d22bd4b0ac8a25bdefb90e10eb",
              "Original old-global-clear proof changed");
        const auto proof = network::parse_osd_json(proof_text);
        check(proof.get("passed").boolean && !proof.get("nativeParityVerified").boolean,
              "Original old-global-clear proof scope changed");
        Counts counts;
        for (const auto platform : {"windows", "linux"}) {
            const auto &cases = proof.get("platforms").get(platform).get("cases").array;
            check(cases.size() == 2, "Original old-clear corpus inventory changed");
            for (const auto &row : cases)
                corpus_case(root / (std::string(platform) + "-final") / row.get("name").text, row, counts);
        }
        check(counts.checked == 180 && counts.accepted == 86 && counts.rejected == 94 && counts.below_old == 42 &&
                  counts.lowered_below_old == 26,
              "Original complete old-clear admission coverage changed");
        collect_lsl_fixed_arrays();
        collect_yengine_managed_cycles();
        std::cout << "Verified 180 admission checks (86 accepted-state/resume, 94 atomic rejections), "
                     "including 42 below-old-ledger budgets (26 lowered through the live quota API). "
                     "Original partial-failure-state parity remains pending.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
