// Controlled dependencies for HTTP tests of the native scene admission policy.
// These classes are not concrete world/estate/parcel implementations.
#pragma once
#include "opensim/services/scene_access.hpp"
#include "opensim/server/config.hpp"
struct HttpAccessParcel:opensim::services::AccessParcel {
    bool nearest=false;
    explicit HttpAccessParcel(bool n=false):nearest(n){}
    bool banned(const opensim::framework::Uuid& a)override{return a==id(6);}
    bool restricted(const opensim::framework::Uuid& a)override{return a==id(7);}
    bool banned_or_restricted(const opensim::framework::Uuid& a)override{return banned(a)||restricted(a);}
    std::optional<opensim::services::AccessPoint> nearest_point(const opensim::framework::Vector3&)override{return opensim::services::AccessPoint{64,64};}
};
struct HttpAccessEstate:opensim::services::AccessEstate {
    bool public_region;
    explicit HttpAccessEstate(bool value):public_region(value){}
    bool banned(const opensim::framework::Uuid& a,int)override{return a==id(4);}
    bool public_access()const override{return public_region;}
    bool has_access(const opensim::framework::Uuid&)override{return false;}
    std::optional<std::vector<opensim::framework::Uuid>> groups()const override{return std::vector{id(8)};}
    bool allow_direct_teleport()const override{return true;}
};
struct HttpAccessBackend:opensim::services::SceneAccessBackend {
    opensim::services::AccessRegion info;
    opensim::framework::AgentCircuitManager& circuits;
    bool crossing=true,last_existing=false;
    std::string first,last;
    int roots=0;
    HttpAccessBackend(int number,opensim::framework::AgentCircuitManager& manager,const opensim::server::IniConfig& config):circuits(manager){info.id=id(number);info.name="Scene"+std::to_string(number);info.estate=std::make_shared<HttpAccessEstate>(!config.boolean("Access","Private",false));crossing=config.boolean("Access","Crossing",true);roots=number==2?100:0;}
    opensim::services::AccessRegion& region()override{return info;}
    bool is_god(const opensim::framework::Uuid& a)override{return a==id(1);}
    bool is_estate_manager(const opensim::framework::Uuid& a)override{return a==id(2);}
    bool strict_access_control()const override{return true;}
    bool allow_avatar_crossing()const override{return crossing;}
    std::shared_ptr<opensim::framework::AgentCircuitData> circuit(const opensim::framework::Uuid& a)override{return circuits.get_agent_circuit_data(a);}
    bool has_authorization_service()const override{return true;}
    bool authorize_region(const std::shared_ptr<opensim::framework::AgentCircuitData>& a,const opensim::framework::Uuid&,std::string& reason)override{last_existing=circuits.get_agent_circuit_data(a->agent_id)==a;first=a->first_name.value_or("");last=a->last_name.value_or("");if(a->agent_id==id(9))throw std::runtime_error("fixture authorization");reason=a->agent_id==id(3)?"authorization denied":"";return a->agent_id!=id(3);}
    int user_flags(const opensim::framework::Uuid&)override{return 42;}
    bool has_groups_module()const override{return true;}
    std::optional<std::vector<std::optional<opensim::framework::Uuid>>> memberships(const opensim::framework::Uuid& a)override{if(a==id(5))return std::vector<std::optional<opensim::framework::Uuid>>{id(8)};return std::vector<std::optional<opensim::framework::Uuid>>{};}
    void recalculate_stats()override{}
    int root_agents()override{return roots;}
    int root_npcs()override{return 0;}
    std::shared_ptr<opensim::services::AccessTelehub> telehub(const opensim::framework::Uuid&)override{return {};}
    std::vector<std::shared_ptr<opensim::services::AccessSpawnPoint>> spawn_points()override{return {};}
    std::shared_ptr<opensim::services::AccessParcel> parcel(float,float)override{return std::make_shared<HttpAccessParcel>();}
    std::shared_ptr<opensim::services::AccessParcel> nearest_allowed_parcel(const opensim::framework::Uuid& a,float,float)override{return a==id(7)?std::make_shared<HttpAccessParcel>(true):nullptr;}
};
