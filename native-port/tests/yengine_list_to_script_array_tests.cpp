// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include "opensim/script/lsl_script_class.hpp"
#include "opensim/script/yengine_object_code.hpp"
#include <algorithm>
#include <array>
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
        R"(\b(?:(?:integer|char|float)\s*\[\s*\]|integer|char|object|string)\s+([A-Za-z_][A-Za-z_0-9]*)\s*(?:=|;))");
    std::vector<std::string> names;
    for (auto it = std::sregex_iterator(source.begin(), source.end(), declaration); it != std::sregex_iterator(); ++it)
        names.push_back((*it)[1].str());
    return names;
}

struct GraphComparison {
    bool compare_cells = true;
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
        const auto *box = std::get_if<std::shared_ptr<const LslObjectBox>>(&actual);
        const auto &raw = box && *box ? (*box)->value : actual;
        if (type == "null") {
            check(std::holds_alternative<LslNullObject>(actual) || is_null_lsl_string(actual),
                  where + ": typed null reference differs");
        } else if (type == "integer") {
            if (expected.get("clr").text.ends_with("+LSLInteger")) {
                const auto *integer = std::get_if<LslBoxedInteger>(&raw);
                check(integer && integer->value == network::osd_integer(expected.get("value")),
                      where + ": SDK Int32 differs");
            } else {
                const auto *integer = std::get_if<std::int32_t>(&raw);
                check(integer && *integer == network::osd_integer(expected.get("value")),
                      where + ": raw Int32 differs");
            }
        } else if (type == "char") {
            const auto *character = std::get_if<LslCharacter>(&raw);
            check(character && character->value == network::osd_integer(expected.get("utf16")),
                  where + ": raw UTF16 character differs");
        } else if (type == "float") {
            const auto *number = std::get_if<double>(&raw);
            const auto *sdk = std::get_if<LslBoxedFloat>(&raw);
            const bool expected_sdk = expected.get("clr").text.ends_with("+LSLFloat");
            check(expected_sdk ? bool(sdk) : bool(number), where + ": Double SDK/raw kind differs");
            const auto bits = std::bit_cast<std::uint64_t>(expected_sdk ? sdk->value : *number);
            check(bits == std::stoull(expected.get("bits").text, nullptr, 16), where + ": Double bits differ");
        } else if (type == "single") {
            const auto *single = std::get_if<LslRawSingle>(&raw);
            check(single && std::bit_cast<std::uint32_t>(single->value) ==
                                std::stoull(expected.get("bits").text, nullptr, 16),
                  where + ": Single bits differ");
        } else if (type == "boolean") {
            const auto *boolean = std::get_if<LslRawBoolean>(&raw);
            check(boolean && boolean->value == expected.get("value").boolean, where + ": Boolean differs");
        } else if (type == "key") {
            check(std::holds_alternative<LslBoxedKey>(raw) || std::holds_alternative<LslBoxedUtf16Key>(raw) ||
                      std::holds_alternative<LslBoxedNullKey>(raw),
                  where + ": SDK key was unwrapped");
            check(lsl_string_code_units(raw) == framework::utf16_replacing(expected.get("value").text),
                  where + ": SDK key value differs");
        } else if (type == "vector" || type == "rotation") {
            std::vector<double> components;
            if (type == "vector") {
                const auto value = std::get<framework::Vector3d>(raw);
                components = {value.x, value.y, value.z};
            } else {
                const auto value = std::get<LslRotation>(raw);
                components = {value.x, value.y, value.z, value.w};
            }
            const auto &values = expected.get("value").array;
            check(values.size() == components.size(), where + ": vector/rotation size differs");
            for (std::size_t i = 0; i < values.size(); ++i)
                check(std::bit_cast<std::uint64_t>(components[i]) ==
                          std::stoull(values[i].get("bits").text, nullptr, 16),
                      where + ": vector/rotation bits differ");
        } else if (type == "list") {
            const auto list = std::get<std::shared_ptr<const LslList>>(actual);
            const auto &values = expected.get("value").array;
            check(list && list->items.size() == values.size(), where + ": List differs");
            for (std::size_t i = 0; i < values.size(); ++i)
                value(list->items[i], values[i], where + ".list");
        } else if (type == "string") {
            if (expected.get("clr").text == "System.String")
                check(std::holds_alternative<std::string>(raw) || std::holds_alternative<LslUtf16String>(raw) ||
                          std::holds_alternative<std::shared_ptr<const LslStringReference>>(raw),
                      where + ": SDK String not unwrapped");
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
            check(instance.descriptor->class_index == network::osd_integer(expected.get("class_index")) &&
                      instance.arrays_heap_usage == network::osd_integer(expected.get("arrays_heap_use")),
                  where + ": generated class index/own heap ledger differs");
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
            const auto family = expected.get("element_type").text;
            const auto expected_kind = family == "System.Char"     ? LslFixedArrayKind::Character
                                       : family == "System.Double" ? LslFixedArrayKind::Float
                                       : family == "System.Object" ? LslFixedArrayKind::Object
                                                                   : LslFixedArrayKind::Integer;
            check((*array)->kind == expected_kind, where + ": raw backing type differs");
            const auto &items = expected.get("items").array;
            check(lsl_fixed_array_size(*array) == items.size(), where + ": backing length differs");
            if (compare_cells)
                for (std::size_t i = 0; i < items.size(); ++i)
                    value(lsl_fixed_array_get(*array, i), items[i], where + "[" + std::to_string(i) + "]");
        } else
            throw std::runtime_error(where + ": unreviewed original observation " + type);
    }
};

void compare(const LslCheckpoint &checkpoint, const std::vector<std::string> &names, const network::Value &expected,
             const std::string &where, bool compare_cells = true) {
    const auto &globals = expected.get("globals");
    check(globals.kind == network::Value::Kind::Map && names.size() == checkpoint.globals.size() &&
              globals.map.size() == names.size(),
          where + ": global inventory differs");
    check(checkpoint.arrays_heap_usage && checkpoint.locals_heap_usage &&
              *checkpoint.arrays_heap_usage == network::osd_integer(expected.get("arrays_heap_used")) &&
              *checkpoint.locals_heap_usage == network::osd_integer(expected.get("locals_heap_used")),
          where + ": global/local heap ledger differs");
    GraphComparison graph;
    graph.compare_cells = compare_cells;
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

LslCheckpoint checkpoint(const LslInstance &instance) {
    const auto result = instance.checkpoint();
    check(result.has_value(), "List2Array checkpoint missing");
    return *result;
}

void fault(const std::string &actual, const network::Value &expected) {
    auto type = expected.get("type").text;
    check(type.starts_with("System."), "Unexpected original exception type");
    type.erase(0, 7);
    check(actual == type + ": " + expected.get("message").text, "List2Array exact fault differs: " + actual);
}

bool identity(const LslValue &a, const LslValue &b) {
    if (std::holds_alternative<LslNullObject>(a) && std::holds_alternative<LslNullObject>(b))
        return true;
    if (a.index() != b.index())
        return false;
    return std::visit(
        [&](const auto &x) {
            if constexpr (requires { x.get(); })
                return x == std::get<std::decay_t<decltype(x)>>(b);
            return false;
        },
        a);
}

Class wrapper(char family) {
    const auto element = family == 'C' ? "char" : family == 'F' ? "float" : "integer";
    auto descriptor =
        std::make_shared<LslScriptClassDescriptor>(*make_lsl_script_array_descriptor("List2Array-test", element, 1));
    descriptor->class_index = 0;
    const std::array<std::int32_t, 1> lengths{3};
    auto value = std::get<Class>(make_lsl_script_array(descriptor, lengths));
    if (family == 'O')
        lsl_script_class_store_field(value, 5, 0, make_lsl_fixed_array(LslFixedArrayKind::Object, 3));
    return value;
}

LslValue payload(const std::string &name, const Class &target) {
    if (name == "null")
        return LslNullObject{};
    if (name == "raw_char")
        return make_lsl_object_box(LslCharacter{65});
    if (name == "raw_int")
        return make_lsl_object_box(std::int32_t{7});
    if (name == "raw_single")
        return make_lsl_object_box(LslRawSingle{1.5f});
    if (name == "raw_double")
        return make_lsl_object_box(2.5);
    if (name == "raw_bool")
        return make_lsl_object_box(LslRawBoolean{true});
    if (name == "raw_string")
        return make_lsl_reference_string(u"raw");
    if (name == "sdk_int")
        return make_lsl_object_box(LslBoxedInteger{9});
    if (name == "sdk_float")
        return make_lsl_object_box(LslBoxedFloat{3.5});
    if (name == "sdk_string")
        return make_lsl_object_box(LslBoxedString{"sdk"});
    if (name == "sdk_null_string")
        return make_lsl_object_box(LslBoxedNullString{});
    if (name == "sdk_key")
        return make_lsl_object_box(LslBoxedKey{"11111111-2222-3333-4444-555555555555"});
    if (name == "vector")
        return make_lsl_object_box(framework::Vector3d{1, 2, 3});
    if (name == "rotation")
        return make_lsl_object_box(LslRotation{0, 0, 0, 1});
    if (name == "list")
        return std::make_shared<const LslList>(LslList{{make_lsl_object_box(LslBoxedInteger{5})}});
    if (name == "class")
        return target;
    throw std::runtime_error("Unknown backend source payload: " + name);
}

void direct_case(const network::Value &row) {
    const auto family = row.get("family").text.at(0);
    const auto name = row.get("name").text;
    auto target = wrapper(family);
    LslValue destination = target;
    LslValue source_value = LslNullObject{};
    const bool element = row.get("source").kind == network::Value::Kind::Map;
    auto source = std::make_shared<LslList>();
    if (element) {
        source_value = payload(name, target);
        source->items = {source_value};
    } else {
        const auto valid = family == 'C' ? make_lsl_object_box(LslCharacter{65}) : make_lsl_object_box(std::int32_t{7});
        source->items = {valid, valid};
        if (name == "partial_source")
            source->items = {valid};
        if (name == "null_data_zero" || name == "null_data_one")
            source->items.clear();
        if (name.starts_with("null_backing"))
            lsl_script_class_store_field(target, 5, 0, LslNullObject{});
        if (name.starts_with("wrong_backing"))
            lsl_script_class_store_field(target, 5, 0, make_lsl_reference_string(u"wrong"));
        if (name == "null_wrapper_zero")
            destination = LslNullObject{};
        if (name == "raw_array_wrapper_zero")
            destination = target->instance_arrays[5][0];
        if (name == "raw_string_wrapper_zero")
            destination = make_lsl_reference_string(u"wrong");
    }
    LslValue list = std::shared_ptr<const LslList>(source);
    if (name == "null_list_zero" || name == "wrong_backing_null_list")
        list = LslNullList{};
    {
        GraphComparison graph;
        graph.value(target->instance_arrays[5][0], row.get("before"), "backend before " + name);
    }
    check(target->arrays_heap_usage == network::osd_integer(row.get("own_before")),
          "Backend preparation own heap differs");
    std::string error;
    try {
        lsl_list_to_script_array(list, network::osd_integer(row.get("source_start")), destination,
                                 network::osd_integer(row.get("destination_start")),
                                 network::osd_integer(row.get("count")));
    } catch (const std::exception &e) {
        error = e.what();
    }
    if (row.get("error").kind == network::Value::Kind::Map)
        fault(error, row.get("error"));
    else
        check(error.empty(), "Backend List2Array unexpectedly faulted: " + error);
    {
        GraphComparison graph;
        graph.value(target->instance_arrays[5][0], row.get("after"), "backend after " + name);
    }
    check(target->arrays_heap_usage == network::osd_integer(row.get("own_after")),
          "List2Array changed class cached own ledger");
    if (family == 'O' && element) {
        const auto stored = lsl_fixed_array_get(std::get<Array>(target->instance_arrays[5][0]), 1);
        check(identity(stored, source_value) == row.get("object_destination_stored_is_source").boolean,
              "Object[] raw box/reference identity changed");
        if (name == "sdk_string") {
            const auto field = std::get<std::shared_ptr<const LslObjectBox>>(source_value)->string_field;
            check(std::get<std::shared_ptr<const LslStringReference>>(stored) == field,
                  "SDK String did not retain its field reference");
        }
    }
}

void resume(LslInstance &instance, const std::vector<std::string> &names, const network::Value &metadata, bool migrated,
            std::int64_t &clock, unsigned &sleeps, unsigned expected_sleeps) {
    clock += checkpoint(instance).sleep_remaining + 1;
    const auto &expected = metadata.get(migrated ? "final" : "live_resume");
    const auto &expected_fault = metadata.get(migrated ? "resume_exception" : "live_resume_exception");
    const bool partial = metadata.get("name").text.starts_with("source_integer_partial_");
    auto previous = instance.checkpoint();
    bool finished = false;
    for (unsigned step = 0; step < 10000; ++step) {
        const auto result = instance.run(1);
        if (result.status == LslRunStatus::Faulted) {
            check(expected_fault.kind == network::Value::Kind::Map,
                  "Unexpected source List2Array fault: " + result.error);
            fault(result.error, expected_fault);
            check(previous && !instance.checkpoint(), "Faulted VM supplied a checkpoint");
            compare(*previous, names, expected, "List2Array pre-fault state", !partial);
            finished = true;
            break;
        }
        check(result.error.empty() && result.status != LslRunStatus::Sleeping,
              "List2Array replayed Sleep/faulted: " + result.error);
        const auto current = instance.checkpoint();
        check(current.has_value(), "List2Array resumed checkpoint unavailable");
        if (result.status == LslRunStatus::Idle) {
            check(expected_fault.kind != network::Value::Kind::Map, "Original List2Array fault became success");
            compare(*current, names, expected, "List2Array resumed output");
            finished = true;
            break;
        }
        previous = current;
    }
    check(finished && sleeps == expected_sleeps, "List2Array execution bound/Sleep count differs");
}

void inspect_partial(std::shared_ptr<const LslProgram> program, const LslHost &host, const LslCheckpoint &capture,
                     const std::vector<std::string> &names, const network::Value &metadata, std::int64_t &clock,
                     unsigned &sleeps) {
    auto derived = capture;
    const auto input = std::ranges::find(names, std::string("input"));
    check(input != names.end() && derived.active_event, "Partial-copy input/event missing");
    derived.active_event->arguments = {derived.globals[static_cast<std::size_t>(input - names.begin())]};
    // Derived observation only: an unused event argument exposes the live class
    // before a fault, without asking the faulted VM for a fabricated checkpoint.
    auto observed = restore(program, host, derived, false);
    const auto *event = observed->current_event();
    check(event && event->arguments.size() == 1, "Partial-copy live observer unavailable");
    const auto value = std::get<Class>(event->arguments[0]);
    const auto array = std::get<Array>(value->instance_arrays[5][0]);
    clock += checkpoint(*observed).sleep_remaining + 1;
    const auto prior_sleeps = sleeps;
    const auto result = observed->run(10000);
    check(result.status == LslRunStatus::Faulted && !observed->checkpoint(),
          "Partial-copy source did not fault as original");
    fault(result.error, metadata.get("live_resume_exception"));
    const std::array<std::int32_t, 3> expected{11, 0, 0};
    for (std::size_t i = 0; i < expected.size(); ++i)
        check(std::get<std::int32_t>(lsl_fixed_array_get(array, i)) == expected[i],
              "List2Array discarded source partial writes");
    check(value->arrays_heap_usage == 16 && sleeps == prior_sleeps, "Partial-copy own heap/Sleep differs");
}

void source_case(const std::filesystem::path &folder, const network::Value &metadata) {
    const auto source = read(folder / "source.lsl");
    const auto names = declarations(source);
    const auto compiled = compile_lsl(source);
    std::string diagnostics;
    for (const auto &entry : compiled.diagnostics)
        diagnostics += " " + entry.message;
    check(bool(compiled), "List2Array source rejected:" + diagnostics);
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.heap_limit_bytes = 100000000;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](std::int32_t) { ++sleeps; };
    auto live = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
    bool paused = false;
    for (unsigned step = 0; step < 10000; ++step) {
        const auto result = live->run(1);
        check(result.error.empty() && result.status != LslRunStatus::Faulted,
              "List2Array source faulted before capture: " + result.error);
        if (result.status == LslRunStatus::Sleeping) {
            paused = true;
            break;
        }
        check(result.status != LslRunStatus::Idle, "List2Array source completed before Sleep");
    }
    check(paused && sleeps == 1, "List2Array source did not pause exactly once");
    const auto capture = checkpoint(*live);
    compare(capture, names, metadata.get("initial"), "List2Array source capture");
    auto internal = restore(compiled.program, host, capture, false);
    compare(checkpoint(*internal), names, metadata.get("initial"), "List2Array internal reload");
    const auto projected = live->persistent_checkpoint();
    check(projected.has_value(), "List2Array persistent projection missing");
    auto persistent = restore(compiled.program, host, *projected, true);
    compare(checkpoint(*persistent), names, metadata.get("after_load"), "List2Array persistent reload");
    if (metadata.get("name").text.starts_with("source_integer_partial_"))
        inspect_partial(compiled.program, host, capture, names, metadata, clock, sleeps);
    resume(*live, names, metadata, false, clock, sleeps, 1);
    resume(*internal, names, metadata, false, clock, sleeps, 1);
    resume(*persistent, names, metadata, true, clock, sleeps, 1);
    const auto object_bytes = read(folder / "original.yobj"), snapshot_bytes = read(folder / "snapshot.bin");
    const auto object =
        decode_yengine_object_code({reinterpret_cast<const std::uint8_t *>(object_bytes.data()), object_bytes.size()});
    const auto saved =
        decode_yengine_snapshot({reinterpret_cast<const std::uint8_t *>(snapshot_bytes.data()), snapshot_bytes.size()});
    check(object && saved, "Original List2Array object/snapshot rejected");
    unsigned imported_sleeps = 0;
    LslHost import_host = host;
    import_host.request_sleep = [&](std::int32_t) { ++imported_sleeps; };
    LslInstance imported(compiled.program, {}, import_host);
    check(imported.restore_yengine_snapshot(*saved, metadata.get("source_hash").text, source, *object),
          "Original List2Array continuation rejected");
    compare(checkpoint(imported), names, metadata.get("after_load"), "actual List2Array import");
    resume(imported, names, metadata, true, clock, imported_sleeps, 0);
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected original List2Array fixture directory");
        const std::filesystem::path root = argv[1];
        const auto text = read(root / "verification.json");
        check(framework::sha256_hex(text) == "b8e00b3b7da1e8d4a680396f54df66e5bdec6fa8d32e9a913058bbf70e973e8b",
              "Original List2Array proof changed");
        const auto proof = network::parse_osd_json(text);
        check(proof.get("passed").boolean && !proof.get("nativeParityVerified").boolean,
              "Original List2Array scope changed");
        check(framework::sha256_hex(read(root / "Reference.cs")) == proof.get("generatorSha256").text &&
                  framework::sha256_hex(read(root / "verify.py")) == proof.get("verifierSha256").text &&
                  framework::sha256_hex(read(root / "decode.py")) == proof.get("decoderSha256").text,
              "Original List2Array provenance changed");
        unsigned sources = 0, direct = 0;
        std::vector<std::string> failures;
        for (const auto platform : {"windows", "linux"}) {
            const auto &cases = proof.get("platforms").get(platform).get("cases").array;
            check(cases.size() == 10, "List2Array case inventory changed");
            for (const auto &row : cases) {
                const auto folder = root / platform / row.get("name").text;
                const auto metadata_text = read(folder / "metadata.json");
                check(framework::sha256_hex(metadata_text) == row.get("metadataSha256").text &&
                          framework::sha256_hex(read(folder / "source.lsl")) == row.get("sourceSha256").text &&
                          framework::sha256_hex(read(folder / "snapshot.bin")) == row.get("snapshotSha256").text &&
                          framework::sha256_hex(read(folder / "original.yobj")) == row.get("originalObjectSha256").text,
                      "Original List2Array capture changed");
                const auto metadata = network::parse_osd_json(metadata_text);
                try {
                    source_case(folder, metadata);
                    ++sources;
                } catch (const std::exception &error) {
                    failures.push_back(std::string(platform) + "/" + row.get("name").text + " source: " + error.what());
                }
                if (row.get("directHelper").kind == network::Value::Kind::Map) {
                    const auto observations_text = read(folder / "direct-helper-observations.json");
                    check(framework::sha256_hex(observations_text) == row.get("directHelper").get("sha256").text,
                          "Direct List2Array observation changed");
                    const auto observations = network::parse_osd_json(observations_text);
                    check(observations.array.size() == 33, "Direct List2Array matrix inventory changed");
                    for (const auto &observation : observations.array) {
                        try {
                            direct_case(observation);
                            ++direct;
                        } catch (const std::exception &error) {
                            failures.push_back(std::string(platform) + "/" + row.get("name").text + "/" +
                                               observation.get("name").text + ": " + error.what());
                        }
                    }
                }
            }
        }
        for (const auto &failure : failures)
            std::cerr << failure << "\n";
        check(failures.empty() && sources == 20 && direct == 264, "List2Array source/backend parity checks failed");
        collect_lsl_fixed_arrays();
        std::cout << "PASS 20 original captures x4paths,264 direct List2Array observations; partial-state inspection "
                     "is derived\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
    return 0;
}
