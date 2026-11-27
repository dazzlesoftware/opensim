// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "yengine_utf16_test_support.hpp"
#include "opensim/framework/dotnet_string_hash.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/scene/object_asset_xml.hpp"
#include "opensim/script/yengine_object_code.hpp"
#include "opensim/script/lsl_fixed_array.hpp"
#include "opensim/script/yengine_xml_values.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <map>
#include <memory>

namespace object_test {
using namespace utf16_test;

std::span<const std::uint8_t> bytes(const std::string &value) {
    return {reinterpret_cast<const std::uint8_t *>(value.data()), value.size()};
}

struct Declarations {
    std::vector<std::string> names;
    std::map<std::string, std::string> types;

    explicit Declarations(std::string source) {
        source = global_declaration_source(std::move(source));
        const std::regex declaration(
            R"(\b(char|integer|float|string|key|list|vector|rotation|object)\s+([A-Za-z_][A-Za-z_0-9]*)\s*(?:=|;))");
        for (auto it = std::sregex_iterator(source.begin(), source.end(), declaration); it != std::sregex_iterator();
             ++it) {
            names.push_back((*it)[2].str());
            types.emplace(names.back(), (*it)[1].str());
        }
    }

    const LslValue &get(const LslCheckpoint &state, const std::string &name) const {
        const auto found = std::find(names.begin(), names.end(), name);
        check(found != names.end(), "Unknown original Object global: " + name);
        return state.globals.at(static_cast<std::size_t>(found - names.begin()));
    }
};

const LslValue &payload(const LslValue &value) {
    if (const auto *box = std::get_if<std::shared_ptr<const LslObjectBox>>(&value)) {
        check(*box && valid_lsl_object_box(**box), "Invalid native Object value box");
        return (*box)->value;
    }
    return value;
}

const void *reference(const LslValue &value) {
    if (const auto *box = std::get_if<std::shared_ptr<const LslObjectBox>>(&value))
        return box->get();
    if (const auto *text = std::get_if<std::shared_ptr<const LslStringReference>>(&value))
        return text->get();
    if (const auto *list = std::get_if<std::shared_ptr<const LslList>>(&value))
        return list->get();
    if (const auto *array = std::get_if<std::shared_ptr<const LslFixedArray>>(&value))
        return array->get();
    check(std::holds_alternative<LslNullObject>(value) || std::holds_alternative<LslNullString>(value) ||
              std::holds_alternative<LslNullList>(value),
          "Object reference silently became an unboxed scalar");
    return nullptr;
}

std::uint64_t original_bits(const network::Value &value) {
    if (!value.get("bits").text.empty())
        return std::stoull(value.get("bits").text, nullptr, 16);
    return std::bit_cast<std::uint64_t>(network::osd_real(value));
}

struct ContentReferences {
    std::map<std::int32_t, const void *> native;
    std::map<const void *, std::int32_t> original;

    void compare(std::int32_t id, const void *pointer, const std::string &where) {
        check(id > 0 && pointer, where + ": missing fixed-array reference identity");
        const auto [managed, managed_new] = native.emplace(id, pointer);
        const auto [local, local_new] = original.emplace(pointer, id);
        check((managed_new || managed->second == pointer) && (local_new || local->second == id),
              where + ": fixed-array graph aliases differ");
    }
};

std::string fixed_array_cells_hash(const std::shared_ptr<const LslFixedArray> &array) {
    std::string bytes;
    const auto number = [&](std::uint64_t bits, unsigned count) {
        for (unsigned index = 0; index < count; ++index)
            bytes.push_back(static_cast<char>((bits >> (index * 8)) & 255));
    };
    for (std::size_t index = 0; index < lsl_fixed_array_size(array); ++index) {
        const auto cell = lsl_fixed_array_get(array, index);
        switch (array->kind) {
        case LslFixedArrayKind::Character:
            number(std::get<LslCharacter>(cell).value, 2);
            break;
        case LslFixedArrayKind::Float:
            number(std::bit_cast<std::uint64_t>(std::get<double>(cell)), 8);
            break;
        case LslFixedArrayKind::Integer:
            number(std::bit_cast<std::uint32_t>(std::get<std::int32_t>(cell)), 4);
            break;
        case LslFixedArrayKind::Object:
            if (std::holds_alternative<LslNullObject>(cell))
                number(0, 1);
            else {
                const auto units = lsl_string_code_units(cell);
                check(units.has_value(), "Unreviewed Object element in large-array fingerprint");
                const auto text = framework::utf8(*units);
                number(1, 1);
                number(text.size(), 4);
                bytes += text;
            }
        }
    }
    return framework::sha256_hex(bytes);
}

std::string integer_list_cells_hash(const std::shared_ptr<const LslList> &list) {
    std::string data;
    for (const auto &item : list->items) {
        const auto value = unbox_lsl_value(item);
        const auto *integer = std::get_if<std::int32_t>(&value);
        check(integer != nullptr, "Unreviewed List element in integer-list fingerprint");
        const auto bits = std::bit_cast<std::uint32_t>(*integer);
        for (unsigned index = 0; index < 4; ++index)
            data.push_back(static_cast<char>((bits >> (index * 8)) & 255));
    }
    return framework::sha256_hex(data);
}

void content(const LslValue &actual, const network::Value &expected, const std::string &where,
             std::string_view declared = {}, ContentReferences *references = nullptr) {
    const auto &value = payload(actual);
    const auto type = expected.get("type").text;
    const auto clr = expected.get("clr").text;
    if (type == "null") {
        const bool matches = declared == "string" || declared == "key" ? std::holds_alternative<LslNullString>(value)
                             : declared == "list"                      ? std::holds_alternative<LslNullList>(value)
                                                                       : std::holds_alternative<LslNullObject>(value);
        check(matches, where + ": original null storage type differs");
    } else if (type == "fixed_array" || type == "array_reference" || type == "fixed_array_summary") {
        const auto *array = std::get_if<std::shared_ptr<const LslFixedArray>>(&actual);
        check(array && *array && references, where + ": fixed-array carrier missing");
        static constexpr std::array<std::string_view, 4> names{"System.Char[]", "System.Double[]", "System.Int32[]",
                                                               "System.Object[]"};
        check(clr == names.at(static_cast<std::size_t>((*array)->kind)), where + ": fixed-array CLR type differs");
        references->compare(network::osd_integer(expected.get("reference_id")), array->get(), where);
        if (type == "fixed_array_summary") {
            const auto count = lsl_fixed_array_size(*array);
            check(count == static_cast<std::size_t>(network::osd_integer(expected.get("length"))) && count > 0,
                  where + ": large-array length differs");
            check(fixed_array_cells_hash(*array) == expected.get("cells_sha256").text,
                  where + ": complete large-array cell fingerprint differs");
            for (const auto &[index, name] : std::array<std::pair<std::size_t, const char *>, 3>{
                     {{0, "first"}, {count / 2, "middle"}, {count - 1, "last"}}})
                content(lsl_fixed_array_get(*array, index), expected.get(name), where + "/" + name, {}, references);
        }
        if (type == "fixed_array") {
            const auto &items = expected.get("value").array;
            check(lsl_fixed_array_size(*array) == items.size() &&
                      items.size() == static_cast<std::size_t>(network::osd_integer(expected.get("length"))),
                  where + ": fixed-array length differs");
            for (std::size_t index = 0; index < items.size(); ++index)
                content(lsl_fixed_array_get(*array, index), items[index], where + "[" + std::to_string(index) + "]", {},
                        references);
        }
    } else if (type == "boolean" && clr == "System.Boolean") {
        const auto *boolean = std::get_if<LslRawBoolean>(&value);
        check(boolean && boolean->value == expected.get("value").boolean, where + ": raw CLR Boolean element differs");
    } else if (type == "unsupported" && clr == "System.Boolean") {
        const auto *boolean = std::get_if<LslRawBoolean>(&value);
        check(boolean && expected.get("value").text == (boolean->value ? "True" : "False"),
              where + ": raw CLR Boolean element differs");
    } else if (type == "integer") {
        const auto *number = std::get_if<std::int32_t>(&value);
        if (clr.ends_with("+LSLInteger")) {
            const auto *boxed = std::get_if<LslBoxedInteger>(&value);
            check(boxed != nullptr, where + ": SDK Integer became raw CLR Int32");
            number = &boxed->value;
        }
        check(number && *number == network::osd_integer(expected.get("value")), where + ": Integer differs");
    } else if (type == "float") {
        const auto *number = std::get_if<double>(&value);
        if (clr.ends_with("+LSLFloat")) {
            const auto *boxed = std::get_if<LslBoxedFloat>(&value);
            check(boxed != nullptr, where + ": SDK Float became raw CLR Double");
            number = &boxed->value;
        }
        check(number && std::bit_cast<std::uint64_t>(*number) == original_bits(expected),
              where + ": original Float bits differ");
    } else if (type == "char") {
        const auto *character = std::get_if<LslCharacter>(&value);
        check(character && character->value == network::osd_integer(expected.get("value")),
              where + ": original Char code unit differs");
    } else if (type == "string") {
        if (expected.get("utf16").kind != network::Value::Kind::Array) {
            check(clr.ends_with("+LSLString") && std::holds_alternative<LslBoxedNullString>(value),
                  where + ": SDK nullable String field differs");
            return;
        }
        if (clr == "System.String")
            check(std::holds_alternative<std::shared_ptr<const LslStringReference>>(actual),
                  where + ": raw String reference was not retained");
        else
            check(clr.ends_with("+LSLString") && (std::holds_alternative<LslBoxedString>(value) ||
                                                  std::holds_alternative<LslBoxedUtf16String>(value)),
                  where + ": SDK String boxing type differs");
        const auto units = lsl_string_code_units(actual);
        const auto &original = expected.get("utf16").array;
        check(units && units->size() == original.size(), where + ": String length differs");
        for (std::size_t i = 0; i < original.size(); ++i)
            check((*units)[i] == network::osd_integer(original[i]), where + ": String code units differ");
    } else if (type == "vector" || type == "rotation") {
        std::vector<double> components;
        if (type == "vector") {
            const auto *vector = std::get_if<framework::Vector3d>(&value);
            check(vector != nullptr, where + ": Vector payload differs");
            components = {vector->x, vector->y, vector->z};
        } else {
            const auto *rotation = std::get_if<LslRotation>(&value);
            check(rotation != nullptr, where + ": Rotation payload differs");
            components = {rotation->x, rotation->y, rotation->z, rotation->w};
        }
        check(components.size() == expected.get("value").array.size(), where + ": component count differs");
        for (std::size_t i = 0; i < components.size(); ++i)
            check(std::bit_cast<std::uint64_t>(components[i]) == original_bits(expected.get("value").array[i]),
                  where + ": component IEEE bits differ");
    } else if (type == "list_summary") {
        const auto *list = std::get_if<std::shared_ptr<const LslList>>(&value);
        const auto count = static_cast<std::size_t>(network::osd_integer(expected.get("length")));
        check(list && *list && count > 0 && (*list)->items.size() == count, where + ": large List length differs");
        check(integer_list_cells_hash(*list) == expected.get("cells_sha256").text,
              where + ": complete large List cell fingerprint differs");
        for (const auto &[index, name] : std::array<std::pair<std::size_t, const char *>, 3>{
                 {{0, "first"}, {count / 2, "middle"}, {count - 1, "last"}}})
            content((*list)->items[index], expected.get(name), where + "/" + name, {}, references);
    } else if (type == "list") {
        const auto *list = std::get_if<std::shared_ptr<const LslList>>(&value);
        check(list && *list && (*list)->items.size() == expected.get("value").array.size(),
              where + ": List storage differs");
        for (std::size_t i = 0; i < (*list)->items.size(); ++i)
            content((*list)->items[i], expected.get("value").array[i], where + "[" + std::to_string(i) + "]", {},
                    references);
    } else
        throw std::runtime_error(where + ": Unreviewed original Object payload " + clr);
    if (declared == "object")
        (void)reference(actual);
}

void state(const LslCheckpoint &actual, const Declarations &declarations, const network::Value &observation,
           const network::Value &row) {
    auto expected = observation.get("globals");
    if (row.get("name").text.starts_with("typename_hash_")) {
        const auto *array = std::get_if<std::shared_ptr<const LslFixedArray>>(&declarations.get(actual, "input"));
        check(array && *array && (*array)->identity_hash && *(*array)->identity_hash > 0 &&
                  *(*array)->identity_hash <= 0x3ffffffu,
              "Fixed-array hash was not cached on its backing instance");
        check(row.get("hash_call_lines").array.size() == 2, "Original fixed-array hash-call controls changed");
        for (const auto *name : {"first", "second"})
            expected.map.at(name).map.at("value").integer = *(*array)->identity_hash;
    }
    // Hash values of reference types are process-dependent. The independent
    // seeded hash/identity corpus proves their algorithms; here we also prove
    // that the source call reads the matching String or List instance.
    if (!row.get("hash_call_lines").array.empty() && expected.map.contains("result") &&
        expected.map.contains("input") && row.get("direct_object_hash").kind == network::Value::Kind::Integer &&
        network::osd_integer(expected.get("result").get("value")) ==
            network::osd_integer(row.get("direct_object_hash"))) {
        const auto &input = declarations.get(actual, "input");
        const auto kind = row.get("direct_object_input").get("type").text;
        if (kind == "string") {
            const auto units = lsl_string_code_units(input);
            check(units.has_value(), "String Object hash input missing");
            expected.map.at("result").map.at("value").integer =
                std::bit_cast<std::int32_t>(framework::dotnet_string_hash(*units));
        } else if (kind == "list") {
            const auto *list = std::get_if<std::shared_ptr<const LslList>>(&input);
            check(list && *list && (*list)->identity_hash && *(*list)->identity_hash > 0 &&
                      *(*list)->identity_hash <= 0x3ffffffu,
                  "List Object hash was not cached on its backing instance");
            expected.map.at("result").map.at("value").integer = *(*list)->identity_hash;
        }
    }
    check(declarations.names.size() == actual.globals.size() && actual.globals.size() == expected.map.size(),
          "Object global declaration/storage count differs");
    ContentReferences references;
    for (std::size_t i = 0; i < declarations.names.size(); ++i) {
        const auto &name = declarations.names[i];
        content(actual.globals[i], expected.get(name), name, declarations.types.at(name), &references);
    }
    const auto &expected_alias = observation.get("object_hash").get("alias_same_backing");
    if (expected_alias.kind == network::Value::Kind::Boolean) {
        const auto *input = std::get_if<std::shared_ptr<const LslList>>(&payload(declarations.get(actual, "input")));
        const auto *alias = std::get_if<std::shared_ptr<const LslList>>(&payload(declarations.get(actual, "alias")));
        check(input && alias && ((*input == *alias) == expected_alias.boolean), "Original List backing alias differs");
    }
    std::map<std::string, LslValue> indexed_references;
    const auto named_reference = [&](const std::string &name) -> const LslValue & {
        const auto bracket = name.find('[');
        if (bracket == std::string::npos)
            return declarations.get(actual, name);
        if (const auto found = indexed_references.find(name); found != indexed_references.end())
            return found->second;
        check(name.back() == ']' && name.find('[', bracket + 1) == std::string::npos,
              "Unreviewed indexed original reference path: " + name);
        const auto &value = declarations.get(actual, name.substr(0, bracket));
        const auto *array = std::get_if<std::shared_ptr<const LslFixedArray>>(&value);
        check(array && *array, "Indexed original reference root is not a fixed array: " + name);
        auto element = lsl_fixed_array_get(*array, std::stoul(name.substr(bracket + 1, name.size() - bracket - 2)));
        // Array.GetValue boxes a primitive cell afresh for each observed path.
        // Object[] already stores the reference and returns that same object.
        if ((*array)->kind != LslFixedArrayKind::Object)
            element = make_lsl_object_box(std::move(element));
        return indexed_references.emplace(name, std::move(element)).first->second;
    };
    for (const auto &pair : observation.get("object_reference_pairs").array)
        check((reference(named_reference(pair.get("left").text)) ==
               reference(named_reference(pair.get("right").text))) == pair.get("same_reference").boolean,
              "Object reference aliases differ: " + pair.get("left").text + "/" + pair.get("right").text);
    const auto resolve = [&](const std::string &path) -> const LslValue & {
        const auto slash = path.find('/');
        check(slash != std::string::npos, "Invalid original reference path: " + path);
        auto cursor = slash + 1;
        auto end = path.find('/', cursor);
        const auto component = path.substr(cursor, end - cursor);
        const LslValue *value = nullptr;
        if (path.starts_with("global/"))
            value = &declarations.get(actual, component);
        else {
            check(path.starts_with("queue"), "Unsupported original reference root: " + path);
            const auto event = std::stoul(path.substr(5, slash - 5));
            value = &actual.queued_events.at(event).arguments.at(std::stoul(component));
        }
        while (end != std::string::npos) {
            cursor = end + 1;
            end = path.find('/', cursor);
            const auto *list = std::get_if<std::shared_ptr<const LslList>>(&payload(*value));
            check(list && *list, "Original reference path traversed a non-List: " + path);
            value = &(*list)->items.at(std::stoul(path.substr(cursor, end - cursor)));
        }
        return *value;
    };
    const auto string_field = [](const LslValue &value) -> std::pair<bool, const void *> {
        if (const auto *text = std::get_if<std::shared_ptr<const LslStringReference>>(&value))
            return {true, text->get()};
        if (const auto *box = std::get_if<std::shared_ptr<const LslObjectBox>>(&value)) {
            check(*box && valid_lsl_object_box(**box), "Invalid reference-pair Object box");
            if (std::holds_alternative<LslBoxedString>((*box)->value) ||
                std::holds_alternative<LslBoxedUtf16String>((*box)->value) ||
                std::holds_alternative<LslBoxedKey>((*box)->value) ||
                std::holds_alternative<LslBoxedUtf16Key>((*box)->value) ||
                std::holds_alternative<LslBoxedNullString>((*box)->value) ||
                std::holds_alternative<LslBoxedNullKey>((*box)->value))
                return {true, (*box)->string_field.get()};
        }
        return {false, nullptr};
    };
    for (const auto &pair : observation.get("all_reference_pairs").array) {
        const auto &left = resolve(pair.get("left").text);
        const auto &right = resolve(pair.get("right").text);
        const auto label = pair.get("left").text + "/" + pair.get("right").text;
        check((reference(left) == reference(right)) == pair.get("same_object_reference").boolean,
              "Queued/nested Object aliases differ: " + label);
        const auto left_field = string_field(left), right_field = string_field(right);
        check(left_field.first == pair.get("left_has_string_field").boolean &&
                  right_field.first == pair.get("right_has_string_field").boolean,
              "SDK String field presence differs: " + label);
        if (pair.get("same_string_field_reference").kind == network::Value::Kind::Boolean)
            check((left_field.second == right_field.second) == pair.get("same_string_field_reference").boolean,
                  "SDK String field aliases differ: " + label);
    }
}

void artifact_hashes(const std::filesystem::path &folder, const network::Value &row) {
    for (const auto &[name, digest] : row.get("sha256").map) {
        // These entries identify the original build inputs, rather than files
        // within a case directory. The pinned corpus metadata binds them;
        // executable case artifacts below must exist and match individually.
        if (name.starts_with("generator_") || name.starts_with("original_")) {
            check(digest.text.size() == 64 && digest.text.find_first_not_of("0123456789abcdef") == std::string::npos,
                  "Invalid original provenance hash: " + name);
            continue;
        }
        const auto path = folder / name;
        check(std::filesystem::is_regular_file(path) && framework::sha256_hex(read(path)) == digest.text,
              "Original Object artifact missing or changed: " + name);
    }
}

void fixture(const std::filesystem::path &folder, const network::Value &row, std::int32_t heap_limit = 1024 * 1024,
             std::size_t execution_budget = 1) {
    artifact_hashes(folder, row);
    const auto source = read(folder / "source.lsl");
    const auto compiled = compile_lsl(source);
    std::string diagnostic;
    for (const auto &error : compiled.diagnostics)
        diagnostic += " " + error.message;
    check(bool(compiled) == row.get("accepted").boolean, "Object compile acceptance differs:" + diagnostic);
    if (!compiled)
        return;
    check(yengine_source_matches(source, row.get("source_hash").text), "Object source token identity differs");
    const Declarations declarations(source);
    const auto compare_state = [&](const LslCheckpoint &checkpoint, const network::Value &expected,
                                   const std::string &stage) {
        try {
            state(checkpoint, declarations, expected, row);
        } catch (const std::exception &error) {
            throw std::runtime_error(stage + ": " + error.what());
        }
    };
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.heap_limit_bytes = heap_limit;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](int) { ++sleeps; };
    auto vm = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
    auto previous = vm->checkpoint();
    bool stopped = false;
    const bool captured = row.get("suspended").boolean;
    for (unsigned step = 0; step < 5000; ++step) {
        const auto result = vm->run(execution_budget);
        if (result.status == LslRunStatus::Faulted) {
            const auto &error = row.get("initial_exception");
            const auto type = error.get("type").text;
            check(type.starts_with("System.") && result.error == type.substr(7) + ": " + error.get("message").text,
                  "Object fault category/text differs: " + result.error);
            check(previous.has_value(), "Object pre-fault state missing");
            compare_state(*previous, row.get("initial"), "before fault");
            stopped = true;
            break;
        }
        previous = vm->checkpoint();
        check(previous.has_value(), "Object checkpoint missing");
        if (result.status == LslRunStatus::Sleeping) {
            check(captured && sleeps == 1 && previous->sleep_remaining > 0, "Object source sleep differs");
            compare_state(*previous, row.get("initial"), "sleep capture");
            stopped = true;
            break;
        }
        if (result.status == LslRunStatus::Idle) {
            check(!captured && row.get("initial_exception").kind != network::Value::Kind::Map,
                  "Original Object fault/sleep became success");
            compare_state(*previous, row.get("initial"), "completed initial entry");
            check(vm->run(execution_budget).status == LslRunStatus::Idle, "Object entry replayed");
            stopped = true;
            break;
        }
        const auto wire = encode_lsl_checkpoint(*previous);
        const auto decoded = decode_lsl_checkpoint(wire);
        check(decoded && encode_lsl_checkpoint(*decoded) == wire, "Object internal checkpoint changed bytes");
        auto next = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
        check(next->restore_checkpoint(*decoded), "Object internal fresh-VM restore rejected");
        vm = std::move(next);
    }
    check(stopped, "Object source execution exceeded bound");
    if (!captured)
        return;
    std::vector<std::unique_ptr<LslInstance>> paths;
    paths.push_back(std::move(vm));
    const auto projection = paths.front()->persistent_checkpoint();
    check(projection.has_value(), "Persistent Object capture missing");
    const auto check_fault = [](const std::string &actual, const network::Value &error, const std::string &stage) {
        const auto type = error.get("type").text;
        check(type.starts_with("System.") && actual == type.substr(7) + ": " + error.get("message").text,
              stage + ": original fault differs: " + actual);
    };
    if (row.get("production_write_exception").kind == network::Value::Kind::Map) {
        bool failed = false;
        try {
            (void)encode_persistent_lsl_checkpoint(*projection);
        } catch (const std::exception &error) {
            check_fault(error.what(), row.get("production_write_exception"), "persistent write");
            failed = true;
        }
        check(failed, "Original persistent write fault became success");
    } else {
        const auto wire = encode_persistent_lsl_checkpoint(*projection);
        const auto decoded = decode_lsl_checkpoint(wire);
        const bool queue_failed = row.get("queue_restore_exception").kind == network::Value::Kind::Map;
        const bool binary_failed = row.get("binary_load_exception").kind == network::Value::Kind::Map;
        if (binary_failed) {
            check(decoded.has_value(), "Valid original binary graph became a native format failure");
            auto external = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
            check(!external->restore_checkpoint(*decoded), "Original binary heap-accounting failure became success");
        } else if (queue_failed) {
            check(!decoded && !wire.empty(), "Queued Array write/reload failure stages differ");
            auto binary_only = *projection;
            binary_only.queued_events.clear();
            const auto preceding = decode_lsl_checkpoint(encode_persistent_lsl_checkpoint(binary_only));
            auto external = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
            check(preceding && external->restore_checkpoint(*preceding), "Native binary before queue failure rejected");
            compare_state(*external->checkpoint(), row.get("after_binary_load"), "native binary before queue failure");
        } else {
            auto external = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
            check(decoded && external->restore_checkpoint(*decoded), "Persistent Object capture rejected");
            compare_state(*external->checkpoint(), row.get("after_load"), "native persistent reload");
            paths.push_back(std::move(external));
        }
        const auto snapshot = read(folder / "snapshot.bin");
        const auto base64 = network::binary_base64(
            network::osd_from_binary(std::make_shared<std::vector<std::uint8_t>>(snapshot.begin(), snapshot.end())));
        const auto xml = "<ScriptState Engine=\"YEngine\" SourceHash=\"" + row.get("source_hash").text +
                         "\"><Running>True</Running><Snapshot>" + base64 + "</Snapshot>" +
                         read(folder / "detect-array.xml") + read(folder / "event-queue.xml") + "</ScriptState>";
        const auto original = scene::read_yengine_state_xml(bytes(xml), framework::Uuid{});
        const auto object = decode_yengine_object_code(bytes(read(folder / "original.yobj")));
        auto imported = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
        const auto binary = decode_yengine_snapshot(bytes(snapshot));
        check(binary && object, "Actual original Object binary/object-code format rejected");
        const bool restored = imported->restore_yengine_snapshot(*binary, row.get("source_hash").text, source, *object);
        if (binary_failed) {
            check(!restored, "Original binary import heap-accounting failure became success");
            check(row.get("binary_load_exception").get("type").text == "System.Exception" &&
                      row.get("binary_load_exception").get("message").text == "unknown size of type Boolean",
                  "Unreviewed original binary-load exception category");
        } else if (queue_failed) {
            check(restored, "Actual original binary before queue failure rejected");
            check(!original, "Original queued Array XML failure became success");
            compare_state(*imported->checkpoint(), row.get("after_binary_load"),
                          "original binary before queue failure");
            const auto *array =
                std::get_if<std::shared_ptr<const LslFixedArray>>(&declarations.get(*projection, "input"));
            check(array && *array, "Queued Array control lacks array input");
            static constexpr std::array<std::string_view, 4> names{"System.Char[]", "System.Double[]", "System.Int32[]",
                                                                   "System.Object[]"};
            bool failed = false;
            try {
                (void)parse_yengine_xml_scalar(names.at(static_cast<std::size_t>((*array)->kind)), u"ignored");
            } catch (const std::exception &error) {
                check_fault(error.what(), row.get("queue_restore_exception"), "queued Array XML reload");
                failed = true;
            }
            check(failed, "Queued Array XML cast failure missing");
        } else {
            check(restored && original && original->snapshot,
                  "Actual original capture import rejected: VM=" + std::to_string(restored) +
                      " XML=" + std::to_string(original.has_value()) +
                      " snapshot=" + std::to_string(original && original->snapshot.has_value()));
            imported->restore_queued_events(original->events);
            compare_state(*imported->checkpoint(), row.get("after_load"), "original binary import");
            paths.push_back(std::move(imported));
        }
    }
    for (std::size_t path = 0; path < paths.size(); ++path) {
        auto &current = paths[path];
        clock += current->checkpoint()->sleep_remaining + 1;
        bool done = false;
        auto last = current->checkpoint();
        for (unsigned step = 0; step < 5000; ++step) {
            const auto result = current->run(execution_budget);
            const auto &expected_fault = row.get(path == 0 ? "live_resume_exception" : "resume_exception");
            if (result.status == LslRunStatus::Faulted) {
                check_fault(result.error, expected_fault, "resumed source");
                check(last.has_value(), "Resumed source pre-fault state missing");
                compare_state(*last, row.get(path == 0 ? "live_resume" : "final"), "resumed source before fault");
                done = true;
                break;
            }
            check(result.status != LslRunStatus::Sleeping, "Object resume faulted or replayed sleep: " + result.error);
            const auto checkpoint = current->checkpoint();
            check(checkpoint.has_value(), "Object resume state missing");
            if (result.status == LslRunStatus::Idle) {
                check(expected_fault.kind != network::Value::Kind::Map, "Original resume fault became success");
                compare_state(*checkpoint, row.get(path == 0 ? "live_resume" : "final"),
                              std::array<const char *, 3>{"live resume", "native persistent resume",
                                                          "original imported resume"}[path]);
                check(current->run(execution_budget).status == LslRunStatus::Idle && sleeps == 1,
                      "Object resume replayed effects");
                done = true;
                break;
            }
            last = checkpoint;
            const auto saved = decode_lsl_checkpoint(encode_lsl_checkpoint(*checkpoint));
            auto next = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
            check(saved && next->restore_checkpoint(*saved), "Object resume budget restore rejected");
            current = std::move(next);
        }
        check(done, "Object source resume exceeded bound");
    }
}
} // namespace object_test
