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
            check(std::holds_alternative<LslNullObject>(actual) || std::holds_alternative<LslNullString>(actual) ||
                      std::holds_alternative<LslNullList>(actual),
                  where + ": null reference differs");
        } else if (type == "integer") {
            const auto *integer = std::get_if<std::int32_t>(&actual);
            check(integer && *integer == network::osd_integer(expected.get("value")), where + ": Int32 differs");
        } else if (type == "float") {
            const auto *number = std::get_if<double>(&actual);
            check(number &&
                      std::bit_cast<std::uint64_t>(*number) == std::stoull(expected.get("bits").text, nullptr, 16),
                  where + ": binary64 differs");
        } else if (type == "char") {
            const auto *character = std::get_if<LslCharacter>(&actual);
            check(character && character->value == network::osd_integer(expected.get("value")),
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
            const auto clr = expected.get("clr").text;
            const auto kind = clr == "System.Double[]" ? LslFixedArrayKind::Float
                              : clr == "System.Char[]" ? LslFixedArrayKind::Character
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
    const auto result = instance.checkpoint();
    check(result.has_value(), "Foreign accessor checkpoint missing");
    return *result;
}

void pause(LslInstance &instance) {
    for (unsigned i = 0; i < 10000; ++i) {
        const auto result = instance.run(1);
        check(result.error.empty(), "Foreign/caller source faulted before injection: " + result.error);
        if (result.status == LslRunStatus::Sleeping)
            return;
        check(result.status != LslRunStatus::Idle, "Foreign/caller source completed before capture");
    }
    throw std::runtime_error("Foreign/caller source failed to pause within bound");
}

void check_stage(const LslCheckpoint &saved, const std::vector<std::string> &names, const network::Value &expected,
                 const std::string &label) {
    compare(saved, names, expected, label);
    check(saved.arrays_heap_usage && saved.locals_heap_usage &&
              *saved.arrays_heap_usage == network::osd_integer(expected.get("arrays_heap_used")) &&
              *saved.locals_heap_usage == network::osd_integer(expected.get("locals_heap_used")),
          label + ": heap ledger differs");
}

void resume(LslInstance &instance, std::int64_t &clock, const std::vector<std::string> &names,
            const network::Value &expected, const network::Value &fault, unsigned &sleeps, const std::string &label) {
    auto previous = snapshot(instance);
    clock += previous.sleep_remaining + 1;
    for (unsigned i = 0; i < 10000; ++i) {
        const auto result = instance.run(1);
        if (result.status == LslRunStatus::Faulted) {
            check(fault.get("type").text == "System.InvalidCastException" &&
                      result.error == "InvalidCastException: " + fault.get("message").text,
                  label + ": static accessor cast/fault differs: " + result.error);
            check_stage(previous, names, expected, label + " before expected fault");
            check(sleeps == 2, label + ": source Sleep replayed");
            return;
        }
        check(result.error.empty() && result.status != LslRunStatus::Sleeping,
              label + ": source replay/fault: " + result.error);
        const auto current = snapshot(instance);
        if (result.status == LslRunStatus::Idle) {
            check(fault.kind != network::Value::Kind::Map, label + ": required accessor cast became success");
            check_stage(current, names, expected, label + " completed");
            check(instance.run(1).status == LslRunStatus::Idle && sleeps == 2, label + ": entry replayed");
            return;
        }
        previous = current;
    }
    throw std::runtime_error(label + ": execution bound exceeded");
}

void case_test(const std::filesystem::path &folder, const network::Value &proof) {
    const auto source = read(folder / "source.lsl");
    const auto foreign_source = read(folder / "foreign-source.lsl");
    const auto metadata_text = read(folder / "metadata.json");
    const auto wire = read(folder / "snapshot.bin");
    const auto object_wire = read(folder / "original.yobj");
    check(framework::sha256_hex(source) == proof.get("sourceSha256").text &&
              framework::sha256_hex(foreign_source) == proof.get("foreignSourceSha256").text &&
              framework::sha256_hex(metadata_text) == proof.get("metadataSha256").text &&
              framework::sha256_hex(wire) == proof.get("snapshotSha256").text &&
              framework::sha256_hex(object_wire) == proof.get("objectSha256").text &&
              framework::sha256_hex(read(folder / "foreign-original.yobj")) == proof.get("foreignObjectSha256").text &&
              framework::sha256_hex(read(folder / "foreign-injection.json")) == proof.get("injectionSha256").text,
          "Actual original foreign accessor fixture changed");
    const auto oracle = network::parse_osd_json(metadata_text);
    const auto injection = network::parse_osd_json(read(folder / "foreign-injection.json"));
    check(injection.get("injectedAlias").boolean && injection.get("ownerDifferent").boolean &&
              injection.get("callerCodeDifferent").boolean &&
              network::osd_integer(injection.get("foreignClassIndex")) == 0,
          "Original foreign owner/index provenance changed");
    const auto caller_program = compile_lsl(source);
    const auto owner_program = compile_lsl(foreign_source);
    check(caller_program && owner_program, "Native caller/foreign source rejected");
    const auto names = declarations(source);
    check(names.size() == 7 && names[0] == "foreign" && names[1] == "input", "Caller injection global slots changed");
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.heap_limit_bytes = 100000000;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](std::int32_t) { ++sleeps; };
    // The foreign descriptor, backing, cells and ledger all come from an
    // independently compiled/running native source VM, never hand-built data.
    LslInstance owner(owner_program.program, {}, host);
    pause(owner);
    const auto owner_state = snapshot(owner);
    check(owner_state.globals.size() == 1, "Foreign owner global inventory differs");
    const auto wrapper = std::get<Class>(owner_state.globals[0]);
    check(wrapper && wrapper->descriptor->class_index == 0 &&
              wrapper->descriptor->canonical_name == injection.get("foreignType").text,
          "Native foreign owner/index differs from original");
    LslInstance caller(caller_program.program, {}, host);
    pause(caller);
    auto seed = snapshot(caller);
    check(seed.globals.size() == 7 && std::holds_alternative<LslNullObject>(seed.globals[0]),
          "Paused caller Object injection slot is not empty");
    // This is the native equivalent of original PopObject(foreignWrapper):
    // class Object.Size is zero, so no ledger or descriptor needs fabrication.
    seed.globals[0] = wrapper;
    check(caller.restore_checkpoint(seed), "Paused caller rejected actual foreign source wrapper");
    check_stage(snapshot(caller), names, oracle.get("initial"), "native foreign live injection");
    // A persistent transfer resolves the saved class index in the receiving
    // caller's type table, just as actual original MigrateIn does. Capture the
    // unmodified injected VM before live resume can fault or mutate globals.
    const auto persistent_wire = encode_persistent_lsl_checkpoint(snapshot(caller));
    const auto persistent_state = decode_lsl_checkpoint(persistent_wire);
    LslInstance persistent(caller_program.program, {}, host);
    check(persistent_state && persistent.restore_checkpoint(*persistent_state),
          "Native persistent foreign snapshot rejected by receiving caller");
    check_stage(snapshot(persistent), names, oracle.get("after_load"), "native persistent caller-context reload");
    resume(caller, clock, names, oracle.get("live_resume"), oracle.get("live_resume_exception"), sleeps,
           folder.filename().string() + " live owner");
    const auto object = decode_yengine_object_code(bytes(object_wire));
    const auto original = decode_yengine_snapshot(bytes(wire));
    check(object && original, "Original foreign caller object/snapshot rejected");
    LslInstance imported(caller_program.program, {}, host);
    check(imported.restore_yengine_snapshot(*original, oracle.get("source_hash").text, source, *object),
          "Original caller-context foreign snapshot import rejected");
    check_stage(snapshot(imported), names, oracle.get("after_load"), "original foreign caller reload");
    resume(imported, clock, names, oracle.get("final"), oracle.get("resume_exception"), sleeps,
           folder.filename().string() + " caller-context import");
    resume(persistent, clock, names, oracle.get("final"), oracle.get("resume_exception"), sleeps,
           folder.filename().string() + " native persistent caller-context transfer");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected original foreign element fixture directory");
        const std::filesystem::path root = argv[1];
        const auto text = read(root / "verification.json");
        check(framework::sha256_hex(text) == "d5fec31064ba113f99c25eac75743f6a8b6802c45275f912bbc1bd787d2b0cad",
              "Original foreign element proof changed");
        const auto proof = network::parse_osd_json(text);
        check(proof.get("passed").boolean && !proof.get("nativeParityVerified").boolean,
              "Original foreign element proof scope changed");
        unsigned count = 0;
        std::vector<std::string> failures;
        for (const auto platform : {"windows", "linux"}) {
            const auto &rows = proof.get("platforms").get(platform).get("cases").array;
            check(rows.size() == 12, "Original foreign accessor inventory changed");
            for (const auto &row : rows) {
                const auto label = std::string(platform) + "/" + row.get("name").text;
                try {
                    case_test(root / platform / row.get("name").text, row);
                    ++count;
                } catch (const std::exception &error) {
                    failures.push_back(label + ": " + error.what());
                }
            }
        }
        collect_lsl_fixed_arrays();
        collect_yengine_managed_cycles();
        for (const auto &failure : failures)
            std::cerr << failure << '\n';
        std::cout << "Verified " << count
                  << "/24 original cases through live foreign-owner, original caller-context import and native "
                     "persistent caller-context transfer paths (72 path checks).\n";
        return count == 24 && failures.empty() ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
