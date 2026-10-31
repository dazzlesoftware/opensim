// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/agent_circuit.hpp"
#include "opensim/framework/avatar_appearance.hpp"
#include "opensim/framework/vector_text.hpp"
#include "opensim/network/osd_value.hpp"
#include <cmath>
#include <limits>
#include "opensim/framework/integer_text.hpp"
namespace opensim::framework {
namespace {
using network::Value;
Value uuid(const Uuid& id){Value v(id.str());v.kind=Value::Kind::Uuid;return v;}
Value text(const std::optional<std::string>& s){return Value(s.value_or(""));}
std::string object_text(const std::any& value){
    if(!value.has_value()||value.type()==typeid(std::nullptr_t))return {};
    if(auto p=std::any_cast<std::string>(&value))return *p;
    if(auto p=std::any_cast<const char*>(&value))return *p?*p:"";
    if(auto p=std::any_cast<bool>(&value))return *p?"True":"False";
    if(auto p=std::any_cast<std::int32_t>(&value))return std::to_string(*p);
    if(auto p=std::any_cast<std::uint32_t>(&value))return std::to_string(*p);
    if(auto p=std::any_cast<std::int64_t>(&value))return std::to_string(*p);
    if(auto p=std::any_cast<std::uint64_t>(&value))return std::to_string(*p);
    if(auto p=std::any_cast<std::int16_t>(&value))return std::to_string(*p);
    if(auto p=std::any_cast<std::uint16_t>(&value))return std::to_string(*p);
    if(auto p=std::any_cast<std::int8_t>(&value))return std::to_string(*p);
    if(auto p=std::any_cast<std::uint8_t>(&value))return std::to_string(*p);
    if(auto p=std::any_cast<float>(&value)){if(std::isinf(*p))return std::signbit(*p)?"-Infinity":"Infinity";auto text=vector3_string({*p,0,0});return text.substr(1,text.find(',')-1);}
    if(auto p=std::any_cast<double>(&value)){if(std::isinf(*p))return std::signbit(*p)?"-Infinity":"Infinity";Value v;v.kind=Value::Kind::Real;v.real=*p;auto text=network::osd_string(v);for(auto& c:text)if(c=='e')c='E';return text;}
    if(auto p=std::any_cast<Uuid>(&value))return p->str();
    if(auto p=std::any_cast<Vector3>(&value))return vector3_string(*p);
    if(auto p=std::any_cast<ServiceUrlStringifiable>(&value))return p->text();
    throw std::invalid_argument("Service URL object requires a native ToString adapter");
}
}
std::string service_url_object_text(const std::any& value){return object_text(value);}
network::Value AgentCircuitData::pack(EntityTransferContext* context){
    auto data=Value::object({{"agent_id",uuid(agent_id)},{"base_folder",uuid(base_folder)},{"caps_path",text(caps_path)}});
    if(children_cap_seeds&&!children_cap_seeds->empty()){
        Value seeds;seeds.kind=Value::Kind::Array;for(auto& [handle,seed]:*children_cap_seeds)seeds.array.push_back(Value::object({{"handle",Value(std::to_string(handle))},{"seed",text(seed)}}));data.map["children_seeds"]=seeds;
    }
    data.map["child"]=Value(child);data.map["circuit_code"]=Value(std::to_string(circuit_code));
    data.map["first_name"]=text(first_name);data.map["last_name"]=text(last_name);
    data.map["inventory_folder"]=uuid(inventory_folder);data.map["secure_session_id"]=uuid(secure_session_id);data.map["session_id"]=uuid(session_id);
    data.map["service_session_id"]=text(service_session_id);data.map["start_pos"]=Value(vector3_string(start_position));
    data.map["client_ip"]=text(ip_address);data.map["viewer"]=text(viewer());data.map["channel"]=text(channel);data.map["mac"]=text(mac);data.map["id0"]=text(id0);
    if(start_far>0){Value v;v.kind=Value::Kind::Real;v.real=start_far;data.map["far"]=v;}
    if(appearance){data.map["appearance_serial"]=Value(appearance->serial);data.map["packed_appearance"]=appearance->pack(context);}
    if(service_urls&&!service_urls->empty()){
        auto enumerated=service_urls; // A callback may replace the public field.
        Value old;old.kind=Value::Kind::Array;
        for(auto [key,value]:*enumerated){old.array.emplace_back(key);old.array.emplace_back(object_text(value));}
        data.map["service_urls"]=old;
    }
    if(service_urls&&!service_urls->empty()){
        auto enumerated=service_urls;
        auto modern=Value::object({});
        for(auto [key,value]:*enumerated)modern.set(key,Value(object_text(value)));
        data.map["serviceurls"]=modern;
    }
    return data;
}
void AgentCircuitData::unpack(const network::Value& data){
    if(data.kind!=Value::Kind::Map)throw std::invalid_argument("Circuit requires an OSD map");
    auto id=[&](const char* key,Uuid& v){if(data.contains(key))v=network::osd_uuid(data.get(key));};
    auto str=[&](const char* key,auto& v){if(data.contains(key))v=network::osd_string(data.get(key));};
    id("agent_id",agent_id);id("base_folder",base_folder);str("caps_path",caps_path);
    children_cap_seeds=std::make_shared<ChildrenSeeds>();
    if(auto& seeds=data.get("children_seeds");seeds.kind==Value::Kind::Array)for(auto& pair:seeds.array)if(pair.kind==Value::Kind::Map){
        std::uint64_t handle=0;if(pair.contains("handle")&&!try_parse_unsigned_integer(network::osd_string(pair.get("handle")),handle))continue;
        if(!children_cap_seeds->contains(handle)&&pair.contains("seed"))(*children_cap_seeds)[handle]=network::osd_string(pair.get("seed"));
    }
    if(data.contains("child"))child=network::osd_boolean(data.get("child"));
    if(data.contains("circuit_code"))try_parse_unsigned_integer(network::osd_string(data.get("circuit_code")),circuit_code);
    str("first_name",first_name);str("last_name",last_name);id("inventory_folder",inventory_folder);id("secure_session_id",secure_session_id);id("session_id",session_id);
    str("service_session_id",service_session_id);str("client_ip",ip_address);
    if(data.contains("viewer"))viewer(network::osd_string(data.get("viewer")));
    str("channel",channel);str("mac",mac);str("id0",id0);
    if(data.contains("teleport_flags"))teleport_flags=network::osd_unsigned(data.get("teleport_flags"));
    if(data.contains("start_pos"))parse_vector3(network::osd_string(data.get("start_pos")),start_position);
    if(data.contains("far"))start_far=static_cast<float>(network::osd_real(data.get("far")));
    try{
        appearance=std::make_shared<AvatarAppearance>();
        if(data.contains("appearance_serial"))appearance->serial=network::osd_integer(data.get("appearance_serial"));
        if(data.get("packed_appearance").kind==Value::Kind::Map)appearance->unpack(&data.get("packed_appearance"));
    }catch(const std::exception&){}
    service_urls=std::make_shared<ServiceUrls>();
    if(auto& modern=data.get("serviceurls");modern.kind==Value::Kind::Map){for(auto& key:modern.ordered_keys())(*service_urls)[key]=network::osd_string(modern.get(key));}
    else if(auto& old=data.get("service_urls");old.kind==Value::Kind::Array){for(std::size_t i=0;i+1<old.array.size();i+=2)(*service_urls)[network::osd_string(old.array[i])]=network::osd_string(old.array[i+1]);}
}
}
