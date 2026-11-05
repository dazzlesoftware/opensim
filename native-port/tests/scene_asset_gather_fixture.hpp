struct SceneFixture;
struct ScanSettings : scene::GatherRegionSettings {
    SceneFixture *f;
    int tag;
    framework::Uuid texture(int) override;
};
struct ScanInfo : scene::GatherRegionInfo {
    SceneFixture *f;
    int tag;
    std::shared_ptr<ScanSettings> data;
    std::shared_ptr<scene::GatherRegionSettings> settings() override;
    framework::Uuid id() override;
};
struct ScanEnv : scene::GatherEnvironment {
    SceneFixture *f;
    int tag;
    void gather(scene::GatheredAssetMap &) override;
};
struct ScanLandData : scene::GatherLandData {
    SceneFixture *f;
    int tag;
    std::shared_ptr<ScanEnv> env;
    std::shared_ptr<scene::GatherEnvironment> environment() override;
};
struct ScanParcel : scene::GatherParcel {
    SceneFixture *f;
    int tag;
    std::shared_ptr<ScanLandData> value;
    std::shared_ptr<scene::GatherLandData> data() override;
};
struct ScanLand : scene::GatherLand {
    SceneFixture *f;
    int tag;
    std::vector<std::shared_ptr<scene::GatherParcel>> list;
    void for_each(const std::function<void(std::shared_ptr<scene::GatherParcel>)> &) override;
};
struct ScanRegistry : scene::GatherEntities {
    SceneFixture *f;
    int tag;
    std::shared_ptr<std::vector<std::shared_ptr<scene::GatherEntity>>> list;
    decltype(list) snapshot() override;
};
struct ScanAppearance : scene::GatherAppearance {
    SceneFixture *f;
    std::shared_ptr<LiveTexture> tex;
    std::shared_ptr<scene::GatherTexture> texture() override;
};
struct ScanAvatar : scene::GatherPresence {
    SceneFixture *f;
    std::shared_ptr<ScanAppearance> a;
    bool child() override;
    bool deleted() override;
    std::shared_ptr<scene::GatherAppearance> appearance() override;
};
struct ScanScene : scene::GatherScene {
    SceneFixture *f;
    int tag;
    std::shared_ptr<ScanInfo> data;
    std::shared_ptr<ScanEnv> env;
    std::shared_ptr<ScanLand> parcels;
    std::shared_ptr<ScanRegistry> registry;
    std::shared_ptr<scene::GatherRegionInfo> info() override;
    std::shared_ptr<scene::GatherEnvironment> environment() override;
    std::shared_ptr<scene::GatherLand> land() override;
    std::shared_ptr<scene::GatherEntities> entities() override;
};
struct SceneFixture {
    int profile, cancel, nth, seen = 0;
    bool running = true;
    std::function<void(const std::string &)> log;
    std::function<void(int)> fault;
    std::shared_ptr<LiveFixture> live;
    std::vector<std::shared_ptr<scene::GatherScene>> scenes;
    std::shared_ptr<scene::UuidGathererState> last;
    void hit(int p, int tag) {
        log("S:" + std::to_string(p) + ":" + std::to_string(tag));
        fault(200 + p);
        if (p == cancel && ++seen == nth)
            running = false;
    }
    SceneFixture(int p, int c, int n, std::function<void(const std::string &)> l, std::function<void(int)> h)
        : profile(p), cancel(c), nth(n), log(std::move(l)), fault(std::move(h)) {
        live = std::make_shared<LiveFixture>(p == 13 ? 1 : 0, 3, 0, 1, log, [this](int point) {
            fault(point);
            hit(51, point - 100);
        });
        if (profile == 25) {
            live->parts[0]->inv->list->clear();
            for (int n = 100; n < 200; ++n) {
                auto item = std::make_shared<framework::TaskInventoryItem>([] { return 0; });
                item->asset_id = ::id(n);
                item->type = 6;
                live->parts[0]->inv->list->push_back(item);
            }
        }
        for (int i = 0; i < 2; ++i) {
            auto s = std::make_shared<ScanScene>();
            s->f = this;
            s->tag = i;
            s->data = std::make_shared<ScanInfo>();
            s->data->f = this;
            s->data->tag = i;
            s->data->data = std::make_shared<ScanSettings>();
            s->data->data->f = this;
            s->data->data->tag = i;
            s->env = std::make_shared<ScanEnv>();
            s->env->f = this;
            s->env->tag = i;
            s->parcels = std::make_shared<ScanLand>();
            s->parcels->f = this;
            s->parcels->tag = i;
            auto parcel = std::make_shared<ScanParcel>();
            parcel->f = this;
            parcel->tag = i;
            parcel->value = std::make_shared<ScanLandData>();
            parcel->value->f = this;
            parcel->value->tag = i;
            parcel->value->env = std::make_shared<ScanEnv>();
            parcel->value->env->f = this;
            parcel->value->env->tag = 10 + i;
            s->parcels->list.push_back(parcel);
            s->registry = std::make_shared<ScanRegistry>();
            s->registry->f = this;
            s->registry->tag = i;
            auto avatar = std::make_shared<ScanAvatar>();
            avatar->f = this;
            avatar->a = std::make_shared<ScanAppearance>();
            avatar->a->f = this;
            auto tex = std::make_shared<LiveTexture>();
            tex->def = std::make_shared<framework::TextureFace>();
            tex->def->texture_id(::id(90));
            tex->list = std::make_shared<std::vector<std::shared_ptr<framework::TextureFace>>>(45);
            for (int index : {8, 9, 10, 11, 19, 20, 40, 41, 42, 43, 44}) {
                auto face = std::make_shared<framework::TextureFace>(tex->def);
                face->texture_id(::id(80 + index));
                tex->list->at(static_cast<std::size_t>(index)) = face;
            }
            avatar->a->tex = tex;
            s->registry->list = std::make_shared<std::vector<std::shared_ptr<scene::GatherEntity>>>(
                std::initializer_list<std::shared_ptr<scene::GatherEntity>>{
                    live->group, avatar, std::make_shared<scene::GatherEntity>(), nullptr});
            scenes.push_back(s);
            if (i == 0)
                switch (profile) {
                case 2:
                    scenes[0].reset();
                    break;
                case 3:
                    s->data.reset();
                    break;
                case 4:
                    s->data->data.reset();
                    break;
                case 5:
                    s->env.reset();
                    break;
                case 6:
                    s->parcels.reset();
                    break;
                case 7:
                    s->parcels->list[0].reset();
                    break;
                case 8:
                    parcel->value.reset();
                    break;
                case 9:
                    parcel->value->env.reset();
                    break;
                case 10:
                    s->registry.reset();
                    break;
                case 11:
                    s->registry->list.reset();
                    break;
                case 12:
                    s->registry->list->clear();
                    break;
                case 14:
                    s->registry->list->at(0).reset();
                    break;
                case 17:
                    avatar->a.reset();
                    break;
                case 18:
                    avatar->a->tex.reset();
                    break;
                case 19:
                    tex->list.reset();
                    break;
                case 20:
                    tex->list->resize(9);
                    break;
                case 21:
                    tex->list->clear();
                    break;
                case 22:
                    for (auto &face : *tex->list)
                        if (face)
                            face->texture_id(framework::Uuid("c228d1cf-4b5d-4ba8-84f4-899a0796aa97"));
                    break;
                case 23:
                    for (auto &face : *tex->list)
                        if (face)
                            face->texture_id({});
                    break;
                case 24:
                    tex->list->at(8).reset();
                    break;
                }
        }
        if (profile == 1)
            scenes.clear();
    }
    std::string call(const std::shared_ptr<scene::UuidGathererBackend> &assets) {
        auto b = std::make_shared<scene::SceneAssetGatherBackend>();
        b->assets = assets;
        b->scenes = [this](auto &visit) {
            for (auto &s : scenes)
                if (!visit(s))
                    break;
        };
        b->running = [this] {
            hit(45, 0);
            return running;
        };
        b->sleep = [this](int n) { hit(46, n); };
        b->stamp = [this](auto id) { hit(47, number(id)); };
        b->info = [this](auto &text) {
            log("I:" + text);
            hit(48, 0);
        };
        b->warn = [this](auto &text) {
            log("W:" + text);
            live->hit(24, 0);
        };
        scene::SceneAssetGatherer gatherer(b);
        auto state = gatherer.gather();
        last = state;
        std::string result = "scene:";
        bool first = true;
        for (auto &[key, type] : state->gathered) {
            if (!first)
                result += ',';
            first = false;
            result += label(key) + ":" + std::to_string(type);
        }
        return result + ":" + (running ? "1" : "0");
    }
};
inline framework::Uuid ScanSettings::texture(int index) {
    f->hit(32, index);
    return ::id(1 + tag * 20 + index);
}
inline std::shared_ptr<scene::GatherRegionSettings> ScanInfo::settings() {
    f->hit(31, tag);
    return data;
}
inline framework::Uuid ScanInfo::id() {
    f->hit(49, tag);
    return ::id(70 + tag);
}
inline void ScanEnv::gather(scene::GatheredAssetMap &ids) {
    f->hit(34, tag);
    ids[::id(20 + tag)] = 0;
    ids[framework::Uuid{}] = 0;
}
inline std::shared_ptr<scene::GatherEnvironment> ScanLandData::environment() {
    f->hit(38, tag);
    return env;
}
inline std::shared_ptr<scene::GatherLandData> ScanParcel::data() {
    f->hit(37, tag);
    return value;
}
inline void ScanLand::for_each(const std::function<void(std::shared_ptr<scene::GatherParcel>)> &visit) {
    f->hit(36, tag);
    for (auto &p : list)
        visit(p);
}
inline decltype(ScanRegistry::list) ScanRegistry::snapshot() {
    f->hit(40, tag);
    return list;
}
inline std::shared_ptr<scene::GatherTexture> ScanAppearance::texture() {
    f->hit(44, 0);
    return tex;
}
inline bool ScanAvatar::child() {
    f->hit(41, 0);
    return f->profile == 15;
}
inline bool ScanAvatar::deleted() {
    f->hit(42, 0);
    return f->profile == 16;
}
inline std::shared_ptr<scene::GatherAppearance> ScanAvatar::appearance() {
    f->hit(43, 0);
    return a;
}
inline std::shared_ptr<scene::GatherRegionInfo> ScanScene::info() {
    f->hit(30, tag);
    return data;
}
inline std::shared_ptr<scene::GatherEnvironment> ScanScene::environment() {
    f->hit(33, tag);
    return env;
}
inline std::shared_ptr<scene::GatherLand> ScanScene::land() {
    f->hit(35, tag);
    return parcels;
}
inline std::shared_ptr<scene::GatherEntities> ScanScene::entities() {
    f->hit(39, tag);
    return registry;
}
