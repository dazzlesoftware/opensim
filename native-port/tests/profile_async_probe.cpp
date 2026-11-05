// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "profile_list_fixture.hpp"
#include "opensim/services/open_profile_client.hpp"
#include "opensim/scene/profile_user_agent.hpp"
#include <future>
#include <thread>
namespace opensim::scene {
struct UserProfileAsyncProbe {
    static std::recursive_mutex& gate(UserProfileRequests& m, bool hg) {
        return (hg ? m.hg_ : m.local_).gate;
    }
    static std::string
    queues(UserProfileRequests& m,
           const std::function<std::string(const UserProfileRequests::AsyncRequest&)>& row) {
        auto one = [&](auto& q) {
            std::string out;
            std::lock_guard lock(q.stack);
            for (auto i = q.items.rbegin(); i != q.items.rend(); ++i) {
                if (!out.empty())
                    out += ',';
                out += row(*i);
            }
            return std::string(q.running ? "1" : "0") + ":" + out;
        };
        auto a = one(m.local_), b = one(m.hg_);
        return a + ";" + b;
    }
};
}
namespace {
std::uint64_t extra;
int info_kind;
std::shared_ptr<ProfileAccountData> account_data, last_account;
std::shared_ptr<UserProfileProperties> input_props;
std::shared_ptr<server::JsonRpcMembers> last_props;
ProfileText message;
std::shared_ptr<ProfileMemberships> memberships;
std::vector<std::function<void()>> jobs;
std::array<bool, 5> active;
struct AsyncClient;
std::array<std::shared_ptr<AsyncClient>, 5> clients;
bool X(int bit) {
    return (extra & (std::uint64_t{1} << bit)) != 0;
}
void AsyncFail(int bit, const char* text) {
    if (X(bit))
        throw std::runtime_error(text);
}
std::shared_ptr<UserProfileProperties> FillProps(Uuid id) {
    auto p = std::make_shared<UserProfileProperties>();
    p->UserId = id;
    p->PartnerId = Id(7);
    p->PublishProfile = true;
    p->PublishMature = true;
    p->WebUrl = text;
    p->WantToMask = INT_MIN;
    p->WantToText = text;
    p->SkillsMask = INT_MAX;
    p->SkillsText = text;
    p->Language = text;
    p->ImageId = Id(9);
    p->AboutText = text;
    p->FirstLifeImageId = Id(8);
    p->FirstLifeText = text;
    return p;
}
std::string DumpProps(const std::shared_ptr<server::JsonRpcMembers>& data) {
    if (!data)
        return "N";
    auto p = std::dynamic_pointer_cast<UserProfileProperties>(data);
    if (!p)
        return "X";
    return p->UserId.str() + ":" + p->PartnerId.str() + ":" + (p->PublishProfile ? "1" : "0") + ":" +
           (p->PublishMature ? "1" : "0") + ":" + T(p->WebUrl) + ":" + std::to_string(p->WantToMask) + ":" +
           T(p->WantToText) + ":" + std::to_string(p->SkillsMask) + ":" + T(p->SkillsText) + ":" +
           T(p->Language) + ":" + p->ImageId.str() + ":" + T(p->AboutText) + ":" + p->FirstLifeImageId.str() +
           ":" + T(p->FirstLifeText);
}
std::string Bytes(const std::shared_ptr<std::vector<std::uint8_t>>& data) {
    if (!data)
        return "N";
    std::string out;
    for (auto c : *data) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
std::string Groups(const std::shared_ptr<ProfileMemberships>& data) {
    if (!data)
        return "N";
    std::string out;
    for (auto& g : *data) {
        if (!out.empty())
            out += ',';
        out += g ? Group(*g) : "N";
    }
    return out;
}
std::string ClientName(const std::shared_ptr<ProfileRequestClient>& client);
struct AsyncModule : UserProfileRequests {
    using UserProfileRequests::UserProfileRequests;
    bool is_friend_online(std::shared_ptr<ProfileRequestClient> c, Uuid id) override {
        trace += "online:" + ClientName(c) + ":" + id.str() + ";";
        return !X(51);
    }
};
std::shared_ptr<AsyncModule> current;
std::thread locker;
std::shared_ptr<std::promise<void>> released;
void Release() {
    if (!locker.joinable())
        return;
    released->set_value();
    locker.join();
    released.reset();
}
void Hold(bool hg) {
    Release();
    auto entered = std::make_shared<std::promise<void>>();
    auto ready = entered->get_future();
    released = std::make_shared<std::promise<void>>();
    auto signal = released->get_future().share();
    auto owner = current;
    locker = std::thread([owner, hg, entered, signal] {
        std::lock_guard lock(UserProfileAsyncProbe::gate(*owner, hg));
        entered->set_value();
        signal.wait();
    });
    ready.get();
}

struct AsyncNpc : ProfileNpcClient {
    ProfileText about() override {
        trace += "npcabout;";
        AsyncFail(36, "npcabout");
        return text;
    }
    ProfileText born() override {
        trace += "npcborn;";
        AsyncFail(37, "npcborn");
        return u"npc born";
    }
    Uuid image() override {
        trace += "npcimage;";
        AsyncFail(38, "npcimage");
        return Id(9);
    }
};
struct AsyncPresence : FixturePresence {
    std::shared_ptr<ProfileNpcClient> npc_client() override {
        trace += "npc;";
        AsyncFail(35, "npc");
        return X(34) ? nullptr : std::make_shared<AsyncNpc>();
    }
    bool viewer_ui_god() override {
        trace += "god;";
        AsyncFail(23, "god");
        return X(22);
    }
};
struct AsyncAccounts : ProfileAccountService {
    std::shared_ptr<ProfileAccountData> get(Uuid scope, Uuid id) override {
        trace += "account:" + scope.str() + ":" + id.str() + ";";
        AsyncFail(3, "account");
        return X(2) ? nullptr : account_data;
    }
};
struct BridgeClient : network::XmlRpcRequesterClient {
    std::shared_ptr<network::Value> reply;
    explicit BridgeClient(std::shared_ptr<network::Value> value) : reply(std::move(value)) {
    }
    std::shared_ptr<network::NwcXmlRpcResponse>
    send(const std::shared_ptr<network::NwcXmlRpcRequest>& request,
         const std::optional<std::u16string>&) override {
        if (info_kind == 20)
            throw std::runtime_error("bridge");
        if (request->method_name != u"get_user_info")
            throw std::logic_error("Unexpected bridge method");
        auto response = std::make_shared<network::NwcXmlRpcResponse>();
        response->set_value(reply);
        return response;
    }
    void close() override {
    }
};
std::shared_ptr<ProfileUserInfo> BridgeInfo(Uuid id) {
    using network::Value;
    auto fields = std::make_shared<Value>(Value::object({}));
    Value flags(account_data->flags);
    switch (info_kind) {
    case 9:
        flags = Value("1");
        break;
    case 10:
        flags = Value::null_reference();
        break;
    case 11:
        flags = Value::long_integer(1);
        break;
    case 12:
        flags = Value(true);
        break;
    case 13:
        flags.kind = Value::Kind::Real;
        flags.real = 1.5;
        break;
    case 14:
        flags = Value::object({});
        break;
    case 15:
        flags.kind = Value::Kind::Array;
        break;
    case 16:
        flags.kind = Value::Kind::Binary;
        break;
    case 17:
        flags.kind = Value::Kind::Date;
        break;
    }
    if (info_kind != 19) {
        fields->set("user_flags", flags);
        fields->set("user_created", info_kind == 18 ? Value("1") : Value(account_data->created));
    }
    services::UserAgentConnectorServices services;
    services.simulation_options.automatic_purge = false;
    services.client = [fields](int timeout) -> std::shared_ptr<network::XmlRpcRequesterClient> {
        if (timeout != 10000)
            throw std::logic_error("Unexpected bridge timeout");
        return std::make_shared<BridgeClient>(fields);
    };
    services.debug = services.error = services.warn = [](const auto&) {};
    ConnectorProfileUserAgent adapter(u"http://home/", std::move(services));
    return adapter.get_info(id);
}
struct AsyncAgent : ProfileUserAgent {
    std::shared_ptr<ProfileUserInfo> get_info(Uuid id) override {
        trace += "userinfo:" + id.str() + ";";
        AsyncFail(6, "userinfo");
        if (X(7))
            return {};
        if (info_kind >= 8) {
            if (X(8))
                return std::make_shared<ProfileUserInfo>();
            return BridgeInfo(id);
        }
        auto info = std::make_shared<ProfileUserInfo>();
        if (X(8))
            return info;
        if (info_kind != 6) {
            std::any value;
            switch (info_kind) {
            case 1:
                value = std::string("1");
                break;
            case 2:
                break;
            case 3:
                value = std::int64_t{1};
                break;
            case 4:
                value = true;
                break;
            case 5:
                value = 1.5;
                break;
            default:
                value = account_data->flags;
            }
            (*info)["user_flags"] = value;
        }
        (*info)["user_created"] =
            info_kind == 7 ? std::any(std::string("1")) : std::any(account_data->created);
        return info;
    }
};
struct AsyncFriends : ProfileFriends {
    bool online(Uuid owner, Uuid id) override {
        trace += "friends:" + owner.str() + ":" + id.str() + ";";
        AsyncFail(20, "friends");
        return X(19);
    }
};
struct AsyncPresenceService : ProfilePresenceService {
    std::shared_ptr<ProfileAgents> agents(const std::vector<std::string>& ids) override {
        trace += "agents:" + ids.at(0) + ";";
        AsyncFail(27, "agents");
        if (X(25))
            return {};
        auto value = std::make_shared<ProfileAgents>();
        value->count = X(26) ? 0 : 1;
        return value;
    }
};
struct AsyncGroups : ProfileRequestGroups {
    std::shared_ptr<ProfileMemberships> memberships(Uuid id) override {
        trace += "members:" + id.str() + ";";
        AsyncFail(16, "members");
        return X(15) ? nullptr : ::memberships;
    }
};
struct AsyncDirectory : Directory {
    void web_failed(Uuid id) override {
        trace += "webfailed:" + id.str() + ";";
        AsyncFail(13, "webfailed");
    }
};
struct AsyncContext : Context {
    std::shared_ptr<AsyncDirectory> directory = std::make_shared<AsyncDirectory>();
    std::shared_ptr<ProfileUserDirectory> users() override {
        return flags & 128 ? nullptr : directory;
    }
    bool allow_web_urls() override {
        return !X(0);
    }
    ProfileText system_agent_born() override {
        return u"01/01/2007 00:00:00";
    }
    std::shared_ptr<ProfileRequestPresence> presence(Uuid id) override {
        auto p = Context::presence(id);
        if (!p)
            return {};
        auto value = std::make_shared<AsyncPresence>();
        value->child = p->child;
        value->npc = p->npc;
        value->current_parcel = p->current_parcel;
        value->deleted = X(33);
        return value;
    }
    Uuid region_scope() override {
        if ((flags & 4194304) || X(4))
            throw std::runtime_error("Object reference not set to an instance of an object.");
        trace += "scope;";
        return Id(8);
    }
    std::shared_ptr<ProfileAccountService> account_service() override {
        if (flags & 4194304)
            throw std::runtime_error("Object reference not set to an instance of an object.");
        trace += "accounts;";
        return X(1) ? nullptr : std::make_shared<AsyncAccounts>();
    }
    std::shared_ptr<ProfileUserAgent> user_agent(const ProfileText& url) override {
        trace += "connector:" + T(url) + ";";
        AsyncFail(5, "connector");
        return std::make_shared<AsyncAgent>();
    }
    std::shared_ptr<ProfileFriends> friends() override {
        if (flags & 4194304)
            throw std::runtime_error("Object reference not set to an instance of an object.");
        return X(18) ? nullptr : std::make_shared<AsyncFriends>();
    }
    std::shared_ptr<ProfilePresenceService> presence_service() override {
        if (flags & 4194304)
            throw std::runtime_error("Object reference not set to an instance of an object.");
        return X(24) ? nullptr : std::make_shared<AsyncPresenceService>();
    }
    std::shared_ptr<ProfileRequestGroups> groups() override {
        return X(14) ? nullptr : std::make_shared<AsyncGroups>();
    }
    bool legacy_profile(std::shared_ptr<UserProfileProperties>& props, const ProfileText& uri) override {
        trace += "legacy:" + DumpProps(props) + ";";
        AsyncFail(10, "legacy");
        if (X(52)) {
            services::OpenProfileClient client(
                uri,
                [&](const network::Value& request, std::u16string_view,
                    const ProfileText& server) -> std::shared_ptr<network::Value> {
                    trace += "legacyrpc:" + request.get("avatar_id").text + ":" + T(server) + ";";
                    if (!X(9))
                        return {};
                    auto value = [](const ProfileText& t) {
                        if (!t)
                            return network::Value::null_reference();
                        network::Value v(utf8(*t));
                        v.sdk_string = std::make_shared<const std::u16string>(*t);
                        return v;
                    };
                    auto row =
                        network::Value::object({{"ProfileUrl", value(text)},
                                                {"AboutText", value(text)},
                                                {"Image", X(54) ? value(text) : network::Value(Id(9).str())},
                                                {"FirstLifeImage", Id(8).str()},
                                                {"wantmask", X(53) ? value(text) : network::Value("-12")},
                                                {"wanttext", value(text)},
                                                {"skillsmask", "17"},
                                                {"languages", value(text)}});
                    network::Value array;
                    array.kind = network::Value::Kind::Array;
                    array.array.push_back(row);
                    return std::make_shared<network::Value>(network::Value::object({{"data", array}}));
                });
            return client.request_avatar_properties(props);
        }
        if (X(11))
            props = FillProps(Id(4));
        if (X(12))
            props.reset();
        if (props)
            props->AboutText = u"legacy";
        return X(9);
    }
    void debug(const ProfileText& value, const std::optional<std::u16string>& error) override {
        trace += "debug:" + T(value) + ":" + T(error) + ";";
        AsyncFail(29, "debug");
    }
    void process_error(const std::exception& error) override {
        trace += "error:" + T(utf16_replacing(error.what())) + ";";
        AsyncFail(28, "processerror");
    }
    void schedule(std::function<void()> work) override {
        trace += "schedule;";
        AsyncFail(30, "schedule");
        if (X(31))
            work();
        else
            jobs.push_back(std::move(work));
    }
    void get_foreign_image(Uuid image, const ProfileText& url, bool local) override {
        if (flags & (4194304 | 67108864))
            throw std::runtime_error("Object reference not set to an instance of an object.");
        trace += "asset:" + image.str() + ":" + T(url) + ":" + (local ? "1" : "0") + ";";
        Fail(134217728, "asset");
    }
    bool request_object(std::shared_ptr<server::JsonRpcMembers>& data, const ProfileText& method,
                        const ProfileText& url, const ProfileText& id) override {
        Uuid g;
        trace += "props-rpc:" + utf8(method.value_or(u"")) + ":" + T(url) + ":" + DumpProps(data) + ":" +
                 (id && try_parse_sdk_uuid(*id, g) && !g.is_zero() ? "1" : "0") + ";";
        last_props = data;
        if (mode == 7)
            throw std::runtime_error("rpc");
        if (mode == 0)
            return false;
        if (mode != 8) {
            auto old = std::dynamic_pointer_cast<UserProfileProperties>(data);
            if (!old)
                throw std::runtime_error("Object reference not set to an instance of an object.");
            auto p = FillProps(old->UserId);
            if (mode == 3 || mode == 4)
                p->UserId = Id(4);
            if (mode == 10)
                p->UserId = {};
            if (mode == 11)
                p->ImageId = {};
            if (mode == 12)
                p->FirstLifeImageId = {};
            if (mode == 3 || mode == 4)
                data = p;
            else
                *old = *p;
        }
        if (mode == 5 || mode == 9)
            data.reset();
        if (mode == 6)
            data = std::make_shared<UserPreferences>();
        last_props = data;
        return mode != 2 && mode != 4 && mode != 9;
    }
};
struct AsyncClient : Client {
    int id;
    bool callback = false;
    explicit AsyncClient(int value) : id(value) {
    }
    Uuid agent_id() override {
        if (id == 1)
            return Client::agent_id();
        trace += "agent:" + Id(id).str() + ";";
        Fail(8192, "agent");
        return Id(id);
    }
    bool is_active() override {
        trace += "active:" + std::to_string(id) + ";";
        AsyncFail(39, "active");
        return active[id];
    }
    std::shared_ptr<ProfileRequestPresence> scene_presence() override {
        return X(21) ? nullptr : std::make_shared<AsyncPresence>();
    }
    void send_properties(const AvatarProfileProperties& p) override {
        auto member = Identity(p.membership);
        trace += "props:" + std::to_string(id) + ":" + p.avatar.str() + ":" + T(p.about) + ":" + T(p.born) +
                 ":" + member + ":" + Bytes(p.membership) + ":" + T(p.first_life_about) + ":" +
                 std::to_string(p.flags) + ":" + p.first_life_image.str() + ":" + p.image.str() + ":" +
                 T(p.url) + ":" + p.partner.str() + ";";
        AsyncFail(40, "props");
        if (X(43)) {
            std::shared_ptr<UserProfileCacheEntry> e;
            if (current->profile_cache().try_get_value(p.avatar, e) && e && e->props) {
                e->props->WantToMask = 99;
                e->props->WebUrl = u"callback";
            }
        }
        if (X(44))
            active[2] = false;
        if (X(47))
            current->profile_cache().clear();
        if (X(46) && !callback) {
            callback = true;
            try {
                current->request_avatar_properties(clients[id], p.avatar);
            } catch (...) {
                callback = false;
                throw;
            }
            callback = false;
        }
    }
    void send_interests(const AvatarProfileInterests& p) override {
        trace += "interests:" + std::to_string(id) + ":" + p.avatar.str() + ":" + std::to_string(p.wants) +
                 ":" + T(p.want_text) + ":" + std::to_string(p.skills) + ":" + T(p.skill_text) + ":" +
                 T(p.languages) + ";";
        AsyncFail(41, "interests");
        if (X(45) && !memberships->empty() && memberships->at(0))
            memberships->at(0)->GroupName = u"mutated group";
    }
    void send_groups(Uuid avatar, std::shared_ptr<ProfileMemberships> groups) override {
        for (auto& g : *groups)
            if (!g)
                throw std::runtime_error("Object reference not set to an instance of an object.");
        auto identity = Identity(groups);
        trace +=
            "groups:" + std::to_string(id) + ":" + avatar.str() + ":" + identity + ":" + Groups(groups) + ";";
        AsyncFail(42, "groups");
    }
};
std::string ClientName(const std::shared_ptr<ProfileRequestClient>& client) {
    return client ? std::to_string(std::dynamic_pointer_cast<AsyncClient>(client)->id) : "0";
}
std::string AsyncState() {
    auto one = [&](int n) {
        std::shared_ptr<UserProfileCacheEntry> e;
        current->profile_cache().try_get_value(Id(n), e);
        if (!e)
            return std::string("N");
        auto identity = Identity(e), props = Identity(e->props), member = Identity(e->membershipType),
             wait = Identity(e->ClientsWaitingProps), groups = Identity(e->avatarGroups);
        std::string rows;
        if (e->ClientsWaitingProps)
            for (auto& c : *e->ClientsWaitingProps) {
                if (!rows.empty())
                    rows += ',';
                rows += ClientName(c);
            }
        return identity + ":" + props + ":" + DumpProps(e->props) + ":" + T(e->born) + ":" + member + ":" +
               Bytes(e->membershipType) + ":" + std::to_string(e->flags) + ":" + wait + ":" + rows + ":" +
               groups + ":" + Groups(e->avatarGroups) + ":" + DumpList(e->picksList);
    };
    std::string rows;
    for (int i = 0; i < 5; ++i) {
        if (!rows.empty())
            rows += ';';
        rows += std::to_string(i) + "=" + one(i);
    }
    auto queue = UserProfileAsyncProbe::queues(*current, [](const auto& r) {
        auto presence = Identity(r.presence);
        return r.agent.str() + ":" + ClientName(r.client) + ":" + presence + ":" + std::to_string(r.type);
    });
    auto input = Identity(input_props), last = Identity(last_props), account = Identity(last_account);
    return std::to_string(current->profile_cache().count()) + ":" + rows + "|" + queue + "|" +
           std::to_string(jobs.size()) + "|" + input + ":" + DumpProps(input_props) + "|" + last + ":" +
           DumpProps(last_props) + "|" + T(message) + "|" + account + ":" +
           (last_account ? std::to_string(last_account->flags) + ":" + std::to_string(last_account->created) +
                               ":" + T(last_account->title)
                         : "N");
}
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string title, u, n;
        int aflags, created;
        in >> mode >> flags >> extra >> aflags >> created >> title >> u >> n >> info_kind;
        uri = Decode(u);
        text = Decode(n);
        asset_uri = uri;
        account_data = std::make_shared<ProfileAccountData>();
        account_data->flags = aflags;
        account_data->created = created;
        account_data->title = Decode(title);
        trace.clear();
        cache_now = 0;
        saved.reset();
        identities.clear();
        retained.clear();
        active = {false, true, true, true, true};
        clients = {};
        for (int i = 1; i < 5; ++i)
            clients[i] = std::make_shared<AsyncClient>(i);
        jobs.clear();
        memberships = std::make_shared<ProfileMemberships>();
        if (X(17))
            memberships->push_back(nullptr);
        else {
            auto g = std::make_shared<GroupMembershipData>();
            g->GroupID = Id(7);
            g->GroupName = text;
            g->GroupTitle = text;
            g->GroupPowers = UINT64_MAX;
            g->ListInProfile = true;
            memberships->push_back(g);
        }
        last_props.reset();
        last_account.reset();
        input_props = FillProps(Id(1));
        message = u"initial";
        auto context = std::make_shared<AsyncContext>();
        ExpiringCacheBackend backend{[] { return cache_now; },
                                     [](auto, int due) { return std::make_shared<PicksTimer>(due); }};
        current = std::make_shared<AsyncModule>(context, backend);
        active_module = current.get();
        std::string output;
        for (std::string token; in >> token;) {
            std::string result = "OK";
            try {
                if (token[0] == 't')
                    cache_now = std::stod(token.substr(1));
                else if (token.size() == 2 && token[1] >= '0' && token[1] <= '4') {
                    int id = token[1] - '0';
                    auto client = clients[id];
                    switch (token[0]) {
                    case 'R':
                        current->request_avatar_properties(client, Id(3));
                        break;
                    case 'T':
                        current->request_avatar_properties(client, Id(4));
                        break;
                    case 'O':
                        current->request_avatar_properties(client, Id(1));
                        break;
                    case 'Z':
                        current->request_avatar_properties(client, {});
                        break;
                    case 'S':
                        current->request_avatar_properties(client,
                                                           Uuid("11111111-1111-0000-0000-000100bba000"));
                        break;
                    case 'D':
                        current->process_request({client, nullptr, Id(3), 0});
                        break;
                    case 'J':
                        current->process_request({client, nullptr, Id(3), 1});
                        break;
                    case 'I':
                        active[id] = false;
                        break;
                    case 'V':
                        active[id] = true;
                        break;
                    case 'U':
                        current->avatar_interests_update(client, UINT32_MAX, text, 0x80000000, text, text);
                        break;
                    case 'P':
                        current->avatar_properties_update(client, input_props);
                        break;
                    case 'F':
                        result = current->UserProfileRequests::is_friend_online(client, Id(3)) ? "1" : "0";
                        break;
                    }
                } else if (token == "L")
                    Hold(false);
                else if (token == "Y")
                    Hold(true);
                else if (token == "l")
                    Release();
                else if (token == "W") {
                    if (!jobs.empty()) {
                        auto job = std::move(jobs.front());
                        jobs.erase(jobs.begin());
                        job();
                    }
                } else if (token == "A") {
                    for (int i = 0; !jobs.empty() && i < 32; ++i) {
                        auto job = std::move(jobs.front());
                        jobs.erase(jobs.begin());
                        job();
                    }
                } else if (token == "C")
                    current->profile_cache().clear();
                else if (token == "K")
                    input_props.reset();
                else if (token == "M")
                    input_props = FillProps(Id(1));
                else if (token == "G")
                    result = current->get_profile_data(input_props, flags & 16, uri, message) ? "1" : "0";
                else if (token == "H")
                    result = current->get_user_account_data(Id(3), last_account) ? "1" : "0";
                else if (token == "Q")
                    ExpiringCacheProbe<Uuid, std::shared_ptr<UserProfileCacheEntry>, UuidHash>::purge(
                        current->profile_cache());
                else if (token == "N")
                    current->profile_cache().add_or_update(Id(3), nullptr, 300.0);
                else {
                    auto e = std::make_shared<UserProfileCacheEntry>();
                    e->flags = 39;
                    e->born = u"seed";
                    e->membershipType = std::make_shared<std::vector<std::uint8_t>>(
                        std::initializer_list<std::uint8_t>{1, 2, 0});
                    e->picksList = std::make_shared<UserProfileNameList>();
                    (*e->picksList)[Id(7)] = u"other";
                    if (token == "E") {
                        e->props = FillProps(Id(3));
                        e->avatarGroups = memberships;
                    }
                    if (token == "B") {
                        e->ClientsWaitingProps = std::make_shared<ProfileClientSet>();
                        e->ClientsWaitingProps->insert(clients[2]);
                        e->ClientsWaitingProps->insert(clients[1]);
                    }
                    current->profile_cache().add_or_update(Id(3), e, 300.0);
                }
            } catch (const std::exception& error) {
                result = "EX:" + H(error.what());
            }
            if (!output.empty())
                output += '/';
            output += result + ":" + AsyncState();
        }
        std::cout << output << "#" << H(trace) << "\n";
        Release();
        jobs.clear();
        current.reset();
    }
}
