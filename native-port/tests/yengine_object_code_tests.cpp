// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/yengine_object_code.hpp"
#include "opensim/script/yengine_continuation.hpp"
#include "opensim/framework/uuid.hpp"
#include "opensim/network/osd_json.hpp"
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

using namespace opensim;
using namespace script;

namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}

std::string read(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    check(bool(stream), "Could not read original object fixture: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void compare_record(const YEngineObjectRecord &actual, const network::Value &expected) {
    using Kind = YEngineObjectRecordKind;
    auto opcode = *actual.opcode;
    bool constant_opcode = false;
    if (actual.kind == Kind::EmitInteger && opcode == 0x20) {
        const auto value = actual.integers.at(0);
        // The original loader shortens ldc.i4 before its ObjectTokens callback.
        if (value >= -1 && value <= 8) {
            opcode = static_cast<std::int16_t>(0x16 + value);
            constant_opcode = true;
        } else if (value >= 0 && value <= 127)
            opcode = 0x1f;
    }
    check(opcode == network::osd_integer(expected.get("opcode_value")), "Original opcode value differs");
    const auto &source = expected.get("source");
    check(actual.location.file == source.get("file").text &&
              actual.location.line == network::osd_integer(source.get("line")) &&
              actual.location.column == network::osd_integer(source.get("column")),
          "Original opcode source location differs");
    const auto &operand = expected.get("operand");
    const auto &kind = expected.get("operand_kind").text;
    if (kind == "none") {
        check(actual.kind == Kind::EmitNone || constant_opcode, "Original operand-free instruction differs");
    } else if (kind == "integer" || kind == "local" || kind == "label") {
        const auto wanted = kind == "integer" ? Kind::EmitInteger : kind == "local" ? Kind::EmitLocal : Kind::EmitLabel;
        check(actual.kind == wanted && actual.integers.size() == 1 &&
                  actual.integers.front() == network::osd_integer(operand),
              "Original numeric/local/label operand differs");
    } else if (kind == "labels") {
        check(actual.kind == Kind::EmitLabels && actual.integers.size() == operand.array.size(),
              "Original switch label count differs");
        for (std::size_t i = 0; i < actual.integers.size(); ++i)
            check(actual.integers[i] == network::osd_integer(operand.array[i]), "Original switch label differs");
    } else if (kind == "string") {
        check(actual.kind == Kind::EmitString && actual.strings.at(0) == operand.text,
              "Original string literal differs");
    } else if (kind == "field") {
        check(actual.kind == Kind::EmitField && operand.text.ends_with(":" + actual.strings.at(1)),
              "Original field name differs");
    } else if (kind == "method") {
        check((actual.kind == Kind::EmitInternalMethod || actual.kind == Kind::EmitExternalMethod) &&
                  (operand.text == actual.strings.at(0) || operand.text.ends_with(":" + actual.strings.at(0))),
              "Original method name differs");
    } else if (kind == "type" || kind == "constructor") {
        check(actual.kind == (kind == "type" ? Kind::EmitType : Kind::EmitConstructor) &&
                  !actual.strings.at(0).empty() && !operand.text.empty(),
              "Original reflected type operand differs");
    } else if (kind == "double") {
        check(actual.kind == Kind::EmitDouble &&
                  std::bit_cast<double>(*actual.floating_bits) == network::osd_real(operand),
              "Original double literal differs");
    } else if (kind == "float") {
        check(actual.kind == Kind::EmitFloat &&
                  std::bit_cast<float>(static_cast<std::uint32_t>(*actual.floating_bits)) ==
                      static_cast<float>(network::osd_real(operand)),
              "Original float literal differs");
    } else
        throw std::runtime_error("Unhandled original operand kind: " + kind);
}

void compare_continuation(const YEngineObjectMethod &method, const YEngineMethodContinuation &actual,
                          const network::Value &expected, const network::Value &captured) {
    const auto name = expected.get("name").text;
    check(actual.supported,
          "Original captured method rejected by continuation analyzer: " + name + ": " + actual.error);
    const auto &calls = expected.get("call_labels").array;
    check(actual.calls.size() == calls.size(), "Original continuation label count differs: " + name);
    for (const auto &call : calls) {
        const auto number = network::osd_integer(call.get("call_no"));
        check(actual.calls.contains(number), "Original call number missing: " + name);
        const auto &found = actual.calls.at(number);
        const auto &label = call.get("label");
        const auto label_number = network::osd_integer(label.get("number"));
        const auto &source = call.get("source");
        check(found.call_no == number && found.label_number == label_number && found.label == label.get("name").text &&
                  found.label_record == method.label_records.at(label_number),
              "Original call label identity differs: " + name);
        check(found.location.file == source.get("file").text &&
                  found.location.line == network::osd_integer(source.get("line")) &&
                  found.location.column == network::osd_integer(source.get("column")),
              "Original call label source differs: " + name);
        const auto offset = network::osd_integer(label.get("offset"));
        const network::Value *target = nullptr;
        for (const auto &record : expected.get("records").array)
            if (network::osd_integer(record.get("offset")) >= offset && record.get("operand_kind").text == "method") {
                target = &record;
                break;
            }
        check(target, "Original callback has no continuation call target: " + name);
        const auto &original = target->get("operand").text;
        const auto separator = original.rfind(':');
        check(separator != std::string::npos, "Original callback call target malformed: " + name);
        const auto target_name = original.substr(separator + 1);
        if (original.starts_with("<script>:"))
            check(found.kind == YEngineContinuationKind::InternalCall && found.callee == target_name,
                  "Original internal continuation target differs: " + name);
        else {
            const auto wanted = target_name == "CheckRunStack" ? YEngineContinuationKind::EntryCheck
                                : target_name == "CheckRunQuick" || target_name == "CheckRun"
                                    ? YEngineContinuationKind::BudgetCheck
                                    : YEngineContinuationKind::ExternalCall;
            check(found.kind == wanted && found.callee.ends_with(":" + target_name),
                  "Original external continuation target differs: " + name);
        }
        check(found.resume_record > found.label_record && found.resume_record < method.records.size() &&
                  method.records[found.resume_record].opcode.has_value(),
              "Continuation resume record is not an original opcode: " + name);
    }
    const auto &slots = expected.get("saved_slots").array;
    check(actual.slots.size() == slots.size(), "Original saved-slot count differs: " + name);
    std::set<std::size_t> occupied;
    for (const auto &slot : slots) {
        const auto index = static_cast<std::size_t>(network::osd_integer(slot.get("index")));
        const YEngineSavedSlot *found = nullptr;
        for (const auto &candidate : actual.slots)
            if (candidate.index == index) {
                check(!found, "Analyzer duplicated original saved-slot index: " + name);
                found = &candidate;
            }
        check(found, "Original saved slot omitted: " + name);
        const auto storage =
            slot.get("storage_kind").text == "argument" ? YEngineSavedStorage::Argument : YEngineSavedStorage::Local;
        check(found->storage == storage && found->number == network::osd_integer(slot.get("storage_number")) &&
                  found->name == slot.get("name").text && found->type == slot.get("type").text,
              "Original saved-slot declaration/storage differs: " + name);
        check(found->definitions.size() == found->definition_records.size(),
              "Saved-slot definition provenance missing: " + name);
        for (std::size_t i = 0; i < found->definition_records.size(); ++i) {
            const auto record = found->definition_records[i];
            check(record < method.records.size() &&
                      method.records[record].location.file == found->definitions[i].file &&
                      method.records[record].location.line == found->definitions[i].line &&
                      method.records[record].location.column == found->definitions[i].column,
                  "Saved-slot definition provenance differs from original record: " + name);
        }
        occupied.insert(index);
    }
    // Event arguments are counted by original nSaves but copied separately.
    // Compare the genuine captured array and its unused null cells, rather than
    // assuming the number of copied slot descriptors equals its allocation size.
    const auto &values = captured.get("slots").get("children").array;
    check(actual.slot_count == values.size(), "Original CaptureStackFrame allocation size differs: " + name);
    std::set<std::size_t> unused(actual.unused_slots.begin(), actual.unused_slots.end());
    check(unused.size() == actual.unused_slots.size(), "Analyzer duplicated unused frame slots: " + name);
    for (std::size_t index = 0; index < values.size(); ++index) {
        check(occupied.contains(index) != unused.contains(index),
              "Original frame slot is missing or multiply classified: " + name);
        if (unused.contains(index))
            check(network::osd_integer(values[index].get("kind")) == 0,
                  "Original unused frame slot was not null: " + name);
    }
}

std::size_t fixture(const std::filesystem::path &directory, std::size_t &continuations) {
    const auto wire = read(directory / "original.yobj");
    const auto bytes = std::span(reinterpret_cast<const std::uint8_t *>(wire.data()), wire.size());
    const auto code = decode_yengine_object_code(bytes);
    check(bool(code), "Original portable object was rejected: " + directory.string());
    const auto expected = network::parse_osd_json(read(directory / "object_metadata.json"));
    auto cache_key = read(directory / "cache_key.txt");
    while (!cache_key.empty() && (cache_key.back() == '\n' || cache_key.back() == '\r'))
        cache_key.pop_back();
    const auto asset = framework::Uuid::random();
    check(yengine_object_cache_key(read(directory / "source.lsl"), asset) == cache_key,
          "Original SHA256 six-bit object cache key differs");
    check(yengine_object_cache_key("// engine selection\n" + cache_key + " \r\n", asset) == cache_key &&
              yengine_object_cache_key("// no source\n \t\r\n", asset) == asset.str(),
          "Original compiled-key/comment-only cache selection differs");
    check(code->compiled_version == network::osd_integer(expected.get("compiled_version")) &&
              code->source_hash == expected.get("source_hash").text,
          "Original object identity differs");
    check(code->states.size() == expected.get("states").array.size(), "Original state count differs");
    for (std::size_t i = 0; i < code->states.size(); ++i)
        check(code->states[i] == expected.get("states").array[i].text, "Original state name differs");
    for (const auto &global : code->globals)
        check(expected.get("global_names").get(global.type).get(std::to_string(global.index)).text == global.name,
              "Original global slot/name differs");
    const auto continuation_metadata = analyze_yengine_continuations(*code);
    check(continuation_metadata.compiled_version == code->compiled_version &&
              continuation_metadata.source_hash == code->source_hash,
          "Continuation analyzer changed original object identity");
    const auto captures = network::parse_osd_json(read(directory / "metadata.json"));
    const auto genuine = !network::osd_boolean(captures.get("supplemental_original_writer_graph"));
    const auto &methods = expected.get("methods").array;
    check(code->methods.size() == methods.size(), "Original method count differs");
    std::size_t records = 0;
    for (const auto &method : methods) {
        const auto &name = method.get("name").text;
        check(code->methods.contains(name), "Original method declaration missing: " + name);
        const auto &actual = code->methods.at(name);
        check(actual.has_body == !method.get("records").array.empty(),
              "Original unused method declaration/body distinction differs");
        check(actual.return_type == method.get("return_type").text &&
                  actual.arguments.size() == method.get("parameters").array.size(),
              "Original method signature differs");
        for (std::size_t i = 0; i < actual.arguments.size(); ++i) {
            const auto &argument = method.get("parameters").array[i];
            check(actual.arguments[i].name == argument.get("name").text &&
                      actual.arguments[i].type == argument.get("type").text,
                  "Original parameter declaration differs");
        }
        check(actual.locals.size() == method.get("locals").array.size(), "Original local count differs");
        for (const auto &local : method.get("locals").array) {
            const auto number = network::osd_integer(local.get("number"));
            check(actual.locals.contains(number) && actual.locals.at(number).name == local.get("name").text &&
                      actual.locals.at(number).type == local.get("type").text,
                  "Original local declaration differs");
        }
        check(actual.labels.size() == method.get("labels").array.size(), "Original label count differs");
        for (const auto &label : method.get("labels").array) {
            const auto number = network::osd_integer(label.get("number"));
            check(actual.labels.contains(number) && actual.labels.at(number) == label.get("name").text,
                  "Original label declaration differs");
            if (label.get("offset").kind == network::Value::Kind::Integer)
                check(actual.label_records.contains(number), "Original marked label lost its target");
        }
        std::vector<const YEngineObjectRecord *> opcodes;
        for (const auto &record : actual.records)
            if (record.opcode)
                opcodes.push_back(&record);
        std::size_t index = 0;
        for (const auto &record : method.get("records").array) {
            if (record.get("operand_kind").text == "exception")
                continue;
            check(index < opcodes.size(), "Original instruction omitted by metadata reader");
            compare_record(*opcodes[index++], record);
        }
        check(index == opcodes.size(), "Metadata reader invented original instructions");
        if (genuine)
            for (const auto &captured : captures.get("frames").array)
                if (captured.get("function").text == name) {
                    check(continuation_metadata.methods.contains(name),
                          "Original captured continuation metadata omitted: " + name);
                    compare_continuation(actual, continuation_metadata.methods.at(name), method, captured);
                    ++continuations;
                }
        records += index;
    }
    for (std::size_t length = 0; length < bytes.size(); ++length)
        check(!decode_yengine_object_code(bytes.first(length)), "Truncated portable object was accepted");
    auto invalid = std::vector<std::uint8_t>(bytes.begin(), bytes.end());
    invalid.push_back(0);
    check(!decode_yengine_object_code(invalid), "Trailing portable object bytes were ignored");
    invalid.assign(bytes.begin(), bytes.end());
    invalid[10] ^= 1;
    check(!decode_yengine_object_code(invalid), "Wrong original compiled version was accepted");
    return records;
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Original managed fixture directory required");
        std::size_t fixtures = 0, records = 0, continuations = 0;
        for (const auto &directory : std::filesystem::directory_iterator(argv[1]))
            if (directory.is_directory() && std::filesystem::is_regular_file(directory.path() / "original.yobj")) {
                records += fixture(directory.path(), continuations);
                ++fixtures;
            }
        check(fixtures >= 5 && records > 100 && continuations >= 10, "Original portable metadata coverage missing");
        std::cout << "PASS: " << fixtures << " original object files, " << records
                  << " compiler callbacks, signatures, locals, labels, operands and source positions; " << continuations
                  << " genuine managed continuation contracts\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
