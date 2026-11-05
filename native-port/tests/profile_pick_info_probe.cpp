// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "profile_list_fixture.hpp"
#include "opensim/framework/vector3d_text.hpp"
#include <bit>
namespace {
int seams, local_grid, region_x, region_y;
ProfileText position, gate;
Uuid parcel_id, target_id, query_id;
std::shared_ptr<server::JsonRpcMembers> last_pick;
std::shared_ptr<UserProfilePick> held_pick;
void PickFail(int mask, const char* message) {
    if (seams & mask)
        throw std::runtime_error(message);
}
std::string DoubleBits(double value) {
    std::ostringstream s;
    s << std::hex << std::setfill('0') << std::setw(16) << std::bit_cast<std::uint64_t>(value);
    return s.str();
}
std::shared_ptr<UserProfilePick> FillPick(Uuid id) {
    auto p = std::make_shared<UserProfilePick>();
    p->PickId = id;
    p->CreatorId = Id(7);
    p->TopPick = true;
    p->Name = text;
    p->OriginalName = text;
    p->Desc = text;
    p->ParcelId = parcel_id;
    p->SnapshotId = Id(9);
    p->ParcelName = text;
    p->SimName = text;
    p->GlobalPos = position;
    p->Gatekeeper = gate;
    p->SortOrder = INT_MIN;
    p->Enabled = true;
    return p;
}
std::string DumpPick(const std::shared_ptr<server::JsonRpcMembers>& value) {
    if (!value)
        return "N";
    auto p = std::dynamic_pointer_cast<UserProfilePick>(value);
    if (!p)
        return "X";
    return p->PickId.str() + ":" + p->CreatorId.str() + ":" + (p->TopPick ? "1" : "0") + ":" + T(p->Name) +
           ":" + T(p->OriginalName) + ":" + T(p->Desc) + ":" + p->ParcelId.str() + ":" + p->SnapshotId.str() +
           ":" + T(p->ParcelName) + ":" + T(p->SimName) + ":" + T(p->GlobalPos) + ":" + T(p->Gatekeeper) +
           ":" + std::to_string(p->SortOrder) + ":" + (p->Enabled ? "1" : "0");
}
struct PickHomeGrid : ProfileHomeGrid {
    ProfileText gatekeeper_url() override {
        throw std::logic_error("unexpected gatekeeper");
    }
    int is_local_grid(const ProfileText& value, bool resolve) override {
        trace += "isgrid:" + T(value) + ":" + (resolve ? "1" : "0") + ";";
        PickFail(1, "isgrid");
        return local_grid;
    }
};
struct PickGridService : ProfileRequestGridService {
    std::shared_ptr<ProfileRequestGridRegion> region_by_name(Uuid scope, const ProfileText& name) override {
        trace += "region:" + scope.str() + ":" + T(name) + ";";
        PickFail(32, "region");
        if (seams & 64)
            return {};
        auto r = std::make_shared<ProfileRequestGridRegion>();
        r->x = region_x;
        r->y = region_y;
        return r;
    }
};
struct PickContext : PicksContext {
    PickContext() {
        home = std::make_shared<PickHomeGrid>();
    }
    std::shared_ptr<ProfileRequestGridService> grid_service() override {
        if (flags & 4194304)
            throw std::runtime_error("Object reference not set to an instance of an object.");
        trace += "gridservice;";
        PickFail(2, "gridservice");
        return seams & 4 ? nullptr : std::make_shared<PickGridService>();
    }
    Uuid region_scope() override {
        if ((flags & 4194304) || (seams & 8))
            throw std::runtime_error("Object reference not set to an instance of an object.");
        trace += "scope;";
        PickFail(16, "scope");
        return Id(8);
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
        trace += "pick-rpc:" + utf8(method.value_or(u"")) + ":" + T(url) + ":" + DumpPick(data) + ":" +
                 (id && try_parse_sdk_uuid(*id, g) && !g.is_zero() ? "1" : "0") + ";";
        last_pick = data;
        if (mode == 7)
            throw std::runtime_error("rpc");
        if (mode == 0)
            return false;
        if (mode != 8) {
            auto p = std::dynamic_pointer_cast<UserProfilePick>(data);
            auto fill = FillPick(p->PickId);
            if (mode == 3 || mode == 4)
                fill->PickId = Id(4);
            if (mode == 10)
                fill->CreatorId = {};
            if (mode == 11)
                fill->SnapshotId = {};
            if (mode == 12)
                fill->GlobalPos = u"bad";
            if (mode == 13)
                fill->PickId = {};
            if (mode == 14)
                fill->ParcelId = {};
            if (mode == 15)
                fill->ParcelId = Id(5);
            if (mode == 16)
                fill->GlobalPos.reset();
            if (mode == 17)
                fill->Gatekeeper.reset();
            if (mode == 3 || mode == 4)
                data = fill;
            else
                *p = *fill;
        }
        if (mode == 5 || mode == 9)
            data.reset();
        if (mode == 6)
            data = std::make_shared<UserPreferences>();
        last_pick = data;
        return mode != 2 && mode != 4 && mode != 9;
    }
};
struct PickClient : PicksClient {
    void send_pick_info(const AvatarPickInfo& p) override {
        trace += "pick:" + p.pick.str() + ":" + p.creator.str() + ":" + (p.top_pick ? "1" : "0") + ":" +
                 p.parcel.str() + ":" + T(p.name) + ":" + T(p.description) + ":" + p.snapshot.str() + ":" +
                 T(p.user) + ":" + T(p.original_name) + ":" + T(p.sim_name) + ":" +
                 DoubleBits(p.global_position[0]) + ":" + DoubleBits(p.global_position[1]) + ":" +
                 DoubleBits(p.global_position[2]) + ":" + std::to_string(p.sort_order) + ":" +
                 (p.enabled ? "1" : "0") + ";";
        if (seams & 256)
            if (auto ad = std::dynamic_pointer_cast<UserProfilePick>(last_pick))
                ad->Name = u"callback";
        if (seams & 512) {
            auto again = active_module->get_pick(target_id, query_id);
            auto identity = Identity(again);
            trace += "again:" + identity + ":" + DumpPick(again) + ";";
        }
        Fail(268435456, "pick");
    }
};
std::string PickState(UserProfileRequests& m) {
    auto one = [&](Uuid key) {
        std::shared_ptr<UserProfileCacheEntry> e;
        m.profile_cache().try_get_value(key, e);
        auto count = std::to_string(m.profile_cache().count());
        if (!e)
            return count + ":N";
        auto identity = Identity(e);
        std::string rows;
        if (e->picks)
            for (auto& row : *e->picks) {
                if (!rows.empty())
                    rows += ',';
                auto row_id = Identity(row.second);
                rows += row.first.str() + "=" + row_id + ":" + DumpPick(row.second);
            }
        return count + ":" + identity + ":" + std::to_string(e->flags) + ":" + T(e->born) + ":" +
               DumpList(e->picksList) + ":" + DumpList(e->classifiedsLists) + ":" +
               (e->props ? e->props->UserId.str() : "N") + ":" + (e->picks ? rows : "N");
    };
    auto a = one(target_id), b = one(Id(2)), saved_id = Identity(held_pick), last_id = Identity(last_pick);
    return a + ";" + b + ";" + saved_id + ":" + DumpPick(held_pick) + ";" + last_id + ":" +
           DumpPick(last_pick);
}
}
#ifndef OPENSIM_PROFILE_PICK_INFO_NO_MAIN
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string u, t, q, n, p, g, a, id;
        in >> mode >> flags >> seams >> local_grid >> region_x >> region_y >> u >> t >> q >> n >> p >> g >>
            a >> id;
        uri = Decode(u);
        auto target = Decode(t), query = Decode(q);
        target_id = {};
        query_id = {};
        parcel_id = {};
        if (target)
            try_parse_sdk_uuid(*target, target_id);
        if (query)
            try_parse_sdk_uuid(*query, query_id);
        auto parcel = Decode(id);
        if (parcel)
            try_parse_sdk_uuid(*parcel, parcel_id);
        text = Decode(n);
        position = Decode(p);
        gate = Decode(g);
        asset_uri = Decode(a);
        trace.clear();
        last_pick.reset();
        held_pick.reset();
        saved.reset();
        cache_now = 0;
        identities.clear();
        retained.clear();
        auto context = std::make_shared<PickContext>();
        ExpiringCacheBackend backend{[] { return cache_now; },
                                     [](auto, int due) { return std::make_shared<PicksTimer>(due); }};
        UserProfileRequests m(context, backend);
        active_module = &m;
        std::shared_ptr<ProfileRequestClient> client = flags & 1 ? nullptr : std::make_shared<PickClient>();
        std::string output;
        for (std::string token; in >> token;) {
            std::string result = "OK";
            try {
                if (token[0] == 't')
                    cache_now = std::stod(token.substr(1));
                else if (token[0] == 'm')
                    mode = std::stoi(token.substr(1));
                else if (token == "R") {
                    std::vector<ProfileText> args;
                    if (!(flags & 8)) {
                        args.push_back(target);
                        if (!(flags & 2097152))
                            args.push_back(query);
                    }
                    std::shared_ptr<AdvancedProfileClient> sender =
                        flags & 2 ? std::make_shared<AdvancedProfileClient>() : client;
                    m.pick_info_request(sender, flags & 4 ? nullptr : &args);
                } else if (token == "G")
                    held_pick = m.get_pick(target_id, query_id);
                else if (token == "H")
                    held_pick = m.get_pick(target_id, Id(4));
                else if (token == "M") {
                    if (!held_pick)
                        throw std::runtime_error("Object reference not set to an instance of an object.");
                    held_pick->Name = u"mutated";
                    held_pick->GlobalPos = position;
                    held_pick->ParcelId = parcel_id;
                } else if (token == "C")
                    m.profile_cache().clear();
                else if (token == "P")
                    ExpiringCacheProbe<Uuid, std::shared_ptr<UserProfileCacheEntry>, UuidHash>::purge(
                        m.profile_cache());
                else if (token == "S")
                    m.profile_cache().add_or_update(target_id, nullptr, 300.0);
                else {
                    auto e = std::make_shared<UserProfileCacheEntry>();
                    e->flags = 55;
                    e->born = u"seed";
                    e->picksList = std::make_shared<UserProfileNameList>();
                    (*e->picksList)[Id(7)] = u"other";
                    e->classifiedsLists = std::make_shared<UserProfileNameList>();
                    (*e->classifiedsLists)[Id(9)] = u"ad";
                    e->props = std::make_shared<UserProfileProperties>();
                    e->props->UserId = Id(7);
                    e->picks = std::make_shared<
                        OrderedDictionary<Uuid, std::shared_ptr<UserProfilePick>, UuidHash>>();
                    auto seed = FillPick(query_id);
                    if (token == "D")
                        seed->GlobalPos = u"bad";
                    if (token != "W")
                        (*e->picks)[query_id] = token == "E" ? nullptr : seed;
                    m.profile_cache().add_or_update(token == "B" ? Id(2) : target_id, e, 300.0);
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

#endif
