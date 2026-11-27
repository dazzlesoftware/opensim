// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "lsl_array_conversion_support.hpp"
#include <algorithm>

namespace opensim::script {
namespace {
using namespace array_conversion_detail;

void negative(std::string_view parameter, std::int32_t value, bool length) {
#ifdef _WIN32
    constexpr auto newline = "\r\n";
#else
    constexpr auto newline = "\n";
#endif
    const auto name = std::string(parameter), number = std::to_string(value);
    throw std::runtime_error("ArgumentOutOfRangeException: " + name + " ('" + number + "') " +
                             (length ? "must be a non-negative value." : "must be greater than or equal to '0'.") +
                             " (Parameter '" + name + "')" + newline + "Actual value was " + number + ".");
}

[[noreturn]] void unbox_failure() {
    throw std::runtime_error("InvalidCastException: At least one element in the source array could not be cast down "
                             "to the destination array type.");
}

LslValue convert(const LslValue &cell, LslFixedArrayKind source, LslFixedArrayKind destination) {
    if (source == destination)
        return cell;
    if (destination == LslFixedArrayKind::Object)
        return make_lsl_object_box(cell);
    if (source == LslFixedArrayKind::Character) {
        const auto value = std::get<LslCharacter>(cell).value;
        if (destination == LslFixedArrayKind::Integer)
            return static_cast<std::int32_t>(value);
        if (destination == LslFixedArrayKind::Float)
            return static_cast<double>(value);
    }
    if (source == LslFixedArrayKind::Integer && destination == LslFixedArrayKind::Float)
        return static_cast<double>(std::get<std::int32_t>(cell));
    if (source == LslFixedArrayKind::Object) {
        const auto *stored = &payload(cell);
        // CLR unboxing is exact: SDK scalars and raw integers are distinct,
        // and boxed Int32 cannot widen while being unboxed to Double.
        if (destination == LslFixedArrayKind::Integer && std::holds_alternative<std::int32_t>(*stored))
            return *stored;
        if (destination == LslFixedArrayKind::Float && std::holds_alternative<double>(*stored))
            return *stored;
        if (destination == LslFixedArrayKind::Character && std::holds_alternative<LslCharacter>(*stored))
            return *stored;
        unbox_failure();
    }
    throw std::logic_error("Unsupported CLR array conversion reached copy loop");
}

bool compatible(LslFixedArrayKind source, LslFixedArrayKind destination) {
    return source == destination || source == LslFixedArrayKind::Object || destination == LslFixedArrayKind::Object ||
           (source == LslFixedArrayKind::Character &&
            (destination == LslFixedArrayKind::Integer || destination == LslFixedArrayKind::Float)) ||
           (source == LslFixedArrayKind::Integer && destination == LslFixedArrayKind::Float);
}
} // namespace

void lsl_script_array_copy(const LslValue &source_value, std::int32_t source_start, const LslValue &destination_value,
                           std::int32_t destination_start, std::int32_t count) {
    std::lock_guard lock(lsl_reference_graph_mutex());
    // Both casts precede either dereference in the original helper.
    const auto source = class_reference(source_value);
    const auto destination = class_reference(destination_value);
    if (!source || !destination || !source->descriptor || !destination->descriptor)
        null_reference();
    const auto &source_name = source->descriptor->canonical_name;
    const auto &destination_name = destination->descriptor->canonical_name;
    const auto source_prefix = source_name.find('['), destination_prefix = destination_name.find('[');
    if (source_prefix == std::string::npos || destination_prefix == std::string::npos)
        throw std::runtime_error("InvalidCastException: non-array passed: " + source_name + " and/or " +
                                 destination_name);
    if (source_prefix != destination_prefix ||
        source_name.compare(0, source_prefix, destination_name, 0, destination_prefix) != 0 ||
        std::count(source_name.begin() + source_prefix, source_name.end(), ']') !=
            std::count(destination_name.begin() + destination_prefix, destination_name.end(), ']'))
        throw std::runtime_error("ArrayTypeMismatchException: " + source_name + " vs " + destination_name);
    const auto source_array = backing(*source);
    const auto destination_array = backing(*destination);
    if (!source_array)
        throw std::runtime_error("ArgumentNullException: Value cannot be null. (Parameter 'sourceArray')");
    if (!destination_array)
        throw std::runtime_error("ArgumentNullException: Value cannot be null. (Parameter 'destinationArray')");
    if (count < 0)
        negative("length", count, true);
    if (source_start < 0)
        negative("sourceIndex", source_start, false);
    if (destination_start < 0)
        negative("destinationIndex", destination_start, false);
    const auto source_size = lsl_fixed_array_size(source_array),
               destination_size = lsl_fixed_array_size(destination_array);
    const auto source_end = static_cast<std::uint64_t>(source_start) + static_cast<std::uint32_t>(count);
    const auto destination_end = static_cast<std::uint64_t>(destination_start) + static_cast<std::uint32_t>(count);
    if (source_end > source_size)
        throw std::runtime_error("ArgumentException: Source array was not long enough. Check the source index, length, "
                                 "and the array's lower bounds. (Parameter 'sourceArray')");
    if (destination_end > destination_size)
        throw std::runtime_error(
            "ArgumentException: Destination array was not long enough. Check the destination index, "
            "length, and the array's lower bounds. (Parameter 'destinationArray')");
    if (!compatible(source_array->kind, destination_array->kind))
        throw std::runtime_error(
            "ArrayTypeMismatchException: Source array type cannot be assigned to destination array type.");
    const bool backward = source_array == destination_array && destination_start > source_start &&
                          static_cast<std::uint64_t>(destination_start) < source_end;
    for (std::int64_t step = 0; step < count; ++step) {
        const auto offset = backward ? static_cast<std::int64_t>(count) - 1 - step : step;
        const auto cell = lsl_fixed_array_get(source_array, static_cast<std::size_t>(source_start + offset));
        // Object-to-value copies can fault after earlier cells were written.
        lsl_fixed_array_set(destination_array, static_cast<std::size_t>(destination_start + offset),
                            convert(cell, source_array->kind, destination_array->kind));
    }
}
} // namespace opensim::script
