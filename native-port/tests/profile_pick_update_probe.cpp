// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/vector_text.hpp"
#define OPENSIM_PROFILE_PICK_INFO_NO_MAIN
#include "profile_pick_info_probe.cpp"
#undef OPENSIM_PROFILE_PICK_INFO_NO_MAIN
namespace {
int edit_seams, get_mode, update_mode;
std::uint32_t world_x, world_y;
Vector3 edit_position;
void EditFail(int mask, const char* message) {
    if (edit_seams & mask)
        throw std::runtime_error(message);
}
std::string FloatBits(float v) {
    std::ostringstream s;
    s << std::hex << std::setfill('0') << std::setw(8) << std::bit_cast<std::uint32_t>(v);
    return s.str();
}
void MissingRegion() {
    if (edit_seams & 32)
        throw std::runtime_error("Object reference not set to an instance of an object.");
}
struct EditLand : ProfileRequestLand {
    ProfileText name() override {
        if (edit_seams & 256)
            throw std::runtime_error("Object reference not set to an instance of an object.");
        trace += "landname;";
        EditFail(512, "landname");
        return text;
    }
};
struct EditScene : ProfileRequestScene {
    bool presence;
    explicit EditScene(bool value) : presence(value) {
    }
    std::uint32_t world_x() override {
        MissingRegion();
        trace += "worldx;";
        EditFail(64, "worldx");
        return ::world_x;
    }
    std::uint32_t world_y() override {
        MissingRegion();
        trace += "worldy;";
        EditFail(128, "worldy");
        return ::world_y;
    }
    std::uint64_t region_handle() override {
        MissingRegion();
        trace += "handle;";
        EditFail(1024, "handle");
        return (std::uint64_t{::world_x} << 32) | ::world_y;
    }
    ProfileText region_name() override {
        MissingRegion();
        trace += "regionname;";
        EditFail(131072, "regionname");
        return text;
    }
    ProfileText scene_name() override {
        trace += "scenename;";
        EditFail(65536, "scenename");
        return text;
    }
    std::shared_ptr<ProfileRequestLand> land(float x, float y) override {
        if (edit_seams & 2048)
            throw std::runtime_error("Object reference not set to an instance of an object.");
        trace += "land:" + std::string(presence ? "1" : "0") + ":" + FloatBits(x) + ":" + FloatBits(y) + ";";
        EditFail(4096, "land");
        return edit_seams & 32768 ? nullptr : std::make_shared<EditLand>();
    }
    std::shared_ptr<ProfileRequestMoney> money() override {
        throw std::logic_error("unexpected money");
    }
};
struct EditPresence : ProfileRequestPresence {
    std::shared_ptr<ProfileNpcClient> npc_client() override {
        throw std::logic_error("unexpected NPC");
    }
    bool viewer_ui_god() override {
        trace += "god;";
        EditFail(1, "god");
        return edit_seams & 2;
    }
    Vector3 absolute_position() override {
        trace += "position;";
        EditFail(4, "position");
        return edit_position;
    }
    std::shared_ptr<ProfileRequestScene> scene() override {
        trace += "pscene;";
        EditFail(8, "pscene");
        return edit_seams & 16 ? nullptr : std::make_shared<EditScene>(true);
    }
};
struct EditHomeGrid : PickHomeGrid {
    ProfileText gatekeeper_url() override {
        trace += "gatekeeper;";
        EditFail(8192, "gatekeeper");
        return gate;
    }
};
struct EditContext : PickContext {
    EditContext() {
        home = std::make_shared<EditHomeGrid>();
    }
    std::shared_ptr<ProfileRequestPresence> presence(Uuid id) override {
        auto value = Context::presence(id);
        if (!value)
            return {};
        auto p = std::make_shared<EditPresence>();
        p->child = value->child;
        p->npc = value->npc;
        p->current_parcel = value->current_parcel;
        return p;
    }
    void warn_pick_parcel(float x, float y, const ProfileText& name) override {
        trace +=
            "warn:" + T(ProfileText(u"[PROFILES]: PickInfoUpdate found no parcel info at {0},{1} in {2}")) +
            ":" + FloatBits(x) + ":" + FloatBits(y) + ":" + T(name) + ";";
        EditFail(16384, "warn");
    }
    bool request_object(std::shared_ptr<server::JsonRpcMembers>& data, const ProfileText& method,
                        const ProfileText& url, const ProfileText& id) override {
        int previous = mode;
        mode = method == ProfileText(u"picks_update") ? update_mode : get_mode;
        try {
            bool result = PickContext::request_object(data, method, url, id);
            mode = previous;
            return result;
        } catch (...) {
            mode = previous;
            throw;
        }
    }
};
struct EditClient : PickClient {
    std::shared_ptr<ProfileRequestScene> scene() override {
        trace += "scene;";
        EditFail(262144, "scene");
        return edit_seams & 524288 ? nullptr : std::make_shared<EditScene>(false);
    }
    void send_picks(Uuid target, std::shared_ptr<UserProfileNameList> list) override {
        PicksClient::send_picks(target, list);
        if (edit_seams & 1048576) {
            std::shared_ptr<UserProfileCacheEntry> e;
            if (active_module->profile_cache().try_get_value(target_id, e) && e && e->picks)
                for (auto& row : *e->picks)
                    if (row.second) {
                        row.second->Name = u"callback-edit";
                        row.second->GlobalPos = u"<9,8,7>";
                    }
        }
        if (edit_seams & 2097152)
            active_module->profile_cache().clear();
    }
};
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string u, c, n, p, old, g;
        in >> mode >> get_mode >> update_mode >> flags >> edit_seams >> ::world_x >> ::world_y >> u >> c >>
            n >> p >> old >> g;
        uri = Decode(u);
        auto creator = Decode(c);
        target_id = {};
        if (creator)
            try_parse_sdk_uuid(*creator, target_id);
        query_id = Id(3);
        text = Decode(n);
        try_parse_sdk_vector3(Decode(p).value_or(u""), edit_position);
        position = Decode(old);
        gate = Decode(g);
        seams = 0;
        local_grid = 1;
        region_x = 512;
        region_y = 768;
        parcel_id = Id(5);
        asset_uri = u"http://assets/";
        trace.clear();
        last_pick.reset();
        held_pick.reset();
        saved.reset();
        cache_now = 0;
        identities.clear();
        retained.clear();
        auto context = std::make_shared<EditContext>();
        ExpiringCacheBackend backend{[] { return cache_now; },
                                     [](auto, int due) { return std::make_shared<PicksTimer>(due); }};
        UserProfileRequests m(context, backend);
        active_module = &m;
        std::shared_ptr<ProfileRequestClient> client = flags & 1 ? nullptr : std::make_shared<EditClient>();
        ProfilePickUpdate query;
        query.pick = query_id;
        query.creator = target_id;
        query.snapshot = Id(9);
        query.name = text;
        query.description = text;
        query.sort_order = INT_MIN;
        query.top_pick = true;
        query.enabled = true;
        std::string output;
        for (std::string token; in >> token;) {
            std::string result = "OK";
            try {
                if (token[0] == 't')
                    cache_now = std::stod(token.substr(1));
                else if (token == "U")
                    m.pick_info_update(client, query);
                else if (token == "G")
                    held_pick = m.get_pick(target_id, query_id);
                else if (token == "C")
                    m.profile_cache().clear();
                else if (token == "Z")
                    update_mode = 0;
                else if (token == "Y")
                    update_mode = 8;
                else if (token == "R")
                    mode = 12;
                else if (token == "P")
                    ExpiringCacheProbe<Uuid, std::shared_ptr<UserProfileCacheEntry>, UuidHash>::purge(
                        m.profile_cache());
                else if (token == "N")
                    m.profile_cache().add_or_update(target_id, nullptr, 300.0);
                else {
                    auto e = std::make_shared<UserProfileCacheEntry>();
                    e->flags = 55;
                    e->born = u"seed";
                    e->classifiedsLists = std::make_shared<UserProfileNameList>();
                    (*e->classifiedsLists)[Id(9)] = u"ad";
                    e->props = std::make_shared<UserProfileProperties>();
                    e->props->UserId = Id(7);
                    if (token != "L") {
                        e->picksList = std::make_shared<UserProfileNameList>();
                        e->picks = std::make_shared<
                            OrderedDictionary<Uuid, std::shared_ptr<UserProfilePick>, UuidHash>>();
                        if (token == "A" || token == "D" || token == "E") {
                            (*e->picksList)[query_id] = u"existing";
                            auto seed = FillPick(query_id);
                            if (token == "D")
                                seed->GlobalPos = u"bad";
                            (*e->picks)[query_id] = token == "E" ? nullptr : seed;
                        }
                        if (token == "F" || token == "B")
                            for (int i = 0; i < (token == "F" ? 20 : 19); ++i)
                                (*e->picksList)[Id(100 + i)] = u"row";
                    }
                    m.profile_cache().add_or_update(target_id, e, 300.0);
                }
            } catch (const std::exception& error) {
                result = "EX:" + H(error.what());
            }
            if (!output.empty())
                output += '/';
            output += result + ":" + PickState(m);
        }
        std::cout << output << "|" << H(trace) << "\n";
    }
}
