// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/entity_transfer_context.hpp"
#include "opensim/framework/uuid.hpp"
#include "opensim/network/rpc.hpp"
namespace opensim::framework {
class AgentTransferObject {
public:
    virtual ~AgentTransferObject() = default;
    virtual std::string to_xml2() = 0;
    virtual std::string extra_to_xml_string() = 0;
    virtual void extra_from_xml_string(const std::string& value) = 0;
    virtual bool has_group_changed() const = 0;
    virtual void has_group_changed(bool value) = 0;
};
class AgentTransferScene {
public:
    virtual ~AgentTransferScene() = default;
    virtual std::shared_ptr<AgentTransferObject> deserialize_object(const std::string& xml) = 0;
    virtual bool allow_script_crossings() const {
        return false;
    }
};
class IAgentData {
public:
    virtual ~IAgentData() = default;
    virtual Uuid agent_uuid() const = 0;
    virtual void agent_uuid(const Uuid& id) = 0;
    virtual network::Value pack(EntityTransferContext* context) = 0;
    virtual void unpack(network::Value& data, AgentTransferScene* scene, EntityTransferContext* context) = 0;
};
}
