// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/agent_circuit.hpp"
#include "opensim/framework/ip_address.hpp"
#include "opensim/services/presence_service.hpp"
#include <exception>
namespace opensim::services {
class UserAgentVerificationModule {
public:
    virtual ~UserAgentVerificationModule()=default;
    virtual bool verify_client(const std::shared_ptr<framework::AgentCircuitData>& circuit,const std::string& address)=0;
};
class VerifiedSceneClient {
public:
    virtual ~VerifiedSceneClient()=default;
    virtual framework::Uuid session_id()const=0;
};
class VerifiedScenePresence {
public:
    virtual ~VerifiedScenePresence()=default;
    virtual std::shared_ptr<VerifiedSceneClient> controlling_client()const=0;
    virtual framework::Uuid agent_id()const=0;
};
class SceneClientVerificationBackend {
public:
    virtual ~SceneClientVerificationBackend()=default;
    virtual framework::AgentCircuitManager* authenticate_handler()const=0;
    virtual std::shared_ptr<framework::AgentCircuitData> agent_circuit(const framework::Uuid& agent);
    virtual void remove_circuit(const framework::Uuid& agent);
    virtual std::optional<std::string> region_name()const=0;
    virtual std::shared_ptr<UserAgentVerificationModule> user_verification_module()=0;
    virtual std::shared_ptr<VerifiedScenePresence> scene_presence(const framework::Uuid& agent)=0;
    virtual IPresenceService* presence_service()=0;
    virtual void close_agent(const framework::Uuid& agent,bool force)=0;
    virtual void sleep(int milliseconds);
    virtual void debug(const std::string& message)=0;
    virtual void warn(const std::string& message)=0;
    virtual void cleanup_exception(const std::exception& exception)=0;
};
class SceneClientVerification {
public:
    explicit SceneClientVerification(SceneClientVerificationBackend& backend):backend_(backend){}
    virtual ~SceneClientVerification()=default;
    bool verify_client(std::shared_ptr<framework::AgentCircuitData> circuit,const framework::IpEndpoint* endpoint,bool& via_login);
    virtual bool check_client(framework::Uuid agent,const framework::IpEndpoint* endpoint);
    virtual std::shared_ptr<VerifiedScenePresence> wait_get_scene_presence(framework::Uuid agent);
private:
    SceneClientVerificationBackend& backend_;
};
}
