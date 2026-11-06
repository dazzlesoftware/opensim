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
#pragma once
#include "opensim/framework/uuid.hpp"
#include "opensim/framework/ordered_dictionary.hpp"
#include "opensim/framework/quaternion_text.hpp"
#include <any>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
namespace opensim::network {
struct Value;
}
namespace opensim::framework {
class AvatarAppearance;
struct EntityTransferContext;
struct ServiceUrlStringifiable {
    std::function<std::string()> text;
};
std::string service_url_object_text(const std::any& value);
struct AgentCircuitData {
    Uuid agent_id, base_folder, inventory_folder, secure_session_id, session_id;
    std::shared_ptr<AvatarAppearance> appearance;
    std::optional<std::string> caps_path = std::string{}, service_session_id = std::string{};
    using ChildrenSeeds = OrderedDictionary<std::uint64_t, std::optional<std::string>>;
    std::shared_ptr<ChildrenSeeds> children_cap_seeds;
    bool child = false;
    // Set by the receiving server after home-grid verification, never unpacked from the wire.
    bool verified_foreign_login = false;
    std::uint32_t circuit_code = 0, teleport_flags = 0;
    std::optional<std::string> first_name, last_name, ip_address, channel, mac, id0;
    Vector3 start_position;
    float start_far = -1;
    std::optional<Vector3> transfer_look_at;
    struct TransferredCamera {
        Vector3 center, at_axis, left_axis, up_axis;
    };
    std::optional<TransferredCamera> transfer_camera;
    struct TransferredMovement {
        Vector3 velocity;
        Quaternion rotation;
        std::uint32_t controls;
        bool always_run;
    };
    std::optional<TransferredMovement> transfer_movement;
    struct TransferredSeat {
        Uuid part;
        Vector3 offset;
        bool explicit_target;
    };
    std::optional<TransferredSeat> transfer_seat;
    std::optional<bool> transfer_viewer_god_mode;
    Uuid active_group_id;
    using ServiceUrls = OrderedDictionary<std::string, std::any>;
    std::shared_ptr<ServiceUrls> service_urls;
    void viewer(std::optional<std::string> value) {
        viewer_ = std::move(value);
    }
    std::optional<std::string> viewer() const;
    std::string name() const;
    network::Value pack(EntityTransferContext* context = nullptr);
    void unpack(const network::Value& map);

  private:
    std::optional<std::string> viewer_;
};
struct Login {
    Uuid agent, base_folder, inventory_folder, secure_session, session;
    std::string caps_path;
    std::uint32_t circuit_code = 0;
    std::optional<std::string> first = "Test", last = "User";
    Vector3 start_position{128, 128, 70};
    float start_far = -1;
    std::shared_ptr<AvatarAppearance> appearance;
};
struct AuthenticateResponse {
    bool authorised = false;
    std::optional<Login> login_info;
};
// Shared circuit objects preserve the original manager's reference semantics.
// Returned snapshots copy the index, not the circuit objects it refers to.
class AgentCircuitManager {
  public:
    using Circuit = std::shared_ptr<AgentCircuitData>;
    using AgentIndex = std::unordered_map<Uuid, Circuit, UuidHash>;
    AuthenticateResponse authenticate_session(const Uuid& session, const Uuid& agent,
                                              std::uint32_t code) const;
    void add_new_circuit(const Circuit& data);
    void add_new_circuit(std::uint32_t code, const Circuit& data);
    void remove_circuit(std::uint32_t code);
    void remove_circuit(const Uuid& agent);
    void remove_circuit(const Circuit& data);
    bool remove_current_circuit(const Circuit& data);
    Circuit get_agent_circuit_data(std::uint32_t code) const;
    Circuit get_agent_circuit_data(const Uuid& agent) const;
    AgentIndex get_agent_circuits() const;
    void update_agent_data(const AgentCircuitData& data);
    bool try_change_circuit_code(std::uint32_t old_code, std::uint32_t new_code);
    void update_agent_child_status(std::uint32_t code, bool child);
    bool get_agent_child_status(std::uint32_t code) const;

  private:
    void add_locked(const Circuit& data);
    mutable std::mutex mutex_;
    std::unordered_map<std::uint32_t, Circuit> circuits_;
    AgentIndex by_agent_;
};
}
