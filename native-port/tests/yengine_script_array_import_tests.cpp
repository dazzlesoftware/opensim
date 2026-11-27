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
            const auto *integer = std::get_if<std::int32_t>(&actual);
            check(integer && *integer == network::osd_integer(expected.get("value")), where + ": Int32 differs");
        } else if (type == "string") {
            const auto units = lsl_string_code_units(actual);
            const auto &expected_units = expected.get("utf16").array;
            check(units && units->size() == expected_units.size(), where + ": string length differs");
            for (std::size_t i = 0; i < expected_units.size(); ++i)
                check((*units)[i] == network::osd_integer(expected_units[i]), where + ": UTF16 unit differs");
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

void malformed_context(const std::filesystem::path &folder) {
    const auto source = read(folder / "source.lsl");
    const auto row = network::parse_osd_json(read(folder / "metadata.json"));
    const auto snapshot_wire = read(folder / "snapshot.bin");
    const auto object_wire = read(folder / "original.yobj");
    const auto compiled = compile_lsl(source);
    check(bool(compiled), "Malformed-context native control rejected");
    std::int64_t clock = 638000000000000000LL;
    LslHost host;
    host.heap_limit_bytes = 100000000;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    using Mutation = std::function<void(YEngineSnapshot &, YEngineObjectCode &, std::string &)>;
    auto change_class = [](YEngineSnapshot &snapshot, const auto &edit) {
        auto globals = std::make_shared<YEngineManagedValue>(*snapshot.managed_globals[9]);
        auto &values = std::get<YEngineManagedArray>(globals->value);
        check(!values.empty() && values[0]->kind == YEngineManagedValue::Kind::ScriptClass,
              "Malformed-context fixture lacks original script class");
        auto wrapper = std::make_shared<YEngineManagedValue>(*values[0]);
        edit(std::get<YEngineManagedScriptClass>(wrapper->value));
        values[0] = wrapper;
        snapshot.managed_globals[9] = globals;
    };
    const std::vector<std::pair<std::string, Mutation>> mutations{
        {"global class lane CLR array type",
         [](auto &snapshot, auto &, auto &) {
             auto lane = std::make_shared<YEngineManagedValue>(*snapshot.managed_globals[9]);
             lane->element_type = "ob";
             snapshot.managed_globals[9] = lane;
         }},
        {"global class lane node kind",
         [](auto &snapshot, auto &, auto &) {
             auto lane = std::make_shared<YEngineManagedValue>(*snapshot.managed_globals[9]);
             lane->kind = YEngineManagedValue::Kind::LslList;
             snapshot.managed_globals[9] = lane;
         }},
        {"missing class index",
         [=](auto &snapshot, auto &, auto &) {
             change_class(snapshot, [](auto &value) { value.class_index = 31337; });
         }},
        {"delegate as class context", [](auto &, auto &object, auto &) { object.types.at("integer[]").kind = 1; }},
        {"missing class table entry", [](auto &, auto &object, auto &) { object.types.erase("integer[]"); }},
        {"class instance shape",
         [](auto &, auto &object, auto &) { object.types.at("integer[]").instance_sizes[3] = 2; }},
        {"class element type",
         [](auto &, auto &object, auto &) { object.types.at("integer[]").element_type = "float"; }},
        {"class rank", [](auto &, auto &object, auto &) { object.types.at("integer[]").array_rank = 2; }},
        {"dimension storage type",
         [=](auto &snapshot, auto &, auto &) {
             change_class(snapshot, [](auto &value) {
                 auto lane = std::make_shared<YEngineManagedValue>(*value.instance_arrays[3]);
                 auto &dimensions = std::get<YEngineManagedArray>(lane->value);
                 auto bad = std::make_shared<YEngineManagedValue>();
                 bad->kind = YEngineManagedValue::Kind::Double;
                 bad->value = 1.0;
                 dimensions[0] = bad;
                 value.instance_arrays[3] = lane;
             });
         }},
        {"source hash", [](auto &, auto &, auto &hash) { hash[0] = hash[0] == 'A' ? 'B' : 'A'; }},
        {"object-code source hash",
         [](auto &, auto &object, auto &) { object.source_hash[0] = object.source_hash[0] == 'A' ? 'B' : 'A'; }},
    };
    std::vector<std::string> failures;
    for (const auto &[name, mutate] : mutations) {
        try {
            auto snapshot = decode_yengine_snapshot(bytes(snapshot_wire));
            auto object = decode_yengine_object_code(bytes(object_wire));
            check(snapshot && object, "Malformed-context original decoder rejected");
            auto hash = row.get("source_hash").text;
            mutate(*snapshot, *object, hash);
            LslInstance target(compiled.program, {}, host);
            check(target.run(10000).status == LslRunStatus::Sleeping, "Malformed-context live VM failed to suspend");
            const auto before = target.checkpoint();
            check(before.has_value(), "Malformed-context live VM checkpoint missing");
            const auto original = encode_lsl_checkpoint(*before);
            check(!target.restore_yengine_snapshot(*snapshot, hash, source, *object),
                  "Malformed class context was admitted");
            const auto after = target.checkpoint();
            check(after && encode_lsl_checkpoint(*after) == original,
                  "Rejected class-context migration changed the live VM state");
        } catch (const std::exception &error) {
            failures.push_back(name + ": " + error.what());
        }
    }
    if (!failures.empty()) {
        std::string message = "Malformed-context checks failed:";
        for (const auto &failure : failures)
            message += "\n  " + failure;
        throw std::runtime_error(message);
    }
}

struct Corpus {
    std::filesystem::path root;
    std::string_view proof_hash, suffix;
    unsigned cases;
};

unsigned corpus(const Corpus &fixture, std::vector<std::string> &failures) {
    const auto proof_text = read(fixture.root / "verification.json");
    check(framework::sha256_hex(proof_text) == fixture.proof_hash, "Original generated-array migration proof changed");
    const auto proof = network::parse_osd_json(proof_text);
    check(proof.get("passed").boolean && !proof.get("nativeParityVerified").boolean,
          "Original migration proof scope differs");
    unsigned passed = 0;
    for (const auto platform : {"windows", "linux"}) {
        const auto &rows = proof.get("platforms").get(platform).get("cases").array;
        check(rows.size() == fixture.cases, "Original migration case inventory differs");
        auto platform_root = fixture.root / platform;
        if (!std::filesystem::exists(platform_root))
            platform_root = fixture.root / (std::string(platform) + std::string(fixture.suffix));
        for (const auto &row : rows) {
            const auto label = fixture.root.filename().string() + "/" + platform + "/" + row.get("name").text;
            try {
                import_case(platform_root / row.get("name").text, row);
                ++passed;
                std::cout << "PASS " << label << '\n';
            } catch (const std::exception &error) {
                failures.push_back(label + ": " + error.what());
            }
        }
    }
    return passed;
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 5, "Expected base-array, lvalue, heap and receiver fixture directories");
        const std::array fixtures{
            Corpus{argv[1], "d8277e63982ff02c77e878ab7a61e45b22c5a97b9c97bc482766720dc4143311", "", 4},
            Corpus{argv[2], "1db57d484e53fcb2db5a15eeaa9d46f9478622df96aa3cbe2a7ac092be80778d", "-frozen-observations",
                   4},
            Corpus{argv[3], "00151d05d20e4b62d5772b8bbb035d663c791b760fe54ef6d15a733f566b6fec", "", 4},
            Corpus{argv[4], "6218c0c087c12eb9610a34b36233f2f7f16b21678493cab0d2316dffd202a732", "-observed", 5},
        };
        std::vector<std::string> failures;
        unsigned passed = 0;
        for (const auto &fixture : fixtures) {
            try {
                passed += corpus(fixture, failures);
            } catch (const std::exception &error) {
                failures.push_back(fixture.root.string() + ": " + error.what());
            }
        }
        try {
            malformed_context(std::filesystem::path(argv[1]) / "windows" / "rank1_members");
        } catch (const std::exception &error) {
            failures.push_back(error.what());
        }
        collect_lsl_fixed_arrays();
        collect_yengine_managed_cycles();
        std::cout << "Imported and resumed " << passed << "/34 actual original generated-array captures.\n";
        for (const auto &failure : failures)
            std::cerr << "FAIL " << failure << '\n';
        return failures.empty() && passed == 34 ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
