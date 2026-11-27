// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/script/lsl_fixed_array.hpp"
#include "opensim/script/lsl_script_class.hpp"
#include <stdexcept>

namespace opensim::script::array_conversion_detail {
std::string object_type_name(const LslValue &);

inline const LslValue &payload(const LslValue &value) {
    if (const auto *box = std::get_if<std::shared_ptr<const LslObjectBox>>(&value); box && *box)
        return (*box)->value;
    return value;
}

inline bool is_null(const LslValue &value) {
    if (std::holds_alternative<LslNullObject>(value) || std::holds_alternative<LslNullString>(value) ||
        std::holds_alternative<LslNullList>(value))
        return true;
    return std::visit(
        [](const auto &item) {
            if constexpr (requires { item.get(); })
                return !item;
            return false;
        },
        value);
}

[[noreturn]] inline void null_reference() {
    throw std::runtime_error("NullReferenceException: Object reference not set to an instance of an object.");
}

inline std::shared_ptr<const LslScriptClass> class_reference(const LslValue &value) {
    if (is_null(value))
        return {};
    if (const auto *instance = std::get_if<std::shared_ptr<const LslScriptClass>>(&value))
        return *instance;
    throw std::runtime_error("InvalidCastException: Unable to cast object of type '" + object_type_name(value) +
                             "' to type 'OpenSim.Region.ScriptEngine.Yengine.XMRSDTypeClObj'.");
}

inline const LslScriptClass &wrapper(const LslValue &value) {
    const auto instance = class_reference(value);
    if (!instance)
        null_reference();
    return *instance;
}

inline std::shared_ptr<const LslFixedArray> backing(const LslScriptClass &instance) {
    const auto &value = instance.instance_arrays[5].at(0);
    if (is_null(value))
        return {};
    if (const auto *array = std::get_if<std::shared_ptr<const LslFixedArray>>(&value))
        return *array;
    throw std::runtime_error("InvalidCastException: Unable to cast object of type '" + object_type_name(value) +
                             "' to type 'System.Array'.");
}

inline std::string short_type_name(const LslValue &value) {
    auto name = object_type_name(value);
    const auto separator = name.find_last_of(".+");
    return separator == std::string::npos ? name : name.substr(separator + 1);
}
} // namespace opensim::script::array_conversion_detail
