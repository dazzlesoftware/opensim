// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/avatar_appearance.hpp"
#include "opensim/network/osd_value.hpp"
#include <stdexcept>
namespace opensim::framework {
namespace {
using network::Value;
Value real(float value) {
    Value v;
    v.kind = Value::Kind::Real;
    v.real = value;
    return v;
}
Value uuid(const Uuid& id) {
    Value v(id.str());
    v.kind = Value::Kind::Uuid;
    return v;
}
Value array() {
    Value v;
    v.kind = Value::Kind::Array;
    return v;
}
template <class T> T& required(const std::shared_ptr<T>& p) {
    if (!p)
        throw std::invalid_argument("Null appearance component");
    return *p;
}
}
network::Value AvatarAppearance::pack(const EntityTransferContext* context) {
    auto result = Value::object({});
    result.set("serial", Value(serial));
    result.set("height", real(avatar_height));
    result.set("aphz", real(hover_z));
    if (!texture)
        return result;
    auto& w = required(wearables);
    bool pv8 = false;
    std::size_t count = 15;
    if (context) {
        if (static_cast<double>(context->outbound_version) >= .8) {
            pv8 = true;
            count = w.size();
        } else if (static_cast<double>(context->outbound_version) >= .6)
            count = 16;
        if (pv8 && count > 16) {
            auto extra = array();
            for (std::size_t i = 16; i < count; ++i)
                extra.array.push_back(required(w[i]).pack());
            result.set("wrbls8", extra);
            count = 16;
        }
    }
    auto wears = array();
    for (std::size_t i = 0; i < std::min(count, w.size()); ++i)
        wears.array.push_back(required(w[i]).pack());
    result.set("wearables", wears);
    if (pv8)
        result.set("te8", network::osd_from_binary(
                              std::make_shared<std::vector<std::uint8_t>>(texture->bakes_bytes())));
    else {
        auto textures = array();
        for (unsigned i = 0; i < 29; ++i)
            textures.array.push_back(uuid(texture->get_face(i)->texture_id()));
        result.set("textures", textures);
    }
    if (cache_items) {
        if (auto baked = WearableCacheItem::baked_to_osd(*cache_items, 0, 6); baked && !baked->array.empty())
            result.set("bakedcache", *baked);
        if (auto baked = WearableCacheItem::baked_to_osd(*cache_items, 6, -1); baked && !baked->array.empty())
            result.set("bc8", *baked);
    }
    result.set("visualparams", network::osd_from_binary(visual_params));
    std::lock_guard lock(mutex_);
    auto attachs = array();
    for (auto& a : attachments())
        attachs.array.push_back(a.pack());
    result.set("attachments", attachs);
    return result;
}
network::Value AvatarAppearance::pack_for_notecard(bool no_huds) {
    auto result = Value::object({});
    result.set("serial", Value(serial));
    result.set("height", real(avatar_height));
    result.set("aphz", real(hover_z));
    auto wears = array();
    for (int i = 0; i < 15; ++i)
        wears.array.push_back(array());
    result.set("wearables", wears);
    auto textures = array();
    textures.array.push_back(uuid(default_texture_id));
    for (unsigned i = 1; i < 29; ++i)
        textures.array.push_back(uuid(required(texture).get_face(i)->texture_id()));
    result.set("textures", textures);
    bool extra = false;
    for (unsigned i : {40u, 41u, 42u, 43u, 44u}) {
        auto face = texture->faces[i];
        if (face && face->texture_id() != default_texture_id && !face->texture_id().is_zero())
            extra = true;
    }
    if (extra)
        result.set("te8", network::osd_from_binary(
                              std::make_shared<std::vector<std::uint8_t>>(texture->bakes_bytes())));
    result.set("visualparams", network::osd_from_binary(visual_params));
    std::lock_guard lock(mutex_);
    auto attachs = array();
    for (auto& a : attachments()) {
        if (no_huds && a.point >= 31 && a.point <= 38)
            continue;
        attachs.array.push_back(a.pack());
    }
    result.set("attachments", attachs);
    return result;
}
void AvatarAppearance::unpack(const network::Value* data) {
    default_wearables();
    reset_baked_textures();
    default_params();
    clear_attachments();
    if (!data)
        return;
    if (data->kind != Value::Kind::Map)
        throw std::invalid_argument("Appearance requires an OSD map");
    if (data->contains("serial"))
        serial = network::osd_integer(data->get("serial"));
    if (data->contains("aphz"))
        hover_z = static_cast<float>(network::osd_real(data->get("aphz")));
    if (data->contains("height"))
        set_size({.45f, .6f, static_cast<float>(network::osd_real(data->get("height")))});
    try {
        auto& extra = data->get("wrbls8");
        std::size_t extra_count = extra.kind == Value::Kind::Array ? extra.array.size() : 0;
        auto& wears = data->get("wearables");
        if (wears.kind == Value::Kind::Array && wears.array.size() + extra_count > 0) {
            wearables = std::make_shared<Wearables>(wears.array.size() + extra_count);
            auto load = [&](const Value& item) {
                if (item.kind != Value::Kind::Array)
                    throw std::invalid_argument("Wearable requires an OSD array");
                return std::make_shared<AvatarWearable>(item);
            };
            for (std::size_t i = 0; i < wears.array.size(); ++i)
                (*wearables)[i] = load(wears.array[i]);
            for (std::size_t i = 0; i < extra_count; ++i)
                (*wearables)[wears.array.size() + i] = load(extra.array[i]);
        }
        if (data->contains("te8")) {
            auto bytes = network::osd_binary(data->get("te8"));
            texture = std::make_shared<TextureEntry>(*bytes);
        } else if (auto& textures = data->get("textures"); textures.kind == Value::Kind::Array) {
            for (unsigned i = 0; i < textures.array.size() && i < 29; ++i)
                texture->create_face(i)->texture_id(network::osd_uuid(textures.array[i]));
        }
        auto& baked = data->get("bakedcache");
        if (baked.kind == Value::Kind::Array) {
            cache_items = std::make_shared<WearableCacheItems>(WearableCacheItem::defaults());
            auto load = [&]() {
                for (auto& item : baked.array)
                    if (item.kind == Value::Kind::Map) {
                        auto index = network::osd_integer(item.get("textureindex"));
                        if (index < 0 || static_cast<std::size_t>(index) >= cache_items->size())
                            continue;
                        auto& c = (*cache_items)[index];
                        c->cache_id = network::osd_uuid(item.get("cacheid"));
                        c->texture_id = network::osd_uuid(item.get("textureid"));
                        c->texture_asset.reset();
                    }
            };
            load();
            // Original bc8 branch loops bakedOSDArray again, ignoring bc8 rows.
            if (data->get("bc8").kind == Value::Kind::Array)
                load();
        }
        if (auto& params = data->get("visualparams");
            params.kind == Value::Kind::Binary || params.kind == Value::Kind::Array)
            visual_params = network::osd_binary(params);
        if (auto& attachs = data->get("attachments"); attachs.kind == Value::Kind::Array)
            for (auto& a : attachs.array)
                append_attachment(std::make_shared<AvatarAttachment>(a));
    } catch (const std::exception&) {
        // Source logs and retains every mutation made before the failure.
    }
}
}
