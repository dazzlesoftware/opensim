// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_script_class.hpp"
#include <iostream>
#include <stdexcept>

using namespace opensim::script;

namespace {
using Class = std::shared_ptr<const LslScriptClass>;
using Array = std::shared_ptr<const LslFixedArray>;

void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

LslCheckpoint base() {
    LslCheckpoint result;
    result.program_hash = "class-checkpoint";
    result.state = "default";
    return result;
}

void round_trip() {
    auto descriptor = make_lsl_script_array_descriptor("source-hash", "integer", 1);
    const std::array<std::int32_t, 1> dimensions{3};
    auto value = std::get<Class>(make_lsl_script_array(descriptor, dimensions));
    value->arrays_heap_usage = -17;
    value->identity_hash = 12345;
    auto backing = std::get<Array>(value->instance_arrays[5][0]);
    lsl_fixed_array_set(backing, 2, std::int32_t{11});
    auto checkpoint = base();
    checkpoint.globals = {value, backing, value};
    checkpoint.initialized_globals = 3;
    const auto wire = encode_lsl_checkpoint(checkpoint);
    check(wire[3] == 21, "Class did not select schema21");
    auto decoded = decode_lsl_checkpoint(wire);
    check(bool(decoded), "Class checkpoint rejected");
    auto restored = std::get<Class>(decoded->globals[0]);
    check(restored == std::get<Class>(decoded->globals[2]), "Class alias split");
    check(std::get<Array>(restored->instance_arrays[5][0]) == std::get<Array>(decoded->globals[1]),
          "Class backing alias split");
    check(restored->arrays_heap_usage == -17 && restored->identity_hash == 12345, "Internal class ledger/hash lost");
    check(same_lsl_script_class_type(*restored->descriptor, *descriptor), "Class descriptor changed");
    check(encode_lsl_checkpoint(*decoded) == wire, "Class internal encoding unstable");
    auto persistent = decode_lsl_checkpoint(encode_persistent_lsl_checkpoint(checkpoint));
    check(bool(persistent), "Persistent class checkpoint rejected");
    auto external = std::get<Class>(persistent->globals[0]);
    check(!external->identity_hash && external->arrays_heap_usage == 16, "Persistent class ledger/hash incorrect");
    check(value->arrays_heap_usage == -17 && value->identity_hash == 12345, "Writer mutated class");
    for (std::size_t length = 0; length < wire.size(); ++length)
        check(!decode_lsl_checkpoint(std::span(wire).first(length)), "Truncated class checkpoint accepted");
    auto old = wire;
    old[3] = 20;
    check(!decode_lsl_checkpoint(old), "Schema20 accepted class tag");
}

void invalid_descriptors() {
    auto descriptor = make_lsl_script_array_descriptor("source-hash", "integer", 1);
    auto value = std::get<Class>(make_lsl_script_class(descriptor));
    auto checkpoint = base();
    checkpoint.globals = {value};
    const auto rejected = [&] {
        try {
            (void)encode_lsl_checkpoint(checkpoint);
        } catch (const std::invalid_argument &) {
            return true;
        }
        return false;
    };
    value->instance_arrays[3][0] = LslBoxedInteger{3};
    check(rejected(), "Boxed dimension field accepted");
    value->instance_arrays[3][0] = std::int32_t{3};
    value->instance_arrays[3].push_back(std::int32_t{0});
    check(rejected(), "Mismatched instance lane size accepted");
    auto malformed = std::make_shared<LslScriptClassDescriptor>(*descriptor);
    malformed->canonical_name = "float[]";
    auto instance = std::make_shared<LslScriptClass>();
    instance->descriptor = malformed;
    checkpoint.globals = {Class(instance)};
    check(rejected(), "Noncanonical descriptor accepted");
}

void ordinary_string_heap() {
    auto descriptor = std::make_shared<LslScriptClassDescriptor>();
    descriptor->source_hash = "source";
    descriptor->canonical_name = "Example";
    descriptor->instance_sizes[7] = 3;
    descriptor->instance_sizes[5] = 1;
    auto value = std::get<Class>(make_lsl_script_class(descriptor));
    value->instance_arrays[7] = {std::string{" \t"}, LslNullString{}, std::string{"x"}};
    value->instance_arrays[5][0] = std::string{" \t"};
    auto checkpoint = base();
    checkpoint.globals = {value};
    const auto decoded = decode_lsl_checkpoint(encode_persistent_lsl_checkpoint(checkpoint));
    check(bool(decoded), "Ordinary string-lane class rejected");
    check(std::get<Class>(decoded->globals[0])->arrays_heap_usage == 6,
          "Typed string whitespace charged like Object string");
}

void cycle_and_empty() {
    auto descriptor = make_lsl_script_array_descriptor("source-hash", "integer", 1);
    auto value = std::get<Class>(make_lsl_script_class(descriptor));
    auto objects = std::get<Array>(make_lsl_fixed_array(LslFixedArrayKind::Object, 1));
    value->instance_arrays[5][0] = objects;
    lsl_fixed_array_set(objects, 0, value);
    auto checkpoint = base();
    checkpoint.globals = {value};
    auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(checkpoint));
    check(bool(decoded), "Class/array cycle rejected");
    auto restored = std::get<Class>(decoded->globals[0]);
    auto backing = std::get<Array>(restored->instance_arrays[5][0]);
    check(std::get<Class>(lsl_fixed_array_get(backing, 0)) == restored, "Class cycle split");
    const std::array<std::int32_t, 1> dimensions{0};
    checkpoint.globals = {make_lsl_script_array(descriptor, dimensions)};
    auto external = decode_lsl_checkpoint(encode_persistent_lsl_checkpoint(checkpoint));
    check(bool(external), "Empty class rejected");
    restored = std::get<Class>(external->globals[0]);
    check(std::holds_alternative<LslNullObject>(restored->instance_arrays[5][0]), "Empty backing not normalized");
    check(restored->arrays_heap_usage == 4, "Empty wrapper ledger changed");
}
} // namespace

int main() {
    try {
        round_trip();
        cycle_and_empty();
        invalid_descriptors();
        ordinary_string_heap();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
