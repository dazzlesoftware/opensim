#pragma once
// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "profile_requests_fixture.hpp"
#include <unordered_map>
namespace {
UserProfileRequests* active_module = nullptr;
double cache_now = 0;
std::shared_ptr<UserProfileNameList> saved;
std::unordered_map<const void*, int> identities;
std::vector<std::shared_ptr<void>> retained;
template <class T> std::string Identity(std::shared_ptr<T> value) {
    if (!value)
        return "N";
    auto found = identities.find(value.get());
    if (found != identities.end())
        return std::to_string(found->second);
    int id = static_cast<int>(identities.size() + 1);
    identities[value.get()] = id;
    retained.push_back(std::move(value));
    return std::to_string(id);
}
std::string DumpList(const std::shared_ptr<UserProfileNameList>& value) {
    if (!value)
        return "N";
    std::string out;
    for (const auto& item : *value) {
        if (!out.empty())
            out += ',';
        out += item.first.str() + "=" + T(item.second);
    }
    return out;
}
struct PicksTimer : ExpiringCacheTimer {
    explicit PicksTimer(int due) {
        trace += "T:" + std::to_string(due) + ";";
    }
    void change(int due) override {
        trace += "TC:" + std::to_string(due) + ";";
    }
    void dispose() override {
        trace += "TD;";
    }
};
struct PicksClient : Client {
    void send_picks(Uuid target, std::shared_ptr<UserProfileNameList> value) override {
        trace += "picks:" + target.str() + ":" + DumpList(value) + ":" + Identity(value) + ";";
        saved = value;
        if (flags & 16777216)
            (*value)[Id(9)] = text;
        if (flags & 33554432) {
            auto again = active_module->get_picks(target);
            trace += "again:" + Identity(again) + ":" + DumpList(again) + ";";
        }
        Fail(16384, "picks");
    }
};
network::Value UuidValue(Uuid id) {
    network::Value v;
    v.kind = network::Value::Kind::Uuid;
    v.text = id.str();
    return v;
}
network::Value TextValue(const ProfileText& input) {
    network::Value v(utf8(input.value_or(u"")));
    v.sdk_string = std::make_shared<const std::u16string>(input.value_or(u""));
    return v;
}
network::Value Row(Uuid id, ProfileText name, const char* id_key = "pickuuid") {
    network::Value result;
    result.kind = network::Value::Kind::Map;
    result.set(id_key, UuidValue(id));
    result.set("name", TextValue(name));
    return result;
}
struct PicksContext : Context {
    bool request(std::shared_ptr<network::Value>& data, const ProfileText& method, const ProfileText& url,
                 const ProfileText& id) override {
        const char* id_key =
            method == ProfileText(u"avatarclassifiedsrequest") ? "classifieduuid" : "pickuuid";
        Uuid parsed;
        auto valid = id && try_parse_sdk_uuid(*id, parsed) && !parsed.is_zero();
        trace += "osd:" + utf8(method.value_or(u"")) + ":" + T(url) + ":" +
                 network::osd_uuid(data->get(method == ProfileText(u"picks_delete") ? "pickId" : "creatorId"))
                     .str() +
                 ":" + (valid ? "1" : "0") + ";";
        if (mode == 7)
            throw std::runtime_error("rpc");
        if (mode == 0)
            return false;
        if (mode == 4) {
            data = std::make_shared<network::Value>();
            data->kind = network::Value::Kind::Array;
            return true;
        }
        if (mode == 5) {
            data.reset();
            return true;
        }
        data = std::make_shared<network::Value>();
        data->kind = network::Value::Kind::Map;
        if (mode == 2)
            return true;
        if (mode == 3) {
            data->set("result", network::Value("bad"));
            return true;
        }
        if (mode == 17) {
            data->set("result", network::Value::null_reference());
            return true;
        }
        network::Value list;
        list.kind = network::Value::Kind::Array;
        if (mode == 6)
            list.array.push_back(network::Value::null_reference());
        else if (mode == 8) {
            list.array.push_back(Row(Id(3), u"old", id_key));
            list.array.push_back(Row(Id(3), text, id_key));
            network::Value empty;
            empty.kind = network::Value::Kind::Map;
            list.array.push_back(empty);
        } else if (mode == 10) {
            network::Value item;
            item.kind = network::Value::Kind::Map;
            item.set(id_key, network::Value("bad"));
            item.set("name", network::Value(std::int32_t(42)));
            list.array.push_back(item);
        } else if (mode == 11)
            list.array.push_back(network::Value("bad"));
        else if (mode == 13) {
            list.array.push_back(Row(Id(3), text, id_key));
            list.array.push_back(network::Value(std::int32_t(7)));
        } else if (mode == 14) {
            network::Value first;
            first.kind = network::Value::Kind::Map;
            first.set("name", TextValue(text));
            list.array.push_back(first);
            network::Value second;
            second.kind = network::Value::Kind::Map;
            second.set(id_key, network::Value(Id(4).str()));
            list.array.push_back(second);
        } else if (mode != 12) {
            list.array.push_back(Row(Id(3), text, id_key));
            list.array.push_back(Row(Id(4), u"second", id_key));
        }
        data->set("result", std::move(list));
        return mode != 9;
    }
};
std::string Group(const GroupMembershipData& g) {
    return g.GroupID.str() + ":" + T(g.GroupName) + ":" + (g.AllowPublish ? "1" : "0") + ":" +
           (g.MaturePublish ? "1" : "0") + ":" + T(g.Charter) + ":" + g.FounderID.str() + ":" +
           g.GroupPicture.str() + ":" + std::to_string(g.MembershipFee) + ":" +
           (g.OpenEnrollment ? "1" : "0") + ":" + (g.ShowInList ? "1" : "0") + ":" +
           (g.AcceptNotices ? "1" : "0") + ":" + std::to_string(g.Contribution) + ":" +
           std::to_string(g.GroupPowers) + ":" + (g.Active ? "1" : "0") + ":" + g.ActiveRole.str() + ":" +
           (g.ListInProfile ? "1" : "0") + ":" + T(g.GroupTitle);
}
std::string State(UserProfileRequests& module, Uuid target) {
    std::shared_ptr<UserProfileCacheEntry> e;
    module.profile_cache().try_get_value(target, e);
    if (!e)
        return std::to_string(module.profile_cache().count()) + ":N";
    auto entry_id = Identity(e);
    int mask = (e->picksList ? 1 : 0) | (e->picks ? 2 : 0) | (e->classifiedsLists ? 4 : 0) |
               (e->classifieds ? 8 : 0) | (e->props ? 16 : 0) | (e->avatarGroups ? 32 : 0) |
               (e->born ? 64 : 0) | (e->membershipType ? 128 : 0) | (e->ClientsWaitingProps ? 256 : 0);
    std::string bytes;
    if (e->membershipType)
        for (auto c : *e->membershipType) {
            bytes += "0123456789abcdef"[c >> 4];
            bytes += "0123456789abcdef"[c & 15];
        }
    auto list_id = Identity(e->picksList);
    return std::to_string(module.profile_cache().count()) + ":" + entry_id + ":" + std::to_string(mask) +
           ":" + std::to_string(e->flags) + ":" + T(e->born) + ":" +
           (e->props ? e->props->UserId.str() : "N") + ":" + (e->membershipType ? bytes : "N") + ":" +
           (e->ClientsWaitingProps ? std::to_string(e->ClientsWaitingProps->size()) : "N") + ":" +
           (e->avatarGroups ? Group(*e->avatarGroups->at(0)) : "N") + ":" + list_id + ":" +
           DumpList(e->picksList);
}
}
namespace opensim::framework {
template <> struct ExpiringCacheProbe<Uuid, std::shared_ptr<UserProfileCacheEntry>, UuidHash> {
    static void purge(UserProfileRequests::Cache& cache) {
        UserProfileRequests::Cache::purge_state(cache.state_);
    }
};
}
