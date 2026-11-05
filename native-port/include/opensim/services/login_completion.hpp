// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/login_orchestration.hpp"
#include "opensim/services/login_agent_factory.hpp"
#include "opensim/services/agent_launch.hpp"
#include "opensim/services/login_response.hpp"
#include <mutex>
namespace opensim::services {
struct LoginCompletionRequest {
    std::optional<std::string> start, viewer, channel, mac, id0, remote_ip, first, last;
    std::optional<RegionEndpoint> client_endpoint;
    framework::Uuid real_id;
};
struct LoginCompletionOptions {
    std::string welcome = "Welcome to OpenSim!", map_url, profile_url, openid_url, search_url, currency,
                classified_fee, destinations_url, avatars_url,
                dst_zone = "America/Los_Angeles;Pacific Standard Time";
    std::int32_t max_agent_groups = 60;
};
struct LoginCompletionDependencies {
    AgentLauncher* launcher = nullptr;
    LoginAgentFactory* factory = nullptr;
    std::function<std::optional<GridUserInfo>(const std::string&)> logged_in;
    std::function<std::optional<std::vector<std::shared_ptr<LoginFriendInfo>>>(const framework::Uuid&)>
        friends;
    std::function<std::optional<std::vector<std::shared_ptr<InventoryItem>>>(const framework::Uuid&)>
        gestures;
    std::shared_ptr<LoginResponseLibrary> library;
    LoginResponseEnvironment environment;
};
// Exceptions intentionally escape to LLLoginService's presence-cleanup boundary.
// Response serialization happens after that boundary in the protocol handler.
class LoginCompletion {
  public:
    using Result = std::variant<network::Value, std::shared_ptr<LoginResponse>>;
    LoginCompletion(LoginCompletionDependencies dependencies, LoginCompletionOptions options = {})
        : dependencies_(std::move(dependencies)), options_(std::move(options)) {}
    Result finish(PreparedLogin& prepared, const LoginCompletionRequest& request);
    void welcome_message(std::string value) {
        std::lock_guard lock(welcome_mutex_);
        options_.welcome = std::move(value);
    }
    std::string welcome_message() const {
        std::lock_guard lock(welcome_mutex_);
        return options_.welcome;
    }

  private:
    mutable std::mutex welcome_mutex_;
    LoginCompletionDependencies dependencies_;
    LoginCompletionOptions options_;
};
}
