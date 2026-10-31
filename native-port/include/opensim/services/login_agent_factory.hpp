// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/account_store.hpp"
#include "opensim/framework/avatar_appearance.hpp"
#include <string_view>
namespace opensim::services {
struct LoginAgentDetails {
    framework::Uuid session,secure_session;
    std::uint32_t circuit=0;
    framework::Vector3 position;
    std::optional<std::string> ip_address,viewer,channel,mac,id0;
};
class LoginAgentFactory {
public:
    using Config=std::vector<std::pair<std::string,std::string>>;
    using Store=std::function<bool(UserAccount&)>;
    using CapsPath=std::function<std::string()>;
    using Keys=std::function<std::vector<std::string>()>;
    using Read=std::function<std::string(const std::string&,const std::string&)>;
    LoginAgentFactory(Config config={},std::optional<std::string> gatekeeper={},Store store={},CapsPath caps={});
    LoginAgentFactory(Keys keys,Read read,std::optional<std::string> gatekeeper,Store store={},CapsPath caps={});
    std::shared_ptr<framework::AgentCircuitData> make_agent(UserAccount& account,const framework::AvatarAppearance* appearance,const LoginAgentDetails& details)const;
    void set_service_urls(framework::AgentCircuitData& circuit,UserAccount& account)const;
    static bool is_service_key(std::u16string_view key);
private:
    Keys keys_;Read read_;std::optional<std::string> gatekeeper_;Store store_;CapsPath caps_;
};
}
