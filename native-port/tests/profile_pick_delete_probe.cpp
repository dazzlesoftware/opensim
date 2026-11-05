// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "profile_list_fixture.hpp"
namespace {
std::string DeleteState(UserProfileRequests& m) {
    auto one = [&](Uuid key) {
        std::shared_ptr<UserProfileCacheEntry> e;
        m.profile_cache().try_get_value(key, e);
        auto count = std::to_string(m.profile_cache().count());
        if (!e)
            return count + ":N";
        auto identity = Identity(e), list_id = Identity(e->picksList);
        std::string rows;
        if (e->picks)
            for (auto& row : *e->picks) {
                if (!rows.empty())
                    rows += ',';
                auto item_id = Identity(row.second);
                rows += row.first.str() + "=" + item_id + ":" +
                        (row.second ? row.second->PickId.str() + ":" + T(row.second->Name) : "N");
            }
        auto props = Identity(e->props), ads = Identity(e->classifiedsLists);
        return count + ":" + identity + ":" + std::to_string(e->flags) + ":" + T(e->born) + ":" + list_id +
               ":" + DumpList(e->picksList) + ":" + (e->picks ? rows : "N") + ":" + props + ":" +
               (e->props ? e->props->UserId.str() : "N") + ":" + ads + ":" + DumpList(e->classifiedsLists);
    };
    auto a = one(Id(1)), b = one(Id(2)), saved_id = Identity(saved);
    return a + ";" + b + ";" + saved_id + ":" + DumpList(saved);
}
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string u, q, n;
        in >> mode >> flags >> u >> q >> n;
        uri = Decode(u);
        auto arg = Decode(q);
        text = Decode(n);
        Uuid query;
        if (arg)
            try_parse_sdk_uuid(*arg, query);
        trace.clear();
        cache_now = 0;
        saved.reset();
        identities.clear();
        retained.clear();
        auto context = std::make_shared<PicksContext>();
        ExpiringCacheBackend backend{[] { return cache_now; },
                                     [](auto, int due) { return std::make_shared<PicksTimer>(due); }};
        UserProfileRequests m(context, backend);
        active_module = &m;
        std::shared_ptr<ProfileRequestClient> client = flags & 1 ? nullptr : std::make_shared<PicksClient>();
        std::string output;
        for (std::string token; in >> token;) {
            std::string result = "OK";
            try {
                if (token[0] == 't')
                    cache_now = std::stod(token.substr(1));
                else if (token[0] == 'm')
                    mode = std::stoi(token.substr(1));
                else if (token == "D" || token == "Z" || token == "0")
                    m.pick_delete(client, token == "D" ? query : token == "Z" ? Id(4) : Uuid{});
                else if (token == "G")
                    saved = m.get_picks(Id(1));
                else if (token == "R") {
                    std::vector<ProfileText> args{utf16(Id(1).str())};
                    m.picks_request(client, &args);
                } else if (token == "N")
                    m.profile_cache().add_or_update(Id(1), nullptr, 300.0);
                else if (token == "C")
                    m.profile_cache().clear();
                else if (token == "M") {
                    if (!saved)
                        throw std::runtime_error("Object reference not set to an instance of an object.");
                    (*saved)[Id(8)] = text;
                } else if (token == "W") {
                    if (!saved)
                        throw std::runtime_error("Object reference not set to an instance of an object.");
                    saved->clear();
                } else if (token == "P")
                    ExpiringCacheProbe<Uuid, std::shared_ptr<UserProfileCacheEntry>, UuidHash>::purge(
                        m.profile_cache());
                else {
                    auto e = std::make_shared<UserProfileCacheEntry>();
                    e->flags = 55;
                    e->born = u"seed";
                    e->props = std::make_shared<UserProfileProperties>();
                    e->props->UserId = Id(7);
                    e->classifiedsLists = std::make_shared<UserProfileNameList>();
                    (*e->classifiedsLists)[Id(9)] = u"ad";
                    if (token != "L") {
                        e->picksList = std::make_shared<UserProfileNameList>();
                        e->picks = std::make_shared<
                            OrderedDictionary<Uuid, std::shared_ptr<UserProfilePick>, UuidHash>>();
                        if (token != "E") {
                            (*e->picksList)[query] = text;
                            (*e->picksList)[Id(4)] = u"second";
                            auto pick = std::make_shared<UserProfilePick>();
                            pick->PickId = query;
                            pick->Name = text;
                            (*e->picks)[query] = pick;
                            (*e->picks)[Id(4)] = nullptr;
                        }
                    }
                    m.profile_cache().add_or_update(token == "B" ? Id(2) : Id(1), e, 300.0);
                }
            } catch (const std::exception& error) {
                result = "EX:" + H(error.what());
            }
            if (!output.empty())
                output += '/';
            output += result + ":" + DeleteState(m);
        }
        std::cout << output << "|" << H(trace) << "\n";
    }
}
