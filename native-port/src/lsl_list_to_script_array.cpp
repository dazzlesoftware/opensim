// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "lsl_array_conversion_support.hpp"
#include <bit>

namespace opensim::script {
namespace {
using namespace array_conversion_detail;

// xmrList2Array unwraps exactly these three SDK scalar structs. In particular,
// an SDK key is not an SDK String, even though both contain string fields.
LslValue unwrap_list_scalar(const LslValue &value) {
    if (is_null(value))
        return LslNullObject{};
    const auto *box = std::get_if<std::shared_ptr<const LslObjectBox>>(&value);
    if (box && !valid_lsl_object_box(**box))
        throw std::invalid_argument("Invalid CLR Object box");
    const auto &item = payload(value);
    if (const auto *integer = std::get_if<LslBoxedInteger>(&item))
        return make_lsl_object_box(integer->value);
    if (const auto *floating = std::get_if<LslBoxedFloat>(&item))
        return make_lsl_object_box(floating->value);
    if (std::holds_alternative<LslBoxedNullString>(item))
        return LslNullObject{};
    if (std::holds_alternative<LslBoxedString>(item) || std::holds_alternative<LslBoxedUtf16String>(item)) {
        if (box)
            return (*box)->string_field;
        const auto units = *lsl_string_code_units(item);
        return make_lsl_reference_string(units, units.empty());
    }
    return value;
}

[[noreturn]] void bounds_error() {
    throw std::runtime_error("IndexOutOfRangeException: Index was outside the bounds of the array.");
}

[[noreturn]] void incompatible_value(const LslValue &value) {
    const auto &item = payload(value);
    const bool primitive = std::holds_alternative<LslCharacter>(item) || std::holds_alternative<std::int32_t>(item) ||
                           std::holds_alternative<LslRawSingle>(item) || std::holds_alternative<double>(item) ||
                           std::holds_alternative<LslRawBoolean>(item);
    if (primitive)
        throw std::runtime_error("ArgumentException: Cannot widen from source type to target type either because the "
                                 "source type is a not a primitive type or the conversion cannot be accomplished.");
    throw std::runtime_error("InvalidCastException: Object cannot be stored in an array of this type.");
}

LslValue primitive_element(LslFixedArrayKind kind, const LslValue &value) {
    const auto &item = payload(value);
    if (is_null(value)) {
        if (kind == LslFixedArrayKind::Character)
            return LslCharacter{0};
        if (kind == LslFixedArrayKind::Integer)
            return std::int32_t{0};
        return 0.0;
    }
    if (kind == LslFixedArrayKind::Character) {
        if (const auto *character = std::get_if<LslCharacter>(&item))
            return *character;
    } else if (kind == LslFixedArrayKind::Integer) {
        if (const auto *integer = std::get_if<std::int32_t>(&item))
            return *integer;
        if (const auto *character = std::get_if<LslCharacter>(&item))
            return std::int32_t(character->value);
    } else if (kind == LslFixedArrayKind::Float) {
        if (const auto *floating = std::get_if<double>(&item))
            return *floating;
        if (const auto *single = std::get_if<LslRawSingle>(&item))
            return double(single->value);
        if (const auto *integer = std::get_if<std::int32_t>(&item))
            return double(*integer);
        if (const auto *character = std::get_if<LslCharacter>(&item))
            return double(character->value);
    }
    incompatible_value(value);
}

void set_element(const std::shared_ptr<const LslFixedArray> &target, std::int32_t index, const LslValue &value) {
    if (!target)
        null_reference();
    if (index < 0 || static_cast<std::size_t>(index) >= lsl_fixed_array_size(target))
        bounds_error();
    const auto converted = target->kind == LslFixedArrayKind::Object ? value : primitive_element(target->kind, value);
    lsl_fixed_array_set(target, static_cast<std::size_t>(index), converted);
}

std::int32_t add_index(std::int32_t start, std::int32_t offset) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(start) + static_cast<std::uint32_t>(offset));
}
} // namespace

void lsl_list_to_script_array(const LslValue &source, std::int32_t source_start, const LslValue &destination,
                              std::int32_t destination_start, std::int32_t count) {
    std::lock_guard lock(lsl_reference_graph_mutex());
    // Original ordering is wrapper -> List.Data -> backing Array cast -> loop.
    // Do these checks even when count is zero or negative.
    const auto &instance = array_conversion_detail::wrapper(destination);
    if (!instance.descriptor || instance.descriptor->element_type.empty())
        throw std::runtime_error("InvalidCastException: only do arrays not " +
                                 (instance.descriptor ? instance.descriptor->canonical_name : std::string{}));
    if (array_conversion_detail::is_null(source))
        array_conversion_detail::null_reference();
    const auto *list = std::get_if<std::shared_ptr<const LslList>>(&source);
    if (!list)
        throw std::runtime_error("InvalidCastException: Unable to cast object of type '" +
                                 array_conversion_detail::object_type_name(source) +
                                 "' to type 'OpenSim.Region.ScriptEngine.Shared.LSL_Types+list'.");
    const auto target = array_conversion_detail::backing(instance);
    for (std::int32_t offset = 0; offset < count; ++offset) {
        const auto from = add_index(source_start, offset);
        if (from < 0 || static_cast<std::size_t>(from) >= (*list)->items.size())
            bounds_error();
        const auto value = unwrap_list_scalar((*list)->items[static_cast<std::size_t>(from)]);
        set_element(target, add_index(destination_start, offset), value);
    }
}
} // namespace opensim::script
