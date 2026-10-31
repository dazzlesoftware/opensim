// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/login_recovery.hpp"
#include "opensim/framework/identity.hpp"
#include <httplib.h>
#include <chrono>
namespace opensim::services {
network::Value duplicate_login_message(const framework::Uuid& scope,const framework::Uuid& user,std::uint32_t timestamp,const std::string& key){
    using network::Value;
    auto value=Value::object({{"from_agent_id","6571e388-6218-4574-87db-f9379718315e"},{"from_agent_session",std::string(framework::ZeroUuid)},
        {"to_agent_id",user.str()},{"im_session_id",std::string(framework::ZeroUuid)},{"timestamp",std::to_string(timestamp)},
        {"from_agent_name","GRID"},{"message","New login detected"},{"from_group","FALSE"},{"parent_estate_id","0"},
        {"position_x","0"},{"position_y","0"},{"position_z","0"},{"region_id",scope.str()},{"binary_bucket","AA=="},
        {"dialog","+g=="},{"offline","AA=="},{"region_handle",Value(std::int32_t{0})}});
    if(!key.empty())value.map.emplace("message_key",Value(key));return value;
}
namespace {
bool send_kick(const std::string& url,const network::Value& message){
    try{
        const auto scheme=url.find("://");if(scheme==std::string::npos)return false;
        const auto separator=url.find_first_of("/?#",scheme+3);
        const auto origin=url.substr(0,separator);
        auto target=separator==std::string::npos?std::string("/"):url.substr(separator);
        if(target.starts_with('?'))target.insert(0,"/");
        if(const auto fragment=target.find('#');fragment!=std::string::npos)target.resize(fragment);
        if(target.empty())target="/";
        httplib::Client client(origin);if(!client.is_valid())return false;
        client.set_connection_timeout(10);client.set_read_timeout(10);client.set_write_timeout(10);client.set_max_timeout(10000);client.set_follow_location(true);
        const auto response=client.Post(target,network::xml_rpc_request({"grid_instant_message",{message}}),"text/xml");
        if(!response||response->status<200||response->status>=300)return false;
        const auto value=network::parse_xml_rpc_response(response->body);
        return value.kind==network::Value::Kind::Map&&value.get("success").kind==network::Value::Kind::String&&value.get("success").text=="TRUE";
    }catch(const std::exception&){return false;}
}
}
bool recover_duplicate_session(GridService& grid,GridUserService& users,const framework::Uuid& scope,const framework::Uuid& user,const GridUserInfo& info,const std::string& message_key){
    const auto region=grid.by_id(scope,info.last_region);if(!region)return false;
    const auto uri=region->server_uri();if(uri.empty())return false;
    const auto seconds=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    send_kick(uri,duplicate_login_message(scope,user,static_cast<std::uint32_t>(seconds),message_key));
    // The original ignores delivery success and clears stale grid-user state.
    users.logged_out(user.str(),{},info.last_region,info.last_position,info.last_look_at);
    return true;
}
}
