// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/user_server_urls.hpp"
#include "opensim/framework/http_host.hpp"
#include "opensim/framework/invariant_case.hpp"
#include <mutex>

namespace opensim::services {
using framework::Uuid;

struct UserServerUrls::Impl {
    struct User {
        std::mutex mutex;
        std::optional<std::string> home;
        std::optional<double> last_web_failure;
        std::optional<std::unordered_map<std::string, std::string>> urls;
    };

    UserServerUrlServices services;
    framework::ExpiringCacheOS<Uuid, std::shared_ptr<User>, framework::UuidHash> users;
    std::function<double()> now;
    framework::ExpiringCacheOS<std::string, bool> bad_homes;

    Impl(UserServerUrlServices dependencies, framework::ExpiringCacheBackend backend)
        : services(std::move(dependencies)), users(500, backend), now(backend.timestamp), bad_homes(500, backend) {}
};

UserServerUrls::UserServerUrls(UserServerUrlServices services, framework::ExpiringCacheBackend backend)
    : impl_(std::make_unique<Impl>(std::move(services), std::move(backend))) {
    if (!impl_->services.foreign_home || !impl_->services.fetch)
        throw std::invalid_argument("User server URL lookup requires identity and remote directory services");
}

UserServerUrls::~UserServerUrls() = default;

void UserServerUrls::web_failed(Uuid user) {
    std::shared_ptr<Impl::User> entry;
    if (impl_->users.try_get_value(user, entry)) {
        std::lock_guard lock(entry->mutex);
        entry->last_web_failure = impl_->now();
    }
}

std::optional<std::string> UserServerUrls::get(Uuid user, const std::string &key) {
    return lookup(user, key, nullptr);
}

std::optional<std::string> UserServerUrls::get(Uuid user, const std::string &key, bool &recent_failure) {
    return lookup(user, key, &recent_failure);
}

std::optional<std::string> UserServerUrls::lookup(Uuid user, const std::string &key, bool *recent_failure) {
    if (recent_failure)
        *recent_failure = false;
    std::shared_ptr<Impl::User> entry;
    if (!impl_->users.try_get_value(user, entry)) {
        entry = std::make_shared<Impl::User>();
        entry->home = impl_->services.foreign_home(user);
        impl_->users.add(user, entry, 1800000);
    }
    if (!entry->home || entry->home->empty())
        return {};
    std::lock_guard lock(entry->mutex);
    if (entry->last_web_failure) {
        const auto elapsed = impl_->now() - *entry->last_web_failure;
        if (recent_failure) {
            if (elapsed <= 120000)
                *recent_failure = true;
            else
                entry->last_web_failure.reset();
        } else {
            if (elapsed < 120000)
                return {};
            entry->last_web_failure.reset();
        }
    }
    if (!entry->urls) {
        if (recent_failure && *recent_failure)
            return {};
        const auto home = framework::lower_invariant_culture(std::string_view(*entry->home));
        bool bad = false;
        if (impl_->bad_homes.try_get_value(home, bad) && bad)
            return {};
        try {
            entry->urls = impl_->services.fetch(user, home);
        } catch (...) {
            const auto error = std::current_exception();
            if (impl_->services.http_failure && impl_->services.http_failure(error))
                impl_->bad_homes.add(home, true, 120000);
            // A failed lookup is an empty dictionary, not an absent dictionary:
            // it is not retried until the user record itself expires.
            entry->urls.emplace();
            if (recent_failure) {
                entry->last_web_failure = impl_->now();
                *recent_failure = true;
            }
        }
    }
    const auto found = entry->urls->find(key);
    if (found == entry->urls->end())
        return {};
    const framework::HttpHost endpoint(found->second);
    return endpoint.valid() ? std::optional(endpoint.uri) : std::nullopt;
}
} // namespace opensim::services
