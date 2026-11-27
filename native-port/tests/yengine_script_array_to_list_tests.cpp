// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include "opensim/script/lsl_script_class.hpp"
#include "opensim/script/yengine_object_code.hpp"
#include <iostream>
using namespace utf16_test;

namespace {
using Array = std::shared_ptr<const LslFixedArray>;
using Class = std::shared_ptr<const LslScriptClass>;
using List = std::shared_ptr<const LslList>;
using Box = std::shared_ptr<const LslObjectBox>;
using String = std::shared_ptr<const LslStringReference>;

std::span<const std::uint8_t> bytes(const std::string &value) {
    return {reinterpret_cast<const std::uint8_t *>(value.data()), value.size()};
}

LslCheckpoint snapshot(const LslInstance &instance) {
    const auto state = instance.checkpoint();
    check(state.has_value(), "Array2List checkpoint missing");
    return *state;
}

void pause(LslInstance &instance) {
    for (unsigned i = 0; i < 10000; ++i) {
        const auto result = instance.run(1);
        check(result.error.empty(), "Array2List source preparation faulted: " + result.error);
        if (result.status == LslRunStatus::Sleeping)
            return;
    }
    throw std::runtime_error("Array2List source did not pause");
}

bool same_object(const LslValue &a, const LslValue &b) {
    if (a.index() != b.index())
        return false;
    if (const auto *box = std::get_if<Box>(&a))
        return *box == std::get<Box>(b);
    if (const auto *text = std::get_if<String>(&a))
        return *text == std::get<String>(b);
    if (const auto *list = std::get_if<List>(&a))
        return *list == std::get<List>(b);
    if (const auto *array = std::get_if<Array>(&a))
        return *array == std::get<Array>(b);
    if (const auto *value = std::get_if<Class>(&a))
        return *value == std::get<Class>(b);
    return false;
}

void converted(const LslValue &actual, const network::Value &expected, const std::string &label) {
    const auto *box = std::get_if<Box>(&actual);
    check(box && *box && valid_lsl_object_box(**box), label + ": SDK value is not a reference-preserving box");
    const auto &value = (*box)->value;
    const auto type = expected.get("type").text;
    if (type == "integer") {
        const auto *integer = std::get_if<LslBoxedInteger>(&value);
        check(integer && integer->value == network::osd_integer(expected.get("value")), label + ": SDKInteger differs");
    } else if (type == "float") {
        const auto *number = std::get_if<LslBoxedFloat>(&value);
        check(number &&
                  std::bit_cast<std::uint64_t>(number->value) == std::stoull(expected.get("bits").text, nullptr, 16),
              label + ": SDKFloat binary64 differs");
    } else if (type == "string") {
        check(std::holds_alternative<LslBoxedString>(value) || std::holds_alternative<LslBoxedUtf16String>(value),
              label + ": SDKString carrier differs");
        const auto text = lsl_string_code_units(actual);
        check(text && text->size() == expected.get("utf16").array.size(), label + ": SDKString UTF16 length differs");
        for (std::size_t i = 0; i < text->size(); ++i)
            check((*text)[i] == network::osd_integer(expected.get("utf16").array[i]),
                  label + ": SDKString unit differs");
    } else if (type == "vector" || type == "rotation") {
        std::vector<double> components;
        if (type == "vector") {
            const auto vector = std::get<framework::Vector3d>(value);
            components = {vector.x, vector.y, vector.z};
        } else {
            const auto rotation = std::get<LslRotation>(value);
            components = {rotation.x, rotation.y, rotation.z, rotation.w};
        }
        check(components.size() == expected.get("value").array.size(), label + ": tuple dimensions differ");
        for (std::size_t i = 0; i < components.size(); ++i)
            check(std::bit_cast<std::uint64_t>(components[i]) ==
                      std::stoull(expected.get("value").array[i].get("bits").text, nullptr, 16),
                  label + ": tuple bits differ");
    } else
        throw std::runtime_error(label + ": unreviewed result SDK type " + type);
}

void output(const LslValue &actual, const network::Value &expected, const std::string &label) {
    const auto *list = std::get_if<List>(&actual);
    check(list && *list && expected.get("type").text == "list" &&
              (*list)->items.size() == expected.get("value").array.size(),
          label + ": List shape differs");
    for (std::size_t i = 0; i < (*list)->items.size(); ++i)
        converted((*list)->items[i], expected.get("value").array[i], label + "/" + std::to_string(i));
}

Array objects(std::vector<LslValue> values) {
    auto array = std::get<Array>(make_lsl_fixed_array(LslFixedArrayKind::Object, values.size()));
    for (std::size_t i = 0; i < values.size(); ++i)
        lsl_fixed_array_set(array, i, values[i]);
    return array;
}

LslValue backing(std::string_view mode, const Class &wrapper, const Array &original) {
    if (mode == "source_backing" || mode == "null_wrapper" || mode == "wrong_integer_wrapper")
        return original;
    if (mode == "null_backing")
        return LslNullObject{};
    if (mode == "wrong_string_backing")
        return make_lsl_reference_string(u"wrong");
    if (mode == "object_raw_string") {
        auto text = make_lsl_reference_string(u"raw");
        return objects({text, text});
    }
    if (mode == "object_raw_integer")
        return objects({make_lsl_object_box(std::int32_t{7}), make_lsl_object_box(std::int32_t{-9})});
    if (mode == "object_raw_double")
        return objects({make_lsl_object_box(1.2345678901234567), make_lsl_object_box(-2.25)});
    if (mode == "object_sdk_integer") {
        auto value = make_lsl_object_box(LslBoxedInteger{23});
        return objects({value, value});
    }
    if (mode == "object_sdk_float") {
        auto value = make_lsl_object_box(LslBoxedFloat{2.5});
        return objects({value, value});
    }
    if (mode == "object_sdk_string") {
        auto value = make_lsl_object_box(LslBoxedString{"sdk"});
        return objects({value, value});
    }
    if (mode == "object_sdk_key") {
        auto value = make_lsl_object_box(LslBoxedKey{"key"});
        return objects({value, value});
    }
    if (mode == "object_vector") {
        auto value = make_lsl_object_box(framework::Vector3d{1, 2, 3});
        return objects({value, value});
    }
    if (mode == "object_rotation") {
        auto value = make_lsl_object_box(LslRotation{1, 2, 3, 4});
        return objects({value, value});
    }
    if (mode == "object_list") {
        auto list = std::make_shared<LslList>();
        list->items = {make_lsl_object_box(LslBoxedInteger{5})};
        return objects({List{list}, LslNullObject{}});
    }
    if (mode == "object_class")
        return objects({wrapper, wrapper});
    if (mode == "object_null")
        return objects({LslNullObject{}, LslNullObject{}});
    if (mode == "object_nested_integer") {
        auto first = std::get<Array>(make_lsl_fixed_array(LslFixedArrayKind::Integer, 2));
        auto second = std::get<Array>(make_lsl_fixed_array(LslFixedArrayKind::Integer, 2));
        lsl_fixed_array_set(first, 0, std::int32_t{3});
        lsl_fixed_array_set(first, 1, std::int32_t{4});
        lsl_fixed_array_set(second, 0, std::int32_t{5});
        lsl_fixed_array_set(second, 1, std::int32_t{6});
        return objects({first, second});
    }
    if (mode == "object_raw_char")
        return objects({make_lsl_object_box(LslCharacter{65}), make_lsl_object_box(LslCharacter{90})});
    if (mode == "object_boolean")
        return objects({make_lsl_object_box(LslRawBoolean{true}), make_lsl_object_box(LslRawBoolean{false})});
    if (mode == "object_single")
        return objects({make_lsl_object_box(LslRawSingle{1.25f}), make_lsl_object_box(LslRawSingle{-2.5f})});
    if (mode == "object_prefix_null")
        return objects({make_lsl_object_box(std::int32_t{7}), LslNullObject{}});
    throw std::runtime_error("Unreviewed original matrix mode " + std::string(mode));
}

void source_state(const LslInstance &instance, const network::Value &expected) {
    const auto saved = snapshot(instance);
    check(saved.globals.size() == 3, "Array2List source global inventory differs");
    output(saved.globals[1], expected.get("globals").get("output"), "source List output");
    check(std::get<std::int32_t>(saved.globals[2]) ==
                  network::osd_integer(expected.get("globals").get("after").get("value")) &&
              instance.heap_bytes_used() == network::osd_integer(expected.get("heap_used")),
          "Source result/heap differs");
}

void case_test(const std::filesystem::path &folder, const network::Value &proof) {
    const auto source = read(folder / "source.lsl");
    const auto metadata_text = read(folder / "metadata.json");
    const auto matrix_text = read(folder / "array-to-list-matrix.json");
    const auto wire = read(folder / "snapshot.bin");
    const auto object_wire = read(folder / "original.yobj");
    check(framework::sha256_hex(source) == proof.get("sourceSha256").text &&
              framework::sha256_hex(metadata_text) == proof.get("metadataSha256").text &&
              framework::sha256_hex(matrix_text) == proof.get("matrixSha256").text &&
              framework::sha256_hex(wire) == proof.get("snapshotSha256").text &&
              framework::sha256_hex(object_wire) == proof.get("objectSha256").text,
          "Original Array2List fixture changed");
    const auto oracle = network::parse_osd_json(metadata_text);
    const auto rows = network::parse_osd_json(matrix_text).array;
    const auto compiled = compile_lsl(source);
    check(bool(compiled), "Original Array2List source rejected");
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.heap_limit_bytes = 100000000;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](std::int32_t) { ++sleeps; };
    LslInstance owner(compiled.program, {}, host);
    pause(owner);
    source_state(owner, oracle.get("initial"));
    const auto captured = snapshot(owner);
    const auto wrapper = std::get<Class>(captured.globals[0]);
    const auto original = std::get<Array>(wrapper->instance_arrays[5][0]);
    check(rows.size() == 770, "Original Array2List matrix row count differs");
    for (std::size_t n = 0; n < rows.size(); ++n) {
        const auto &row = rows[n];
        const auto label = "Array2List row " + std::to_string(n);
        const auto mode = row.get("mode").text;
        auto storage = backing(mode, wrapper, original);
        // Match the oracle's explicit backend field injection. HeapTrackerObject
        // would reject some unsupported child graphs before the helper is tested.
        wrapper->instance_arrays[5][0] = storage;
        const auto *input = std::get_if<Array>(&storage);
        const auto argument = mode == "null_wrapper"            ? LslValue{LslNullObject{}}
                              : mode == "wrong_integer_wrapper" ? make_lsl_object_box(std::int32_t{42})
                                                                : LslValue{wrapper};
        std::optional<LslValue> result;
        std::string error;
        try {
            result = lsl_script_array_to_list(argument, network::osd_integer(row.get("start")),
                                              network::osd_integer(row.get("count")));
        } catch (const std::exception &failure) {
            error = failure.what();
        }
        const auto &expected_error = row.get("error");
        if (expected_error.kind == network::Value::Kind::Map) {
            const auto type = expected_error.get("type").text;
            check(type.starts_with("System.") && error == type.substr(7) + ": " + expected_error.get("message").text,
                  label + ": exact error differs: " + error);
        } else {
            check(error.empty() && result, label + ": unexpected error: " + error);
            output(*result, row.get("result"), label);
            const auto list = std::get<List>(*result);
            const auto start = network::osd_integer(row.get("start"));
            for (std::size_t i = 0; i < list->items.size(); ++i) {
                check(input && *input, label + ": successful nonempty result lacks source backing");
                const auto cell = lsl_fixed_array_get(*input, static_cast<std::size_t>(start) + i);
                check(same_object(list->items[i], cell) == row.get("resultElementSourceAliases").array[i].boolean,
                      label + ": source value-box alias differs");
                const auto box = std::get<Box>(list->items[i]);
                const auto *string = std::get_if<String>(&cell);
                check((box->string_field && string && box->string_field == *string) ==
                          row.get("stringFieldSourceAliases").array[i].boolean,
                      label + ": SDKString field alias differs");
            }
            if (list->items.size() == 2)
                check(same_object(list->items[0], list->items[1]) == row.get("resultPairSame").boolean,
                      label + ": distinct output value boxes merged");
        }
    }
    wrapper->instance_arrays[5][0] = original;
    clock += snapshot(owner).sleep_remaining + 1;
    check(owner.run(10000).status == LslRunStatus::Idle && sleeps == 1, "Array2List source resume replayed/faulted");
    source_state(owner, oracle.get("live_resume"));
    const auto object = decode_yengine_object_code(bytes(object_wire));
    const auto saved = decode_yengine_snapshot(bytes(wire));
    LslInstance imported(compiled.program, {}, host);
    check(object && saved && imported.restore_yengine_snapshot(*saved, oracle.get("source_hash").text, source, *object),
          "Original Array2List snapshot import rejected");
    source_state(imported, oracle.get("after_load"));
    clock += snapshot(imported).sleep_remaining + 1;
    check(imported.run(10000).status == LslRunStatus::Idle && sleeps == 1,
          "Imported Array2List source replayed/faulted");
    source_state(imported, oracle.get("final"));
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected original Array2List fixture directory");
        const std::filesystem::path root = argv[1];
        const auto text = read(root / "verification.json");
        check(framework::sha256_hex(text) == "4a935ccdfbe27035db4a106559898100a3252522b99418d105092182a28b3baf",
              "Original Array2List proof changed");
        const auto proof = network::parse_osd_json(text);
        unsigned checked = 0;
        for (const auto platform : {"windows", "linux"}) {
            const auto &rows = proof.get("platforms").get(platform).get("cases").array;
            check(rows.size() == 3, "Original source inventory differs");
            for (const auto &row : rows) {
                case_test(root / platform / row.get("name").text, row);
                ++checked;
            }
        }
        collect_lsl_fixed_arrays();
        collect_yengine_managed_cycles();
        check(checked == 6, "Original Array2List platform coverage incomplete");
        std::cout << "Matched six original source captures/imports and4620 direct Array2List conversion/identity/fault "
                     "controls.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
