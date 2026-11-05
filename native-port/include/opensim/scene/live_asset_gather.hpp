// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/uuid_gatherer.hpp"
#include "opensim/framework/texture_entry.hpp"
#include "opensim/framework/task_inventory_item.hpp"
#include "opensim/services/simulator_features.hpp"
namespace opensim::scene {
// Adapters return live source references. Scene owners supply synchronization
// when sharing their mutable scene models across threads.
class GatherTexture {
  public:
    virtual ~GatherTexture() = default;
    virtual std::shared_ptr<framework::TextureFace> default_texture() = 0;
    virtual std::shared_ptr<std::vector<std::shared_ptr<framework::TextureFace>>> faces() = 0;
};
std::shared_ptr<GatherTexture> gather_texture(std::shared_ptr<framework::TextureEntry>);
struct GatherRenderMaterials {
    struct Override {
        std::optional<std::u16string> data;
    };
    std::shared_ptr<std::vector<framework::Uuid>> entries;
    std::shared_ptr<std::vector<Override>> overrides;
};
class GatherShape {
  public:
    virtual ~GatherShape() = default;
    virtual std::shared_ptr<GatherTexture> textures() = 0;
    virtual framework::Uuid sculpt() = 0;
    virtual framework::Uuid projection() = 0;
    virtual std::shared_ptr<GatherRenderMaterials> materials() = 0;
};
class GatherDynAttrs {
  public:
    virtual ~GatherDynAttrs() = default;
    std::recursive_mutex mutex;
    virtual bool try_store(const std::string &space, const std::string &name, services::FeatureData &) = 0;
};
class GatherInventory {
  public:
    virtual ~GatherInventory() = default;
    virtual std::shared_ptr<std::vector<std::shared_ptr<framework::TaskInventoryItem>>> items() = 0;
};
using GatherAnimations = framework::OrderedDictionary<std::string, bool>;
class GatherPart {
  public:
    virtual ~GatherPart() = default;
    virtual std::shared_ptr<GatherShape> shape() = 0;
    virtual std::int32_t sides() = 0;
    virtual framework::Uuid collision_sound() = 0;
    virtual framework::Uuid invalid_collision_sound() = 0;
    virtual framework::Uuid sound() = 0;
    virtual framework::AssetBytes particles() = 0;
    virtual std::shared_ptr<GatherInventory> inventory() = 0;
    virtual std::shared_ptr<GatherAnimations> animations() = 0;
    virtual std::shared_ptr<GatherDynAttrs> dynattrs() = 0;
    virtual std::optional<std::string> name() = 0;
    virtual framework::Uuid id() = 0;
};
class GatherEntity {
  public:
    virtual ~GatherEntity() = default;
};
class GatherObject : public GatherEntity {
  public:
    virtual bool deleted() = 0;
    virtual std::shared_ptr<std::vector<std::shared_ptr<GatherPart>>> parts() = 0;
    virtual bool temporary() = 0;
    virtual void dispose() = 0;
    virtual std::optional<std::string> name() = 0;
    virtual framework::Uuid id() = 0;
};
// Only the SDK texture result and constructor success used by the gatherer.
// This does not implement the complete ParticleSystem public model.
framework::Uuid gather_particle_texture(std::span<const std::uint8_t>);
class GatherAppearance {
  public:
    virtual ~GatherAppearance() = default;
    virtual std::shared_ptr<GatherTexture> texture() = 0;
};
class GatherPresence : public GatherEntity {
  public:
    virtual bool child() = 0;
    virtual bool deleted() = 0;
    virtual std::shared_ptr<GatherAppearance> appearance() = 0;
};
class GatherEnvironment {
  public:
    virtual ~GatherEnvironment() = default;
    virtual void gather(GatheredAssetMap &) = 0;
};
class GatherRegionSettings {
  public:
    virtual ~GatherRegionSettings() = default;
    virtual framework::Uuid texture(int index) = 0;
};
class GatherRegionInfo {
  public:
    virtual ~GatherRegionInfo() = default;
    virtual std::shared_ptr<GatherRegionSettings> settings() = 0;
    virtual framework::Uuid id() = 0;
};
class GatherLandData {
  public:
    virtual ~GatherLandData() = default;
    virtual std::shared_ptr<GatherEnvironment> environment() = 0;
};
class GatherParcel {
  public:
    virtual ~GatherParcel() = default;
    virtual std::shared_ptr<GatherLandData> data() = 0;
};
class GatherLand {
  public:
    virtual ~GatherLand() = default;
    virtual void for_each(const std::function<void(std::shared_ptr<GatherParcel>)> &) = 0;
};
class GatherEntities {
  public:
    virtual ~GatherEntities() = default;
    virtual std::shared_ptr<std::vector<std::shared_ptr<GatherEntity>>> snapshot() = 0;
};
class GatherScene {
  public:
    virtual ~GatherScene() = default;
    virtual std::shared_ptr<GatherRegionInfo> info() = 0;
    virtual std::shared_ptr<GatherEnvironment> environment() = 0;
    virtual std::shared_ptr<GatherLand> land() = 0;
    virtual std::shared_ptr<GatherEntities> entities() = 0;
};
struct SceneAssetGatherBackend {
    // Enumerator adapters preserve the owner's list order and mutation rules.
    std::function<void(const std::function<bool(std::shared_ptr<GatherScene>)> &)> scenes;
    std::shared_ptr<UuidGathererBackend> assets;
    std::function<bool()> running;
    std::function<void(int)> sleep;
    std::function<void(framework::Uuid)> stamp;
    std::function<void(const std::string &)> info, warn;
};
class SceneAssetGatherer {
  public:
    explicit SceneAssetGatherer(std::shared_ptr<SceneAssetGatherBackend>);
    std::shared_ptr<UuidGathererState> gather();

  private:
    std::shared_ptr<SceneAssetGatherBackend> backend_;
};

}
