// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include "opensim/script/lsl_script_class.hpp"
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
    check(checkpoint.arrays_heap_usage && checkpoint.locals_heap_usage, where + ": heap ledger missing");
    const auto global_heap = network::osd_integer(expected.get("arrays_heap_used"));
    const auto local_heap = network::osd_integer(expected.get("locals_heap_used"));
    check(*checkpoint.arrays_heap_usage == global_heap && *checkpoint.locals_heap_usage == local_heap,
          where + ": global/local heap differs actual=" + std::to_string(*checkpoint.arrays_heap_usage) + "/" +
              std::to_string(*checkpoint.locals_heap_usage) + " expected=" + std::to_string(global_heap) + "/" +
              std::to_string(local_heap));
    check(global_heap + local_heap == network::osd_integer(expected.get("heap_used")),
          where + ": original heap observation inconsistent");
    GraphComparison graph;
    for (std::size_t i = 0; i < names.size(); ++i)
        graph.value(checkpoint.globals[i], globals.get(names[i]), where + "." + names[i]);
}

std::unique_ptr<LslInstance> restore(std::shared_ptr<const LslProgram> program, const LslHost &host,
                                     const LslCheckpoint &checkpoint, bool persistent) {
    const auto encoded = persistent ? encode_persistent_lsl_checkpoint(checkpoint) : encode_lsl_checkpoint(checkpoint);
    const auto decoded = decode_lsl_checkpoint(encoded);
    auto instance = std::make_unique<LslInstance>(std::move(program), LslContext{}, host);
    check(decoded && instance->restore_checkpoint(*decoded), "Native array checkpoint restore rejected");
    return instance;
}

void source_case(const std::filesystem::path &folder, const network::Value &proof_row) {
    const auto source = read(folder / "source.lsl");
    const auto metadata_text = read(folder / "metadata.json");
    check(framework::sha256_hex(read(folder / "snapshot.bin")) == proof_row.get("snapshotSha256").text,
          "Original heap snapshot artifact changed");
    check(framework::sha256_hex(source) == proof_row.get("sourceSha256").text &&
              framework::sha256_hex(metadata_text) == proof_row.get("metadataSha256").text,
          "Original array source/metadata artifact changed: " + folder.string());
    const auto metadata = network::parse_osd_json(metadata_text);
    check(metadata.get("accepted").boolean && metadata.get("suspended").boolean,
          "Original array source control was not accepted and suspended");
    const auto names = declarations(source);
    const auto compiled = compile_lsl(source);
    std::string diagnostics;
    for (const auto &diagnostic : compiled.diagnostics)
        diagnostics += " " + diagnostic.message;
    check(bool(compiled), folder.filename().string() + ": native source rejected:" + diagnostics);
    std::int64_t clock = 100000000;
    unsigned sleeps = 0;
    LslHost host;
    host.heap_limit_bytes = 100000000;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](std::int32_t) { ++sleeps; };
    auto live = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
    bool suspended = false;
    for (unsigned step = 0; step < 10000; ++step) {
        const auto result = live->run(1);
        check(result.error.empty(), "Array source faulted before capture: " + result.error);
        if (result.status == LslRunStatus::Sleeping) {
            suspended = true;
            break;
        }
        check(result.status != LslRunStatus::Idle, "Array source completed before expected Sleep");
    }
    check(suspended && sleeps == 1, "Array source did not suspend exactly once");
    const auto captured = live->checkpoint();
    check(captured && captured->sleep_remaining > 0, "Array Sleep checkpoint missing");
    compare(*captured, names, metadata.get("initial"), folder.filename().string() + " native capture");
    auto internal = restore(compiled.program, host, *captured, false);
    compare(*internal->checkpoint(), names, metadata.get("initial"), "internal array reload");
    const auto persistent = live->persistent_checkpoint();
    check(persistent.has_value(), "Array persistent checkpoint unavailable");
    auto external = restore(compiled.program, host, *persistent, true);
    compare(*external->checkpoint(), names, metadata.get("after_load"), "persistent array reload");
    std::array<std::unique_ptr<LslInstance>, 3> paths{std::move(live), std::move(internal), std::move(external)};
    for (std::size_t path = 0; path < paths.size(); ++path) {
        auto &instance = paths[path];
        clock += instance->checkpoint()->sleep_remaining + 1;
        const auto &expected = metadata.get(path == 2 ? "final" : "live_resume");
        const auto &expected_fault = metadata.get(path == 2 ? "resume_exception" : "live_resume_exception");
        auto previous = instance->checkpoint();
        bool finished = false;
        for (unsigned step = 0; step < 10000; ++step) {
            const auto result = instance->run(1);
            if (result.status == LslRunStatus::Faulted) {
                check(expected_fault.get("type").text == "System.InvalidCastException",
                      "Unexpected native array fault: " + result.error);
                check(
                    result.error ==
                        "InvalidCastException: Unable to cast object of type 'System.String' to type 'System.Int32[]'.",
                    "Native array cast exception category/text differs: " + result.error);
                check(previous.has_value(), "Array pre-fault checkpoint missing");
                compare(*previous, names, expected, "array state before expected cast fault");
                finished = true;
                break;
            }
            check(result.error.empty() && result.status != LslRunStatus::Sleeping,
                  "Array resume replayed Sleep or faulted: " + result.error);
            const auto current = instance->checkpoint();
            check(current.has_value(), "Array resume checkpoint missing");
            if (result.status == LslRunStatus::Idle) {
                check(expected_fault.kind != network::Value::Kind::Map, "Original array fault became success");
                compare(*current, names, expected, "native array resumed output");
                check(instance->run(1).status == LslRunStatus::Idle && sleeps == 1, "Array resume replayed effects");
                finished = true;
                break;
            }
            previous = current;
        }
        check(finished, "Array resume exceeded execution bound");
    }
}

void fixture(const std::filesystem::path &root) {
    const auto proof_text = read(root / "verification.json");
    check(framework::sha256_hex(proof_text) == "00151d05d20e4b62d5772b8bbb035d663c791b760fe54ef6d15a733f566b6fec",
          "Original generated-array heap proof changed");
    const auto proof = network::parse_osd_json(proof_text);
    check(proof.get("passed").boolean && !proof.get("nativeParityVerified").boolean,
          "Original heap proof scope changed");
    unsigned cases = 0;
    for (const auto platform : {"windows", "linux"}) {
        const auto &rows = proof.get("platforms").get(platform).get("cases").array;
        check(rows.size() == 4, "Original generated-array heap inventory changed");
        for (const auto &row : rows) {
            source_case(root / platform / row.get("name").text, row);
            ++cases;
        }
    }
    check(cases == 8, "Original heap platform coverage incomplete");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected generated-array heap fixture directory");
        fixture(argv[1]);
        collect_lsl_fixed_arrays();
        std::cout << "Matched eight original generated-array heap observations through native live/internal/persistent "
                     "paths.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
