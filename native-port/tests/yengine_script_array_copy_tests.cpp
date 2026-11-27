// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include "opensim/script/lsl_script_class.hpp"
#include "opensim/script/yengine_object_code.hpp"
#include "opensim/script/yengine_reference_projection.hpp"
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
        R"(\b(?:(?:integer|float|char)(?:\s*\[\s*(?:,\s*)*\])+|integer|float|char|object|string)\s+([A-Za-z_][A-Za-z_0-9]*)\s*(?:=|;))");
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
        const LslValue *scalar = &actual;
        if (const auto *box = std::get_if<std::shared_ptr<const LslObjectBox>>(&actual); box && *box)
            scalar = &(*box)->value;
        if (type == "null") {
            check(std::holds_alternative<LslNullObject>(actual), where + ": null Object differs");
        } else if (type == "integer") {
            const auto *integer = std::get_if<std::int32_t>(scalar);
            check(integer && *integer == network::osd_integer(expected.get("value")), where + ": Int32 differs");
        } else if (type == "float") {
            const auto *number = std::get_if<double>(scalar);
            check(number &&
                      std::bit_cast<std::uint64_t>(*number) == std::stoull(expected.get("bits").text, nullptr, 16),
                  where + ": binary64 bits differ");
        } else if (type == "char") {
            const auto *character = std::get_if<LslCharacter>(scalar);
            check(character && character->value == network::osd_integer(expected.get("utf16")),
                  where + ": UTF16 character differs");
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
            const auto element = expected.get("element_type").text;
            const auto expected_kind = element == "System.Double"   ? LslFixedArrayKind::Float
                                       : element == "System.Char"   ? LslFixedArrayKind::Character
                                       : element == "System.Object" ? LslFixedArrayKind::Object
                                                                    : LslFixedArrayKind::Integer;
            check((element == "System.Double" || element == "System.Char" || element == "System.Object" ||
                   element == "System.Int32") &&
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

std::string native_fault(const network::Value &fault) {
    auto type = fault.get("type").text;
    check(type.starts_with("System."), "Unreviewed original array-copy exception family");
    auto message = fault.get("message").text;
    for (std::size_t at = 0; (at = message.find("\r\n", at)) != std::string::npos;)
        message.replace(at, 2, "\n");
#ifdef _WIN32
    for (std::size_t at = 0; (at = message.find('\n', at)) != std::string::npos; at += 2)
        message.replace(at, 1, "\r\n");
#endif
    return type.substr(7) + ": " + message;
}

struct Declaration {
    std::string primitive, suffix, name;
};

std::vector<Declaration> typed_declarations(std::string source) {
    source = global_declaration_source(std::move(source));
    const std::regex declaration(
        R"(\b(integer|float|char|object|string)((?:\s*\[\s*(?:,\s*)*\])*)\s+([A-Za-z_][A-Za-z_0-9]*)\s*(?:=|;))");
    std::vector<Declaration> result;
    for (auto it = std::sregex_iterator(source.begin(), source.end(), declaration); it != std::sregex_iterator(); ++it)
        result.push_back({(*it)[1].str(), (*it)[2].str(), (*it)[3].str()});
    return result;
}

struct Projected {
    LslCheckpoint state;
    YEngineReferenceProjection projection;
};

Projected project_capture(const YEngineSnapshot &snapshot, const YEngineObjectCode &object, const std::string &source) {
    YEngineScriptClassProjectionContext context;
    for (const auto &[name, type] : object.types) {
        if (!type.array_rank)
            continue;
        auto descriptor = std::make_shared<LslScriptClassDescriptor>(
            *make_lsl_script_array_descriptor(object.source_hash, type.element_type, type.array_rank));
        check(descriptor->canonical_name == name, "Original array descriptor canonical spelling differs");
        descriptor->class_index = type.index;
        for (std::size_t lane = 0; lane < type.instance_sizes.size(); ++lane)
            check(type.instance_sizes[lane] >= 0 &&
                      descriptor->instance_sizes[lane] == static_cast<std::uint32_t>(type.instance_sizes[lane]),
                  "Original generated array physical shape differs");
        check(context.classes.emplace(type.index, descriptor).second, "Original generated class indices collided");
    }
    context.check_heap_total = [](std::int32_t) { return true; };
    Projected result{{}, YEngineReferenceProjection(std::move(context))};
    std::array<std::size_t, 11> consumed{};
    for (const auto &declaration : typed_declarations(source)) {
        const auto lane = !declaration.suffix.empty()          ? 9u
                          : declaration.primitive == "integer" ? 3u
                          : declaration.primitive == "float"   ? 2u
                          : declaration.primitive == "char"    ? 1u
                          : declaration.primitive == "string"  ? 7u
                                                               : 5u;
        const auto *raw = snapshot.managed_globals[lane]
                              ? std::get_if<YEngineManagedArray>(&snapshot.managed_globals[lane]->value)
                              : nullptr;
        check(raw && consumed[lane] < raw->size(), "Original capture global lane inventory differs");
        const auto type = !declaration.suffix.empty()          ? YEngineReferenceType::ScriptClass
                          : declaration.primitive == "integer" ? YEngineReferenceType::Integer
                          : declaration.primitive == "float"   ? YEngineReferenceType::Float
                          : declaration.primitive == "char"    ? YEngineReferenceType::Character
                          : declaration.primitive == "string"  ? YEngineReferenceType::String
                                                               : YEngineReferenceType::Object;
        const auto value = result.projection.project(raw->at(consumed[lane]++), type);
        check(value.has_value(), "Original captured array graph projection rejected");
        result.state.globals.push_back(*value);
    }
    return result;
}

void direct_copy(const std::string &source, const network::Value &row, const YEngineSnapshot &snapshot,
                 const YEngineObjectCode &object) {
    auto projected = project_capture(snapshot, object, source);
    const auto names = declarations(source);
    compare(projected.state, names, row.get("after_load"), "original projected capture before direct copy");
    auto global = [&](std::string_view name) -> LslValue & {
        const auto found = std::find(names.begin(), names.end(), name);
        check(found != names.end(), "Copy argument global declaration missing");
        return projected.state.globals.at(static_cast<std::size_t>(found - names.begin()));
    };
    LslValue source_value, destination_value;
    std::int32_t source_start = 0, destination_start = 0, count = 0;
    if (row.get("name").text == "copy_argument_count_yield_rebind") {
        check(!snapshot.managed_frames.empty() && snapshot.managed_frames[0].function == "default state_entry",
              "Original retained array-copy caller frame missing");
        const auto *slots = std::get_if<YEngineManagedArray>(&snapshot.managed_frames[0].slots->value);
        check(slots && slots->size() == 6, "Original retained array-copy argument inventory differs");
        const auto source_argument = projected.projection.project(slots->at(1), YEngineReferenceType::ScriptClass);
        const auto destination_argument = projected.projection.project(slots->at(3), YEngineReferenceType::ScriptClass);
        check(source_argument && destination_argument &&
                  std::get<Class>(*source_argument) == std::get<Class>(global("source_alias")) &&
                  std::get<Class>(*destination_argument) == std::get<Class>(global("destination_alias")),
              "Original retained arguments lost global alias identity");
        source_value = *source_argument;
        destination_value = *destination_argument;
        count = 1; // Count's source continuation returns one after the captured Sleep.
    } else {
        const std::regex call(
            R"(xmrArrayCopy\(([A-Za-z_][A-Za-z_0-9]*),(-?[0-9]+),([A-Za-z_][A-Za-z_0-9]*),(-?[0-9]+),(-?[0-9]+)\))");
        std::smatch arguments;
        check(std::regex_search(source, arguments, call), "Original bounded copy call argument spelling differs");
        source_value = global(arguments[1].str());
        source_start = std::stoi(arguments[2].str());
        destination_value = global(arguments[3].str());
        destination_start = std::stoi(arguments[4].str());
        count = std::stoi(arguments[5].str());
    }
    std::string error;
    try {
        lsl_script_array_copy(source_value, source_start, destination_value, destination_start, count);
    } catch (const std::exception &failure) {
        error = failure.what();
    }
    const auto &fault = row.get("resume_exception");
    check(error == (fault.kind == network::Value::Kind::Map ? native_fault(fault) : std::string{}),
          "Direct copy exact fault differs: " + error);
    // The helper does not execute the caller's trailing after=1 assignment.
    GraphComparison graph;
    for (std::size_t index = 0; index < names.size(); ++index)
        if (names[index] != "after")
            graph.value(projected.state.globals[index], row.get("final").get("globals").get(names[index]),
                        "direct post-copy state." + names[index]);
}

std::unique_ptr<LslInstance> restore_native(std::shared_ptr<const LslProgram> program, const LslHost &host,
                                            const LslCheckpoint &state, bool persistent) {
    const auto wire = persistent ? encode_persistent_lsl_checkpoint(state) : encode_lsl_checkpoint(state);
    const auto saved = decode_lsl_checkpoint(wire);
    check(saved && (persistent || encode_lsl_checkpoint(*saved) == wire), "Array copy checkpoint codec rejected");
    auto instance = std::make_unique<LslInstance>(std::move(program), LslContext{}, host);
    check(instance->restore_checkpoint(*saved), "Array copy native checkpoint restore rejected");
    return instance;
}

void resume(LslInstance &instance, std::int64_t &clock, const std::vector<std::string> &names,
            const network::Value &row, bool migrated, unsigned &sleeps, const std::string &where) {
    const auto &stage = row.get(migrated ? "final" : "live_resume");
    const auto &fault = row.get(migrated ? "resume_exception" : "live_resume_exception");
    auto previous = instance.checkpoint();
    check(previous.has_value(), where + ": resume checkpoint missing");
    clock += previous->sleep_remaining + 1;
    for (unsigned step = 0; step < 10000; ++step) {
        const auto result = instance.run(1);
        if (result.status == LslRunStatus::Faulted) {
            check(fault.kind == network::Value::Kind::Map && result.error == native_fault(fault),
                  where + ": original copy fault differs: " + result.error);
            check(sleeps == 1, where + ": copy arguments replayed Sleep before fault");
            // Faulted VM snapshots are unavailable through checkpoint(). Direct
            // projected-capture checks separately assert every post-fault write.
            return;
        }
        check(result.error.empty() && result.status != LslRunStatus::Sleeping,
              where + ": copy resume faulted or replayed Sleep: " + result.error);
        const auto current = instance.checkpoint();
        check(current.has_value(), where + ": resumed checkpoint missing");
        if (result.status == LslRunStatus::Idle) {
            check(fault.kind != network::Value::Kind::Map, where + ": original copy fault became success");
            compare(*current, names, stage, where + " final");
            heap(instance, *current, stage, where + " final");
            check(instance.run(1).status == LslRunStatus::Idle && sleeps == 1, where + ": entry replayed");
            return;
        }
        previous = current;
    }
    throw std::runtime_error(where + ": copy resume exceeded execution bound");
}

unsigned source_case(const std::filesystem::path &folder, const network::Value &proof_row,
                     std::vector<std::string> &failures, unsigned &direct_cases) {
    const auto source = read(folder / "source.lsl"), metadata_text = read(folder / "metadata.json");
    const auto snapshot_wire = read(folder / "snapshot.bin"), object_wire = read(folder / "original.yobj");
    check(framework::sha256_hex(source) == proof_row.get("sourceSha256").text &&
              framework::sha256_hex(metadata_text) == proof_row.get("metadataSha256").text &&
              framework::sha256_hex(snapshot_wire) == proof_row.get("snapshotSha256").text &&
              framework::sha256_hex(object_wire) == proof_row.get("originalObjectSha256").text,
          "Original array-copy fixture artifact changed");
    const auto row = network::parse_osd_json(metadata_text);
    const auto snapshot = decode_yengine_snapshot(bytes(snapshot_wire));
    const auto object = decode_yengine_object_code(bytes(object_wire));
    check(snapshot && object && object->source_hash == row.get("source_hash").text && row.get("accepted").boolean &&
              row.get("suspended").boolean && row.get("migrated").boolean,
          "Original array-copy capture/object context rejected");
    const auto prefix = folder.parent_path().filename().string() + "/" + folder.filename().string();
    try {
        direct_copy(source, row, *snapshot, *object);
        ++direct_cases;
    } catch (const std::exception &error) {
        failures.push_back(prefix + "/direct helper: " + error.what());
    }
    const auto compiled = compile_lsl(source);
    std::string diagnostics;
    for (const auto &diagnostic : compiled.diagnostics)
        diagnostics += " " + diagnostic.message;
    check(bool(compiled), "Original array-copy source rejected:" + diagnostics);
    const auto names = declarations(source);
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
        check(result.error.empty(), "Array-copy source faulted before capture: " + result.error);
        if (result.status == LslRunStatus::Sleeping) {
            suspended = true;
            break;
        }
        check(result.status != LslRunStatus::Idle, "Array-copy source completed before expected Sleep");
    }
    check(suspended && sleeps == 1, "Array-copy source did not suspend exactly once");
    const auto captured = live->checkpoint();
    check(captured.has_value(), "Array-copy source capture missing");
    compare(*captured, names, row.get("initial"), prefix + "/live capture");
    heap(*live, *captured, row.get("initial"), prefix + "/live capture");

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
            check(state.has_value(), prefix + "/" + name + ": capture missing");
            compare(*state, names, row.get(migrated ? "after_load" : "initial"), prefix + "/" + name + " capture");
            heap(*instance, *state, row.get(migrated ? "after_load" : "initial"), prefix + "/" + name + " capture");
            paths.push_back({std::move(name), migrated, std::move(instance)});
        } catch (const std::exception &error) {
            failures.push_back(prefix + "/" + name + ": " + error.what());
        }
    };
    add_path("internal", false, [&] { return restore_native(compiled.program, host, *captured, false); });
    add_path("persistent", true, [&] {
        const auto state = paths[0].instance->persistent_checkpoint();
        check(state.has_value(), "Array-copy persistent capture missing");
        return restore_native(compiled.program, host, *state, true);
    });
    add_path("original import", true, [&] {
        auto instance = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
        check(instance->restore_yengine_snapshot(*snapshot, row.get("source_hash").text, source, *object),
              "Original array-copy class/frame import rejected");
        return instance;
    });
    unsigned passed = 0;
    for (auto &path : paths) {
        try {
            resume(*path.instance, clock, names, row, path.migrated, sleeps, prefix + "/" + path.name);
            ++passed;
        } catch (const std::exception &error) {
            failures.push_back(prefix + "/" + path.name + ": " + error.what());
        }
    }
    return passed;
}

void object_alias_copy() {
    const auto descriptor = make_lsl_script_array_descriptor("alias", "integer", 1);
    const std::array dimensions{std::int32_t{3}};
    const auto wrapper = std::get<Class>(make_lsl_script_array(descriptor, dimensions));
    const auto backing = std::get<Array>(make_lsl_fixed_array(LslFixedArrayKind::Object, 3));
    lsl_script_class_store_field(wrapper, 5, 0, backing);
    const auto box = make_lsl_object_box(std::int32_t{11});
    lsl_fixed_array_set(backing, 0, box);
    lsl_script_array_copy(wrapper, 0, wrapper, 1, 2);
    check(std::get<std::shared_ptr<const LslObjectBox>>(lsl_fixed_array_get(backing, 1)) ==
                  std::get<std::shared_ptr<const LslObjectBox>>(box) &&
              std::holds_alternative<LslNullObject>(lsl_fixed_array_get(backing, 2)),
          "Overlapping Object[] copy reboxed an existing reference or changed null identity");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected original array-copy fixture directory");
        const std::filesystem::path root = argv[1];
        const auto proof_text = read(root / "verification.json");
        check(framework::sha256_hex(proof_text) == "23d99eefc68f0aecf3542c47146c044456f41dddbfc680253b101b7e1908c898",
              "Original array-copy proof changed");
        const auto proof = network::parse_osd_json(proof_text);
        check(proof.get("passed").boolean && !proof.get("nativeParityVerified").boolean &&
                  framework::sha256_hex(read(root / "Reference.cs")) == proof.get("generatorSha256").text &&
                  framework::sha256_hex(read(root / "decode.py")) == proof.get("decoderSha256").text &&
                  framework::sha256_hex(read(root / "wire_support.py")) == proof.get("wireSupportSha256").text &&
                  framework::sha256_hex(read(root / "verify.py")) == proof.get("verifierSha256").text,
              "Original array-copy evidence scope/provenance changed");
        std::vector<std::string> failures;
        unsigned passed = 0, direct_cases = 0;
        for (const auto platform : {"windows", "linux"}) {
            const auto &platform_proof = proof.get("platforms").get(platform);
            check(framework::sha256_hex(read(root / platform / "metadata.json")) ==
                          platform_proof.get("metadataSha256").text &&
                      platform_proof.get("cases").array.size() == 27,
                  "Original array-copy platform metadata/inventory changed");
            for (const auto &row : platform_proof.get("cases").array) {
                try {
                    passed += source_case(root / platform / row.get("name").text, row, failures, direct_cases);
                } catch (const std::exception &error) {
                    failures.push_back(std::string(platform) + "/" + row.get("name").text + ": " + error.what());
                }
            }
        }
        object_alias_copy();
        collect_lsl_fixed_arrays();
        collect_yengine_managed_cycles();
        std::cout << "Verified " << passed << "/216 VM copy paths and " << direct_cases
                  << "/54 original-capture direct copies (including exact partial writes after faults).\n";
        for (const auto &failure : failures)
            std::cerr << "FAIL " << failure << '\n';
        return passed == 216 && direct_cases == 54 && failures.empty() ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
