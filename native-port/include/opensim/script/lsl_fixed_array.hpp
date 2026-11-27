// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/script/lsl_runtime.hpp"

namespace opensim::script {
enum class LslFixedArrayKind : std::uint8_t { Character, Float, Integer, Object };

struct LslFixedArray {
    LslFixedArrayKind kind;
    mutable std::variant<std::vector<char16_t>, std::vector<double>, std::vector<std::int32_t>, std::vector<LslValue>>
        cells;
    mutable std::optional<std::uint32_t> identity_hash;
};

LslValue make_lsl_fixed_array(LslFixedArrayKind kind, std::size_t length);
std::size_t lsl_fixed_array_size(const std::shared_ptr<const LslFixedArray> &array);
LslValue lsl_fixed_array_get(const std::shared_ptr<const LslFixedArray> &array, std::size_t index);
void lsl_fixed_array_set(const std::shared_ptr<const LslFixedArray> &array, std::size_t index, const LslValue &value);
// Call at quiescent VM boundaries. External shared_ptr roots retain their entire graph.
void collect_lsl_fixed_arrays() noexcept;
} // namespace opensim::script
