// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/primitive_parameter_scene.hpp"
#include "opensim/framework/avatar_appearance.hpp"
#include "opensim/framework/sdk_quaternion.hpp"
#include "opensim/network/osd_value.hpp"
#include <algorithm>

namespace opensim::scene {
namespace {
using namespace framework;

Vector3 translated(Vector3 position, Vector3 offset, Quaternion rotation) {
    const auto rotated = sdk_rotate_vector(offset, rotation);
    return {position.x + rotated.x, position.y + rotated.y, position.z + rotated.z};
}

PrimitiveBaseShapeCore shape_for(const SceneObject &part) {
    auto result = PrimitiveBaseShapeCore::create([](const std::string &) {});
    const auto &source = part.shape;
    result.profile_curve(source.profile_curve);
    result.path_curve = source.path_curve;
    result.pcode = source.pcode;
    result.state = source.state;
    result.scale = part.scale;
    result.path_begin = source.path_begin;
    result.path_end = source.path_end;
    result.profile_begin = source.profile_begin;
    result.profile_end = source.profile_end;
    result.profile_hollow = source.profile_hollow;
    result.path_scale_x = source.path_scale_x;
    result.path_scale_y = source.path_scale_y;
    result.path_shear_x = source.path_shear_x;
    result.path_shear_y = source.path_shear_y;
    result.path_revolutions = source.path_revolutions;
    result.path_twist = source.path_twist;
    result.path_twist_begin = source.path_twist_begin;
    result.path_radius_offset = source.path_radius_offset;
    result.path_taper_x = source.path_taper_x;
    result.path_taper_y = source.path_taper_y;
    result.path_skew = source.path_skew;
    if (!part.extra_params.empty())
        result.read_extra_params(std::make_shared<ShapeByteArray>(part.extra_params));
    return result;
}
} // namespace

std::shared_ptr<PrimitiveParameterMaterial> primitive_parameter_material_from_cache(const network::Value &material) {
    using namespace network;
    auto result = std::make_shared<PrimitiveParameterMaterial>();
    const auto scaled = [&](const char *key) { return 0.0001f * static_cast<float>(osd_real(material.get(key))); };
    result->normal_map = osd_uuid(material.get("NormMap"));
    result->specular_map = osd_uuid(material.get("SpecMap"));
    result->normal_repeat_x = scaled("NormRepeatX");
    result->normal_repeat_y = scaled("NormRepeatY");
    result->normal_offset_x = scaled("NormOffsetX");
    result->normal_offset_y = scaled("NormOffsetY");
    result->normal_rotation = scaled("NormRotation");
    result->specular_repeat_x = scaled("SpecRepeatX");
    result->specular_repeat_y = scaled("SpecRepeatY");
    result->specular_offset_x = scaled("SpecOffsetX");
    result->specular_offset_y = scaled("SpecOffsetY");
    result->specular_rotation = scaled("SpecRotation");
    const auto &color = material.get("SpecColor");
    for (std::size_t i = 0; i < result->specular_color.size(); ++i)
        result->specular_color[i] = color.kind == Value::Kind::Array && color.array.size() == 4
                                        ? static_cast<std::uint8_t>(osd_unsigned(color.array[i]))
                                        : 0;
    result->specular_exponent = static_cast<std::uint8_t>(osd_unsigned(material.get("SpecExp")));
    result->environment_intensity = static_cast<std::uint8_t>(osd_unsigned(material.get("EnvIntensity")));
    result->alpha_mode = static_cast<std::uint8_t>(osd_unsigned(material.get("DiffuseAlphaMode")));
    result->alpha_cutoff = static_cast<std::uint8_t>(osd_unsigned(material.get("AlphaMaskCutoff")));
    return result;
}

PrimitiveParameterScene::PrimitiveParameterScene(const World &world, const ViewerRegion *viewers,
                                                 PrimitiveParameterSceneServices services)
    : world_(world), viewers_(viewers), services_(std::move(services)) {}

std::optional<ViewerAgent> PrimitiveParameterScene::wearer(const SceneObject &root) const {
    if (root.is_attachment && viewers_)
        for (const auto &avatar : viewers_->agents())
            if (avatar.local_id == root.parent_id)
                return avatar;
    return {};
}

framework::Vector3 PrimitiveParameterScene::avatar_position(const ViewerAgent &avatar) const {
    if (avatar.seat) {
        if (const auto root = world_.object(avatar.seat->parent_local))
            return translated(root->position, avatar.seat->offset, root->rotation);
        return avatar.seat->offset;
    }
    return avatar.position;
}

framework::Quaternion PrimitiveParameterScene::avatar_rotation(const ViewerAgent &avatar) const {
    if (avatar.seat)
        if (const auto root = world_.object(avatar.seat->parent_local))
            return sdk_multiply_quaternion(root->rotation, avatar.seat->rotation);
    return avatar.seat ? avatar.seat->rotation : avatar.rotation;
}

framework::Vector3 PrimitiveParameterScene::part_position(const SceneObject &part, const SceneObject &root) const {
    const auto owner = wearer(root);
    const auto position = owner ? avatar_position(*owner) : root.position;
    return part.id == root.id ? position : translated(position, part.position, root.rotation);
}

script::LslRotation PrimitiveParameterScene::part_rotation(const SceneObject &part, const SceneObject &root) const {
    auto rotation = part.id == root.id ? root.rotation : sdk_multiply_quaternion(root.rotation, part.rotation);
    if (root.is_attachment && root.shape.state != 0)
        if (const auto owner = wearer(root)) {
            const auto owner_rotation = owner->mouselook_rotation.value_or(
                part.id == root.id ? avatar_rotation(*owner) : (owner->seat ? owner->seat->rotation : owner->rotation));
            rotation = part.id == root.id ? owner_rotation : sdk_multiply_quaternion(owner_rotation, rotation);
        }
    return {rotation.x, rotation.y, rotation.z, rotation.w};
}

std::vector<framework::Uuid> PrimitiveParameterScene::sitting_avatars(framework::Uuid root_id) const {
    std::vector<framework::Uuid> result;
    const auto root = world_.object(root_id);
    if (!root || !viewers_)
        return result;
    auto avatars = viewers_->agents();
    std::erase_if(avatars, [&](const ViewerAgent &avatar) {
        return !avatar.root || !avatar.seat || avatar.seat->parent_local != root->local_id;
    });
    std::ranges::sort(avatars, {}, [](const ViewerAgent &avatar) { return avatar.seat->order; });
    for (const auto &avatar : avatars)
        result.push_back(avatar.circuit->agent_id);
    return result;
}

script::LslList PrimitiveParameterScene::read(framework::Uuid host, framework::Uuid target,
                                              const script::LslList &rules, script::LslList &result) const {
    if (const auto part = world_.object(target)) {
        const auto root = world_.object(world_.link_key(target, 1));
        if (!root)
            throw PrimitiveParameterValueError("Object reference not set to an instance of an object.",
                                               "System.NullReferenceException");
        auto shape = shape_for(*part);
        TextureEntry textures(part->texture_entry);
        PrimitivePartParameterState state{*part, shape, textures};
        state.absolute_position = part_position(*part, *root);
        state.local_position = part->id == root->id && !root->is_attachment ? state.absolute_position : part->position;
        state.rotation = part_rotation(*part, *root);
        state.effective_flags = part->flags;
        state.sit_target_set =
            part->sit_target_position != Vector3{} ||
            (part->sit_target_orientation != Quaternion{} && part->sit_target_orientation != Quaternion{0, 0, 0, 1});
        state.material = services_.material;
        state.material_texture_visible = [&](Uuid texture) {
            const auto source = world_.object(host);
            return source && services_.material_texture_visible &&
                   services_.material_texture_visible(*source, *part, texture);
        };
        return read_primitive_part_parameters(state, rules, result);
    }
    if (const auto avatar = viewers_ ? viewers_->agent(target) : std::nullopt) {
        AvatarParameterState state;
        state.name = avatar->circuit->name();
        state.offset_position = avatar->seat ? avatar->seat->offset : avatar->position;
        state.absolute_position = avatar_position(*avatar);
        state.rotation = avatar->seat ? avatar->seat->rotation : avatar->rotation;
        state.world_rotation = avatar_rotation(*avatar);
        state.legacy_sit_offsets = services_.legacy_sit_offsets;
        state.seat = [&] {
            const auto seat = world_.object(avatar->seat ? avatar->seat->parent_local : 0u);
            if (!seat)
                return AvatarParameterSeat{};
            const auto root = world_.object(world_.link_key(seat->id, 1));
            return AvatarParameterSeat{true, root.has_value(), root.has_value(),
                                       root ? part_position(*root, *root) : Vector3{},
                                       root ? root->rotation : Quaternion{}};
        };
        state.appearance = [&] {
            const auto appearance = avatar->appearance();
            return appearance ? AvatarParameterAppearance{true, appearance->avatar_size(), appearance->avatar_height}
                              : AvatarParameterAppearance{};
        };
        return read_primitive_avatar_parameters(state, rules, result);
    }
    throw PrimitiveParameterValueError("Object reference not set to an instance of an object.",
                                       "System.NullReferenceException");
}

script::LslList PrimitiveParameterScene::query(framework::Uuid host, script::PrimitiveQueryApi api,
                                               framework::Uuid target, std::int32_t link,
                                               const script::LslList &rules) const {
    script::PrimitiveParameterQueryBackend backend;
    backend.other_object_permitted = [&](Uuid object) {
        const auto source = world_.object(host);
        const auto root = world_.object(world_.link_key(object, 1));
        return source && root && source->owner == root->owner && (root->owner_mask & 0x4000u) != 0;
    };
    backend.select = [&](Uuid object, std::int32_t number, script::PrimitiveQuerySelection selection) {
        return select_primitive_parameter_entities(
            world_, [&](Uuid root) { return sitting_avatars(root); }, object, number, selection);
    };
    backend.read = [&](Uuid object, const script::LslList &input, script::LslList &output) {
        return read(host, object, input, output);
    };
    backend.error = services_.error;
    return script::query_primitive_parameters(backend, api, target, link, rules);
}
} // namespace opensim::scene
