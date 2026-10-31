/*
Copyright (c) Contributors, http://opensimulator.org/
See CONTRIBUTORS.TXT for a full list of copyright holders.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenSimulator Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "opensim/server/grid_identity.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <stdexcept>
namespace opensim::server {
namespace {
std::vector<std::string> split(const std::string& value){std::vector<std::string> result;std::size_t start=0;while(start<=value.size()){const auto end=value.find(',',start);result.push_back(value.substr(start,end==std::string::npos?end:end-start));if(end==std::string::npos)break;start=end+1;}return result;}
void add(std::vector<framework::HttpHost>& aliases,const std::string& value){framework::HttpHost host(framework::trim_utf8(value));if(host.valid()&&std::none_of(aliases.begin(),aliases.end(),[&](const auto& old){return old.equals(host);}))aliases.push_back(host);}
}
GridIdentity::GridIdentity(const IniConfig& config,const std::optional<std::string>& fallback,framework::RegionUri::Resolver resolver):resolver_(std::move(resolver)){
    const auto sections=[&](const std::string& key,std::initializer_list<const char*> names,std::optional<std::string> value=std::string{}){for(const auto* name:names){const auto fields=config.section(name);if(fields.contains(key))value=fields.at(key);}return value;};
    auto gatekeeper=sections("GatekeeperURI",{"Const","Startup","Hypergrid"}).value_or("");if(gatekeeper.empty())gatekeeper=config.get("GatekeeperService","ExternalName");if(gatekeeper.empty())gatekeeper=config.get("GridService","Gatekeeper");bool ignore_missing=false;
    if(gatekeeper.empty()){if(!fallback)ignore_missing=true;else if(!fallback->empty())gate_=framework::HttpHost(fallback,true,resolver_);}else{gate_=framework::HttpHost(gatekeeper,true,resolver_);has_hypergrid=true;}
    if(!ignore_missing&&(!gate_||!gate_->resolved()))throw std::runtime_error("GatekeeperURI configuration error");grid_url=gate_uri();const auto alias=sections("GatekeeperURIAlias",{"Const","Startup","Hypergrid"}).value_or("");if(!framework::trim_utf8(alias).empty())add_grid_aliases(split(alias));
    const auto home=sections("HomeURI",{"Const","Startup","Hypergrid"}).value_or("");if(home.empty()){if(!gatekeeper.empty())home_=gate_;else if(fallback&&!fallback->empty())home_=framework::HttpHost(fallback,true,resolver_);}else home_=framework::HttpHost(home,true,resolver_);
    if(!home_||!home_->resolved()){if(!ignore_missing)throw std::runtime_error("HomeURI configuration error");}else if(ignore_missing){gate_=home_;grid_url=gate_uri();}
    const auto homes=sections("HomeURIAlias",{"Const","Startup","Hypergrid"}).value_or("");if(!framework::trim_utf8(homes).empty())for(const auto& value:split(homes))add(home_aliases_,value);
    name=sections("GridName",{"Const","GridInfo","SimulatorFeatures"}).value_or("");if(name.empty())name=sections("gridname",{"Const","GridInfo","SimulatorFeatures"}).value_or("");nick=sections("GridNick",{"Const","GridInfo","SimulatorFeatures"}).value_or("");if(name.empty())name="Another bad configured grid";
    const auto uri=[&](const std::string& value,const char* error){framework::HttpUri parsed(framework::trim_utf8(value),true,resolver_);if(!parsed.resolved())throw std::runtime_error(error);return parsed.uri;};
    search_url=sections("SearchServerURI",{"Const","GridInfo","SimulatorFeatures"}).value_or("");if(!search_url.empty())search_url=uri(search_url,"SearchServerURI configuration error");destination_url=sections("DestinationGuideURI",{"Const","GridInfo","SimulatorFeatures"}).value_or("");if(destination_url.empty())destination_url=sections("DestinationGuide",{"Const","GridInfo","SimulatorFeatures"}).value_or("");if(!destination_url.empty())destination_url=uri(destination_url,"DestinationGuideURL configuration error");economy_url=sections("economy",{"Economy","GridInfo"},std::nullopt);if(economy_url&&!economy_url->empty())economy_url=uri(*economy_url,"economyURL configuration error");const auto stun=sections("StunServers",{"Const","Startup","GridInfo","SimulatorFeatures"}).value_or("");if(!framework::trim_utf8(stun).empty())set_stuns(split(stun));
}
std::optional<std::string> GridIdentity::gate_uri()const{return gate_?std::optional{gate_->uri}:std::nullopt;}
std::string GridIdentity::gate_uri_slash()const{return gate_?gate_->uri_slash():"";}
std::optional<std::string> GridIdentity::home_uri()const{return home_?std::optional{home_->uri}:std::nullopt;}
std::string GridIdentity::home_uri_slash()const{return home_?home_->uri_slash():"";}
int GridIdentity::is_local_grid(const framework::HttpHost& value)const{if(!value.valid())return (value.flags&framework::HttpHost::Empty)==0?-1:1;if(gate_&&value.equals(*gate_))return 1;for(const auto& alias:gate_aliases_)if(value.equals(alias))return 1;return 0;}
int GridIdentity::is_local_grid(const std::optional<std::string>& value,bool resolve_check)const{framework::HttpHost host(value);const auto result=is_local_grid(host);if(result!=0||!resolve_check)return result;return host.resolve_dns(resolver_)?0:-2;}
int GridIdentity::is_local_home(const std::optional<std::string>& value,bool resolve_check)const{framework::HttpHost host(value);if(!host.valid())return (host.flags&framework::HttpHost::Empty)==0?-1:1;if(home_&&host.equals(*home_))return 1;for(const auto& alias:home_aliases_)if(host.equals(alias))return 1;return resolve_check&&!host.resolve_dns(resolver_)?-2:0;}
void GridIdentity::add_grid_aliases(const std::optional<std::vector<std::string>>& values){if(!values)throw std::invalid_argument("Null aliases");if(values->empty())return;for(const auto& value:*values)add(gate_aliases_,value);if(!gate_aliases_.empty()){grid_aliases=std::vector<std::string>{};for(const auto& alias:gate_aliases_)grid_aliases->push_back(alias.uri);}}
void GridIdentity::set_stuns(const std::optional<std::vector<std::string>>& values){if(!values)throw std::invalid_argument("Null STUN servers");std::vector<std::string> result;for(const auto& value:*values){framework::HttpHost host(framework::trim_utf8(value));if(host.valid())result.push_back("stun:"+host.host_port());}stun_servers=result.empty()?std::nullopt:std::optional{result};}
void GridIdentity::set_grid_url(const std::optional<std::string>& value){framework::HttpHost host(value,true,resolver_);if(host.resolved())grid_url=host.uri;}
void GridIdentity::set_search_url(const std::optional<std::string>& value){framework::HttpUri uri(value,true,resolver_);if(uri.resolved())search_url=uri.uri;}
void GridIdentity::set_destination_url(const std::optional<std::string>& value){framework::HttpUri uri(value,true,resolver_);if(uri.resolved())destination_url=uri.uri;}
void GridIdentity::set_economy_url(const std::optional<std::string>& value){framework::HttpUri uri(value,true,resolver_);if(uri.resolved())economy_url=uri.uri;}
}
