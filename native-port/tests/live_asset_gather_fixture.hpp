// Test adapters for the unchanged-source live traversal oracle.
#include "opensim/scene/live_asset_gather.hpp"
struct LiveFixture;
struct LiveTexture : scene::GatherTexture {
    std::shared_ptr<framework::TextureFace> def;
    std::shared_ptr<std::vector<std::shared_ptr<framework::TextureFace>>> list;
    std::shared_ptr<framework::TextureFace> default_texture() override {
        return def;
    }
    std::shared_ptr<std::vector<std::shared_ptr<framework::TextureFace>>> faces() override {
        return list;
    }
};
struct LiveShape : scene::GatherShape {
    LiveFixture *f;
    int tag;
    std::shared_ptr<LiveTexture> tex;
    std::shared_ptr<scene::GatherRenderMaterials> render;
    std::shared_ptr<scene::GatherTexture> textures() override;
    framework::Uuid sculpt() override;
    framework::Uuid projection() override;
    std::shared_ptr<scene::GatherRenderMaterials> materials() override;
};
struct LiveInventory : scene::GatherInventory {
    LiveFixture *f;
    int tag;
    std::shared_ptr<std::vector<std::shared_ptr<framework::TaskInventoryItem>>> list;
    decltype(list) items() override;
};
struct LiveAttrs : scene::GatherDynAttrs {
    LiveFixture *f;
    int tag;
    services::FeatureData store;
    bool try_store(const std::string &, const std::string &, services::FeatureData &) override;
};
struct LivePart : scene::GatherPart {
    LiveFixture *f;
    int tag;
    std::shared_ptr<LiveShape> sh;
    std::shared_ptr<LiveInventory> inv;
    std::shared_ptr<LiveAttrs> attrs;
    std::shared_ptr<scene::GatherAnimations> anim;
    framework::AssetBytes particle;
    std::shared_ptr<scene::GatherShape> shape() override;
    std::int32_t sides() override;
    framework::Uuid collision_sound() override;
    framework::Uuid invalid_collision_sound() override;
    framework::Uuid sound() override;
    framework::AssetBytes particles() override;
    std::shared_ptr<scene::GatherInventory> inventory() override;
    std::shared_ptr<scene::GatherAnimations> animations() override;
    std::shared_ptr<scene::GatherDynAttrs> dynattrs() override;
    std::optional<std::string> name() override;
    framework::Uuid id() override;
};
struct LiveObject : scene::GatherObject {
    LiveFixture *f;
    std::shared_ptr<std::vector<std::shared_ptr<scene::GatherPart>>> list;
    bool deleted() override;
    decltype(list) parts() override;
    bool temporary() override;
    void dispose() override;
    std::optional<std::string> name() override;
    framework::Uuid id() override;
};
struct LiveFixture {
    int profile, sides, mutation, mutate_nth, mutate_seen = 0;
    bool disposed = false;
    std::function<void(const std::string &)> log;
    std::function<void(int)> fault;
    std::shared_ptr<LiveObject> group;
    std::vector<std::shared_ptr<LivePart>> parts;
    void hit(int point, int tag) {
        log("L:" + std::to_string(point) + ":" + std::to_string(tag));
        fault(100 + point);
        if (mutation && point == 10 && ++mutate_seen == mutate_nth) {
            if (mutation == 1)
                parts[0]->sh->render->overrides->at(0).data.reset();
            if (mutation == 2)
                parts[0]->sh->render.reset();
            if (mutation == 3)
                parts[0]->sh.reset();
        }
    }
    LiveFixture(int p, int s, int m, int nth, std::function<void(const std::string &)> l,
                std::function<void(int)> h)
        : profile(p), sides(s), mutation(m), mutate_nth(nth), log(std::move(l)), fault(std::move(h)) {
        using F = services::SimulatorFeatureValue;
        auto value = [](int n) {
            network::Value v;
            v.kind = network::Value::Kind::Uuid;
            v.text = ::id(n).str();
            return F::from_value(v);
        };
        group = std::make_shared<LiveObject>();
        group->f = this;
        group->list = std::make_shared<std::vector<std::shared_ptr<scene::GatherPart>>>();
        for (int i = 0; i < 2; ++i) {
            auto part = std::make_shared<LivePart>();
            part->f = this;
            part->tag = i;
            parts.push_back(part);
            group->list->push_back(part);
            part->sh = std::make_shared<LiveShape>();
            part->sh->f = this;
            part->sh->tag = i;
            auto tex = std::make_shared<LiveTexture>();
            tex->def = std::make_shared<framework::TextureFace>();
            tex->def->texture_id(::id(1));
            tex->def->material_id(::id(2));
            tex->list = std::make_shared<std::vector<std::shared_ptr<framework::TextureFace>>>();
            for (int j = 0; j < 3; ++j) {
                auto face = std::make_shared<framework::TextureFace>(tex->def);
                face->texture_id(::id(j + 3));
                face->material_id(::id(j + 6));
                tex->list->push_back(face);
            }
            part->sh->tex = tex;
            part->sh->render = std::make_shared<scene::GatherRenderMaterials>();
            part->sh->render->entries = std::make_shared<std::vector<framework::Uuid>>(
                std::initializer_list<framework::Uuid>{::id(9), ::id(-1)});
            part->sh->render->overrides =
                std::make_shared<std::vector<scene::GatherRenderMaterials::Override>>(
                    std::initializer_list<scene::GatherRenderMaterials::Override>{
                        {framework::utf16(::id(10).str() + "x")}});
            part->particle = std::make_shared<std::vector<std::uint8_t>>(86);
            auto bytes = ::id(13).bytes();
            std::copy(bytes.begin(), bytes.end(), part->particle->begin() + 36);
            part->inv = std::make_shared<LiveInventory>();
            part->inv->f = this;
            part->inv->tag = i;
            part->inv->list = std::make_shared<std::vector<std::shared_ptr<framework::TaskInventoryItem>>>();
            for (int j = 0; j < 2; ++j) {
                auto item = std::make_shared<framework::TaskInventoryItem>([] { return 0; });
                item->asset_id = ::id(14 + j);
                item->type = j ? 6 : 0;
                part->inv->list->push_back(item);
            }
            part->anim = std::make_shared<scene::GatherAnimations>();
            for (int n : {16, -2, 0})
                (*part->anim)[::id(n).str()] = true;
            part->attrs = std::make_shared<LiveAttrs>();
            part->attrs->f = this;
            part->attrs->tag = i;
            auto store = F::object(), array = F::list(), map = F::object(), mat = F::object();
            mat->map["NormMap"] = value(17);
            mat->map["SpecMap"] = value(18);
            map->map["Material"] = mat;
            array->array.push_back(map);
            store->map["Materials"] = array;
            part->attrs->store = store;
        }
        auto part = parts[0];
        auto &sh = part->sh;
        switch (profile) {
        case 2:
            group->list.reset();
            break;
        case 3:
            group->list->at(0).reset();
            break;
        case 4:
            part->sh.reset();
            break;
        case 5:
            sh->tex.reset();
            break;
        case 6:
            sh->tex->def.reset();
            break;
        case 7:
            sh->tex->list.reset();
            break;
        case 8:
            sh->tex->list->clear();
            break;
        case 9:
            sh->render.reset();
            break;
        case 10:
            sh->render->entries.reset();
            break;
        case 11:
            sh->render->overrides.reset();
            break;
        case 12:
            part->particle.reset();
            break;
        case 13:
        case 32:
            part->particle = std::make_shared<std::vector<std::uint8_t>>(90);
            (*part->particle)[0] = 68;
            break;
        case 14:
            part->inv.reset();
            break;
        case 15:
            part->inv->list.reset();
            break;
        case 16:
            part->inv->list->at(0).reset();
            break;
        case 17:
            part->anim.reset();
            break;
        case 18:
            part->attrs.reset();
            break;
        case 20:
            part->attrs->store.reset();
            break;
        case 21:
            part->attrs->store->map["Materials"] = F::from_value(network::Value{});
            break;
        case 22:
            part->attrs->store->map["Materials"]->array[0].reset();
            break;
        case 23:
            part->attrs->store->map["Materials"]->array[0] = F::from_value(network::Value(std::int32_t(3)));
            break;
        case 24:
            part->attrs->store->map["Materials"]->array[0]->map["Material"].reset();
            break;
        case 25:
            part->attrs->store->map["Materials"]->array[0]->map["Material"] =
                F::from_value(network::Value(std::int32_t(3)));
            break;
        case 26:
            part->attrs->store->map["Materials"]->array[0]->map["Material"]->map["NormMap"].reset();
            break;
        case 28: {
            auto array = part->attrs->store->map["Materials"];
            auto second = F::object(), mat = F::object();
            mat->map["SpecMap"] = value(19);
            second->map["Material"] = mat;
            array->array[0]->map["Material"]->map["SpecMap"].reset();
            array->array.push_back(second);
            break;
        }
        case 30:
            group.reset();
            break;
        case 31:
            group->list->at(1).reset();
            break;
        case 33:
            group->list->clear();
            break;
        }
    }
};
inline std::shared_ptr<scene::GatherTexture> LiveShape::textures() {
    f->hit(6, tag);
    return tex;
}
inline framework::Uuid LiveShape::sculpt() {
    f->hit(8, tag);
    return ::id(-2);
}
inline framework::Uuid LiveShape::projection() {
    f->hit(9, tag);
    return ::id(-1);
}
inline std::shared_ptr<scene::GatherRenderMaterials> LiveShape::materials() {
    f->hit(10, tag);
    return render;
}
inline decltype(LiveInventory::list) LiveInventory::items() {
    f->hit(16, tag);
    return list;
}
inline bool LiveAttrs::try_store(const std::string &, const std::string &, services::FeatureData &out) {
    f->hit(19, tag);
    out = store;
    return f->profile != 19;
}
inline std::shared_ptr<scene::GatherShape> LivePart::shape() {
    f->hit(5, tag);
    return sh;
}
inline std::int32_t LivePart::sides() {
    f->hit(7, tag);
    return f->sides;
}
inline framework::Uuid LivePart::collision_sound() {
    f->hit(11, tag);
    return ::id(11);
}
inline framework::Uuid LivePart::invalid_collision_sound() {
    f->hit(12, tag);
    return ::id(99);
}
inline framework::Uuid LivePart::sound() {
    f->hit(13, tag);
    return ::id(12);
}
inline framework::AssetBytes LivePart::particles() {
    f->hit(14, tag);
    return particle;
}
inline std::shared_ptr<scene::GatherInventory> LivePart::inventory() {
    f->hit(15, tag);
    return inv;
}
inline std::shared_ptr<scene::GatherAnimations> LivePart::animations() {
    f->hit(17, tag);
    return anim;
}
inline std::shared_ptr<scene::GatherDynAttrs> LivePart::dynattrs() {
    f->hit(18, tag);
    return attrs;
}
inline std::optional<std::string> LivePart::name() {
    f->hit(20, tag);
    return f->profile == 32 ? std::nullopt : std::optional<std::string>("part");
}
inline framework::Uuid LivePart::id() {
    f->hit(21, tag);
    return ::id(40 + tag);
}
inline bool LiveObject::deleted() {
    f->hit(1, 0);
    return f->profile == 1;
}
inline decltype(LiveObject::list) LiveObject::parts() {
    f->hit(2, 0);
    return list;
}
inline bool LiveObject::temporary() {
    f->hit(3, 0);
    return f->profile == 29;
}
inline void LiveObject::dispose() {
    f->hit(4, 0);
    f->disposed = true;
}
inline std::optional<std::string> LiveObject::name() {
    f->hit(22, 0);
    return f->profile == 32 ? std::nullopt : std::optional<std::string>("object");
}
inline framework::Uuid LiveObject::id() {
    f->hit(23, 0);
    return ::id(50);
}
