// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "profile_list_fixture.hpp"
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
        UserProfileRequests module(context, backend);
        active_module = &module;
        std::shared_ptr<ProfileRequestClient> client = flags & 1 ? nullptr : std::make_shared<PicksClient>();
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
                else if (token == "R") {
                    std::vector<ProfileText> args;
                    if (!(flags & 8))
                        args.push_back(arg);
                    std::shared_ptr<AdvancedProfileClient> sender =
                        flags & 2 ? std::make_shared<AdvancedProfileClient>() : client;
                    module.picks_request(sender, flags & 4 ? nullptr : &args);
                } else if (token == "G") {
                    saved = module.get_picks(target);
                    result = Identity(saved) + ":" + DumpList(saved);
                } else if (token == "M") {
                    if (!saved)
                        throw std::runtime_error("Object reference not set to an instance of an object.");
                    (*saved)[Id(8)] = text;
                } else if (token == "W") {
                    if (!saved)
                        throw std::runtime_error("Object reference not set to an instance of an object.");
                    saved->clear();
                } else if (token == "C")
                    module.profile_cache().clear();
                else if (token == "P")
                    ExpiringCacheProbe<Uuid, std::shared_ptr<UserProfileCacheEntry>, UuidHash>::purge(
                        module.profile_cache());
                else if (token == "D")
                    result = module.profile_cache().remove(target) ? "1" : "0";
                else if (token == "N")
                    module.profile_cache().add_or_update(target, nullptr, 300.0);
                else {
                    auto e = std::make_shared<UserProfileCacheEntry>();
                    if (token == "L" || token == "K") {
                        e->picksList = std::make_shared<UserProfileNameList>();
                        if (token == "K") {
                            (*e->picksList)[Id(3)] = u"seed1";
                            (*e->picksList)[Id(4)] = u"seed2";
                        }
                    }
                    if (token == "O") {
                        e->picks = std::make_shared<
                            OrderedDictionary<Uuid, std::shared_ptr<UserProfilePick>, UuidHash>>();
                        e->classifiedsLists = std::make_shared<UserProfileNameList>();
                        e->classifieds = std::make_shared<
                            OrderedDictionary<Uuid, std::shared_ptr<UserClassifiedAdd>, UuidHash>>();
                        e->props = std::make_shared<UserProfileProperties>();
                        e->props->UserId = Id(7);
                        e->avatarGroups =
                            std::make_shared<std::vector<std::shared_ptr<GroupMembershipData>>>();
                        e->avatarGroups->push_back(std::make_shared<GroupMembershipData>());
                        e->born = u"seed";
                        e->membershipType = std::make_shared<std::vector<std::uint8_t>>(
                            std::initializer_list<std::uint8_t>{1, 2});
                        e->flags = 55;
                        e->ClientsWaitingProps = std::make_shared<ProfileClientSet>();
                        e->ClientsWaitingProps->insert(client);
                        e->ClientsWaitingProps->insert(client);
                    }
                    module.profile_cache().add_or_update(target, e, 300.0);
                }
            } catch (const std::exception& e) {
                result = "EX:" + H(e.what());
            }
            if (!output.empty())
                output += '/';
            output += result + ":" + State(module, target);
        }
        std::cout << output << "|" << H(trace) << "\n";
    }
}
