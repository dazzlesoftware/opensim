// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include "opensim/script/lsl_script_class.hpp"
#include "opensim/script/yengine_object_code.hpp"
#include <functional>
#include <iostream>
#include <map>
#include <memory>

using namespace utf16_test;

namespace {
using Class = std::shared_ptr<const LslScriptClass>;
using Array = std::shared_ptr<const LslFixedArray>;

std::vector<std::string> declarations(std::string source) {
    source = global_declaration_source(std::move(source));
    const std::regex declaration(
        R"(\b(?:integer\s*\[\s*\]|integer|object|string|list)\s+([A-Za-z_][A-Za-z_0-9]*)\s*(?:=|;))");
    std::vector<std::string> names;
    for (auto it = std::sregex_iterator(source.begin(), source.end(), declaration); it != std::sregex_iterator(); ++it)
        names.push_back((*it)[1].str());
    return names;
}

struct GraphComparison {
    std::map<std::int32_t, const void *> references;
    std::map<const void *, std::int32_t> reverse;

    void reference(std::int32_t id, const void *actual, const std::string &where) {
        check(id > 0 && actual, where + ": invalid reference");
        const auto [forward, inserted] = references.emplace(id, actual);
        check(inserted || forward->second == actual, where + ": original aliases split");
        const auto [backward, new_identity] = reverse.emplace(actual, id);
        check(new_identity || backward->second == id, where + ": distinct original references merged");
    }

    void value(const LslValue &actual, const network::Value &expected, const std::string &where) {
        const auto type = expected.get("type").text;
        if (type == "null") {
            check(std::holds_alternative<LslNullObject>(actual), where + ": null Object differs");
        } else if (type == "integer") {
            const auto unboxed = unbox_lsl_value(actual);
            const auto *integer = std::get_if<std::int32_t>(&unboxed);
            check(integer && *integer == network::osd_integer(expected.get("value")), where + ": Int32 differs");
        } else if (type == "string") {
            const auto units = lsl_string_code_units(actual);
            const auto &expected_units = expected.get("utf16").array;
            check(units && units->size() == expected_units.size(), where + ": string length differs");
            for (std::size_t i = 0; i < expected_units.size(); ++i)
                check((*units)[i] == network::osd_integer(expected_units[i]), where + ": UTF16 unit differs");
        } else if (type == "list") {
            const auto *list = std::get_if<std::shared_ptr<const LslList>>(&actual);
            check(list && *list && (*list)->items.size() == expected.get("value").array.size(),
                  where + ": covariant List value missing");
            for (std::size_t i = 0; i < (*list)->items.size(); ++i)
                value((*list)->items[i], expected.get("value").array[i], where + ".list[" + std::to_string(i) + "]");
        } else if (type == "script_class" || type == "script_class_reference") {
            const auto *object = std::get_if<Class>(&actual);
            check(object && *object, where + ": generated class reference missing");
            reference(network::osd_integer(expected.get("reference_id")), object->get(), where);
            if (type == "script_class_reference")
                return;
            const auto &instance = **object;
            check(instance.descriptor && instance.descriptor->canonical_name == expected.get("script_type").text &&
                      instance.descriptor->element_type == expected.get("element_type").text &&
                      instance.descriptor->array_rank == network::osd_integer(expected.get("rank")),
                  where + ": generated type descriptor differs");
            if (expected.get("arrays_heap_use").kind == network::Value::Kind::Integer)
                check(instance.arrays_heap_usage == network::osd_integer(expected.get("arrays_heap_use")),
                      where + ": class own heap ledger differs");
            const auto &dimensions = expected.get("integers").array;
            check(instance.instance_arrays[3].size() == dimensions.size(), where + ": dimension count differs");
            for (std::size_t i = 0; i < dimensions.size(); ++i)
                check(std::get<std::int32_t>(instance.instance_arrays[3][i]) == network::osd_integer(dimensions[i]),
                      where + ": public dimension differs");
            const auto &objects = expected.get("objects").array;
            check(instance.instance_arrays[5].size() == objects.size(), where + ": Object field count differs");
            for (std::size_t i = 0; i < objects.size(); ++i)
                value(instance.instance_arrays[5][i], objects[i], where + ".obj");
        } else if (type == "clr_array" || type == "array_reference") {
            const auto *array = std::get_if<Array>(&actual);
            check(array && *array, where + ": raw backing missing");
            reference(network::osd_integer(expected.get("reference_id")), array->get(), where);
            if (type == "array_reference")
                return;
            const auto kind = expected.get("element_type").text == "System.Object" ? LslFixedArrayKind::Object
                                                                                   : LslFixedArrayKind::Integer;
            check((*array)->kind == kind, where + ": raw backing type differs");
            const auto &items = expected.get("items").array;
            check(lsl_fixed_array_size(*array) == items.size(), where + ": backing length differs");
            for (std::size_t i = 0; i < items.size(); ++i)
                value(lsl_fixed_array_get(*array, i), items[i], where + "[" + std::to_string(i) + "]");
        } else
            throw std::runtime_error(where + ": unreviewed original observation " + type);
    }
};

void compare(const LslCheckpoint &checkpoint, const std::vector<std::string> &names, const network::Value &expected,
             const std::string &where) {
    const auto &globals = expected.get("globals");
    check(globals.kind == network::Value::Kind::Map && names.size() == checkpoint.globals.size() &&
              globals.map.size() == names.size(),
          where + ": global inventory differs");
    GraphComparison graph;
    for (std::size_t i = 0; i < names.size(); ++i)
        graph.value(checkpoint.globals[i], globals.get(names[i]), where + "." + names[i]);
}

std::span<const std::uint8_t> bytes(const std::string &value) {
    return {reinterpret_cast<const std::uint8_t *>(value.data()), value.size()};
}

LslCheckpoint snapshot(const LslInstance &instance) {
    auto result = instance.checkpoint();
    check(result.has_value(), "Whitespace checkpoint missing");
    return *result;
}

void compare_state(const LslInstance &instance, const std::vector<std::string> &names, const network::Value &expected,
                   const std::string &label) {
    const auto saved = snapshot(instance);
    compare(saved, names, expected, label);
    check(instance.heap_bytes_used() == network::osd_integer(expected.get("heap_used")),
          label + ": script heap API differs actual=" + std::to_string(instance.heap_bytes_used()));
    if (saved.arrays_heap_usage)
        check(*saved.arrays_heap_usage == network::osd_integer(expected.get("arrays_heap_used")),
              label + ": global ledger differs");
    if (saved.locals_heap_usage)
        check(*saved.locals_heap_usage == network::osd_integer(expected.get("locals_heap_used")),
              label + ": locals ledger differs");
}

void resume(LslInstance &instance, std::int64_t &clock, const std::vector<std::string> &names,
            const network::Value &expected, unsigned &sleeps, unsigned expected_sleeps) {
    clock += snapshot(instance).sleep_remaining + 1;
    bool completed = false;
    for (unsigned i = 0; i < 10000; ++i) {
        const auto result = instance.run(1);
        check(result.error.empty() && result.status != LslRunStatus::Faulted && result.status != LslRunStatus::Sleeping,
              "Whitespace resume faulted or replayed Sleep: " + result.error);
        if (result.status == LslRunStatus::Idle) {
            compare_state(instance, names, expected, "whitespace resumed");
            completed = true;
            break;
        }
    }
    check(completed && sleeps == expected_sleeps && instance.run(1).status == LslRunStatus::Idle,
          "Whitespace resume exceeded bound or replayed effects");
}

void case_test(const std::filesystem::path &folder, const network::Value &proof) {
    const auto source = read(folder / "source.lsl");
    const auto metadata_text = read(folder / "metadata.json");
    const auto wire = read(folder / "snapshot.bin");
    const auto object_wire = read(folder / "original.yobj");
    check(framework::sha256_hex(source) == proof.get("sourceSha256").text &&
              framework::sha256_hex(metadata_text) == proof.get("metadataSha256").text &&
              framework::sha256_hex(wire) == proof.get("snapshotSha256").text &&
              framework::sha256_hex(object_wire) == proof.get("objectSha256").text,
          "Original whitespace fixture artifact changed");
    const auto oracle = network::parse_osd_json(metadata_text);
    const auto compiled = compile_lsl(source);
    const auto object = decode_yengine_object_code(bytes(object_wire));
    const auto original = decode_yengine_snapshot(bytes(wire));
    check(compiled && object && original, "Whitespace source/object/snapshot rejected");
    const auto names = declarations(source);
    const bool pressure = !proof.get("externalMigrationAccepted").boolean;
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.heap_limit_bytes = pressure ? 128 : 100000000;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](std::int32_t) { ++sleeps; };
    LslInstance live(compiled.program, {}, host);
    bool paused = false;
    for (unsigned i = 0; i < 10000; ++i) {
        const auto result = live.run(1);
        check(result.error.empty() && result.status != LslRunStatus::Faulted,
              "Whitespace source faulted: " + result.error);
        if (result.status == LslRunStatus::Sleeping) {
            paused = true;
            break;
        }
    }
    check(paused && sleeps == 1, "Whitespace source did not reach one Sleep");
    compare_state(live, names, oracle.get("initial"), "whitespace source capture");
    const auto captured = snapshot(live);
    const auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(captured));
    LslInstance internal(compiled.program, {}, host);
    check(decoded && internal.restore_checkpoint(*decoded), "Whitespace internal checkpoint rejected");
    compare_state(internal, names, oracle.get("initial"), "whitespace internal reload");
    resume(live, clock, names, oracle.get("live_resume"), sleeps, 1);
    resume(internal, clock, names, oracle.get("live_resume"), sleeps, 1);
    LslInstance imported(compiled.program, {}, host);
    const auto before = encode_lsl_checkpoint(snapshot(imported));
    const bool admitted = imported.restore_yengine_snapshot(*original, oracle.get("source_hash").text, source, *object);
    if (pressure) {
        check(proof.get("externalFault").get("type").text == "OpenSim.Region.ScriptEngine.Yengine.OutOfHeapException" &&
                  proof.get("externalFault").get("message").text == "(OWNER)oldtotal=128, newtotal=148, limit=128",
              "Original whitespace frame pressure fault changed");
        check(!admitted && encode_lsl_checkpoint(snapshot(imported)) == before,
              "Whitespace global pressure undercount admitted frame class or mutated target");
    } else {
        check(admitted, "Actual original whitespace migration rejected");
        compare_state(imported, names, oracle.get("after_load"), "original whitespace reload");
        resume(imported, clock, names, oracle.get("final"), sleeps, 1);
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected original whitespace heap fixture directory");
        const std::filesystem::path root = argv[1];
        const auto proof_text = read(root / "verification.json");
        check(framework::sha256_hex(proof_text) == "12676ed082272abf842c8954383737a8ea60ea1b4fb33cebb8068513d689d733",
              "Original whitespace heap proof changed");
        const auto proof = network::parse_osd_json(proof_text);
        check(proof.get("passed").boolean && !proof.get("nativeParityVerified").boolean,
              "Original whitespace heap proof scope changed");
        unsigned passed = 0;
        std::vector<std::string> failures;
        for (const auto platform : {"windows", "linux"}) {
            const auto &rows = proof.get("platforms").get(platform).get("cases").array;
            check(rows.size() == 3, "Original whitespace inventory changed");
            for (const auto &row : rows) {
                const auto label = std::string(platform) + "/" + row.get("name").text;
                try {
                    case_test(root / (std::string(platform) + "-final") / row.get("name").text, row);
                    ++passed;
                } catch (const std::exception &error) {
                    failures.push_back(label + ": " + error.what());
                }
            }
        }
        collect_lsl_fixed_arrays();
        collect_yengine_managed_cycles();
        for (const auto &failure : failures)
            std::cerr << failure << '\n';
        std::cout << "Verified " << passed << "/6 original whitespace source/internal/import controls.\n";
        return passed == 6 && failures.empty() ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
