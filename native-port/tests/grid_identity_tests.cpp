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
#include "opensim/services/grid_service.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace opensim;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){const auto path=std::filesystem::temp_directory_path()/("opensim-identity-"+framework::Uuid::random().str()+".ini");try{
 framework::RegionUri::Resolver resolver=[](const std::string& host)->std::optional<std::string>{return host=="bad.invalid"?std::nullopt:std::optional<std::string>{"203.0.113.7"};};server::IniConfig empty;server::GridIdentity missing(empty,std::nullopt,resolver);check(!missing.has_hypergrid&&!missing.gate_uri()&&missing.gate_uri_slash().empty()&&!missing.economy_url,"ignore-missing default struct/null behavior");bool threw=false;try{server::GridIdentity required(empty,"",resolver);}catch(const std::runtime_error& error){threw=std::string(error.what())=="GatekeeperURI configuration error";}check(threw,"empty default requires gatekeeper");
 {std::ofstream file(path);file<<"[Const]\nGatekeeperURI=http://gate.example\nHomeURI=https://home.example:9000\nGatekeeperURIAlias=alias.example,https://alias.example:80, bad/path\nHomeURIAlias=homealias.example\nStunServers=stun.example:3478,stun.example:3478,bad/path\n[GridInfo]\nGridName=Identity Grid\n";}
 auto config=server::IniConfig::load(path);server::GridIdentity info(config,std::nullopt,resolver);check(info.has_hypergrid&&info.gate_uri()=="http://gate.example:80"&&info.home_uri()=="https://home.example:9000","host configuration");check(info.grid_aliases->size()==1&&info.is_local_grid("https://alias.example:80")==1,"alias identity ignores scheme and suppresses duplicate host/port");check(info.is_local_home("homealias.example")==1&&info.is_local_grid("bad/path")==-1&&info.is_local_grid(std::nullopt)==1,"local home/invalid/empty rules");check(info.is_local_grid("http://bad.invalid",true)==-2&&info.is_local_grid("http://elsewhere.example",true)==0,"DNS result classification");check(info.stun_servers->size()==2,"STUN duplicates retained");info.add_grid_aliases(std::vector<std::string>{});check(info.grid_aliases->size()==1,"empty alias setter preserves state");info.set_stuns(std::vector<std::string>{});check(!info.stun_servers,"empty STUN setter clears state");info.set_grid_url("http://new.example");check(info.grid_url=="http://new.example:80"&&info.is_local_grid("gate.example")==1&&info.is_local_grid("new.example")==0,"grid URL display setter does not change identity");
 services::NullRegionStore store("!static");services::GridService service(store,config,[]{return 1700000000;},{},{},resolver);services::GridRegion region;region.id=framework::Uuid::random();region.name="Region";region.x=25600;region.y=25600;region.internal_endpoint=services::RegionEndpoint{"0.0.0.0",9000};region.external_host="127.0.0.1";check(service.register_region({},region).empty(),"register identity test region");check(service.by_name({},"http://gate.example/Region")->id==region.id&&service.local_by_name({},"http://alias.example/Region")->id==region.id,"service uses configured native grid identity");
 std::filesystem::remove(path);std::cout<<"PASS: grid identity configuration and registry integration\n";return 0;
}catch(const std::exception& error){std::filesystem::remove(path);std::cerr<<error.what()<<'\n';return 1;}}
