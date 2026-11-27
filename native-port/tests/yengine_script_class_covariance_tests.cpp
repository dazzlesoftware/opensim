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
        R"(\b(?:integer\s*\[\s*\]|integer|object|string)\s+([A-Za-z_][A-Za-z_0-9]*)\s*(?:=|;))");
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
            check((*array)->kind == LslFixedArrayKind::Integer, where + ": raw backing type differs");
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

void heap(const LslInstance &instance, const LslCheckpoint &state, const network::Value &expected,
          const std::string &where) {
    if (expected.get("heap_used").kind != network::Value::Kind::Integer)
        return;
    check(state.arrays_heap_usage && state.locals_heap_usage, where + ": imported heap ledgers missing");
    const auto arrays = network::osd_integer(expected.get("arrays_heap_used"));
    const auto locals = network::osd_integer(expected.get("locals_heap_used"));
    check(*state.arrays_heap_usage == arrays && *state.locals_heap_usage == locals,
          where + ": imported global/local heap differs: actual=" + std::to_string(*state.arrays_heap_usage) + "/" +
              std::to_string(*state.locals_heap_usage) + " expected=" + std::to_string(arrays) + "/" +
              std::to_string(locals));
    check(instance.heap_bytes_used() == network::osd_integer(expected.get("heap_used")),
          where + ": imported script heap API differs");
}

void import_case(const std::filesystem::path &folder, const network::Value &proof_row) {
    const auto source = read(folder / "source.lsl");
    const auto metadata_text = read(folder / "metadata.json");
    const auto snapshot_wire = read(folder / "snapshot.bin");
    const auto object_wire = read(folder / "original.yobj");
    check(framework::sha256_hex(source) == proof_row.get("sourceSha256").text &&
              framework::sha256_hex(metadata_text) == proof_row.get("metadataSha256").text &&
              framework::sha256_hex(snapshot_wire) == proof_row.get("snapshotSha256").text,
          "Original generated-array fixture hashes changed");
    const auto row = network::parse_osd_json(metadata_text);
    check(framework::sha256_hex(object_wire) == row.get("sha256").get("original.yobj").text,
          "Original generated-array compiled object changed");
    check(row.get("accepted").boolean && row.get("suspended").boolean && row.get("migrated").boolean,
          "Original generated-array control lacks a real migration capture");
    const auto object = decode_yengine_object_code(bytes(object_wire));
    const auto saved = decode_yengine_snapshot(bytes(snapshot_wire));
    check(object && saved, "Original generated-array object/snapshot decoder rejected");
    check(object->source_hash == row.get("source_hash").text, "Original compiled object/source-hash context differs");
    const auto compiled = compile_lsl(source);
    std::string diagnostics;
    for (const auto &diagnostic : compiled.diagnostics)
        diagnostics += " " + diagnostic.message;
    check(bool(compiled), "Original generated-array source rejected by native compiler:" + diagnostics);
    const auto names = declarations(source);
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.heap_limit_bytes = 100000000;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](std::int32_t) { ++sleeps; };
    LslInstance imported(compiled.program, {}, host);
    check(imported.restore_yengine_snapshot(*saved, row.get("source_hash").text, source, *object),
          "Original generated-array class/frame migration rejected");
    auto previous = imported.checkpoint();
    check(previous.has_value(), "Imported generated-array checkpoint missing");
    compare(*previous, names, row.get("after_load"), "original migration capture");
    heap(imported, *previous, row.get("after_load"), "original migration capture");
    clock += previous->sleep_remaining + 1;
    bool finished = false;
    for (unsigned step = 0; step < 10000; ++step) {
        const auto result = imported.run(1);
        const auto &expected_fault = row.get("resume_exception");
        if (result.status == LslRunStatus::Faulted) {
            const auto type = expected_fault.get("type").text;
            check(type.starts_with("System.") &&
                      result.error == type.substr(7) + ": " + expected_fault.get("message").text,
                  "Original migration resumed fault category/text differs: " + result.error);
            check(previous.has_value(), "Imported generated-array pre-fault state missing");
            compare(*previous, names, row.get("final"), "original migration state before fault");
            // Fault transition may discard execution locals. The last safe
            // checkpoint records the original state immediately before the fault.
            if (row.get("final").get("heap_used").kind == network::Value::Kind::Integer) {
                check(previous->arrays_heap_usage && previous->locals_heap_usage &&
                          *previous->arrays_heap_usage ==
                              network::osd_integer(row.get("final").get("arrays_heap_used")) &&
                          *previous->locals_heap_usage ==
                              network::osd_integer(row.get("final").get("locals_heap_used")),
                      "Original migration pre-fault heap ledgers differ");
            }
            finished = true;
            break;
        }
        check(result.error.empty() && result.status != LslRunStatus::Sleeping,
              "Original migration replayed Sleep or faulted: " + result.error);
        const auto current = imported.checkpoint();
        check(current.has_value(), "Imported generated-array resume checkpoint missing");
        if (result.status == LslRunStatus::Idle) {
            check(expected_fault.kind != network::Value::Kind::Map, "Original migration fault became success");
            compare(*current, names, row.get("final"), "original migration resumed output");
            heap(imported, *current, row.get("final"), "original migration resumed output");
            check(imported.run(1).status == LslRunStatus::Idle, "Original migration entry replayed");
            finished = true;
            break;
        }
        previous = current;
    }
    check(finished && sleeps == 0, "Original migration replayed a source Sleep or exceeded the execution bound");
}

// These negatives are malformed migration controls. The primitive Object-lane
// rejection additionally has an actual original RecvArrays backend observation
// in each fixture; the class-lane Object[] rejection is a CLR cast boundary.
void malformed_lanes(const std::filesystem::path &folder) {
    const auto source = read(folder / "source.lsl");
    const auto row = network::parse_osd_json(read(folder / "metadata.json"));
    const auto wire = read(folder / "snapshot.bin");
    const auto object = decode_yengine_object_code(bytes(read(folder / "original.yobj")));
    const auto compiled = compile_lsl(source);
    check(object && compiled, "Malformed covariance setup failed");
    const auto injection = network::parse_osd_json(read(folder / "injection-observations.json"));
    check(injection.get("primitiveControlError").get("type").text == "System.InvalidCastException" &&
              injection.get("primitiveControlError").get("message").text ==
                  "Unable to cast object of type 'System.Int32[]' to type 'System.Object[]'.",
          "Original primitive covariance rejection provenance changed");
    LslHost host;
    host.heap_limit_bytes = 100000000;
    std::int64_t clock = 638000000000000000LL;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    for (const auto kind : {"primitive Object lane", "Object class lane"}) {
        auto saved = decode_yengine_snapshot(bytes(wire));
        check(saved.has_value(), "Original covariance snapshot decode failed");
        const bool primitive = std::string_view(kind) == "primitive Object lane";
        const auto slot = primitive ? 5u : 9u;
        auto lane = std::make_shared<YEngineManagedValue>(*saved->managed_globals[slot]);
        lane->element_type = primitive ? "in" : "ob";
        if (primitive) {
            YEngineManagedArray values;
            for (std::int32_t number : {1, 2}) {
                auto value = std::make_shared<YEngineManagedValue>();
                value->kind = YEngineManagedValue::Kind::Integer;
                value->value = number;
                values.push_back(value);
            }
            lane->value = std::move(values);
        }
        saved->managed_globals[slot] = lane;
        LslInstance target(compiled.program, {}, host);
        check(target.run(10000).status == LslRunStatus::Sleeping, "Live covariance control did not pause");
        const auto before = target.checkpoint();
        check(before.has_value(), "Live covariance checkpoint absent");
        const auto encoded = encode_lsl_checkpoint(*before);
        check(!target.restore_yengine_snapshot(*saved, row.get("source_hash").text, source, *object),
              std::string(kind) + " unexpectedly admitted");
        const auto after = target.checkpoint();
        check(after && encode_lsl_checkpoint(*after) == encoded, std::string(kind) + " rejection changed live state");
    }
}

void fixture(const std::filesystem::path &root) {
    const auto text = read(root / "verification.json");
    check(framework::sha256_hex(text) == "9594f921cfb03f1c486b9310aa5f9df32c0d831c1dbd1a85ab64601e74d8fcc5",
          "Original covariance proof changed");
    const auto proof = network::parse_osd_json(text);
    check(proof.get("passed").boolean && !proof.get("nativeParityVerified").boolean,
          "Original covariance proof scope changed");
    unsigned count = 0;
    for (const auto platform : {"windows", "linux"}) {
        const auto &rows = proof.get("platforms").get(platform).get("cases").array;
        check(rows.size() == 3, "Original covariance inventory changed");
        for (const auto &row : rows) {
            const auto folder = root / platform / row.get("name").text;
            for (const auto &[file, field] : std::array<std::pair<const char *, const char *>, 3>{
                     {{"original.yobj", "objectSha256"},
                      {"injection-observations.json", "injectionSha256"},
                      {"restored-container.json", "restoredContainerSha256"}}})
                check(framework::sha256_hex(read(folder / file)) == row.get(field).text,
                      "Original covariance observation artifact changed");
            import_case(folder, row);
            malformed_lanes(folder);
            ++count;
        }
    }
    check(count == 6, "Original covariance platform coverage incomplete");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected original covariance fixture directory");
        fixture(argv[1]);
        collect_lsl_fixed_arrays();
        std::cout << "Matched six backend-injected original Object-lane covariance imports and resumes; malformed "
                     "lanes rejected atomically.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
