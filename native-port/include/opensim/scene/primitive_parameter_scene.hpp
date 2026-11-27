// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/avatar_parameter_values.hpp"
#include "opensim/scene/primitive_parameter_selection.hpp"
#include "opensim/scene/viewer_region.hpp"

namespace opensim::scene {
// Decode the typed, normalized OSD representation held by the native legacy
// material cache. The returned object can be retained for module object identity.
std::shared_ptr<PrimitiveParameterMaterial> primitive_parameter_material_from_cache(const network::Value &material);

struct PrimitiveParameterSceneServices {
    bool legacy_sit_offsets = false;
    std::function<std::shared_ptr<PrimitiveParameterMaterial>(framework::Uuid)> material;
    std::function<bool(const SceneObject &, const SceneObject &, framework::Uuid)> material_texture_visible;
    std::function<void(const std::string &)> error;
};

// One query uses the live scene's link, geometry, texture and presence state.
// The runtime supplies its actual material module and permission/inventory gate.
class PrimitiveParameterScene {
  public:
    PrimitiveParameterScene(const World &world, const ViewerRegion *viewers,
                            PrimitiveParameterSceneServices services = {});
    script::LslList query(framework::Uuid host, script::PrimitiveQueryApi api, framework::Uuid target,
                          std::int32_t link, const script::LslList &rules) const;

  private:
    const World &world_;
    const ViewerRegion *viewers_;
    PrimitiveParameterSceneServices services_;
    std::optional<ViewerAgent> wearer(const SceneObject &root) const;
    framework::Vector3 avatar_position(const ViewerAgent &avatar) const;
    framework::Quaternion avatar_rotation(const ViewerAgent &avatar) const;
    framework::Vector3 part_position(const SceneObject &part, const SceneObject &root) const;
    script::LslRotation part_rotation(const SceneObject &part, const SceneObject &root) const;
    std::vector<framework::Uuid> sitting_avatars(framework::Uuid root) const;
    script::LslList read(framework::Uuid host, framework::Uuid target, const script::LslList &rules,
                         script::LslList &result) const;
};
} // namespace opensim::scene
