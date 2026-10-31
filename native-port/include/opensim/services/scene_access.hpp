// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/agent_circuit.hpp"
#include "opensim/framework/quaternion_text.hpp"
#include <vector>
namespace opensim::services {
struct AccessPoint {float x=0,y=0;};
class AccessParcel {
public:
    virtual ~AccessParcel()=default;
    virtual bool banned(const framework::Uuid& agent)=0;
    virtual bool restricted(const framework::Uuid& agent)=0;
    virtual bool banned_or_restricted(const framework::Uuid& agent)=0;
    virtual std::optional<AccessPoint> nearest_point(const framework::Vector3& position)=0;
};
class AccessEstate {
public:
    virtual ~AccessEstate()=default;
    virtual bool banned(const framework::Uuid& agent,int flags)=0;
    virtual bool public_access()const=0;
    virtual bool has_access(const framework::Uuid& agent)=0;
    virtual std::optional<std::vector<framework::Uuid>> groups()const=0;
    virtual bool allow_direct_teleport()const=0;
};
class AccessTelehub {
public:
    virtual ~AccessTelehub()=default;
    virtual framework::Vector3 position()const=0;
    virtual framework::Quaternion rotation()const=0;
};
class AccessSpawnPoint {
public:
    virtual ~AccessSpawnPoint()=default;
    virtual framework::Vector3 location(const framework::Vector3& origin,const framework::Quaternion& rotation)const=0;
};
struct AccessRegion {
    framework::Uuid id,scope,telehub;
    std::optional<std::string> name;
    std::uint32_t size_x=256,size_y=256;
    int agent_limit=100;
    std::shared_ptr<AccessEstate> estate;
};
class SceneAccessBackend {
public:
    virtual ~SceneAccessBackend()=default;
    virtual AccessRegion& region()=0;
    virtual bool is_god(const framework::Uuid& agent)=0;
    virtual bool is_estate_manager(const framework::Uuid& agent)=0;
    virtual bool strict_access_control()const=0;
    virtual bool allow_avatar_crossing()const=0;
    virtual std::shared_ptr<framework::AgentCircuitData> circuit(const framework::Uuid& agent)=0;
    virtual bool has_authorization_service()const=0;
    virtual bool authorize_region(const std::shared_ptr<framework::AgentCircuitData>& agent,const framework::Uuid& region,std::string& reason)=0;
    virtual int user_flags(const framework::Uuid& agent)=0;
    virtual bool has_groups_module()const=0;
    virtual std::optional<std::vector<std::optional<framework::Uuid>>> memberships(const framework::Uuid& agent)=0;
    virtual void recalculate_stats()=0;
    virtual int root_agents()=0;
    virtual int root_npcs()=0;
    virtual std::shared_ptr<AccessTelehub> telehub(const framework::Uuid& id)=0;
    virtual std::vector<std::shared_ptr<AccessSpawnPoint>> spawn_points()=0;
    virtual std::shared_ptr<AccessParcel> parcel(float x,float y)=0;
    virtual std::shared_ptr<AccessParcel> nearest_allowed_parcel(const framework::Uuid& agent,float x,float y)=0;
};
class SceneAccess {
public:
    explicit SceneAccess(SceneAccessBackend& backend):backend_(backend){}
    virtual ~SceneAccess()=default;
    bool query_access(const framework::Uuid& agent,const std::optional<std::string>& home,bool teleport,const framework::Vector3& position,std::vector<framework::Uuid>* features,std::string& reason);
    virtual bool authorize_user(std::shared_ptr<framework::AgentCircuitData> agent,bool bypass,std::string& reason);
    bool check_land_position_access(const framework::Uuid& agent,bool not_crossing,bool check_telehub,bool check_agent_count,const framework::Vector3& position,std::string& reason);
    bool test_land_restrictions(const framework::Uuid& agent,std::string& reason,float& x,float& y);
private:
    SceneAccessBackend& backend_;
};
}
