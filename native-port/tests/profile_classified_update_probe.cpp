// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#define OPENSIM_PROFILE_DETAIL_NO_MAIN
#include "profile_classified_info_probe.cpp"
#undef OPENSIM_PROFILE_DETAIL_NO_MAIN
namespace {
int seams, typed_mode;
void UpdateFail(int mask, const char* message) {
    if (seams & mask)
        throw std::runtime_error(message);
}
struct UpdateLand : ProfileRequestLand {
    ProfileText name() override {
        if (seams & 256)
            throw std::runtime_error("Object reference not set to an instance of an object.");
        trace += "landname;";
        return text;
    }
};
struct UpdateMoney : ProfileRequestMoney {
    bool amount_covered(Uuid agent, std::int32_t price) override {
        trace += "covered:" + agent.str() + ":" + std::to_string(price) + ";";
        UpdateFail(4096, "covered");
        return !(seams & 2048);
    }
    void apply_charge(Uuid agent, std::int32_t price, std::int32_t type) override {
        trace += "charge:" + agent.str() + ":" + std::to_string(price) + ":" + std::to_string(type) + ";";
        UpdateFail(8192, "charge");
    }
};
struct UpdateScene : ProfileRequestScene {
    std::uint32_t world_x() override {
        throw std::logic_error("unexpected worldx");
    }
    std::uint32_t world_y() override {
        throw std::logic_error("unexpected worldy");
    }
    std::uint64_t region_handle() override {
        throw std::logic_error("unexpected handle");
    }
    ProfileText scene_name() override {
        throw std::logic_error("unexpected name");
    }
    std::shared_ptr<ProfileRequestLand> land(float x, float y) override {
        if (seams & 32)
            throw std::runtime_error("Object reference not set to an instance of an object.");
        trace += "land:" + Bits(x) + ":" + Bits(y) + ":" + Bits(0) + ";";
        UpdateFail(64, "land");
        return seams & 128 ? nullptr : std::make_shared<UpdateLand>();
    }
    std::shared_ptr<ProfileRequestMoney> money() override {
        trace += "money;";
        UpdateFail(1024, "money");
        return seams & 512 ? nullptr : std::make_shared<UpdateMoney>();
    }
    ProfileText region_name() override {
        if (seams & 16384)
            throw std::runtime_error("Object reference not set to an instance of an object.");
        trace += "region;";
        UpdateFail(32768, "region");
        return text;
    }
};
struct UpdateClient : InfoClient {
    int scenes = 0;
    std::shared_ptr<ProfileRequestScene> scene() override {
        trace += "scene;";
        UpdateFail(2, "scene");
        return (seams & 1) || ((seams & 65536) && scenes++ > 0) ? nullptr : std::make_shared<UpdateScene>();
    }
    Vector3 scene_agent_position() override {
        trace += "sceneagent;";
        UpdateFail(8, "sceneagent");
        if (seams & 4)
            throw std::runtime_error("Object reference not set to an instance of an object.");
        UpdateFail(16, "position");
        return {10, 20, 30};
    }
    void send_classifieds(Uuid id, std::shared_ptr<UserProfileNameList> list) override {
        auto identity = Identity(list);
        trace += "classifieds:" + id.str() + ":" + DumpList(list) + ":" + identity + ";";
        Fail(16384, "classifieds");
    }
};
struct UpdateContext : InfoContext {
    std::shared_ptr<ProfileRequestPresence> presence(Uuid id) override {
        auto p = Context::presence(id);
        if (p)
            p->current_parcel = Id(6);
        return p;
    }
    bool request_object(std::shared_ptr<server::JsonRpcMembers>& data, const ProfileText& method,
                        const ProfileText& url, const ProfileText& id) override {
        auto previous = mode;
        mode = typed_mode;
        try {
            bool result = InfoContext::request_object(data, method, url, id);
            mode = previous;
            return result;
        } catch (...) {
            mode = previous;
            throw;
        }
    }
};
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string u, n, p;
        ProfileClassifiedUpdate query;
        in >> mode >> typed_mode >> flags >> seams >> u >> n >> query.category >> query.parent_estate >>
            query.price >> p;
        uri = Decode(u);
        text = Decode(n);
        position = Decode(p);
        try_parse_sdk_vector3(position.value_or(u""), query.global_position);
        query.classified = Id(3);
        query.parcel = Id(99);
        query.snapshot = Id(9);
        query.flags = 128;
        query.name = text;
        query.description = text;
        trace.clear();
        last.reset();
        cache_now = 0;
        identities.clear();
        retained.clear();
        auto context = std::make_shared<UpdateContext>();
        ExpiringCacheBackend backend{[] { return cache_now; },
                                     [](auto, int due) { return std::make_shared<PicksTimer>(due); }};
        UserProfileRequests m(context, backend);
        active_module = &m;
        std::shared_ptr<ProfileRequestClient> client = flags & 1 ? nullptr : std::make_shared<UpdateClient>();
        std::string output;
        for (std::string token; in >> token;) {
            std::string result = "OK";
            try {
                if (token == "U")
                    m.classified_info_update(query, client);
                else if (token == "C")
                    m.profile_cache().clear();
                else if (token == "M")
                    typed_mode = 0;
                else if (token == "R")
                    mode = 12;
                else {
                    auto e = std::make_shared<UserProfileCacheEntry>();
                    e->flags = 55;
                    e->born = u"seed";
                    e->picksList = std::make_shared<UserProfileNameList>();
                    (*e->picksList)[Id(7)] = u"other";
                    e->classifiedsLists = std::make_shared<UserProfileNameList>();
                    (*e->classifiedsLists)[Id(3)] = u"cached";
                    e->classifieds = std::make_shared<
                        OrderedDictionary<Uuid, std::shared_ptr<UserClassifiedAdd>, UuidHash>>();
                    (*e->classifieds)[Id(3)] = Fill(Id(3));
                    m.profile_cache().add_or_update(token == "B" ? Id(2) : Id(1), e, 300.0);
                    m.classified_owners()[Id(3)] = Id(2);
                    m.classified_interests()[Id(3)] = 2;
                }
            } catch (const std::exception& error) {
                result = "EX:" + H(error.what());
            }
            if (!output.empty())
                output += '/';
            output += result + ":" + InfoState(m);
        }
        std::cout << output << "|" << H(trace) << "\n";
    }
}
