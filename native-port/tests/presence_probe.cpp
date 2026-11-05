/*
Copyright (c) Contributors, http://opensimulator.org/
See CONTRIBUTORS.TXT for a full list of copyright holders.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenSimulator Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "opensim/services/presence_service.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace opensim::services;
using opensim::framework::Uuid;
int main(int argc, char** argv) {
    try {
        const auto seed = argc > 1 ? std::stoi(argv[1]) : 0;
        double now = 0;
        auto cache = std::make_shared<PresenceCache>([&] { return now; });
        NullPresenceStore store, second;
        PresenceService service(store, seed % 2 != 0, cache);
        auto id = [&](int value) {
            std::ostringstream text;
            text << "00000000-0000-0000-0000-" << std::hex << std::setfill('0') << std::setw(12)
                 << (seed + 1) * 100 + value;
            return Uuid(text.str());
        };
        const auto user = "user-" + std::to_string(seed);
        const auto s1 = id(1), s2 = id(2), s3 = id(3), s4 = id(4), s5 = id(5), r1 = id(11), r2 = id(12);
        std::vector<std::string> rows;
        auto flag = [&](bool value) { rows.push_back(value ? "1" : "0"); };
        auto info = [&](const std::optional<PresenceInfo>& value) {
            rows.push_back(value ? value->user_id + "@" + value->region_id.str() : "-");
        };
        flag(service.login(user, s1, id(21)));
        flag(service.report(s1, r1));
        info(service.agent(s1));
        flag(service.login(user, s2, id(22)));
        info(service.agent(s1));
        const auto agents = service.agents({user, "missing", user});
        rows.push_back(std::to_string(agents.size()));
        for (const auto& agent : agents)
            info(agent);
        flag(store.erase("SessionID", s2.str()));
        info(service.agent(s2));
        flag(service.report(s2, r2));
        info(service.agent(s2));
        for (const auto& agent : service.agents({user}))
            info(agent);
        flag(service.login(user + "-two", s3, id(23)));
        flag(service.report(s3, {}));
        flag(service.report(s3, r1));
        rows.push_back(std::to_string(store.get("SecureSessionID", id(23).str()).size()));
        flag(store.verify(id(31), id(23)));
        flag(service.logout_region(r1));
        info(service.agent(s1));
        info(service.agent(s3));
        flag(service.login(user + "-three", s4, id(24)));
        store.erase("SessionID", s4.str());
        now = 300000 + seed % 4;
        cache->purge();
        info(service.agent(s4));
        now = 600001 + seed % 4;
        cache->purge();
        info(service.agent(s4));
        now = seed >= 8 ? 2147483648.0 : 2147483000.0;
        flag(service.login(user + "-wrap", s5, id(25)));
        auto alias = store.get(s5);
        flag(second.get(s5) == alias);
        alias->data["custom"] = "tag";
        alias->session_id = id(6);
        flag(store.get(s5) == alias);
        rows.push_back(std::to_string(store.get("SessionID", s5.str()).size()));
        rows.push_back(std::to_string(store.get("SessionID", id(6).str()).size()));
        rows.push_back(std::to_string(store.get("custom", "tag").size()));
        store.erase("SessionID", s5.str());
        now += 1;
        cache->purge();
        info(service.agent(s5));
        flag(service.logout(s5));
        info(service.agent(s5));
        auto first = std::make_shared<PresenceData>();
        first->user_id = "order";
        first->session_id = id(40);
        store.store(first);
        auto next = std::make_shared<PresenceData>(*first);
        next->session_id = id(41);
        store.store(next);
        store.erase("SessionID", first->session_id.str());
        auto last = std::make_shared<PresenceData>(*first);
        last->session_id = id(42);
        store.store(last);
        flag(store.get("UserID", "order").front() == last);
        rows.push_back(std::to_string(store.get("RegionID", "invalid").size()));
        flag(!store.erase("SessionID", "invalid"));
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (i)
                std::cout << '|';
            std::cout << rows[i];
        }
        std::cout << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
