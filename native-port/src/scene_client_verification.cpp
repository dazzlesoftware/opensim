// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/scene_client_verification.hpp"
#include <chrono>
#include <stdexcept>
#include <thread>
namespace opensim::services {
namespace {
template <class T> T& require(T* value) {
    if (!value)
        throw std::invalid_argument("Null client verification dependency");
    return *value;
}
template <class T> T& require(const std::shared_ptr<T>& value) {
    return require(value.get());
}
std::string incoming(const framework::AgentCircuitData& circuit, SceneClientVerificationBackend& backend,
                     bool region_word = false) {
    auto first = circuit.first_name;
    auto last = circuit.last_name;
    auto region = backend.region_name();
    return first.value_or("") + " " + last.value_or("") + " in " + (region_word ? "region " : "") +
           region.value_or("");
}
}
std::shared_ptr<framework::AgentCircuitData>
SceneClientVerificationBackend::agent_circuit(const framework::Uuid& agent) {
    return require(authenticate_handler()).get_agent_circuit_data(agent);
}
void SceneClientVerificationBackend::remove_circuit(const framework::Uuid& agent) {
    require(authenticate_handler()).remove_circuit(agent);
}
void SceneClientVerificationBackend::sleep(int milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}
bool SceneClientVerification::verify_client(std::shared_ptr<framework::AgentCircuitData> circuit,
                                            const framework::IpEndpoint* endpoint, bool& via_login) {
    via_login = false;
    auto& data = require(circuit);
    if ((data.teleport_flags & 0x40000000u) != 0) {
        backend_.debug("[SCENE]: Incoming client " + incoming(data, backend_, true) + " via HG login");
        via_login = true;
        auto verifier = backend_.user_verification_module();
        if (verifier && endpoint) {
            auto address = require(endpoint->address).str();
            if (!verifier->verify_client(circuit, address)) {
                backend_.debug("[SCENE]: User Client Verification for " + incoming(data, backend_) +
                               " returned false");
                return false;
            }
            backend_.debug("[SCENE]: User Client Verification for " + incoming(data, backend_) +
                           " returned true");
        }
    } else if ((data.teleport_flags & 128u) != 0) {
        auto first = data.first_name;
        auto last = data.last_name;
        auto region = backend_.region_name();
        backend_.debug("[SCENE]: Incoming client " + first.value_or("") + " " + last.value_or("") +
                       " in region " + region.value_or("") +
                       " via regular login. Client IP verification not performed.");
        via_login = true;
    }
    return true;
}
std::shared_ptr<VerifiedScenePresence>
SceneClientVerification::wait_get_scene_presence(framework::Uuid agent) {
    int times = 120;
    std::shared_ptr<VerifiedScenePresence> presence;
    while (!(presence = backend_.scene_presence(agent)) && (times-- > 0))
        backend_.sleep(250);
    if (!presence)
        backend_.warn("[SCENE PRESENCE]: Did not find presence with id " + agent.str() + " in " +
                      backend_.region_name().value_or("") + " before timeout");
    return presence;
}
bool SceneClientVerification::check_client(framework::Uuid agent, const framework::IpEndpoint* endpoint) {
    auto circuit = backend_.agent_circuit(agent);
    if (!circuit)
        return false;
    bool via_login;
    if (verify_client(circuit, endpoint, via_login))
        return true;
    try {
        auto presence = wait_get_scene_presence(agent);
        if (presence) {
            auto service = backend_.presence_service();
            auto client = presence->controlling_client();
            auto session = require(client).session_id();
            require(service).logout(session);
            auto id = presence->agent_id();
            backend_.close_agent(id, false);
        }
        backend_.remove_circuit(agent);
    } catch (const std::exception& exception) {
        backend_.cleanup_exception(exception);
    }
    return false;
}
}
