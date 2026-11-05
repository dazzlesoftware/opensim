// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/live_asset_gather.hpp"
#include "opensim/network/osd_value.hpp"
#include <bit>
#include <stdexcept>
namespace opensim::scene {
namespace {
constexpr auto null_message = "Object reference not set to an instance of an object.";
template <class T> T &required(const std::shared_ptr<T> &value) {
    if (!value)
        throw std::runtime_error(null_message);
    return *value;
}
std::int32_t decrement(std::int32_t value) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value) - 1u);
}
class NativeTexture final : public GatherTexture {
    std::shared_ptr<framework::TextureEntry> entry_;

  public:
    explicit NativeTexture(std::shared_ptr<framework::TextureEntry> e) : entry_(std::move(e)) {}
    std::shared_ptr<framework::TextureFace> default_texture() override {
        return entry_->default_texture;
    }
    std::shared_ptr<std::vector<std::shared_ptr<framework::TextureFace>>> faces() override {
        return std::make_shared<std::vector<std::shared_ptr<framework::TextureFace>>>(entry_->faces.begin(),
                                                                                      entry_->faces.end());
    }
};
std::string osd_type(const services::FeatureData &data) {
    using K = network::Value::Kind;
    if (!data)
        return "";
    if (data->wire_kind == services::SimulatorFeatureValue::WireKind::RawXml)
        return "OSDLLSDxml";
    if (data->wire_kind == services::SimulatorFeatureValue::WireKind::Utf8)
        return "OSDUTF8";
    switch (data->scalar.kind) {
    case K::String:
        return "OSDString";
    case K::Integer:
        return "OSDInteger";
    case K::Boolean:
        return "OSDBoolean";
    case K::Real:
        return "OSDReal";
    case K::Uuid:
        return "OSDUUID";
    case K::Date:
        return "OSDDate";
    case K::Uri:
        return "OSDUri";
    case K::Binary:
        return "OSDBinary";
    case K::Array:
        return "OSDArray";
    case K::Map:
        return "OSDMap";
    case K::Long:
        return "OSDLong";
    default:
        return "OSD";
    }
}
services::FeatureData as_map(const services::FeatureData &value) {
    if (value && value->scalar.kind == network::Value::Kind::Map)
        return value;
    return {};
}
}
std::shared_ptr<GatherTexture> gather_texture(std::shared_ptr<framework::TextureEntry> value) {
    if (!value)
        return {};
    return std::make_shared<NativeTexture>(std::move(value));
}
void UuidGatherer::inspect_live_materials(const std::shared_ptr<GatherPart> &part,
                                          const std::function<void(const std::string &)> &warn) {
    if (!required(part).dynattrs())
        return;
    auto owner = required(part).dynattrs();
    if (!owner)
        throw std::invalid_argument("Value cannot be null. (Parameter 'obj')");
    std::lock_guard lock(owner->mutex);
    services::FeatureData store;
    if (!required(required(part).dynattrs()).try_store("OpenSim", "Materials", store) || !store)
        return;
    auto found = store->map.find("Materials");
    if (found == store->map.end())
        return;
    auto materials = found->second;
    if (!materials || materials->scalar.kind != network::Value::Kind::Array)
        return;
    for (auto &entry : materials->array) {
        if (entry && entry->scalar.kind != network::Value::Kind::Map)
            throw std::runtime_error("Unable to cast object of type 'OpenMetaverse.StructuredData." +
                                     osd_type(entry) + "' to type 'OpenMetaverse.StructuredData.OSDMap'.");
        try {
            auto &map = required(entry).map;
            auto item = map.find("Material");
            if (item != map.end()) {
                auto material = as_map(item->second);
                auto &fields = required(material).map;
                for (const char *key : {"NormMap", "SpecMap"}) {
                    auto texture = fields.find(key);
                    if (texture != fields.end()) {
                        auto &node = required(texture->second);
                        auto id = network::osd_uuid(node.scalar);
                        if (!id.is_zero())
                            state_->gathered[id] = 0;
                    }
                }
            }
        } catch (const std::exception &e) {
            warn("[UUID Gatherer]: exception getting materials: " + std::string(e.what()));
        }
    }
}
void UuidGatherer::add_for_inspection(const std::shared_ptr<GatherObject> &object,
                                      const std::function<void(const std::string &)> &warn) {
    if (!warn)
        throw std::invalid_argument("Live gatherer requires warning logger");
    if (required(object).deleted())
        return;
    auto parts = required(object).parts();
    required(parts);
    auto face = [&](const std::shared_ptr<framework::TextureFace> &value) {
        auto id = required(value).texture_id();
        if (!id.is_zero() && !skipped(id) && !state_->failed.contains(id))
            state_->gathered[id] = 0;
        id = required(value).material_id();
        if (!id.is_zero())
            add_for_inspection(id);
    };
    for (std::size_t i = 0; i < parts->size(); ++i) {
        auto part = parts->at(i);
        try {
            auto texture = required(required(part).shape()).textures();
            if (texture) {
                if (texture->default_texture())
                    face(texture->default_texture());
                if (texture->faces()) {
                    auto sides = required(part).sides();
                    auto faces = texture->faces();
                    for (auto &value : required(faces)) {
                        if (value)
                            face(value);
                        sides = decrement(sides);
                        if (sides <= 0)
                            break;
                    }
                }
            }
            if (!required(required(part).shape()).sculpt().is_zero())
                state_->gathered[required(required(part).shape()).sculpt()] = 0;
            if (!required(required(part).shape()).projection().is_zero())
                state_->gathered[required(required(part).shape()).projection()] = 0;
            if (required(required(part).shape()).materials()) {
                if (required(required(required(part).shape()).materials()).entries) {
                    for (std::size_t j = 0;
                         j < required(required(required(part).shape()).materials()).entries->size(); ++j) {
                        if (!required(required(required(part).shape()).materials()).entries->at(j).is_zero())
                            add_for_inspection(
                                required(required(required(part).shape()).materials()).entries->at(j),
                                std::int8_t(57));
                    }
                }
                if (required(required(required(part).shape()).materials()).overrides) {
                    for (std::size_t j = 0;
                         j < required(required(required(part).shape()).materials()).overrides->size(); ++j) {
                        auto text =
                            required(required(required(part).shape()).materials()).overrides->at(j).data;
                        if (text && !text->empty()) {
                            auto current =
                                required(required(required(part).shape()).materials()).overrides->at(j).data;
                            inspect_embedded(current ? std::u16string_view(*current) : std::u16string_view{});
                        }
                    }
                }
            }
            auto collision = required(part).collision_sound();
            if (!collision.is_zero() && collision != required(part).invalid_collision_sound())
                state_->gathered[collision] = 1;
            auto sound = required(part).sound();
            if (!sound.is_zero())
                state_->gathered[sound] = 1;
            auto particle = required(part).particles();
            if (required(particle).size() > 0) {
                try {
                    auto bytes = required(part).particles();
                    auto id = gather_particle_texture(required(bytes));
                    if (!id.is_zero())
                        state_->gathered[id] = 0;
                } catch (const std::exception &) {
                    auto pn = required(part).name().value_or("");
                    auto pi = required(part).id().str();
                    auto on = required(object).name().value_or("");
                    auto oi = required(object).id().str();
                    warn("[UUID GATHERER]: Could not check particle system for part " + pn + " " + pi +
                         " in object " + on + " " + oi + " since it is corrupt.  Continuing.");
                }
            }
            auto items = required(required(part).inventory()).items();
            for (std::size_t j = 0; j < required(items).size(); ++j) {
                auto item = required(items).at(j);
                add_for_inspection(
                    required(item).asset_id,
                    std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(required(item).type)));
            }
            if (required(part).animations() && required(required(part).animations()).size() > 0) {
                auto animations = required(part).animations();
                for (auto &[key, value] : required(animations)) {
                    auto id = framework::Uuid(key);
                    if (!id.is_zero() && !skipped(id) && !state_->failed.contains(id))
                        state_->gathered[id] = 20;
                }
            }
            inspect_live_materials(part, warn);
        } catch (const std::exception &e) {
            backend_->error("[UUID GATHERER]: Failed to get part - " + std::string(e.what()));
        }
    }
    if (required(object).temporary())
        required(object).dispose();
}
SceneAssetGatherer::SceneAssetGatherer(std::shared_ptr<SceneAssetGatherBackend> backend)
    : backend_(std::move(backend)) {
    if (!backend_ || !backend_->scenes || !backend_->assets || !backend_->running || !backend_->sleep ||
        !backend_->stamp || !backend_->info || !backend_->warn)
        throw std::invalid_argument(
            "Scene asset gatherer requires complete scene/service/status dependencies");
}
std::shared_ptr<UuidGathererState> SceneAssetGatherer::gather() {
    backend_->info("[FLOTSAM ASSET CACHE] gather assets in use");
    auto state = std::make_shared<UuidGathererState>();
    UuidGatherer gatherer(backend_->assets, state);
    std::int32_t cooldown = 0;
    auto increment = [](std::int32_t value) {
        return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value) + 1u);
    };
    backend_->scenes([&](const std::shared_ptr<GatherScene> &scene) {
        for (int i = 0; i < 9; ++i)
            gatherer.add_gathered(required(required(required(scene).info()).settings()).texture(i),
                                  std::int8_t(0));
        auto env = required(scene).environment();
        if (env)
            env->gather(state->gathered);
        if (required(scene).land())
            required(required(scene).land()).for_each([&](const auto &parcel) {
                if (required(parcel).data() && required(required(parcel).data()).environment())
                    required(required(required(parcel).data()).environment()).gather(state->gathered);
            });
        auto entities = required(required(scene).entities()).snapshot();
        for (auto &entity : required(entities)) {
            if (!backend_->running())
                break;
            if (auto object = std::dynamic_pointer_cast<GatherObject>(entity)) {
                if (object->deleted())
                    continue;
                gatherer.add_for_inspection(object, backend_->warn);
                while (gatherer.gather_next()) {
                    cooldown = increment(cooldown);
                    if (cooldown > 50) {
                        backend_->sleep(60);
                        cooldown = 0;
                    }
                }
                cooldown = increment(cooldown);
                if (cooldown > 25) {
                    backend_->sleep(60);
                    cooldown = 0;
                }
            } else if (auto presence = std::dynamic_pointer_cast<GatherPresence>(entity)) {
                if (presence->child() || presence->deleted() || !presence->appearance())
                    continue;
                auto texture = required(presence->appearance()).texture();
                if (!texture)
                    continue;
                auto faces = texture->faces();
                if (!faces)
                    continue;
                for (int index : {8, 9, 10, 11, 19, 20, 40, 41, 42, 43, 44})
                    if (static_cast<std::size_t>(index) < faces->size()) {
                        auto face = faces->at(static_cast<std::size_t>(index));
                        if (!face)
                            continue;
                        if (face->texture_id().is_zero() ||
                            face->texture_id() == framework::Uuid("c228d1cf-4b5d-4ba8-84f4-899a0796aa97"))
                            continue;
                        gatherer.add_gathered(face->texture_id(), std::int8_t(0));
                    }
            }
        }
        entities.reset();
        if (!backend_->running())
            return false;
        backend_->stamp(required(required(scene).info()).id());
        return true;
    });
    gatherer.gather_all();
    state->failed.clear();
    state->uncertain.clear();
    backend_->info("[FLOTSAM ASSET CACHE]     found " + std::to_string(state->gathered.size()) +
                   " possible assets in use)");
    return state;
}

}
