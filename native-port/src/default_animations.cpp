// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/default_animations.hpp"
#include <pugixml.hpp>
#include <fstream>
namespace opensim::scene {
DefaultAnimations::DefaultAnimations(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot open default animation catalog");
    std::string bytes(std::istreambuf_iterator<char>(input), {});
    pugi::xml_document document;
    if (!document.load_buffer(bytes.data(), bytes.size()))
        throw std::runtime_error("Invalid default animation XML");
    auto root = document.document_element();
    if (!root)
        throw std::runtime_error("Missing animation document element");
    for (auto node : root.children()) {
        if (!node.attribute("name"))
            continue;
        if (!node.attribute("state"))
            throw std::runtime_error("Missing animation state attribute");
        std::string key = node.attribute("name").value();
        auto id = framework::parse_sdk_uuid(node.child_value());
        if (!by_name.emplace(key, id).second || !by_id.emplace(id, key).second)
            throw std::runtime_error("Duplicate default animation key");
        std::string state = node.attribute("state").value();
        if (!state.empty())
            states.emplace(id, std::move(state));
    }
}
framework::Uuid DefaultAnimations::resolve(std::string key) const {
    // Scene request threads use invariant culture; bundled catalog keys are ASCII.
    for (auto& c : key)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    auto found = by_name.find(key);
    if (found != by_name.end())
        return found->second;
    framework::Uuid id;
    if (framework::try_parse_sdk_uuid(key, id) && by_id.contains(id))
        return id;
    return {};
}
std::string DefaultAnimations::name(framework::Uuid id) const {
    auto found = by_id.find(id);
    return found == by_id.end() ? id.str() : found->second;
}
}
