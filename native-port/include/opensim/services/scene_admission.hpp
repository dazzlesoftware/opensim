// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/scene_access.hpp"
#include "opensim/services/region_store.hpp"
#include "opensim/services/presence_state.hpp"
#include "opensim/framework/viewer_name.hpp"
#include <mutex>
namespace opensim::services {
using AdmissionPresenceState = PresenceState;
class AdmissionPresence {
  public:
    virtual ~AdmissionPresence() = default;
    virtual framework::Uuid agent_id() const = 0;
    virtual bool deleted() const = 0;
    virtual bool child() const = 0;
    virtual AdmissionPresenceState state() const = 0;
    virtual void state(AdmissionPresenceState value) = 0;
    virtual void do_not_close_after_teleport(bool value) = 0;
    virtual bool has_client() const = 0;
    virtual void teleport_flags(std::uint32_t value) = 0;
    virtual void adjust_known_seeds() = 0;
};
class AdmissionCaps {
  public:
    virtual ~AdmissionCaps() = default;
    virtual void set_agent_caps_seeds(const std::shared_ptr<framework::AgentCircuitData>& circuit) = 0;
    virtual void create_caps(const framework::Uuid& agent, std::uint32_t circuit) = 0;
    virtual void activate_caps(std::uint32_t circuit) = 0;
};
struct AdmissionGridPresence {
    std::optional<std::string> user_id;
};
class SceneAdmissionBackend {
  public:
    virtual ~SceneAdmissionBackend() = default;
    virtual AccessRegion& region() = 0;
    virtual std::recursive_mutex& remove_client_mutex() = 0;
    virtual std::recursive_mutex& new_user_mutex() = 0;
    virtual std::string viewer_name(const std::shared_ptr<framework::AgentCircuitData>& circuit) {
        return framework::viewer_name(circuit.get());
    }
    // OpenSim.Framework.Culture initializes request threads to the invariant culture.
    // A host that changes a thread's culture can supply its own casing adapter.
    virtual std::string lower_current_culture(const std::string& text) {
        return framework::lower_invariant_culture(std::string_view(text));
    }
    virtual void log_incoming(const std::shared_ptr<framework::AgentCircuitData>& circuit,
                              std::uint32_t flags, const GridRegion* source, const std::string& viewer) = 0;
    virtual bool logins_enabled() const = 0;
    virtual std::vector<std::optional<std::string>> allowed_viewers() const = 0;
    virtual std::vector<std::optional<std::string>> banned_viewers() const = 0;
    virtual std::shared_ptr<AdmissionPresence> presence(const framework::Uuid& agent) = 0;
    virtual bool in_transit(const framework::Uuid& agent) = 0;
    virtual void sleep(int milliseconds) = 0;
    virtual bool telehub_allow_landmarks() const = 0;
    virtual bool check_land_access(const framework::Uuid& agent, bool not_crossing, bool check_hub,
                                   bool check_count, const framework::Vector3& position,
                                   std::string& reason) = 0;
    virtual bool check_circuit_land_access(const framework::AgentCircuitData& circuit, bool check_hub,
                                           std::string& reason) {
        return check_land_access(circuit.agent_id, true, check_hub, false, circuit.start_position, reason);
    }
    virtual void remove_circuit(const std::shared_ptr<framework::AgentCircuitData>& circuit) = 0;
    virtual void close_agent(const framework::Uuid& agent, bool force) = 0;
    virtual void remove_cached_account(const framework::Uuid& agent) = 0;
    virtual void add_circuit(const std::shared_ptr<framework::AgentCircuitData>& circuit) = 0;
    virtual bool has_presence_service() const = 0;
    virtual std::optional<AdmissionGridPresence> grid_presence(const framework::Uuid& session) = 0;
    virtual bool see_into_region() const = 0;
    virtual bool authorize_user(const std::shared_ptr<framework::AgentCircuitData>& circuit, bool bypass,
                                std::string& reason) = 0;
    virtual std::shared_ptr<AdmissionCaps> caps() = 0;
    virtual void cache_user_name(const std::shared_ptr<framework::AgentCircuitData>& circuit) = 0;
};
class SceneAdmission {
  public:
    explicit SceneAdmission(SceneAdmissionBackend& backend) : backend_(backend) {}
    virtual ~SceneAdmission() = default;
    bool new_user_connection(std::shared_ptr<framework::AgentCircuitData> circuit, std::uint32_t flags,
                             const GridRegion* source, std::string& reason, bool require_presence = true);
    virtual bool verify_user_presence(const std::shared_ptr<framework::AgentCircuitData>& circuit,
                                      std::string& reason);

  private:
    SceneAdmissionBackend& backend_;
};
}
