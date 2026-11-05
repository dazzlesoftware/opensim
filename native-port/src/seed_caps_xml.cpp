// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/seed_caps.hpp"
#include "opensim/network/sdk_llsd_xml.hpp"
#include "opensim/network/osd_value.hpp"
#include <stdexcept>
namespace opensim::server {
namespace {
SdkLlsdNode node(const network::Value& input) {
    using K = network::Value::Kind;
    SdkLlsdNode result;
    if (input.kind == K::NullReference)
        return result;
    const auto& value = input.view();
    result.value.emplace();
    result.value->kind = value.kind;
    if (value.kind == K::Map) {
        result.tag = "map";
        result.children.reserve(value.map.size());
        result.keys.reserve(value.map.size());
        for (const auto& key : value.ordered_keys()) {
            result.keys.push_back(key);
            result.children.push_back(node(value.get(key)));
        }
    } else if (value.kind == K::Array) {
        result.tag = "array";
        result.children.reserve(value.array.size());
        for (const auto& child : value.array)
            result.children.push_back(node(child));
    } else {
        *result.value = value;
        switch (value.kind) {
        case K::Undefined:
            result.tag = "undef";
            break;
        case K::String:
            result.tag = "string";
            break;
        case K::Integer:
            result.tag = "integer";
            break;
        case K::Boolean:
            result.tag = "boolean";
            break;
        case K::Real:
            result.tag = "real";
            break;
        case K::Uuid:
            result.tag = "uuid";
            break;
        case K::Date:
            result.tag = "date";
            break;
        case K::Uri:
            result.tag = "uri";
            break;
        case K::Binary:
            result.tag = "binary";
            break;
        default:
            break;
        }
    }
    return result;
}
}
SdkLlsdNode deserialize_sdk_llsd_node(std::string_view xml) {
    return node(network::deserialize_sdk_llsd_xml(xml));
}
std::optional<services::Caps::Requested> deserialize_seed_caps(std::string_view xml) {
    auto root = deserialize_sdk_llsd_node(xml);
    if (!root.value)
        return std::nullopt;
    if (root.value->kind != network::Value::Kind::Array)
        throw std::invalid_argument("Expected OSDArray");
    services::Caps::Requested names;
    names.reserve(root.children.size());
    for (const auto& item : root.children)
        names.push_back(item.value ? std::optional<std::string>(network::osd_string(*item.value))
                                   : std::nullopt);
    return names;
}
}
