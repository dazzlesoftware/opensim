// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "profile_requests_fixture.hpp"
int main(int argc, char** argv) {
    if (argc == 5 && std::string_view(argv[1]) == "--network")
        return RunNetwork(argv[2], argv[3], argv[4]);
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        int op;
        std::string u, a, t;
        in >> op >> mode >> flags >> u >> a >> t;
        uri = Decode(u);
        auto arg = Decode(a);
        text = Decode(t);
        trace.clear();
        std::string result = "OK";
        try {
            auto context = std::make_shared<Context>();
            UserProfileRequests module(context);
            std::shared_ptr<ProfileRequestClient> client = flags & 1 ? nullptr : std::make_shared<Client>();
            Uuid target;
            if (arg)
                try_parse_sdk_uuid(*arg, target);
            if (op == 0) {
                std::vector<ProfileText> args;
                if (!(flags & 8))
                    args.push_back(arg);
                std::shared_ptr<AdvancedProfileClient> sender =
                    flags & 2 ? std::make_shared<AdvancedProfileClient>() : client;
                module.notes_request(sender, flags & 4 ? nullptr : &args);
            } else if (op == 1)
                module.notes_update(client, target, text);
            else if (op == 2)
                module.preferences_request(client);
            else if (op == 3)
                module.preferences_update(flags & 2097152, flags & 8388608, client);
            else if (op == 4) {
                ProfileText out;
                auto foreign = module.profile_server_uri(target, out);
                result = std::string(foreign ? "1" : "0") + ":" + T(out);
            } else {
                auto p = module.find_presence(target);
                result = p ? (p->npc ? "NPC" : "P") : "N";
            }
        } catch (const std::exception& e) {
            result = "EX:" + H(e.what());
        }
        std::cout << result << "|" << H(trace) << "\n";
    }
}
