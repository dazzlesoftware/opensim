// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/osd_value.hpp"
namespace opensim::framework {
struct AgentGroupData {
    Uuid group_id;
    std::uint64_t group_powers=0;
    bool accept_notices=false;
    AgentGroupData()=default;
    AgentGroupData(const Uuid& id,std::uint64_t powers,bool notices):group_id(id),group_powers(powers),accept_notices(notices){}
    explicit AgentGroupData(const network::Value* data){unpack(data);}
    network::Value pack()const;
    void unpack(const network::Value* data);
    void unpack(const network::Value& data){unpack(&data);}
};
struct ControllerData {
    Uuid object_id,item_id;
    std::uint32_t ignore_controls=0,event_controls=0;
    ControllerData()=default;
    ControllerData(const Uuid& object,const Uuid& item,std::uint32_t ignore,std::uint32_t events):object_id(object),item_id(item),ignore_controls(ignore),event_controls(events){}
    explicit ControllerData(const network::Value* data){unpack(data);}
    network::Value pack()const;
    void unpack(const network::Value* data);
    void unpack(const network::Value& data){unpack(&data);}
};
}
