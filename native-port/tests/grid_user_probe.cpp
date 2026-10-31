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
#include "opensim/services/grid_user_service.hpp"
#include "opensim/framework/vector_text.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace opensim::services;
using namespace opensim::framework;
class TestStore:public GridUserStore {
public:
    SqliteGridUserStore database{":memory:",true};bool allowed=true;
    std::shared_ptr<GridUserData> get(const std::string& user)override{return database.get(user);}
    std::vector<std::shared_ptr<GridUserData>> get_all(const std::string& prefix)override{return database.get_all(prefix);}
    bool store(const std::shared_ptr<GridUserData>& row)override{return allowed&&database.store(row);}
};
int main(int argc,char** argv){try{
    if(argc==3&&std::string(argv[1])=="--initialize"){SqliteGridUserStore store(argv[2],true);return 0;}
    if(argc>1&&std::string(argv[1])=="--vectors"){std::string line;while(std::getline(std::cin,line)){Vector3 value;const auto parsed=parse_vector3(line,value);std::cout<<parsed<<'|'<<vector3_string(value)<<'\n';}return 0;}
    const auto seed=argc>1?std::stoi(argv[1]):0;auto id=[&](int number){std::ostringstream out;out<<"00000000-0000-0000-0000-"<<std::hex<<std::setfill('0')<<std::setw(12)<<(seed+1)*100+number;return Uuid(out.str());};
    TestStore store;double now=0;std::int32_t unix_seconds=1700000000+seed;auto cache=std::make_shared<GridUserCache>([&]{return now;});GridUserService service(store,[&]{return unix_seconds;},cache);const auto user=id(1).str();std::vector<std::string> rows;
    auto flag=[&](bool value){rows.push_back(value?"1":"0");};auto info=[&](const std::optional<GridUserInfo>& value){if(!value){rows.push_back("-");return;}const auto& v=*value;rows.push_back(v.user_id+"~"+v.home_region.str()+"~"+vector3_string(v.home_position)+"~"+vector3_string(v.home_look_at)+"~"+v.last_region.str()+"~"+vector3_string(v.last_position)+"~"+vector3_string(v.last_look_at)+"~"+(v.online?"True":"False")+"~"+std::to_string(v.login)+"~"+std::to_string(v.logout));};
    info(service.info(user));flag(service.set_home(user,id(2),{128.25f,-2.5f,70},{0,1,0}));info(service.info(user));info(service.logged_in(user));++unix_seconds;flag(service.set_last_position(user,id(99),id(3),{1e9f,.1f,-0.f},{1,0,0}));flag(service.logged_out(user,id(98),id(4),{3,4,5},{0,0,1}));info(service.info(user));
    for(const auto& result:service.info(std::vector<std::string>{user,"missing",user}))info(result);
    const auto short_user="short"+std::to_string(seed);info(service.info(short_user));info(service.logged_in(short_user));info(service.info(short_user));now=300001;cache->purge();info(service.info(short_user));
    const auto prefix=id(10).str();for(int i=0;i<4;++i){auto row=std::make_shared<GridUserData>();row->user_id=prefix+";grid"+std::to_string(i);(*row->data)["Login"]=i==2&&seed%2?"invalid":std::to_string(100+i*100);(*row->data)["Logout"]="0";(*row->data)["HomePosition"]=i==1?"prefix<1,2,3>suffix":"bad";store.store(row);}info(service.info(prefix+";ignored"));
    store.allowed=false;flag(service.set_home(user,id(20),{6,7,8},{0,1,0}));info(service.info(user));const auto phantom=id(21).str();info(service.logged_in(phantom));info(service.info(phantom));flag(static_cast<bool>(store.get(phantom)));store.allowed=true;
    const auto snapshot=store.get(user);std::map<std::string,std::string> sorted;for(const auto& pair:*snapshot->data)sorted[pair.first]=pair.second.value_or("");for(const auto& pair:sorted)rows.push_back(pair.first+"="+pair.second);
    for(std::size_t i=0;i<rows.size();++i){if(i)std::cout<<'|';std::cout<<rows[i];}std::cout<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
