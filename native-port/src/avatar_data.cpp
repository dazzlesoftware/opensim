// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/avatar_data.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/vector_text.hpp"
#include "opensim/network/sdk_boolean_ignorables.hpp"
#include "opensim/services/service_prefix_boundaries.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace opensim::services {
namespace {
const std::string& required(const std::optional<std::string>& s){if(!s)throw std::invalid_argument("Null avatar value");return *s;}
std::vector<std::string> split(std::string_view s,char separator){std::vector<std::string> result;std::size_t start=0;while(true){auto end=s.find(separator,start);if(end==std::string_view::npos){result.emplace_back(s.substr(start));return result;}result.emplace_back(s.substr(start,end-start));start=end+1;}}
framework::Uuid parsed_uuid(const std::string& s){std::string_view text=s;while(text.starts_with(' '))text.remove_prefix(1);if(text.size()<32)throw std::invalid_argument("Short avatar UUID");const auto count=text[8]=='-'?36u:32u;if(text.size()<count)throw std::invalid_argument("Short avatar UUID");return framework::Uuid(text.substr(0,count));}
bool prefix(std::u16string_view text,std::u16string_view value){
    std::size_t matched=0;for(std::size_t i=0;i<text.size();++i){char32_t point=text[i];if(point>=0xd800&&point<=0xdbff&&i+1<text.size()&&text[i+1]>=0xdc00&&text[i+1]<=0xdfff)point=0x10000+((point-0xd800)<<10)+(text[++i]-0xdc00);
        auto& ranges=network::detail::boolean_ignorables;auto ignored=std::lower_bound(ranges.begin(),ranges.end(),point,[](auto range,char32_t n){return range.second<n;});if(ignored!=ranges.end()&&ignored->first<=point)continue;
        if(matched==value.size()){auto& blocked=detail::service_prefix_boundaries;auto at=std::lower_bound(blocked.begin(),blocked.end(),point,[](auto range,char32_t n){return range.second<n;});return at==blocked.end()||at->first>point;}
        if(point!=static_cast<char32_t>(value[matched]))return false;++matched;
    }return matched==value.size();
}
}
std::string avatar_float_string(float value){return framework::service_url_object_text(std::any(value));}
bool parse_avatar_float(const std::optional<std::string>& input,float& value){
    value=0;if(!input)return false;auto text=*input;
    auto special=framework::trim_utf8(text);for(auto& c:special)if(c>='A'&&c<='Z')c+=32;
    if(special=="infinity"||special=="+infinity"||special=="-infinity"){value=special.front()=='-'?-std::numeric_limits<float>::infinity():std::numeric_limits<float>::infinity();return true;}
    if(special=="nan"||special=="+nan"||special=="-nan"){value=std::numeric_limits<float>::quiet_NaN();return true;}
    if(auto zero=text.find('\0');zero!=std::string::npos){if(!std::all_of(text.begin()+static_cast<std::ptrdiff_t>(zero),text.end(),[](char c){return c==0;}))return false;text.resize(zero);}
    auto white=[](char c){return c==' '||(c>='\t'&&c<='\r');};while(!text.empty()&&white(text.front()))text.erase(text.begin());while(!text.empty()&&white(text.back()))text.pop_back();
    std::string numeric;bool digit=false,group_allowed=true;for(char c:text){if(c==','){if(!digit||!group_allowed)return false;continue;}if(c=='.'||c=='e'||c=='E')group_allowed=false;if(c>='0'&&c<='9')digit=true;numeric+=c;}
    if(numeric.empty())return false;for(unsigned char c:numeric)if(!(c>='0'&&c<='9')&&c!='+'&&c!='-'&&c!='.'&&c!='e'&&c!='E')return false;return framework::parse_invariant_float(numeric,value);
}
AvatarData::AvatarData(const AccountValues& values):data(std::make_shared<Fields>()){
    if(auto i=values.find("AvatarType");i!=values.end()){
        if(!i->second.has_value()||i->second.type()==typeid(std::nullptr_t))throw std::invalid_argument("Null avatar type");parse_account_integer(framework::service_url_object_text(i->second),avatar_type);
    }
    for(auto [key,value]:values)if(value.has_value()&&value.type()!=typeid(std::nullptr_t))(*data)[key]=framework::service_url_object_text(value);
}
std::shared_ptr<AccountValues> AvatarData::to_key_value_pairs()const{
    auto result=std::make_shared<AccountValues>();(*result)["AvatarType"]=std::to_string(avatar_type);if(!data)throw std::invalid_argument("Null avatar fields");
    auto enumerated=data;for(auto [key,value]:*enumerated)if(value)(*result)[key]=*value;return result;
}
AvatarData::AvatarData(const framework::AvatarAppearance* appearance):avatar_type(1),data(std::make_shared<Fields>()){
    if(!appearance)throw std::invalid_argument("Null avatar appearance");(*data)["Serial"]=std::to_string(appearance->serial);(*data)["AvatarHeight"]=avatar_float_string(appearance->avatar_height);
    if(!appearance->wearables)throw std::invalid_argument("Null appearance wearables");
    for(int i=0;i<framework::AvatarWearable::LegacyVersionMaxWearables;++i){auto w=appearance->wearables->at(i);if(!w)throw std::invalid_argument("Null appearance wearable");for(std::size_t j=0;j<w->count();++j){auto item=w->at(static_cast<int>(j));(*data)["Wearable "+std::to_string(i)+":"+std::to_string(j)]=item.item.str()+":"+item.asset.str();}}
    if(!appearance->visual_params)throw std::invalid_argument("Null appearance visual parameters");std::string visual;for(auto byte:*appearance->visual_params){if(!visual.empty())visual+=',';visual+=std::to_string(byte);}(*data)["VisualParams"]=visual;
    std::vector<std::pair<int,std::vector<std::string>>> attachments;for(auto a:appearance->attachments())if(!a.item.is_zero()){auto i=std::find_if(attachments.begin(),attachments.end(),[&](auto& pair){return pair.first==a.point;});if(i==attachments.end()){attachments.push_back({a.point,{}});i=attachments.end()-1;}i->second.push_back(a.item.str());}
    for(auto& [point,ids]:attachments){std::string value;for(auto& id:ids){if(!value.empty())value+=',';value+=id;}(*data)["_ap_"+std::to_string(point)]=value;}
}
std::shared_ptr<framework::AvatarAppearance> AvatarData::to_appearance()const{
    auto appearance=std::make_shared<framework::AvatarAppearance>();if(!data)throw std::invalid_argument("Null avatar fields");if(data->empty())return appearance;appearance->clear_wearables();
    try{
        auto fields=data;
        if(fields->contains("Serial"))appearance->serial=account_integer(required(fields->at("Serial")));
        if(fields->contains("AvatarHeight")){float height=0;if(!parse_avatar_float(fields->at("AvatarHeight"),height))throw std::invalid_argument("Invalid avatar height");if(height==0)height=1.9f;appearance->set_size({.45f,.6f,height});}
        const char* legacy[]={"Body","Skin","Hair","Eyes","Shirt","Pants","Shoes","Socks","Jacket","Gloves","UnderShirt","UnderPants","Skirt"};
        for(int i=0;i<13;++i){auto key=std::string(legacy[i])+"Item";if(fields->contains(key))appearance->wearables->at(i)->wear(parsed_uuid(required(fields->at(key))),parsed_uuid(required(fields->at(std::string(legacy[i])+"Asset"))));}
        if(fields->contains("VisualParams")){auto parts=split(required(fields->at("VisualParams")),',');auto bytes=std::make_shared<std::vector<std::uint8_t>>();for(auto& s:parts)bytes->push_back(static_cast<std::uint8_t>(account_integer(s)));appearance->visual_params=bytes;}
        auto wearables=appearance->wearables;auto length=wearables->size();
        for(auto [key,value]:*fields){auto units=framework::utf16_replacing(key);
            if(units.size()>9&&prefix(units,u"Wearable ")){
                auto indices=split(framework::utf8(std::u16string_view(units).substr(9)),':');auto index=account_integer(indices.at(0));auto ids=split(required(value),':');auto item=parsed_uuid(ids.at(0)),asset=parsed_uuid(ids.at(1));
                if(index>=0&&static_cast<std::size_t>(index)>=length){if(index==std::numeric_limits<int>::max())throw std::length_error("Avatar wearable array size overflow");auto grown=std::make_shared<framework::AvatarAppearance::Wearables>(*wearables);grown->resize(static_cast<std::size_t>(index)+1);for(auto i=length;i<grown->size();++i)(*grown)[i]=std::make_shared<framework::AvatarWearable>();wearables=grown;length=grown->size();}
                wearables->at(static_cast<std::size_t>(index))->add(item,asset);continue;
            }
            if(units.size()>4&&prefix(units,u"_ap_")){std::int32_t point=0;if(parse_account_integer(framework::utf8(std::u16string_view(units).substr(4)),point)){auto ids=split(required(value),',');appearance->set_attachment(point,{},{});for(auto& s:ids){framework::Uuid id;if(framework::try_parse_sdk_uuid(s,id))appearance->set_attachment(point|0x80,id,{});}}}
        }
        // The source's resized local array is intentionally never assigned
        // back to appearance.Wearables. Extended entries therefore disappear.
        auto defaults=framework::AvatarWearable::defaults();for(int i=0;i<4;++i)if(appearance->wearables->at(i)->count()==0)appearance->wearables->at(i)->wear(defaults.at(i)->at(0));
    }catch(const std::exception&){return {};}
    return appearance;
}
}
