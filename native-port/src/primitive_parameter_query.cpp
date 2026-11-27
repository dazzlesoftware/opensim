// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/primitive_parameter_query.hpp"
#include "opensim/framework/invariant_number_parse.hpp"
#include "opensim/framework/text.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace opensim::script {
namespace {
class InvalidRuleType final : public std::invalid_argument {
  public:
    explicit InvalidRuleType(const std::string &type)
        : std::invalid_argument("LSLInteger expected but " + type + " given") {}
};

LslList tail(const LslList &rules) {
    LslList remaining;
    remaining.items.assign(rules.items.begin() + 1, rules.items.end());
    for (auto &value : remaining.items)
        if (!std::holds_alternative<LslBoxedKey>(value))
            value = unbox_lsl_value(value);
    return remaining;
}
} // namespace

std::int32_t primitive_rule_integer(const LslValue &input) {
    if (std::holds_alternative<LslBoxedKey>(input))
        throw InvalidRuleType("key");
    const auto value = unbox_lsl_value(input);
    if (const auto integer = std::get_if<std::int32_t>(&value))
        return *integer;
    if (const auto real = std::get_if<double>(&value)) {
        const auto truncated = std::trunc(*real);
        if (!std::isfinite(truncated) || truncated < -2147483648. || truncated >= 2147483648.)
            return std::numeric_limits<std::int32_t>::min();
        return static_cast<std::int32_t>(truncated);
    }
    if (const auto text = std::get_if<std::string>(&value))
        return framework::invariant_convert_to_int32(framework::utf16(*text));
    if (std::holds_alternative<framework::Vector3d>(value))
        throw InvalidRuleType("Vector3");
    if (std::holds_alternative<LslRotation>(value))
        throw InvalidRuleType("Quaternion");
    throw InvalidRuleType("list");
}

LslList query_primitive_parameters(const PrimitiveParameterQueryBackend &backend, PrimitiveQueryApi api,
                                   framework::Uuid target, std::int32_t link, const LslList &input) {
    if (!backend.select || !backend.read)
        throw std::invalid_argument("Primitive parameter query backend is incomplete");
    LslList result;
    if (api == PrimitiveQueryApi::OtherObject &&
        (!backend.other_object_permitted || !backend.other_object_permitted(target)))
        return result;

    auto rules = input;
    for (auto &value : rules.items)
        if (!std::holds_alternative<LslBoxedKey>(value))
            value = unbox_lsl_value(value);
    LslList remaining;
    if (api == PrimitiveQueryApi::Primitive || api == PrimitiveQueryApi::OtherObject) {
        remaining = backend.read(target, rules, result);
        while (remaining.items.size() > 1) {
            try {
                link = primitive_rule_integer(remaining.items.front());
            } catch (const InvalidRuleType &) {
                if (api != PrimitiveQueryApi::OtherObject)
                    throw;
                if (backend.error)
                    backend.error("Error PRIM_LINK_TARGET: parameter must be integer");
                return result;
            }
            rules = tail(remaining);
            const auto selection = api == PrimitiveQueryApi::Primitive ? PrimitiveQuerySelection::Parts
                                                                       : PrimitiveQuerySelection::Entities;
            const auto entities = backend.select(target, link, selection);
            if (entities.empty())
                break;
            for (const auto entity : entities)
                remaining = backend.read(entity, rules, result);
        }
        return result;
    }

    if (api == PrimitiveQueryApi::OsslLink) {
        for (const auto entity : backend.select(target, link, PrimitiveQuerySelection::Parts))
            remaining = backend.read(entity, rules, result);
        while (remaining.items.size() > 1) {
            link = primitive_rule_integer(remaining.items.front());
            const auto entities = backend.select(target, link, PrimitiveQuerySelection::Parts);
            if (entities.empty())
                break;
            rules = tail(remaining);
            for (const auto entity : entities)
                remaining = backend.read(entity, rules, result);
        }
        return result;
    }

    while (!rules.items.empty()) {
        const auto entities = backend.select(target, link, PrimitiveQuerySelection::PartsAndAvatars);
        remaining.items.clear();
        for (const auto entity : entities)
            remaining = backend.read(entity, rules, result);
        if (remaining.items.empty())
            break;
        link = primitive_rule_integer(remaining.items.front());
        rules = tail(remaining);
    }
    return result;
}
} // namespace opensim::script
