// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/avatar_attachment.hpp"
#include "opensim/network/osd_value.hpp"
#include <stdexcept>
namespace opensim::framework {
AvatarAttachment::AvatarAttachment(const network::Value& map){unpack(map);}
network::Value AvatarAttachment::pack()const{
    network::Value item_value(item.str()),asset_value(asset.str());item_value.kind=asset_value.kind=network::Value::Kind::Uuid;
    auto result=network::Value::object({});result.set("point",network::Value(point));result.set("item",item_value);result.set("asset",asset_value);return result;
}
void AvatarAttachment::unpack(const network::Value& map){unpack(&map);}
void AvatarAttachment::unpack(const network::Value* map){
    if(!map||map->kind!=network::Value::Kind::Map)throw std::invalid_argument("Attachment requires an OSD map");
    if(map->contains("point"))point=network::osd_integer(map->get("point"));
    item=map->contains("item")?network::osd_uuid(map->get("item")):Uuid{};
    asset=map->contains("asset")?network::osd_uuid(map->get("asset")):Uuid{};
}
}
