// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "lsl_array_conversion_support.hpp"
#include <bit>

namespace opensim::script {
namespace {
LslValue convert_list_cell(const LslValue &original, std::int32_t output_index) {
    using namespace array_conversion_detail;
    if (is_null(original))
        throw std::runtime_error("NullReferenceException: null element " + std::to_string(output_index));
    // Inspect the actual CLR payload without unwrapping SDK structs. Original
    // Array2List accepts raw numeric/String values but rejects preboxed SDK
    // Integer/Float/String/key values, even though lists normally contain them.
    const auto &value = payload(original);
    if (const auto *number = std::get_if<double>(&value))
        return make_lsl_object_box(LslBoxedFloat{*number});
    if (const auto *number = std::get_if<std::int32_t>(&value))
        return make_lsl_object_box(LslBoxedInteger{*number});
    if (std::holds_alternative<framework::Vector3d>(value) || std::holds_alternative<LslRotation>(value))
        return original; // Preserve the existing SDK Vector/Rotation box reference.
    if (std::holds_alternative<std::string>(value) || std::holds_alternative<LslUtf16String>(value) ||
        std::holds_alternative<std::shared_ptr<const LslStringReference>>(value)) {
        const auto units = lsl_string_code_units(value);
        if (!units)
            throw std::invalid_argument("Invalid raw String carrier in Array2List");
        const auto *existing = std::get_if<std::shared_ptr<const LslStringReference>>(&value);
        const auto reference = existing ? *existing
                                        : std::get<std::shared_ptr<const LslStringReference>>(
                                              make_lsl_reference_string(*units, units->empty()));
        // New SDK struct/value box, same underlying CLR String reference.
        return make_lsl_object_box(box_lsl_string(*units), reference);
    }
    throw std::runtime_error("InvalidCastException: invalid element " + std::to_string(output_index) + " type " +
                             short_type_name(original));
}
} // namespace

LslValue lsl_script_array_to_list(const LslValue &source, std::int32_t start, std::int32_t count) {
    std::lock_guard lock(lsl_reference_graph_mutex());
    const auto &instance = array_conversion_detail::wrapper(source);
    if (!instance.descriptor || instance.descriptor->array_rank == 0)
        throw std::runtime_error("InvalidCastException: only do arrays not " +
                                 (instance.descriptor ? instance.descriptor->canonical_name : std::string{}));
    // Original casts obj to System.Array before allocating output[count]. A
    // wrong backing faults even for count0; null backing is not dereferenced
    // until GetValue, so count0 succeeds and count<0 faults during allocation.
    const auto array = array_conversion_detail::backing(instance);
    if (count < 0)
        throw std::runtime_error("OverflowException: Arithmetic operation resulted in an overflow.");
    auto result = std::make_shared<LslList>();
    result->items.resize(static_cast<std::size_t>(count), LslNullObject{});
    for (std::int32_t i = 0; i < count; ++i) {
        if (!array)
            array_conversion_detail::null_reference();
        const auto index =
            std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(start) + static_cast<std::uint32_t>(i));
        if (index < 0 || static_cast<std::size_t>(index) >= lsl_fixed_array_size(array))
            throw std::runtime_error("IndexOutOfRangeException: Index was outside the bounds of the array.");
        result->items[static_cast<std::size_t>(i)] =
            convert_list_cell(lsl_fixed_array_get(array, static_cast<std::size_t>(index)), i);
    }
    return std::shared_ptr<const LslList>(std::move(result));
}
} // namespace opensim::script
