// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/avatar_wearable.hpp"
#include "opensim/network/rpc.hpp"
#include "opensim/network/osd_value.hpp"
#include <stdexcept>
namespace opensim::framework {
namespace {
network::Value uuid(const Uuid& value){network::Value result(value.str());result.kind=network::Value::Kind::Uuid;return result;}
}
AvatarWearable::AvatarWearable(const network::Value& array){unpack(array);}
network::Value AvatarWearable::pack()const{
    network::Value result;result.kind=network::Value::Kind::Array;for(const auto& item:ids_){auto row=network::Value::object({});row.set("item",uuid(item));row.set("asset",uuid(asset(item)));result.array.push_back(std::move(row));}return result;
}
void AvatarWearable::unpack(const network::Value& array){if(array.kind!=network::Value::Kind::Array)throw std::invalid_argument("Wearable requires an OSD array");unpack(&array);}
void AvatarWearable::unpack(const network::Value* array){
    clear();if(!array)throw std::invalid_argument("Null wearable OSD array");
    if(array->kind!=network::Value::Kind::Array)throw std::invalid_argument("Wearable requires an OSD array");
    for(const auto& row:array->array){if(row.kind!=network::Value::Kind::Map)throw std::invalid_argument("Wearable entry requires an OSD map");add(network::osd_uuid(row.get("item")),network::osd_uuid(row.get("asset")));}
}
}
