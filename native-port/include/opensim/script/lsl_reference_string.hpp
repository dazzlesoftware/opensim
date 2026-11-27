#pragma once
#include "opensim/script/lsl_runtime.hpp"

namespace opensim::script {
// Creates an unbound cache reference. A descriptor owns the live cached value;
// a decoded internal checkpoint binds it to its destination object code.
LslValue make_lsl_class_type_name_string(const LslClassTypeNameOrigin &origin);
// Weak owner-scoped registry: identical source loaded twice never shares a key.
// Used by live descriptors and VM restoration, not by context-free decoding.
std::shared_ptr<const LslStringReference>
lsl_class_type_name_cache(const LslClassTypeNameOrigin &origin,
                          std::shared_ptr<const LslStringReference> preferred = {});
std::optional<LslClassTypeNameOrigin>
lsl_reference_string_class_type_name(const std::shared_ptr<const LslStringReference> &value);

LslValue make_lsl_cached_integer_string(std::int32_t value);
std::optional<std::int32_t> lsl_reference_string_cached_integer(const std::shared_ptr<const LslStringReference> &value);
LslValue make_lsl_cached_array_type_string(std::uint8_t kind);
std::optional<std::uint8_t>
lsl_reference_string_cached_array_type(const std::shared_ptr<const LslStringReference> &value);
} // namespace opensim::script
