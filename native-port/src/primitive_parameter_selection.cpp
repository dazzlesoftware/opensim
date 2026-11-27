// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/primitive_parameter_selection.hpp"
#include "opensim/scene/primitive_parameter_values.hpp"
#include <algorithm>

namespace opensim::scene {
namespace {
using framework::Uuid;
using script::PrimitiveQuerySelection;

constexpr std::int32_t link_set = -1, link_all_others = -2, link_all_children = -3, link_this = -4;

std::vector<Uuid> select_parts(const World &world, Uuid host, Uuid root, std::int32_t link) {
    if (link == link_this)
        return {host};
    if (link == link_set || link == link_all_others || link == link_all_children) {
        auto parts = world.link_children_in_membership_order(root);
        parts.insert(parts.begin(), root);
        if (link == link_all_others)
            std::erase(parts, host);
        else if (link == link_all_children)
            std::erase(parts, root);
        return parts;
    }
    const auto target = world.link_key(host, link);
    return target.is_zero() ? std::vector<Uuid>{} : std::vector<Uuid>{target};
}
} // namespace

std::vector<framework::Uuid> select_primitive_parameter_entities(const World &world,
                                                                 const PrimitiveSittingAvatarProvider &sitting_avatars,
                                                                 framework::Uuid host, std::int32_t link,
                                                                 script::PrimitiveQuerySelection selection) {
    using script::PrimitiveQuerySelection;
    const auto part = world.object(host);
    if (selection == PrimitiveQuerySelection::Entities) {
        // Unlike GetLinkParts, GetLinkEntities returns LINK_THIS before touching
        // ParentGroup, and rejects other negative default selectors first.
        if (link == link_this)
            return {part ? host : Uuid{}};
        if (link < link_this)
            return {};
        if (!part)
            throw PrimitiveParameterValueError("Object reference not set to an instance of an object.",
                                               "System.NullReferenceException");
    } else if (!part) {
        return {};
    }
    const auto root = world.link_key(host, 1);
    if (root.is_zero())
        return {};
    auto result = select_parts(world, host, root, link);
    if (selection == PrimitiveQuerySelection::Parts)
        return result;

    std::size_t part_count = 0;
    if (selection == PrimitiveQuerySelection::Entities && link != link_all_children) {
        if (link < 0)
            return result;
        part_count = world.link_children_in_membership_order(root).size() + 1;
        if (static_cast<std::size_t>(link) <= part_count)
            return result;
    }

    // GetLinkAvatars obtains the sitting list even for LINK_ROOT, LINK_THIS and
    // invalid selectors. Keep that lookup order for the PartsAndAvatars path.
    const auto avatars = sitting_avatars ? sitting_avatars(root) : std::vector<Uuid>{};
    if (link == link_all_children ||
        (selection == PrimitiveQuerySelection::PartsAndAvatars && (link == link_set || link == link_all_others))) {
        result.insert(result.end(), avatars.begin(), avatars.end());
    } else if (link >= 0) {
        if (selection == PrimitiveQuerySelection::PartsAndAvatars)
            part_count = world.link_children_in_membership_order(root).size() + 1;
        if (static_cast<std::size_t>(link) > part_count) {
            const auto index = static_cast<std::size_t>(link) - part_count - 1;
            if (index < avatars.size())
                result.push_back(avatars[index]);
        }
    }
    return result;
}
} // namespace opensim::scene
