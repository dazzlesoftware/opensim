// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/server/json_rpc.hpp"
#include "opensim/services/user_profiles_service.hpp"
namespace opensim::server {
enum class ProfileLogLevel { Debug, Info };
class JsonRpcProfileHandlers {
  public:
    using Logger = std::function<void(ProfileLogLevel, std::u16string_view)>;
    explicit JsonRpcProfileHandlers(std::shared_ptr<services::UserProfilesService> service,
                                    Logger logger = {})
        : service_(std::move(service)), logger_(std::move(logger)) {}
    // Delegates retain a copy of the handler and its service independently of this object's lifetime.
    void register_handlers(JsonRpcDispatcher&) const;
    bool handle(std::u16string_view method, const network::Value&, std::shared_ptr<JsonRpcResponse>&) const;

  private:
    std::shared_ptr<services::UserProfilesService> service_;
    Logger logger_;
};
}
