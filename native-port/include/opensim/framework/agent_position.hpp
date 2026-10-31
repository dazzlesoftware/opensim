// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/entity_transfer_context.hpp"
#include "opensim/framework/agent_transfer.hpp"
#include "opensim/framework/ordered_dictionary.hpp"
#include "opensim/network/osd_value.hpp"
namespace opensim::framework {
struct ChildAgentDataUpdate {
    Uuid active_group_id,agent_id;
    bool always_run=false;
    float avatar_height=0,draw_distance=0,god_level=0;
    std::uint32_t group_access=0;
    Vector3 camera_position,position,velocity;
    std::uint64_t region_handle=0;
    network::BinaryData throttles;
};
class AgentPosition:public IAgentData {
public:
    Uuid agent_id,session_id;
    std::uint64_t region_handle=0;
    std::uint32_t circuit_code=0;
    float far=0;
    Vector3 position,velocity,center,size,at_axis,left_axis,up_axis;
    std::shared_ptr<network::Value> god_data;
    bool changed_grid=false;
    network::BinaryData throttles;
    using ChildrenSeeds=OrderedDictionary<std::uint64_t,std::optional<std::string>>;
    std::shared_ptr<ChildrenSeeds> children_cap_seeds;
    Uuid agent_uuid()const override{return agent_id;}
    void agent_uuid(const Uuid& value)override{agent_id=value;}
    network::Value pack(EntityTransferContext* context=nullptr)override{return static_cast<const AgentPosition&>(*this).pack(context);}
    network::Value pack(EntityTransferContext* context=nullptr)const;
    void unpack(network::Value& args,AgentTransferScene*,EntityTransferContext* context)override{unpack(args,context);}
    // Promotes retained OSD data to shared nodes, preserving caller mutations.
    void unpack(network::Value& args,EntityTransferContext* context=nullptr);
    void copy_from(const ChildAgentDataUpdate* child,const Uuid& session);
};
}
