// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/entity_transfer_context.hpp"
#include "opensim/network/osd_value.hpp"
#include <stdexcept>
namespace opensim::framework {
network::Value EntityTransferContext::pack()const{
    network::Value inbound,outbound;inbound.kind=outbound.kind=network::Value::Kind::Real;
    inbound.real=static_cast<double>(inbound_version);outbound.real=static_cast<double>(outbound_version);
    return network::Value::object({{"InboundVersion",inbound},{"OutboundVersion",outbound},{"WearablesCount",network::Value(static_cast<std::int32_t>(wearables_count))}});
}
void EntityTransferContext::unpack(const network::Value& map){unpack(&map);}
void EntityTransferContext::unpack(const network::Value* map){
    if(!map||map->kind!=network::Value::Kind::Map)throw std::invalid_argument("Transfer context requires an OSD map");
    if(map->contains("InboundVersion"))inbound_version=static_cast<float>(network::osd_real(map->get("InboundVersion")));
    if(map->contains("OutboundVersion"))outbound_version=static_cast<float>(network::osd_real(map->get("OutboundVersion")));
    if(map->contains("WearablesCount"))wearables_count=network::osd_integer(map->get("WearablesCount"));
}
}
