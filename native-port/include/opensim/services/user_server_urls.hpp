// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/expiring_cache.hpp"
#include "opensim/framework/uuid.hpp"
#include <unordered_map>

namespace opensim::services {
struct UserServerUrlServices {
    // Returns the known foreign home, or null for local/unknown identities.
    std::function<std::optional<std::string>(framework::Uuid)> foreign_home;
    std::function<std::unordered_map<std::string, std::string>(framework::Uuid, const std::string &)> fetch;
    std::function<bool(std::exception_ptr)> http_failure;
};

// GetUserServerURL lookup policies. Identity records and fetched service
// dictionaries share the source GetUser thirty-minute cache lifetime.
class UserServerUrls {
  public:
    explicit UserServerUrls(UserServerUrlServices,
                            framework::ExpiringCacheBackend = framework::native_expiring_cache_backend());
    ~UserServerUrls();
    std::optional<std::string> get(framework::Uuid, const std::string &key);
    std::optional<std::string> get(framework::Uuid, const std::string &key, bool &recent_failure);
    void web_failed(framework::Uuid);

  private:
    std::optional<std::string> lookup(framework::Uuid, const std::string &, bool *);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace opensim::services
