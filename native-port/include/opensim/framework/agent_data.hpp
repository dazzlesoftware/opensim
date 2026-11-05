// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/agent_transfer.hpp"
#include "opensim/framework/agent_position.hpp"
#include "opensim/framework/agent_update_records.hpp"
#include "opensim/framework/animation.hpp"
#include "opensim/framework/avatar_appearance.hpp"
#include "opensim/framework/quaternion_text.hpp"
#include "opensim/framework/transfer_list.hpp"
#include <any>
#include <iosfwd>
#include <unordered_map>
namespace opensim::framework {
class AgentData : public IAgentData {
  public:
    Uuid agent_id, region_id, session_id;
    std::uint32_t circuit_code = 0;
    Vector3 position, velocity, center, size, at_axis, left_axis, up_axis;
    bool sender_wants_to_wait_for_root = false;
    float far = 0, aspect = 0;
    network::BinaryData throttles;
    std::uint32_t locomotion_state = 0, control_flags = 0;
    Quaternion head_rotation, body_rotation;
    float energy_level = 0;
    std::shared_ptr<network::Value> god_data;
    bool always_run = false;
    Uuid prey_agent, active_group_id, agent_cof;
    std::uint8_t agent_access = 0, crossing_flags = 0, cross_extra_flags = 0;
    std::optional<std::string> active_group_name, active_group_title;
    using ChildrenSeeds = AgentPosition::ChildrenSeeds;
    std::shared_ptr<ChildrenSeeds> children_cap_seeds;
    std::shared_ptr<std::vector<std::shared_ptr<Animation>>> animations;
    std::shared_ptr<Animation> default_animation, animation_state;
    std::uint8_t motion_state = 0;
    Uuid parent_part;
    Vector3 sit_offset;
    std::shared_ptr<AvatarAppearance> appearance;
    std::shared_ptr<std::vector<std::shared_ptr<ControllerData>>> controllers;
    std::optional<std::string> callback_uri, new_callback_uri;
    std::shared_ptr<TransferList<std::shared_ptr<AgentTransferObject>>> attachment_objects;
    std::shared_ptr<TransferList<std::optional<std::string>>> attachment_object_states;
    using Overrides = OrderedDictionary<std::string, Uuid>;
    std::shared_ptr<Overrides> movement_animation_overrides = std::make_shared<Overrides>();
    std::shared_ptr<TransferList<Uuid>> cached_friends_online;
    AgentData() = default;
    using LegacyTable = std::unordered_map<std::string, std::any>;
    explicit AgentData(const LegacyTable*) {} // Original Hashtable constructor is a no-op.
    Uuid agent_uuid() const override {
        return agent_id;
    }
    void agent_uuid(const Uuid& value) override {
        agent_id = value;
    }
    void set_look_at(const Vector3& value);
    network::Value pack(EntityTransferContext* context = nullptr) override;
    void unpack(network::Value& data, AgentTransferScene* scene = nullptr,
                EntityTransferContext* context = nullptr) override;
    void dump(std::ostream& output) const;
    void dump() const;
};
class CompleteAgentData : public AgentData {
  public:
    network::Value pack(EntityTransferContext* context = nullptr) override {
        return AgentData::pack(context);
    }
    void unpack(network::Value& data, AgentTransferScene* scene = nullptr,
                EntityTransferContext* context = nullptr) override {
        AgentData::unpack(data, scene, context);
    }
};
}
