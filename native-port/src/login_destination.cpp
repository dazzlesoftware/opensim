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
#include "opensim/services/login_destination.hpp"
#include "opensim/framework/vector_text.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
namespace opensim::services {
namespace {
float clamp(float value,std::int32_t size){const float upper=static_cast<float>(size)-0.5f;if(upper<0.5f)throw std::invalid_argument("Invalid region clamp bounds");return std::clamp(value,0.5f,upper);}
}
LoginDestination LoginDestinationSelector::find(const framework::Uuid& scope,const std::optional<GridUserInfo>& info,const std::optional<std::string>& start,const std::optional<GridRegion>& home){
    constexpr std::uint32_t via_home=32,via_region=4096;LoginDestination result;if(!grid_)return result;if(!start)throw std::invalid_argument("Null login start location");
    if(*start=="home"){
        if(!info)return result;if(home){result.position=info->home_position;result.look_at=info->home_look_at;result.flags|=via_home;result.region=home;return result;}const auto defaults=grid_->defaults(scope);result.region=defaults.empty()?alternative(scope):std::optional{defaults.front()};if(result.region){result.flags|=via_region;result.where="safe";}return result;
    }
    if(*start=="last"){
        result.where="last";if(!info)return result;if(!info->last_region.is_zero())result.region=grid_->by_id(scope,info->last_region);if(!result.region){const auto defaults=grid_->defaults(scope);result.region=defaults.empty()?alternative(scope):std::optional{defaults.front()};if(result.region){result.flags|=via_region;result.where="safe";}}
        else{result.position=info->last_position;result.position.x=clamp(result.position.x,result.region->size_x);result.position.y=clamp(result.position.y,result.region->size_y);result.look_at=info->last_look_at;}return result;
    }
    result.flags|=via_region;result.where="url";
    // Regex.Match returns a non-null empty Match for invalid input in the
    // original. Its subsequent Single.Parse throws; retain that failure.
    // Parse this fixed grammar without recursive std::regex matching so large
    // coordinates cannot exhaust the native call stack. The reference regex
    // accepts one final LF through its dollar anchor.
    auto value=std::string_view(*start);if(value.ends_with('\n'))value.remove_suffix(1);if(!value.starts_with("uri:"))throw std::invalid_argument("Invalid login start URI");value.remove_prefix(4);
    std::array<std::string_view,4> fields;for(std::size_t i=0;i<3;++i){const auto end=value.find('&');if(end==std::string_view::npos)throw std::invalid_argument("Invalid login start URI");fields[i]=value.substr(0,end);value.remove_prefix(end+1);}fields[3]=value;if(fields[0].empty())throw std::invalid_argument("Invalid login start URI");
    const auto number=[](std::string_view field){std::size_t i=0;while(i<field.size()&&field[i]>='0'&&field[i]<='9')++i;if(i==0)return false;if(i<field.size()&&field[i]=='.')++i;while(i<field.size()&&field[i]>='0'&&field[i]<='9')++i;return i==field.size();};for(std::size_t i=1;i<4;++i)if(!number(fields[i]))throw std::invalid_argument("Invalid login start coordinates");
    if(!framework::parse_invariant_float(fields[1],result.position.x)||!framework::parse_invariant_float(fields[2],result.position.y)||!framework::parse_invariant_float(fields[3],result.position.z))throw std::invalid_argument("Invalid login start coordinates");const auto name=std::string(fields[0]);if(name.find('@')!=std::string::npos)return result;result.region=grid_->by_name(scope,name);if(result.region)return result;const auto defaults=grid_->defaults(scope);result.region=defaults.empty()?alternative(scope):std::optional{defaults.front()};if(result.region)result.where="safe";return result;
}
std::optional<GridRegion> LoginDestinationSelector::alternative(const framework::Uuid& scope){const auto fallback=grid_->fallback(scope,256000,256000);if(!fallback.empty())return fallback.front();if(fallback_to_any_){const auto online=grid_->online(scope,256000,256000,10);if(!online.empty())return online.front();}return std::nullopt;}
}
