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
#include "opensim/services/grid_region.hpp"
#include "opensim/framework/text.hpp"
#include <bit>
#include <charconv>
#include <limits>
#include <stdexcept>
namespace opensim::services {
namespace {
std::string required(const std::optional<std::string>& value){if(!value)throw std::invalid_argument("Null region value");return *value;}
std::string lower(std::string value){for(auto& c:value)if(c>='A'&&c<='Z')c=static_cast<char>(c+32);return value;}
std::int32_t wrapped(std::uint32_t value){return std::bit_cast<std::int32_t>(value);}
std::uint32_t unsigned_integer(const std::optional<std::string>& value){
    if(!value)throw std::invalid_argument("Null integer");auto text=framework::trim_utf8(*value);if(text.starts_with('+')){text.erase(0,1);if(text.starts_with('-')||text.starts_with('+'))throw std::invalid_argument("Invalid integer sign");}std::uint32_t result=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())throw std::invalid_argument("Invalid unsigned integer");return result;
}
}
std::int32_t parse_region_integer(const std::optional<std::string>& value){
    if(!value)return 0;auto text=framework::trim_utf8(*value);if(text.starts_with('+')){text.erase(0,1);if(text.starts_with('-')||text.starts_with('+'))throw std::invalid_argument("Invalid integer sign");}std::int32_t result=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())throw std::invalid_argument("Invalid region integer");return result;
}
GridRegion GridRegion::from_fields(const RegionFields& fields){
    GridRegion result;result.raw_server_uri=std::nullopt;auto get=[&](const char* name)->const std::optional<std::string>&{return fields.at(name);};
    if(fields.contains("uuid"))result.id=framework::Uuid(required(get("uuid")));
    if(fields.contains("locX"))result.x=parse_region_integer(get("locX"));if(fields.contains("locY"))result.y=parse_region_integer(get("locY"));
    if(fields.contains("sizeX"))result.size_x=parse_region_integer(get("sizeX"));if(fields.contains("sizeY"))result.size_y=parse_region_integer(get("sizeY"));else{result.size_x=256;result.size_y=0;}
    if(fields.contains("regionName"))result.name=get("regionName");
    if(fields.contains("access")){const auto access=parse_region_integer(get("access"));if(access<0||access>255)throw std::out_of_range("Invalid access byte");result.access=static_cast<std::uint8_t>(access);result.maturity=access<=13?0:access<=21?1:2;}
    if(fields.contains("flags")&&get("flags"))result.flags=parse_region_integer(get("flags"));
    result.external_host=fields.contains("serverIP")?get("serverIP"):std::optional<std::string>{"127.0.0.1"};
    if(fields.contains("serverPort")){std::int32_t port=0;try{port=parse_region_integer(get("serverPort"));}catch(...){ }if(port<0||port>65535)throw std::out_of_range("Invalid region port");result.internal_endpoint=RegionEndpoint{"0.0.0.0",static_cast<std::uint16_t>(port)};}
    if(fields.contains("serverHttpPort"))try{result.http_port=unsigned_integer(get("serverHttpPort"));}catch(...){result.http_port=0;}
    if(fields.contains("serverURI"))result.set_server_uri(get("serverURI"));
    if(fields.contains("regionMapTexture"))try{result.terrain_image=framework::Uuid(required(get("regionMapTexture")));}catch(...){ }
    if(fields.contains("parcelMapTexture"))try{result.parcel_image=framework::Uuid(required(get("parcelMapTexture")));}catch(...){ }
    if(fields.contains("regionSecret"))result.region_secret=get("regionSecret");
    if(fields.contains("owner_uuid"))result.estate_owner=framework::Uuid(required(get("owner_uuid")));
    if(fields.contains("Token"))result.token=required(get("Token"));return result;
}
GridRegion GridRegion::from_cells(std::uint32_t x,std::uint32_t y){GridRegion result;result.x=wrapped(x*256u);result.y=wrapped(y*256u);result.raw_server_uri=std::nullopt;return result;}
GridRegion GridRegion::legacy_copy()const{GridRegion result=*this;result.scope={};result.token.clear();result.set_server_uri(server_uri());return result;}
std::string GridRegion::server_uri()const{if(raw_server_uri&&!raw_server_uri->empty())return *raw_server_uri;return "http://"+external_host.value_or("")+(http_port?":"+std::to_string(http_port):"")+"/";}
void GridRegion::set_server_uri(const std::optional<std::string>& value){if(!value){raw_server_uri="";return;}raw_server_uri=value->ends_with('/')?*value:*value+"/";}
OrderedRegionFields GridRegion::fields()const{
    if(!internal_endpoint)throw std::invalid_argument("Region has no internal endpoint");
    OrderedRegionFields result={{"uuid",id.str()},{"locX",std::to_string(x)},{"locY",std::to_string(y)},{"sizeX",std::to_string(size_x)},{"sizeY",std::to_string(size_y)},{"regionName",name}};
    if(flags)result.push_back({"flags",std::to_string(*flags)});
    for(auto& field:OrderedRegionFields{{"serverIP",external_host},{"serverHttpPort",std::to_string(http_port)},{"serverURI",server_uri()},{"serverPort",std::to_string(internal_endpoint->port)},{"regionMapTexture",terrain_image.str()},{"parcelMapTexture",parcel_image.str()},{"access",std::to_string(access)},{"regionSecret",region_secret},{"owner_uuid",estate_owner.str()},{"Token",token}})result.push_back(std::move(field));return result;
}
std::uint64_t GridRegion::handle()const{return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x))<<32)|static_cast<std::uint32_t>(y);}
std::int32_t GridRegion::coord_x()const{return static_cast<std::int32_t>(static_cast<std::uint32_t>(x)>>8);}
std::int32_t GridRegion::coord_y()const{return static_cast<std::int32_t>(static_cast<std::uint32_t>(y)>>8);}
RegionData region_data(const GridRegion& region){RegionData result;result.id=region.id;result.name=region.name;result.x=region.x;result.y=region.y;result.size_x=region.size_x;result.size_y=region.size_y;for(const auto& [key,value]:region.fields())result.data[key]=value;result.data["regionHandle"]=std::to_string(region.handle());return result;}
GridRegion grid_region(const RegionData& data){auto result=GridRegion::from_fields(data.data);result.x=data.x;result.y=data.y;result.size_x=data.size_x;result.size_y=data.size_y;result.id=data.id;result.name=data.name;result.scope=data.scope;return result;}
std::int32_t parse_region_flags(std::int32_t previous,std::string_view specification){
    static const std::pair<const char*,std::int32_t> names[]={{"DefaultRegion",1},{"FallbackRegion",2},{"RegionOnline",4},{"NoDirectLogin",8},{"Persistent",16},{"LockedOut",32},{"NoMove",64},{"Reservation",128},{"Authenticate",256},{"Hyperlink",512},{"DefaultHGRegion",1024}};
    std::uint32_t flags=static_cast<std::uint32_t>(previous);std::size_t start=0;while(start<specification.size()){const auto end=specification.find_first_of(", ",start);auto item=std::string(specification.substr(start,end==std::string_view::npos?specification.size()-start:end-start));start=end==std::string_view::npos?specification.size():end+1;if(item.empty())continue;const char operation=item[0];const bool sign=operation=='+'||operation=='-';if(sign)item.erase(0,1);try{std::optional<std::int32_t> found;for(const auto& [name,value]:names)if(sign?lower(item)==lower(name):item==name){found=value;break;}if(!found)found=parse_region_integer(item);if(operation=='-')flags&=~static_cast<std::uint32_t>(*found);else flags|=static_cast<std::uint32_t>(*found);}catch(...){ }}return wrapped(flags);
}
int compare_region_distance(const RegionData& a,const RegionData& b,std::int32_t x,std::int32_t y){
    const auto distance=[&](const RegionData& value){float dx=static_cast<float>(value.x)-static_cast<float>(x),dy=static_cast<float>(value.y)-static_cast<float>(y);if(dx<0)dx+=static_cast<float>(wrapped(static_cast<std::uint32_t>(value.size_x)-1u));if(dy<0)dy+=static_cast<float>(wrapped(static_cast<std::uint32_t>(value.size_y)-1u));return dx*dx+dy*dy;};const auto da=distance(a),db=distance(b);return da<db?-1:da>db?1:0;
}
}
