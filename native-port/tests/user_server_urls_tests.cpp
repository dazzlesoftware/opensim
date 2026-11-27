// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/user_server_urls.hpp"
#include "opensim/services/account_store.hpp"
#include "opensim/scene/profile_requests.hpp"
#include <iostream>
#include <stdexcept>

using namespace opensim;

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

class Timer final : public framework::ExpiringCacheTimer {
  public:
    void change(std::int32_t) override {}

    void dispose() override {}
};
} // namespace

int main() {
    try {
        const auto user = framework::Uuid::random(), local = framework::Uuid::random();
        double now = 0;
        std::vector<std::function<void()>> purges;
        framework::ExpiringCacheBackend backend;
        backend.timestamp = [&] { return now; };
        backend.timer = [&](auto purge, auto) {
            purges.push_back(purge);
            return std::make_shared<Timer>();
        };
        const auto advance = [&](double timestamp) {
            now = timestamp;
            const auto callbacks = purges;
            for (const auto &purge : callbacks)
                purge();
        };
        int identities = 0, fetches = 0;
        bool offline = false, network_failure = true;
        services::UserServerUrlServices dependencies;
        dependencies.foreign_home = [&](auto id) -> std::optional<std::string> {
            ++identities;
            return id == local ? std::nullopt : std::optional<std::string>("HTTP://HOME.INVALID/");
        };
        dependencies.fetch = [&](auto, const auto &home) {
            require(home == "http://home.invalid/", "Home lookup did not use lower-case source URL");
            ++fetches;
            if (offline)
                throw std::runtime_error("Controlled HTTP failure");
            return std::unordered_map<std::string, std::string>{{"InventoryServerURI", "HTTP://INVENTORY.INVALID/"},
                                                                {"Invalid", "not a URI"}};
        };
        dependencies.http_failure = [&](auto) { return network_failure; };
        auto shared = std::make_shared<services::UserServerUrls>(dependencies, backend);
        auto &urls = *shared;
        services::AccountStore accounts(":memory:", ":memory:", true);
        scene::NativeProfileUserDirectory profile(accounts, {}, {}, shared);
        require(urls.get(user, "InventoryServerURI") == "http://inventory.invalid:80", "Foreign URL lookup failed");
        require(!urls.get(user, "Missing") && !urls.get(user, "Invalid") && fetches == 1 && identities == 1,
                "Missing/invalid URL bypassed cached directory");
        require(!urls.get(local, "InventoryServerURI") && fetches == 1, "Local/unknown identity queried foreign home");
        bool recent = true;
        require(profile.server_url(user, u"InventoryServerURI", recent).value_or(u"") ==
                        u"http://inventory.invalid:80" &&
                    !recent && fetches == 1,
                "Profile and inventory did not share a cached directory");
        profile.web_failed(user);
        require(!urls.get(user, "InventoryServerURI"), "Recent user web failure was ignored");
        require(profile.server_url(user, u"InventoryServerURI", recent).value_or(u"") ==
                        u"http://inventory.invalid:80" &&
                    recent,
                "Out lookup did not retain cached URLs while reporting recent failure");
        advance(119999);
        require(!urls.get(user, "InventoryServerURI"), "User web failure recovered before the source boundary");
        advance(120000);
        require(profile.server_url(user, u"InventoryServerURI", recent).has_value() && recent,
                "Out lookup lost its inclusive failure boundary");
        require(urls.get(user, "InventoryServerURI").has_value() && fetches == 1,
                "Recovered user failure did not retain cached service URLs");
        profile.server_url(user, u"InventoryServerURI", recent);
        require(!recent, "Non-out recovery did not clear the shared profile failure state");
        offline = true;
        const auto failed = framework::Uuid::random(), same_home = framework::Uuid::random();
        require(!urls.get(failed, "InventoryServerURI") && fetches == 2, "Failed directory lookup returned a URL");
        require(!urls.get(same_home, "InventoryServerURI") && fetches == 2,
                "Known bad home was queried for another user");
        advance(240001);
        offline = false;
        require(!urls.get(failed, "InventoryServerURI") && fetches == 2,
                "Failed user's empty dictionary was retried before identity expiry");
        require(urls.get(same_home, "InventoryServerURI").has_value() && fetches == 3,
                "Bad-home expiry prevented a pending user's first query");
        advance(1920002);
        require(urls.get(failed, "InventoryServerURI").has_value() && fetches == 4,
                "Identity expiry did not allow directory refresh");
        offline = true;
        const auto out_failure = framework::Uuid::random();
        require(!urls.get(out_failure, "InventoryServerURI", recent) && recent,
                "Out lookup did not mark a fresh HTTP failure");
        advance(2040003);
        require(!urls.get(out_failure, "InventoryServerURI", recent) && !recent && fetches == 5,
                "Out failure expiry retried the failed user's empty dictionary");
        network_failure = false;
        require(!urls.get(framework::Uuid::random(), "InventoryServerURI", recent) && recent && fetches == 6,
                "Non-HTTP exception did not mark a recent per-user failure");
        require(!urls.get(framework::Uuid::random(), "InventoryServerURI") && fetches == 7,
                "Non-HTTP exception incorrectly poisoned the shared home URL");
        std::cout
            << "PASS: offline user directory lookup, local suppression, URL validation and failure/cache lifecycle\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
