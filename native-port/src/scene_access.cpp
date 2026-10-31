// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/scene_access.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>
namespace opensim::services {
using framework::Uuid;
namespace {AccessEstate& estate(AccessRegion& region){if(!region.estate)throw std::invalid_argument("Null estate settings");return *region.estate;}}
bool SceneAccess::query_access(const Uuid& agent,const std::optional<std::string>&,bool teleport,const framework::Vector3& position,std::vector<Uuid>*,std::string& reason){
    reason.clear();if(backend_.is_god(agent))return true;
    if(!backend_.allow_avatar_crossing()&&!teleport){reason="Region Crossing not allowed";return false;}
    auto circuit=backend_.circuit(agent);if(!circuit){circuit=std::make_shared<framework::AgentCircuitData>();circuit->agent_id=agent;circuit->first_name="";circuit->last_name="";}
    try{if(!authorize_user(circuit,false,reason))return false;}catch(const std::exception& e){reason="Error authorizing agent: "+std::string(e.what());return false;}
    return check_land_position_access(agent,teleport,true,true,position,reason);
}
bool SceneAccess::authorize_user(std::shared_ptr<framework::AgentCircuitData> agent,bool bypass,std::string& reason){
    reason.clear();if(!agent)throw std::invalid_argument("Null agent circuit");
    if(agent->agent_id==Uuid("6571e388-6218-4574-87db-f9379718315e"))return false;
    if(!backend_.strict_access_control())return true;if(backend_.is_god(agent->agent_id))return true;
    if(backend_.has_authorization_service()&&!backend_.authorize_region(agent,backend_.region().id,reason))return false;
    if(!bypass){
        if(!backend_.region().estate)return true;
        const int flags=backend_.user_flags(agent->agent_id);
        if(estate(backend_.region()).banned(agent->agent_id,flags)){reason="Denied access to region "+backend_.region().name.value_or("")+": You have been banned from that region.";return false;}
        if(estate(backend_.region()).public_access())return true;
        if(estate(backend_.region()).has_access(agent->agent_id))return true;
        bool access=false;
        if(backend_.has_groups_module()){
            auto groups=estate(backend_.region()).groups();
            if(groups&&!groups->empty()){
                auto memberships=backend_.memberships(agent->agent_id);
                if(memberships&&!memberships->empty()){
                    std::vector<Uuid> ids;for(const auto& membership:*memberships){if(!membership)throw std::invalid_argument("Object reference not set to an instance of an object.");ids.push_back(*membership);}
                    for(const auto& group:*groups)if(std::find(ids.begin(),ids.end(),group)!=ids.end()){access=true;break;}
                }
            }
        }
        if(!access){reason="Denied access to private region "+backend_.region().name.value_or("")+": You do not have access to that region.";return false;}
    }
    return true;
}
bool SceneAccess::test_land_restrictions(const Uuid& agent,std::string& reason,float& x,float& y){
    if(x<0)x=0;else if(x>=static_cast<float>(backend_.region().size_x))x=static_cast<float>(backend_.region().size_x)-.5f;
    if(y<0)y=0;else if(y>=static_cast<float>(backend_.region().size_y))y=static_cast<float>(backend_.region().size_y)-.5f;
    reason.clear();if(backend_.is_god(agent))return true;
    auto land=backend_.parcel(x,y);if(!land)return false;
    bool banned=land->banned(agent),restricted=land->restricted(agent);
    if(banned||restricted){
        auto nearest=backend_.nearest_allowed_parcel(agent,x,y);std::optional<AccessPoint> position;if(nearest)position=nearest->nearest_point({x,y,0});
        if(!position){reason=banned?"Cannot regioncross into banned parcel.":"Denied access to private region "+backend_.region().name.value_or("")+": You are not on the access list for that region.";return false;}
        x=position->x;y=position->y;
    }
    reason.clear();return true;
}
bool SceneAccess::check_land_position_access(const Uuid& agent,bool not_crossing,bool check_telehub,bool,const framework::Vector3& position,std::string& reason){
    reason.clear();if(backend_.is_god(agent))return true;if(backend_.is_estate_manager(agent))return true;
    backend_.recalculate_stats();int count=backend_.root_agents();count=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(count)-static_cast<std::uint32_t>(backend_.root_npcs()));
    if(count>=backend_.region().agent_limit){reason="The region is full";return false;}
    if(not_crossing){
        if(!estate(backend_.region()).allow_direct_teleport()&&!backend_.region().telehub.is_zero()){
            auto hub=backend_.telehub(backend_.region().telehub);
            if(hub&&check_telehub){
                bool banned=true,valid=false;auto points=backend_.spawn_points();auto origin=hub->position();
                if(points.empty()){auto land=backend_.parcel(origin.x,origin.y);if(land&&!land->banned_or_restricted(agent)){banned=false;valid=true;}}
                else{auto rotation=hub->rotation();for(const auto& point:points){if(!point)throw std::invalid_argument("Null spawn point");auto location=point->location(origin,rotation);auto land=backend_.parcel(location.x,location.y);if(!land)continue;valid=true;if(!land->banned_or_restricted(agent)){banned=false;break;}}}
                if(valid){if(banned){reason="No suitable landing point found";return false;}return true;}
            }
        }
        float x=position.x,y=position.y;if(!test_land_restrictions(agent,reason,x,y)){reason="You dont have access to the region parcels";return false;}
    }else{
        auto land=backend_.parcel(position.x,position.y);if(!land){reason="No parcel found";return false;}
        bool banned=land->banned(agent),restricted=land->restricted(agent);if(banned||restricted){reason=banned?"You are banned from the parcel":"The parcel is restricted";return false;}
    }
    return true;
}
}
