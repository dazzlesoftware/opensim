// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/agent_delete.hpp"
#include <stdexcept>
#include "opensim/network/service_protocol.hpp"
namespace opensim::server {
std::optional<std::string> agent_delete_auth(std::string_view target){
    while(!target.empty()&&(target.back()==' '||target.back()=='\r'||target.back()=='\n'||target.back()=='\t'))target.remove_suffix(1);
    const auto question=target.find('?');if(question==std::string_view::npos)return std::nullopt;
    auto query=target.substr(question+1);query=query.substr(0,query.find('#'));
    auto decode=[](std::string_view value){std::string escaped;auto hex=[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');};for(std::size_t i=0;i<value.size();++i){auto c=value[i];if(c=='%'&&(i+2>=value.size()||!hex(value[i+1])||!hex(value[i+2])))escaped+="%25";else escaped+=c;}return network::service_url_decode(escaped);};
    bool found=false,exact=false;std::optional<std::string> result;std::size_t start=0;
    while(start<=query.size()){auto end=query.find('&',start);auto term=query.substr(start,end==std::string_view::npos?query.size()-start:end-start);auto equals=term.find('=');if(equals!=std::string_view::npos){auto key=decode(term.substr(0,equals));auto lower=key;for(auto& c:lower)if(c>='A'&&c<='Z')c+=32;if(lower=="auth"){auto value=decode(term.substr(equals+1));if(!found){found=true;exact=key=="auth";if(exact)result=value;}else if(exact)*result+=","+value;}}if(end==std::string_view::npos)break;start=end+1;}
    return result;
}
void AgentDeleteHandler::delete_agent(network::HttpResponse& response,const framework::Uuid& agent,const std::optional<std::string>& action,const framework::Uuid& region,const std::optional<std::string>& auth,const std::optional<std::string>& caller_address){
    // The original evaluates RemoteIPEndPoint during the empty-action log,
    // even when logging is disabled, before action.Equals and service access.
    if((!action||action->empty())&&!caller_address)throw std::invalid_argument("Null caller endpoint");
    if(!action)throw std::invalid_argument("Null action");
    if(*action=="release"){
        if(!service_)throw std::invalid_argument("Null simulation service");
        service_->release_agent(region,agent,"");
    }else{
        services::GridRegion destination;destination.id=region;
        auto service=service_;
        dispatch_([service,destination,agent,auth]() mutable {
            if(!service)throw std::invalid_argument("Null simulation service");
            service->close_agent(destination,agent,auth);
        },"AgentHandler.DoAgentDelete");
    }
    response.status=200;
    response.body="OpenSim agent "+agent.str();
}
}
