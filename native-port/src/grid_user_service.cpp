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
#include "opensim/framework/text.hpp"
#include "opensim/framework/vector_text.hpp"
#include <bit>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
namespace opensim::services {
using framework::Uuid;
using framework::Vector3;
namespace {
GridUserData::Fields& fields(const std::shared_ptr<GridUserData::Fields>& value){if(!value)throw std::invalid_argument("Null grid-user Data");return *value;}
double milliseconds(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
std::int32_t integer(double value){return value>=-2147483648.0&&value<2147483648.0?static_cast<std::int32_t>(value):std::numeric_limits<std::int32_t>::min();}
std::string trimmed(const std::string& value){auto text=framework::utf16(value);std::u16string_view view=text;while(!view.empty()&&framework::unicode_whitespace(view.front()))view.remove_prefix(1);while(!view.empty()&&framework::unicode_whitespace(view.back()))view.remove_suffix(1);return framework::utf8(view);}
std::int32_t parsed_int(const std::optional<std::string>& value){auto text=trimmed(value.value());if(!text.empty()&&text.front()=='+')text.erase(0,1);std::int32_t result=0;auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())throw std::invalid_argument("Invalid grid-user Int32");return result;}
bool parsed_bool(const std::optional<std::string>& value){auto text=trimmed(value.value());for(auto& c:text)if(c>='A'&&c<='Z')c=static_cast<char>(c+32);if(text=="true")return true;if(text=="false")return false;throw std::invalid_argument("Invalid grid-user Boolean");}
std::u16string cache_key(const std::string& user){auto key=framework::utf16(user);if(key.size()>36)key.resize(36);return key;}
}
struct GridUserCache::State {
    struct Entry {std::shared_ptr<GridUserData> value;std::int32_t expires;};
    std::recursive_mutex mutex;std::condition_variable_any changed;Clock clock;double start;bool stopping=false;std::optional<double> next;std::thread worker;
    std::unordered_map<std::u16string,Entry> entries;
    explicit State(Clock input):clock(input?input:Clock{milliseconds}),start(clock()){
        if(!input)worker=std::thread([this]{std::unique_lock lock(mutex);while(!stopping){if(!next){changed.wait(lock,[&]{return stopping||next;});continue;}if(clock()<*next){changed.wait_for(lock,std::chrono::duration<double,std::milli>(*next-clock()));continue;}purge();}});
    }
    ~State(){{std::lock_guard lock(mutex);stopping=true;changed.notify_all();}if(worker.joinable())worker.join();}
    std::int32_t now()const{return integer(clock()-start);}
    void purge(){const auto timestamp=now();std::erase_if(entries,[&](const auto& pair){return pair.second.expires>0&&pair.second.expires<timestamp;});next=entries.empty()?std::nullopt:std::optional{clock()+100000};}
    void add(const std::u16string& key,const std::shared_ptr<GridUserData>& value){if(entries.empty())next=clock()+100000;entries[key]={value,std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(now())+300000u)};changed.notify_all();}
};
GridUserCache::GridUserCache(Clock clock):state_(std::make_unique<State>(std::move(clock))){}
GridUserCache::~GridUserCache()=default;
void GridUserCache::purge(){std::lock_guard lock(state_->mutex);state_->purge();}
GridUserService::GridUserService(GridUserStore& store,UnixClock clock,std::shared_ptr<GridUserCache> cache):store_(store),clock_(std::move(clock)),cache_(std::move(cache)){
    if(!clock_)clock_=[] {return integer(std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count());};
    if(!cache_){static auto shared=std::make_shared<GridUserCache>();cache_=shared;}
}
std::shared_ptr<GridUserData> GridUserService::data(const std::string& user){
    auto& cache=*cache_->state_;std::lock_guard lock(cache.mutex);const auto key=cache_key(user);const auto cached=cache.entries.find(key);if(cached!=cache.entries.end())return cached->second.value;
    const auto rows=store_.get_all(framework::utf8(key));if(rows.empty()){cache.add(key,nullptr);return nullptr;}auto selected=rows.front();
    if(rows.size()>1)try{auto current=std::max(parsed_int(fields(selected->data).at("Login")),parsed_int(fields(selected->data).at("Logout")));for(std::size_t i=1;i<rows.size();++i){const auto recent=std::max(parsed_int(fields(rows[i]->data).at("Login")),parsed_int(fields(rows[i]->data).at("Logout")));if(recent>current){current=recent;selected=rows[i];}}}catch(...){ }
    cache.add(key,selected);return selected;
}
GridUserInfo GridUserService::to_info(const GridUserData& row){
    GridUserInfo result;result.user_id=row.user_id.value_or("");
    const auto uuid=[&](const std::string& key,Uuid& target){const auto found=fields(row.data).find(key);if(found!=fields(row.data).end())try{target=Uuid(found->second.value());}catch(...){target={};}};
    const auto vector=[&](const std::string& key,Vector3& target){const auto found=fields(row.data).find(key);if(found!=fields(row.data).end())framework::parse_vector3(found->second.value_or(""),target);};
    uuid("HomeRegionID",result.home_region);uuid("LastRegionID",result.last_region);vector("HomePosition",result.home_position);vector("HomeLookAt",result.home_look_at);vector("LastPosition",result.last_position);vector("LastLookAt",result.last_look_at);
    if(fields(row.data).contains("Online"))try{result.online=parsed_bool(fields(row.data).at("Online"));}catch(...){ }
    if(fields(row.data).contains("Login"))try{result.login=parsed_int(fields(row.data).at("Login"));}catch(...){ }
    if(fields(row.data).contains("Logout"))try{result.logout=parsed_int(fields(row.data).at("Logout"));}catch(...){ }
    return result;
}
std::optional<GridUserInfo> GridUserService::info(const std::string& user){auto& cache=*cache_->state_;std::lock_guard lock(cache.mutex);const auto row=data(user);return row?std::optional{to_info(*row)}:std::nullopt;}
std::vector<std::optional<GridUserInfo>> GridUserService::info(const std::vector<std::string>& users){std::vector<std::optional<GridUserInfo>> result;for(const auto& user:users)result.push_back(info(user));return result;}
void GridUserService::update_cache(const std::string& user,const std::shared_ptr<GridUserData>& value){if(framework::utf16(user).size()>=36)cache_->state_->add(cache_key(user),value);}
GridUserInfo GridUserService::logged_in(const std::string& user){auto& cache=*cache_->state_;std::lock_guard lock(cache.mutex);auto row=data(user);if(!row){row=std::make_shared<GridUserData>();row->user_id=user;}fields(row->data)["Online"]="True";fields(row->data)["Login"]=std::to_string(clock_());store_.store(row);update_cache(user,row);return to_info(*row);}
bool GridUserService::logged_out(const std::string& user,const Uuid&,const Uuid& region,const Vector3& position,const Vector3& look_at){
    auto& cache=*cache_->state_;std::lock_guard lock(cache.mutex);auto row=data(user);if(!row){row=std::make_shared<GridUserData>();row->user_id=user;}fields(row->data)["Online"]="False";fields(row->data)["Logout"]=std::to_string(clock_());fields(row->data)["LastRegionID"]=region.str();fields(row->data)["LastPosition"]=framework::vector3_string(position);fields(row->data)["LastLookAt"]=framework::vector3_string(look_at);if(!store_.store(row))return false;update_cache(user,row);return true;
}
bool GridUserService::set_home(const std::string& user,const Uuid& region,const Vector3& position,const Vector3& look_at){
    auto& cache=*cache_->state_;std::lock_guard lock(cache.mutex);auto row=data(user);if(!row){row=std::make_shared<GridUserData>();row->user_id=user;}fields(row->data)["HomeRegionID"]=region.str();fields(row->data)["HomePosition"]=framework::vector3_string(position);fields(row->data)["HomeLookAt"]=framework::vector3_string(look_at);if(!store_.store(row))return false;update_cache(user,row);return true;
}
bool GridUserService::set_last_position(const std::string& user,const Uuid&,const Uuid& region,const Vector3& position,const Vector3& look_at){
    auto& cache=*cache_->state_;std::lock_guard lock(cache.mutex);auto row=data(user);if(!row){row=std::make_shared<GridUserData>();row->user_id=user;}fields(row->data)["LastRegionID"]=region.str();fields(row->data)["LastPosition"]=framework::vector3_string(position);fields(row->data)["LastLookAt"]=framework::vector3_string(look_at);if(!store_.store(row))return false;update_cache(user,row);return true;
}
int GridUserService::recently_online(){int count=0;const auto now=clock_();for(const auto& row:store_.get_all(""))if(parsed_bool(fields(row->data).at("Online"))&&(static_cast<std::int64_t>(now)-parsed_int(fields(row->data).at("Login")))/86400<5)++count;return count;}
}
