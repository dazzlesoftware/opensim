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
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace opensim::services;
using opensim::framework::Uuid;
namespace {
int failures = 0;
void check(bool value, const char* name) {
    if (!value) {
        ++failures;
        std::cerr << "FAIL: " << name << '\n';
    }
}
class FaultStore : public PresenceStore {
  public:
    NullPresenceStore backing;
    bool store_result = false, report_throws = false, get_throws = false;
    bool store(const std::shared_ptr<PresenceData>& data) override {
        return store_result ? backing.store(data) : false;
    }
    std::shared_ptr<PresenceData> get(const Uuid& session) override {
        if (get_throws)
            throw std::runtime_error("get failed");
        return backing.get(session);
    }
    std::vector<std::shared_ptr<PresenceData>> get(const std::string& field,
                                                   const std::string& value) override {
        return backing.get(field, value);
    }
    bool erase(const std::string& field, const std::string& value) override {
        return backing.erase(field, value);
    }
    void logout_region(const Uuid& region) override {
        backing.logout_region(region);
    }
    bool report(const Uuid& session, const Uuid& region) override {
        if (report_throws)
            throw std::runtime_error("report failed");
        return backing.report(session, region);
    }
    bool verify(const Uuid& agent, const Uuid& session) override {
        return backing.verify(agent, session);
    }
};
void tests() {
    double now = 0;
    auto cache = std::make_shared<PresenceCache>([&] { return now; });
    FaultStore store;
    PresenceService service(store, false, cache);
    const auto session = Uuid::random(), secure = Uuid::random(), region = Uuid::random();
    const auto user = Uuid::random().str();
    check(service.login(user, session, secure) && !store.backing.get(session) &&
              service.agent(session).has_value(),
          "login preserves source success/cache behavior when Store returns false");
    store.report_throws = true;
    check(!service.report(session, region) && service.agent(session).has_value(),
          "report exception returns false and retains cached record");
    store.report_throws = false;
    check(!service.report(session, region) && !service.agent(session),
          "failed report evicts phantom cached presence");
    store.get_throws = true;
    check(!service.report(session, region), "report catches provider lookup exception");
    bool propagated = false;
    try {
        service.agent(session);
    } catch (const std::runtime_error&) {
        propagated = true;
    }
    check(propagated, "GetAgent propagates provider lookup failure like source");
    store.get_throws = false;
    store.store_result = true;
    service.login(user, session, secure);
    check(service.report(session, region) && service.agent(session)->region_id == region,
          "successful provider report updates cached region");
    check(!store.verify(Uuid::random(), secure), "Null provider verification remains always false");
    check(service.logout(session) && !service.agent(session) && !service.logout(session),
          "logout removes provider/cache and missing session returns false");
    NullPresenceStore provider;
    PresenceService first(provider), second(provider);
    auto shared_session = Uuid::random();
    first.login(user, shared_session, secure);
    provider.erase("SessionID", shared_session.str());
    check(second.agent(shared_session).has_value(), "default caches shared across service instances");
    second.logout(shared_session);
    const auto before = std::chrono::steady_clock::now();
    {
        auto automatic = std::make_shared<PresenceCache>();
        PresenceService timed(provider, false, automatic);
        const auto id = Uuid::random();
        timed.login("timer-" + user, id, secure);
        provider.erase("SessionID", id.str());
    }
    check(std::chrono::steady_clock::now() - before < std::chrono::seconds(1),
          "background cache worker joins promptly with live entries");
}
}
int main() {
    try {
        tests();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return failures ? 1 : 0;
}
