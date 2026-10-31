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
#include "opensim/framework/text.hpp"
#include <filesystem>
#include <iostream>
using namespace opensim::services;
using namespace opensim::framework;
namespace {
int failures=0;
void check(bool value,const char* name){if(!value){++failures;std::cerr<<"FAIL: "<<name<<'\n';}}
void tests(const std::string& path){
    const auto user=Uuid::random().str();const auto region=Uuid::random(),home=Uuid::random();double now=0;std::int32_t unix_seconds=1700000000;const Vector3 position{128.25f,-2,70},look{0,1,0};
    {
        SqliteGridUserStore store(path,true);auto cache=std::make_shared<GridUserCache>([&]{return now;});GridUserService service(store,[&]{return unix_seconds;},cache);
        check(!service.info(user),"new user has no grid record");check(service.set_home(user,home,position,look),"home persists");auto info=*service.info(user);check(info.home_region==home&&info.home_position==position&&!info.online&&info.login==0,"home fields and absent login defaults");
        info=service.logged_in(user);check(info.online&&info.login==unix_seconds,"logged-in time and online state");++unix_seconds;check(service.set_last_position(user,Uuid::random(),region,look,position),"last position persists with ignored session argument");check(service.logged_out(user,Uuid::random(),region,position,look),"logout persists location and time");info=*service.info(user);check(!info.online&&info.logout==unix_seconds&&info.last_region==region&&info.last_position==position,"logout state");
        auto multiple=service.info(std::vector<std::string>{user,"missing",user});check(multiple.size()==3&&multiple[0]&&!multiple[1]&&multiple[2],"multiple lookup preserves missing entries and order");
        const auto short_user="short";check(!service.info(short_user),"short user negative lookup cached");service.logged_in(short_user);check(!service.info(short_user),"short-ID update preserves original negative-cache behavior");now=300001;cache->purge();check(service.info(short_user)->online,"short record becomes visible after cache purge");
        const auto prefix=Uuid::random().str();for(int i=0;i<3;++i){auto row=std::make_shared<GridUserData>();row->user_id=prefix+";grid"+std::to_string(i);(*row->data)["Login"]=std::to_string(100+i);(*row->data)["Logout"]=std::to_string(500-i);store.store(row);}check(service.info(prefix)->user_id==prefix+";grid0","prefix lookup selects greatest login/logout timestamp");
        check(store.get_all("short").size()==1&&store.get_all("sh_rt").size()==1,"SQLite prefix LIKE wildcard behavior retained");auto row=store.get(user);row->data=std::make_shared<GridUserData::Fields>();(*row->data)["Online"]="True";store.store(row);auto replaced=store.get(user);check(replaced->data->at("HomeRegionID")==Uuid{}.str()&&replaced->data->at("Login")=="0","REPLACE resets omitted grid-user columns to schema defaults");
        service.logged_in(Uuid::random().str());check(service.recently_online()>=1,"recent online console count");
    }
    {SqliteGridUserStore store(path);auto row=store.get(user);check(row&&row->data->at("Online")=="True","grid-user database survives reopen");}
    Vector3 value;check(parse_vector3("prefix<1, 2, 3>suffix",value)&&value==Vector3{1,2,3},"vector parser source prefix/suffix behavior");check(!parse_vector3("1,2,3",value)&&value==Vector3{},"vector parser minimum-length rule");check(vector3_string({.1f,1e9f,-0.f})=="<0.1, 1E+09, -0>","vector general format and signed zero");check(truncate_utf16(std::string(35,'a')+"\xf0\x9f\x98\x80",36)==std::string(35,'a')+"\xef\xbf\xbd","UTF-16 prefix boundary");
}
}
int main(){const auto path=(std::filesystem::temp_directory_path()/("opensim-grid-user-"+Uuid::random().str()+".db")).string();try{tests(path);}catch(const std::exception& error){std::cerr<<error.what()<<'\n';std::filesystem::remove(path);return 1;}std::filesystem::remove(path);return failures?1:0;}
