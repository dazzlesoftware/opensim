// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "profile_list_fixture.hpp"
#include "opensim/framework/vector_text.hpp"
#include <bit>
namespace {
ProfileText position;
std::shared_ptr<server::JsonRpcMembers> last;
std::string Bits(float f) {
    std::ostringstream s;
    s << std::hex << std::setfill('0') << std::setw(8) << std::bit_cast<std::uint32_t>(f);
    return s.str();
}
std::shared_ptr<UserClassifiedAdd> Fill(Uuid query) {
    auto a = std::make_shared<UserClassifiedAdd>();
    a->ClassifiedId = query;
    a->CreatorId = Id(7);
    a->CreationDate = -1;
    a->ExpirationDate = INT_MAX;
    a->Category = INT_MIN;
    a->Name = text;
    a->Description = text;
    a->ParcelId = Id(5);
    a->ParentEstate = -7;
    a->SnapshotId = Id(9);
    a->SimName = text;
    a->GlobalPos = position;
    a->ParcelName = text;
    a->Flags = 128;
    a->Price = INT_MIN;
    return a;
}
std::string Ad(const std::shared_ptr<server::JsonRpcMembers>& value) {
    if (!value)
        return "N";
    auto a = std::dynamic_pointer_cast<UserClassifiedAdd>(value);
    if (!a)
        return "X";
    return a->ClassifiedId.str() + ":" + a->CreatorId.str() + ":" + std::to_string(a->CreationDate) + ":" +
           std::to_string(a->ExpirationDate) + ":" + std::to_string(a->Category) + ":" + T(a->Name) + ":" +
           T(a->Description) + ":" + a->ParcelId.str() + ":" + std::to_string(a->ParentEstate) + ":" +
           a->SnapshotId.str() + ":" + T(a->SimName) + ":" + T(a->GlobalPos) + ":" + T(a->ParcelName) + ":" +
           std::to_string(a->Flags) + ":" + std::to_string(a->Price);
}
struct InfoClient : Client {
    void send_classified_info(const AvatarClassifiedInfo& a) override {
        trace += "detail:" + a.classified.str() + ":" + a.creator.str() + ":" + std::to_string(a.created) +
                 ":" + std::to_string(a.expires) + ":" + std::to_string(a.category) + ":" + T(a.name) + ":" +
                 T(a.description) + ":" + a.parcel.str() + ":" + std::to_string(a.parent_estate) + ":" +
                 a.snapshot.str() + ":" + T(a.sim_name) + ":" + Bits(a.global_position[0]) + ":" +
                 Bits(a.global_position[1]) + ":" + Bits(a.global_position[2]) + ":" + T(a.parcel_name) +
                 ":" + std::to_string(a.flags) + ":" + std::to_string(a.price) + ";";
        Fail(268435456, "details");
    }
};
struct InfoContext : PicksContext {
    void get_foreign_image(Uuid image, const ProfileText& url, bool local) override {
        if (flags & (4194304 | 67108864))
            throw std::runtime_error("Object reference not set to an instance of an object.");
        trace += "asset:" + image.str() + ":" + T(url) + ":" + (local ? "1" : "0") + ";";
        Fail(134217728, "asset");
    }
    bool request_object(std::shared_ptr<server::JsonRpcMembers>& data, const ProfileText& method,
                        const ProfileText& url, const ProfileText& id) override {
        Uuid guid;
        trace += "ad-rpc:" + utf8(method.value_or(u"")) + ":" + T(url) + ":" + Ad(data) + ":" +
                 (id && try_parse_sdk_uuid(*id, guid) && !guid.is_zero() ? "1" : "0") + ";";
        last = data;
        if (mode == 7)
            throw std::runtime_error("rpc");
        if (mode == 0)
            return false;
        if (mode != 8) {
            auto a = std::dynamic_pointer_cast<UserClassifiedAdd>(data);
            auto fill = Fill(a->ClassifiedId);
            if (mode == 3 || mode == 4)
                fill->ClassifiedId = Id(4);
            if (mode == 10)
                fill->CreatorId = {};
            if (mode == 11)
                fill->SnapshotId = {};
            if (mode == 12)
                fill->GlobalPos = u"bad";
            if (mode == 13)
                fill->ClassifiedId = {};
            if (mode == 14)
                fill->CreatorId = Id(2);
            if (mode == 3 || mode == 4)
                data = fill;
            else
                *a = *fill;
        }
        if (mode == 5 || mode == 9)
            data.reset();
        if (mode == 6)
            data = std::make_shared<UserPreferences>();
        last = data;
        return mode != 2 && mode != 4 && mode != 9;
    }
};
std::string InfoState(UserProfileRequests& m) {
    auto one = [&](Uuid id) {
        std::shared_ptr<UserProfileCacheEntry> e;
        m.profile_cache().try_get_value(id, e);
        auto count = std::to_string(m.profile_cache().count());
        if (!e)
            return count + ":N";
        auto head = count + ":" + Identity(e) + ":" + std::to_string(e->flags) + ":" + T(e->born) + ":" +
                    DumpList(e->picksList) + ":" + DumpList(e->classifiedsLists) + ":";
        std::string rows;
        if (e->classifieds)
            for (auto& pair : *e->classifieds) {
                if (!rows.empty())
                    rows += ',';
                auto identity = Identity(pair.second);
                rows += pair.first.str() + "=" + identity + ":" + Ad(pair.second);
            }
        return head + (e->classifieds ? rows : "N");
    };
    auto a = one(Id(1)), b = one(Id(2));
    std::string owners, counts;
    for (auto& p : m.classified_owners()) {
        if (!owners.empty())
            owners += ',';
        owners += p.first.str() + "=" + p.second.str();
    }
    for (auto& p : m.classified_interests()) {
        if (!counts.empty())
            counts += ',';
        counts += p.first.str() + "=" + std::to_string(p.second);
    }
    auto identity = Identity(last);
    return a + ";" + b + ";" + owners + ";" + counts + ";" + identity + ":" + Ad(last);
}
}
#ifndef OPENSIM_PROFILE_DETAIL_NO_MAIN
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string u, q, n, p, a;
        in >> mode >> flags >> u >> q >> n >> p >> a;
        uri = Decode(u);
        auto arg = Decode(q);
        text = Decode(n);
        position = Decode(p);
        asset_uri = Decode(a);
        trace.clear();
        last.reset();
        cache_now = 0;
        identities.clear();
        retained.clear();
        auto context = std::make_shared<InfoContext>();
        ExpiringCacheBackend backend{[] { return cache_now; },
                                     [](auto, int due) { return std::make_shared<PicksTimer>(due); }};
        UserProfileRequests m(context, backend);
        active_module = &m;
        std::shared_ptr<ProfileRequestClient> client = flags & 1 ? nullptr : std::make_shared<InfoClient>();
        Uuid query;
        if (arg)
            try_parse_sdk_uuid(*arg, query);
        std::string output;
        for (std::string token; in >> token;) {
            std::string result = "OK";
            try {
                if (token[0] == 't')
                    cache_now = std::stod(token.substr(1));
                else if (token[0] == 'm')
                    mode = std::stoi(token.substr(1));
                else if (token == "X")
                    m.classified_delete(query, client);
                else if (token == "Q" || token == "4" || token == "0")
                    m.classified_info_request(token == "Q" ? query : token == "4" ? Id(4) : Uuid{}, client);
                else if (token == "F")
                    m.cache_foreign_image(Id(2), query);
                else if (token == "L") {
                    std::vector<ProfileText> args{utf16(Id(2).str())};
                    m.classifieds_request(client, &args);
                } else if (token == "C")
                    m.profile_cache().clear();
                else if (token == "Z") {
                    m.classified_owners().clear();
                    m.classified_interests().clear();
                } else if (token == "P")
                    ExpiringCacheProbe<Uuid, std::shared_ptr<UserProfileCacheEntry>, UuidHash>::purge(
                        m.profile_cache());
                else if (token == "I" || token == "J" || token == "T" || token == "K") {
                    m.classified_owners()[query] = Id(2);
                    if (token == "I")
                        m.classified_interests().erase(query);
                    else
                        m.classified_interests()[query] = token == "J" ? 0 : token == "T" ? INT_MIN : 2;
                } else if (token == "M") {
                    if (auto ad = std::dynamic_pointer_cast<UserClassifiedAdd>(last)) {
                        ad->Name = u"mutated";
                        ad->GlobalPos = position;
                    }
                } else if (token == "S")
                    m.profile_cache().add_or_update(Id(1), nullptr, 300.0);
                else {
                    auto target = token == "B" ? Id(2) : Id(1);
                    auto e = std::make_shared<UserProfileCacheEntry>();
                    e->flags = 55;
                    e->born = u"seed";
                    e->picksList = std::make_shared<UserProfileNameList>();
                    (*e->picksList)[Id(7)] = u"other";
                    e->classifiedsLists = std::make_shared<UserProfileNameList>();
                    (*e->classifiedsLists)[Id(8)] = u"seed list";
                    e->classifieds = std::make_shared<
                        OrderedDictionary<Uuid, std::shared_ptr<UserClassifiedAdd>, UuidHash>>();
                    auto seed = Fill(query);
                    if (token == "D")
                        seed->GlobalPos = u"bad";
                    (*e->classifieds)[query] = token == "E" ? nullptr : seed;
                    m.profile_cache().add_or_update(target, e, 300.0);
                    if (token == "B") {
                        m.classified_owners()[query] = target;
                        m.classified_interests()[query] = 1;
                    }
                }
            } catch (const std::exception& e) {
                result = "EX:" + H(e.what());
            }
            if (!output.empty())
                output += '/';
            output += result + ":" + InfoState(m);
        }
        std::cout << output << "|" << H(trace) << "\n";
    }
}

#endif
