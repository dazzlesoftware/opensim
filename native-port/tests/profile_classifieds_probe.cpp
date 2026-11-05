// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "profile_list_fixture.hpp"
namespace {
struct ClassifiedClient : Client {
    bool nested = false;
    void send_classifieds(Uuid target, std::shared_ptr<UserProfileNameList> value) override {
        trace += "classifieds:" + target.str() + ":" + DumpList(value) + ":" + Identity(value) + ";";
        saved = value;
        if (flags & 16777216)
            (*value)[Id(9)] = text;
        if ((flags & 33554432) && !nested) {
            nested = true;
            try {
                std::vector<ProfileText> args{utf16(target.str())};
                active_module->classifieds_request(shared_self, args.empty() ? nullptr : &args);
            } catch (...) {
                nested = false;
                throw;
            }
            nested = false;
        }
        Fail(16384, "classifieds");
    }
    std::shared_ptr<ClassifiedClient> shared_self;
};
std::string ClassifiedState(UserProfileRequests& m, Uuid a) {
    auto snapshot = [&](Uuid id) {
        std::shared_ptr<UserProfileCacheEntry> e;
        m.profile_cache().try_get_value(id, e);
        auto before = State(m, id);
        auto list = e ? e->classifiedsLists : nullptr;
        return before + ":" + Identity(list) + ":" + DumpList(list);
    };
    std::string first = snapshot(a), second = snapshot(Id(2)), owners, interests;
    for (const auto& p : m.classified_owners()) {
        if (!owners.empty())
            owners += ',';
        owners += p.first.str() + "=" + p.second.str();
    }
    for (const auto& p : m.classified_interests()) {
        if (!interests.empty())
            interests += ',';
        interests += p.first.str() + "=" + std::to_string(p.second);
    }
    return first + ";" + second + ";" + owners + ";" + interests;
}
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string raw_uri, raw_arg, raw_text;
        in >> mode >> flags >> raw_uri >> raw_arg >> raw_text;
        uri = Decode(raw_uri);
        auto arg = Decode(raw_arg);
        text = Decode(raw_text);
        trace.clear();
        cache_now = 0;
        saved.reset();
        retained.clear();
        identities.clear();
        auto context = std::make_shared<PicksContext>();
        ExpiringCacheBackend backend{[] { return cache_now; },
                                     [](auto, int due) { return std::make_shared<PicksTimer>(due); }};
        UserProfileRequests m(context, backend);
        active_module = &m;
        auto concrete = std::make_shared<ClassifiedClient>();
        concrete->shared_self = concrete;
        std::shared_ptr<ProfileRequestClient> client = flags & 1 ? nullptr : concrete;
        Uuid target;
        if (arg)
            try_parse_sdk_uuid(*arg, target);
        std::string output;
        for (std::string token; in >> token;) {
            std::string result = "OK";
            try {
                if (token[0] == 't')
                    cache_now = std::stod(token.substr(1));
                else if (token[0] == 'm')
                    mode = std::stoi(token.substr(1));
                else if (token == "A" || token == "B") {
                    std::vector<ProfileText> args;
                    if (!(flags & 8))
                        args.push_back(token == "B" ? ProfileText(utf16(Id(2).str())) : arg);
                    std::shared_ptr<AdvancedProfileClient> sender =
                        flags & 2 ? std::make_shared<AdvancedProfileClient>() : client;
                    m.classifieds_request(sender, flags & 4 ? nullptr : &args);
                } else if (token == "G") {
                    saved = m.get_picks(target);
                    result = Identity(saved) + ":" + DumpList(saved);
                } else if (token == "M") {
                    if (!saved)
                        throw std::runtime_error("Object reference not set to an instance of an object.");
                    (*saved)[Id(8)] = text;
                } else if (token == "W") {
                    if (!saved)
                        throw std::runtime_error("Object reference not set to an instance of an object.");
                    saved->clear();
                } else if (token == "H")
                    m.profile_cache().clear();
                else if (token == "C") {
                    m.profile_cache().clear();
                    m.classified_owners().clear();
                    m.classified_interests().clear();
                } else if (token == "P")
                    ExpiringCacheProbe<Uuid, std::shared_ptr<UserProfileCacheEntry>, UuidHash>::purge(
                        m.profile_cache());
                else if (token == "D")
                    result = m.profile_cache().remove(target) ? "1" : "0";
                else if (token == "I") {
                    m.classified_owners()[Id(3)] = target;
                    m.classified_interests().erase(Id(3));
                } else if (token == "J") {
                    m.classified_owners().erase(Id(3));
                    m.classified_interests()[Id(3)] = 7;
                } else if (token == "V") {
                    m.classified_owners()[Id(3)] = Id(2);
                    m.classified_interests()[Id(3)] = std::numeric_limits<std::int32_t>::max();
                } else if (token == "Z") {
                    m.classified_owners().clear();
                    m.classified_interests().clear();
                } else if (token == "N")
                    m.profile_cache().add_or_update(target, nullptr, 300.0);
                else {
                    auto e = std::make_shared<UserProfileCacheEntry>();
                    if (token == "L" || token == "K") {
                        e->classifiedsLists = std::make_shared<UserProfileNameList>();
                        if (token == "K") {
                            (*e->classifiedsLists)[Id(3)] = u"seed1";
                            (*e->classifiedsLists)[Id(4)] = u"seed2";
                        }
                    }
                    if (token == "O") {
                        e->picksList = std::make_shared<UserProfileNameList>();
                        (*e->picksList)[Id(7)] = u"other picks";
                        e->props = std::make_shared<UserProfileProperties>();
                        e->props->UserId = Id(7);
                        e->flags = 55;
                        e->born = u"seed";
                    }
                    m.profile_cache().add_or_update(target, e, 300.0);
                }
            } catch (const std::exception& e) {
                result = "EX:" + H(e.what());
            }
            if (!output.empty())
                output += '/';
            output += result + ":" + ClassifiedState(m, target);
        }
        std::cout << output << "|" << H(trace) << "\n";
        concrete->shared_self.reset();
    }
}
