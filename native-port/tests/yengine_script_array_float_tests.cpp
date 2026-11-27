// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include "opensim/script/lsl_script_class.hpp"
#include "opensim/script/yengine_object_code.hpp"
#include <algorithm>
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
        R"(\b(?:(?:integer|float)\s*\[\s*\]|integer|float|object|string)\s+([A-Za-z_][A-Za-z_0-9]*)\s*(?:=|;))");
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
        } else if (type == "float") {
            const auto *number = std::get_if<double>(&actual);
            check(number &&
                      std::bit_cast<std::uint64_t>(*number) == std::stoull(expected.get("bits").text, nullptr, 16),
                  where + ": binary64 bits differ");
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
                      instance.descriptor->array_rank == network::osd_integer(expected.get("rank")) &&
                      instance.descriptor->class_index == network::osd_integer(expected.get("class_index")),
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
            const auto expected_kind = expected.get("element_type").text == "System.Double"
                                           ? LslFixedArrayKind::Float
                                           : LslFixedArrayKind::Integer;
            check((expected.get("element_type").text == "System.Double" ||
                   expected.get("element_type").text == "System.Int32") &&
                      (*array)->kind == expected_kind,
                  where + ": typed raw backing family differs");
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

bool retained_double(const LslCheckpoint &state) {
    const auto expected = std::bit_cast<std::uint64_t>(7.125);
    auto values = [&](const auto &entries) {
        return std::ranges::any_of(entries, [&](const auto &value) {
            const auto *number = std::get_if<double>(&value);
            return number && std::bit_cast<std::uint64_t>(*number) == expected;
        });
    };
    auto journal = [&](const auto &entries) {
        return std::ranges::any_of(entries, [&](const auto &entry) {
            const auto *number = std::get_if<double>(&entry.value);
            return number && std::bit_cast<std::uint64_t>(*number) == expected;
        });
    };
    if (values(state.locals) || journal(state.evaluations))
        return true;
    for (const auto &frame : state.call_stack)
        if (values(frame.locals) || journal(frame.evaluations))
            return true;
    for (const auto &frame : state.resume_frames)
        if (values(frame.locals) || journal(frame.evaluations))
            return true;
    return false;
}

std::unique_ptr<LslInstance> restore_native(std::shared_ptr<const LslProgram> program, const LslHost &host,
                                            const LslCheckpoint &saved, bool persistent) {
    const auto wire = persistent ? encode_persistent_lsl_checkpoint(saved) : encode_lsl_checkpoint(saved);
    const auto decoded = decode_lsl_checkpoint(wire);
    check(decoded && (persistent || encode_lsl_checkpoint(*decoded) == wire),
          "Float array native checkpoint codec rejected or changed internal bytes");
    auto instance = std::make_unique<LslInstance>(std::move(program), LslContext{}, host);
    check(instance->restore_checkpoint(*decoded), "Float array native checkpoint restore rejected");
    return instance;
}

void resume(LslInstance &instance, std::int64_t &clock, const std::vector<std::string> &names,
            const network::Value &row, bool migrated, unsigned &sleeps, unsigned expected_sleeps,
            const std::string &where) {
    const auto &stage = row.get(migrated ? "final" : "live_resume");
    const auto &fault = row.get(migrated ? "resume_exception" : "live_resume_exception");
    auto previous = instance.checkpoint();
    check(previous.has_value(), where + ": resume checkpoint missing");
    clock += previous->sleep_remaining + 1;
    for (unsigned step = 0; step < 10000; ++step) {
        const auto result = instance.run(1);
        if (result.status == LslRunStatus::Faulted) {
            const auto type = fault.get("type").text;
            check(type.starts_with("System.") && result.error == type.substr(7) + ": " + fault.get("message").text,
                  where + ": exact original fault differs: " + result.error);
            check(previous.has_value(), where + ": pre-fault checkpoint missing");
            compare(*previous, names, stage, where + " before original fault");
            check(previous->arrays_heap_usage && previous->locals_heap_usage &&
                      *previous->arrays_heap_usage == network::osd_integer(stage.get("arrays_heap_used")) &&
                      *previous->locals_heap_usage == network::osd_integer(stage.get("locals_heap_used")),
                  where + ": pre-fault global/local heap ledgers differ");
            check(sleeps == expected_sleeps, where + ": Sleep prefix replayed before fault");
            return;
        }
        check(result.error.empty() && result.status != LslRunStatus::Sleeping,
              where + ": resume faulted or replayed Sleep: " + result.error);
        const auto current = instance.checkpoint();
        check(current.has_value(), where + ": resume checkpoint missing");
        if (result.status == LslRunStatus::Idle) {
            check(fault.kind != network::Value::Kind::Map, where + ": original cast failure became success");
            compare(*current, names, stage, where + " completed");
            heap(instance, *current, stage, where + " completed");
            check(instance.run(1).status == LslRunStatus::Idle && sleeps == expected_sleeps,
                  where + ": completed source entry replayed");
            return;
        }
        previous = current;
    }
    throw std::runtime_error(where + ": resume exceeded execution bound");
}

unsigned source_case(const std::filesystem::path &folder, const network::Value &proof_row,
                     std::vector<std::string> &failures) {
    const auto source = read(folder / "source.lsl");
    const auto metadata_text = read(folder / "metadata.json");
    const auto snapshot_wire = read(folder / "snapshot.bin");
    const auto object_wire = read(folder / "original.yobj");
    check(framework::sha256_hex(source) == proof_row.get("sourceSha256").text &&
              framework::sha256_hex(metadata_text) == proof_row.get("metadataSha256").text &&
              framework::sha256_hex(snapshot_wire) == proof_row.get("snapshotSha256").text &&
              framework::sha256_hex(object_wire) == proof_row.get("originalObjectSha256").text,
          "Original float array fixture artifact changed");
    const auto row = network::parse_osd_json(metadata_text);
    check(row.get("accepted").boolean && row.get("suspended").boolean && row.get("migrated").boolean,
          "Original float array capture scope differs");
    const auto compiled = compile_lsl(source);
    std::string diagnostics;
    for (const auto &diagnostic : compiled.diagnostics)
        diagnostics += " " + diagnostic.message;
    check(bool(compiled), "Original float array source rejected:" + diagnostics);
    const auto names = declarations(source);
    const auto prefix = folder.parent_path().filename().string() + "/" + folder.filename().string();
    const bool needs_retained_double = proof_row.get("name").text == "float_receiver_compound_base_yield";
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.heap_limit_bytes = 100000000;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](std::int32_t) { ++sleeps; };
    auto live = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
    bool suspended = false;
    for (unsigned step = 0; step < 10000; ++step) {
        const auto result = live->run(1);
        check(result.error.empty(), "Float array source faulted before capture: " + result.error);
        if (result.status == LslRunStatus::Sleeping) {
            suspended = true;
            break;
        }
        check(result.status != LslRunStatus::Idle, "Float source completed before expected Sleep");
    }
    check(suspended && sleeps == 1, "Float source did not suspend exactly once");
    const auto capture = live->checkpoint();
    check(capture.has_value(), "Float source capture missing");
    compare(*capture, names, row.get("initial"), prefix + " live capture");
    heap(*live, *capture, row.get("initial"), prefix + " live capture");
    if (needs_retained_double)
        check(retained_double(*capture), "Native Base Sleep lost completed RHS Double7.125");

    struct Path {
        std::string name;
        bool migrated;
        std::unique_ptr<LslInstance> instance;
    };

    std::vector<Path> paths;
    paths.push_back({"live", false, std::move(live)});
    auto add_path = [&](std::string name, bool migrated, auto &&create) {
        try {
            auto instance = create();
            const auto state = instance->checkpoint();
            check(state.has_value(), prefix + "/" + name + ": restored capture missing");
            compare(*state, names, row.get(migrated ? "after_load" : "initial"), prefix + "/" + name + " capture");
            heap(*instance, *state, row.get(migrated ? "after_load" : "initial"), prefix + "/" + name + " capture");
            if (needs_retained_double)
                check(retained_double(*state), prefix + "/" + name + ": retained Double7.125 missing");
            paths.push_back({std::move(name), migrated, std::move(instance)});
        } catch (const std::exception &error) {
            failures.push_back(prefix + "/" + name + ": " + error.what());
        }
    };
    add_path("internal", false, [&] { return restore_native(compiled.program, host, *capture, false); });
    add_path("persistent", true, [&] {
        const auto state = paths[0].instance->persistent_checkpoint();
        check(state.has_value(), "Float persistent capture unavailable");
        return restore_native(compiled.program, host, *state, true);
    });
    add_path("original import", true, [&] {
        const auto object = decode_yengine_object_code(bytes(object_wire));
        const auto saved = decode_yengine_snapshot(bytes(snapshot_wire));
        check(object && saved && object->source_hash == row.get("source_hash").text,
              "Original float object/snapshot context rejected");
        auto instance = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
        check(instance->restore_yengine_snapshot(*saved, row.get("source_hash").text, source, *object),
              "Original float class/frame migration rejected");
        return instance;
    });
    unsigned passed = 0;
    for (auto &path : paths) {
        try {
            resume(*path.instance, clock, names, row, path.migrated, sleeps, 1, prefix + "/" + path.name);
            ++passed;
            std::cout << "PASS " << prefix << "/" << path.name << '\n';
        } catch (const std::exception &error) {
            failures.push_back(prefix + "/" + path.name + ": " + error.what());
        }
    }
    return passed;
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected original float array fixture directory");
        const std::filesystem::path root = argv[1];
        const auto proof_text = read(root / "verification.json");
        check(framework::sha256_hex(proof_text) == "b9582b74360a760611c21c982c051b7550dbfd424a98746308d738a08b1020a1",
              "Original float array proof changed");
        const auto proof = network::parse_osd_json(proof_text);
        check(proof.get("passed").boolean && !proof.get("nativeParityVerified").boolean &&
                  framework::sha256_hex(read(root / "Reference.cs")) == proof.get("generatorSha256").text &&
                  framework::sha256_hex(read(root / "decode.py")) == proof.get("decoderSha256").text &&
                  framework::sha256_hex(read(root / "verify.py")) == proof.get("verifierSha256").text,
              "Original float array generator/decoder/verifier scope changed");
        std::vector<std::string> failures;
        unsigned passed = 0;
        for (const auto platform : {"windows", "linux"}) {
            const auto &platform_proof = proof.get("platforms").get(platform);
            check(framework::sha256_hex(read(root / platform / "metadata.json")) ==
                          platform_proof.get("metadataSha256").text &&
                      platform_proof.get("cases").array.size() == 8,
                  "Original float platform metadata/inventory changed");
            for (const auto &row : platform_proof.get("cases").array) {
                try {
                    passed += source_case(root / platform / row.get("name").text, row, failures);
                } catch (const std::exception &error) {
                    failures.push_back(std::string(platform) + "/" + row.get("name").text + ": " + error.what());
                }
            }
        }
        collect_lsl_fixed_arrays();
        collect_yengine_managed_cycles();
        std::cout << "Verified " << passed << "/64 float array source/checkpoint/original-import paths.\n";
        for (const auto &failure : failures)
            std::cerr << "FAIL " << failure << '\n';
        return passed == 64 && failures.empty() ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
