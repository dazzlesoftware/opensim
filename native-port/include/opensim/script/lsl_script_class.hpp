// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/script/lsl_fixed_array.hpp"
#include <functional>
#include <mutex>

namespace opensim::script {
struct LslScriptClassDescriptor {
    std::string source_hash;
    std::string canonical_name;
    std::string element_type;
    std::uint32_t array_rank = 0;
    std::int32_t class_index = -1;
    std::array<std::uint32_t, 11> instance_sizes{};
    mutable framework::Uuid type_name_owner;
    mutable std::shared_ptr<const LslStringReference> type_name_reference;
};

// A class reference is distinct from every Object value in its instance fields.
struct LslScriptClass {
    std::shared_ptr<const LslScriptClassDescriptor> descriptor;
    mutable std::array<std::vector<LslValue>, 11> instance_arrays;
    mutable std::int32_t arrays_heap_usage = 0;
    mutable std::optional<std::uint32_t> identity_hash;
};

// UpdateArraysHeapUse checks the requested delta against global usage; class
// instance storage has its own ledger and is not added to xmrHeapUsed.
struct LslScriptClassHeapHooks {
    std::function<std::int32_t(const LslValue &)> size_of;
    std::function<void(std::int32_t)> check_delta;
};

std::shared_ptr<const LslScriptClassDescriptor>
make_lsl_script_array_descriptor(std::string source_hash, std::string element_type, std::uint32_t rank);
bool supported_lsl_primitive_array_descriptor(std::string_view name, std::string_view element, std::uint32_t rank);
std::shared_ptr<const LslStringReference>
lsl_script_class_type_name(const std::shared_ptr<const LslScriptClassDescriptor> &descriptor);
bool same_lsl_script_class_type(const LslScriptClassDescriptor &, const LslScriptClassDescriptor &);
LslValue make_lsl_script_class(std::shared_ptr<const LslScriptClassDescriptor>);
LslValue make_lsl_script_array(std::shared_ptr<const LslScriptClassDescriptor>, std::span<const std::int32_t>,
                               const LslScriptClassHeapHooks & = {});
std::int32_t lsl_script_class_object_size(const LslValue &);
LslValue lsl_script_class_field(const std::shared_ptr<const LslScriptClass> &, std::size_t lane, std::size_t index);
void lsl_script_class_store_field(const std::shared_ptr<const LslScriptClass> &, std::size_t lane, std::size_t index,
                                  LslValue, const LslScriptClassHeapHooks & = {});
std::int32_t lsl_script_array_length(const std::shared_ptr<const LslScriptClass> &);
std::int32_t lsl_script_array_length(const std::shared_ptr<const LslScriptClass> &, std::int32_t dimension);
std::int32_t lsl_script_array_index(const std::shared_ptr<const LslScriptClass> &, std::span<const std::int32_t>);
LslValue lsl_script_array_get(const std::shared_ptr<const LslScriptClass> &, std::span<const std::int32_t>);
void lsl_script_array_set(const std::shared_ptr<const LslScriptClass> &, std::span<const std::int32_t>,
                          const LslValue &);
// Original generated-array character conversion helpers preserve UTF16 units.
LslValue lsl_chars_to_string(const LslValue &, std::int32_t start, std::int32_t count);
void lsl_string_to_chars(const LslValue &source, std::int32_t source_start, const LslValue &destination,
                         std::int32_t destination_start, std::int32_t count);
void lsl_script_array_copy(const LslValue &source, std::int32_t source_start, const LslValue &destination,
                           std::int32_t destination_start, std::int32_t count);
LslValue lsl_script_array_to_list(const LslValue &source, std::int32_t start, std::int32_t count);
void lsl_list_to_script_array(const LslValue &source, std::int32_t source_start, const LslValue &destination,
                              std::int32_t destination_start, std::int32_t count);

// Shared with fixed-array ownership so mixed class/array/List cycles are collected together.
std::recursive_mutex &lsl_reference_graph_mutex();
void register_lsl_script_class(const std::shared_ptr<const LslScriptClass> &);
} // namespace opensim::script
