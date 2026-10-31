// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/user_account.hpp"
#include "opensim/framework/text.hpp"
#include <bit>
#include <ctime>
#include <limits>
namespace opensim::services {
bool parse_account_integer(std::string_view text,std::int32_t& value){
    value=0;auto white=[](char c){return c==' '||(c>='\t'&&c<='\r');};
    while(!text.empty()&&white(text.front()))text.remove_prefix(1);
    bool negative=false;if(!text.empty()&&(text.front()=='+'||text.front()=='-')){negative=text.front()=='-';text.remove_prefix(1);}
    std::uint32_t n=0;bool digit=false;const auto limit=negative?2147483648u:2147483647u;
    while(!text.empty()&&text.front()>='0'&&text.front()<='9'){
        auto d=static_cast<std::uint32_t>(text.front()-'0');if(n>(limit-d)/10)return false;n=n*10+d;digit=true;text.remove_prefix(1);
    }
    while(!text.empty()&&white(text.front()))text.remove_prefix(1);while(!text.empty()&&text.front()==0)text.remove_prefix(1);
    if(!digit||!text.empty())return false;value=std::bit_cast<std::int32_t>(negative?0u-n:n);return true;
}
std::int32_t account_integer(std::string_view text){std::int32_t value;if(!parse_account_integer(text,value))throw std::invalid_argument("Invalid account integer");return value;}
namespace {
std::string required_text(const std::any& value){if(!value.has_value()||value.type()==typeid(std::nullptr_t))throw std::invalid_argument("Null account dictionary value");return framework::service_url_object_text(value);}
std::int32_t now(){const auto seconds=static_cast<double>(std::time(nullptr));if(seconds>=2147483648.||seconds< -2147483648.)return std::numeric_limits<std::int32_t>::min();return static_cast<std::int32_t>(seconds);}
std::any nullable(const std::optional<std::string>& value){return value?std::any(*value):std::any{};}
}
UserAccount::UserAccount(const framework::Uuid& scope,const framework::Uuid& principal,std::optional<std::string> first,std::optional<std::string> last,std::optional<std::string> mail,Clock clock)
    :principal_id(principal.str()),scope_id(scope.str()),first_name(std::move(first)),last_name(std::move(last)),email(std::move(mail)),created(clock?clock():now()),service_urls(std::make_shared<framework::AgentCircuitData::ServiceUrls>()){}
UserAccount::UserAccount(const framework::Uuid& scope,std::optional<std::string> first,std::optional<std::string> last,std::optional<std::string> mail,Clock clock)
    :UserAccount(scope,framework::Uuid::random(),std::move(first),std::move(last),std::move(mail),std::move(clock)){}
UserAccount::UserAccount(const AccountValues& values){
    auto string=[&](const char* key,std::optional<std::string>& out){auto i=values.find(key);if(i!=values.end())out=required_text(i->second);};
    auto integer=[&](const char* key,int& out){auto i=values.find(key);if(i!=values.end())out=account_integer(required_text(i->second));};
    auto uuid=[&](const char* key,std::string& out){auto i=values.find(key);if(i!=values.end()){framework::Uuid id;framework::try_parse_sdk_uuid(required_text(i->second),id);out=id.str();}};
    string("FirstName",first_name);string("LastName",last_name);string("Email",email);uuid("PrincipalID",principal_id);uuid("ScopeID",scope_id);
    integer("UserLevel",user_level);integer("UserFlags",user_flags);string("UserTitle",user_title);string("UserCountry",user_country);
    if(auto i=values.find("LocalToGrid");i!=values.end()){
        auto text=framework::utf16_replacing(required_text(i->second));auto white=[](char16_t c){return c==0||framework::unicode_whitespace(c);};
        while(!text.empty()&&white(text.front()))text.erase(text.begin());while(!text.empty()&&white(text.back()))text.pop_back();for(auto& c:text)if(c>='A'&&c<='Z')c+=32;local_to_grid=text==u"true";
    }
    integer("Created",created);
    if(auto i=values.find("ServiceURLs");i!=values.end())if(auto text=std::any_cast<std::string>(&i->second)){
        service_urls=std::make_shared<framework::AgentCircuitData::ServiceUrls>();std::size_t start=0;
        while(start<=text->size()){auto end=text->find(';',start);auto part=std::string_view(*text).substr(start,end==std::string::npos?text->size()-start:end-start);auto split=part.find('*');if(split!=std::string_view::npos&&part.find('*',split+1)==std::string_view::npos)(*service_urls)[std::string(part.substr(0,split))]=std::string(part.substr(split+1));if(end==std::string::npos)break;start=end+1;}
    }
}
std::shared_ptr<AccountValues> UserAccount::to_key_value_pairs()const{
    auto result=std::make_shared<AccountValues>();
    for(auto [key,value]:{std::pair{"FirstName",&first_name},{"LastName",&last_name},{"Email",&email}})(*result)[key]=nullable(*value);
    (*result)["PrincipalID"]=framework::Uuid(principal_id).str();(*result)["ScopeID"]=framework::Uuid(scope_id).str();
    (*result)["Created"]=std::to_string(created);(*result)["UserLevel"]=std::to_string(user_level);(*result)["UserFlags"]=std::to_string(user_flags);
    (*result)["UserTitle"]=nullable(user_title);(*result)["UserCountry"]=nullable(user_country);(*result)["LocalToGrid"]=std::string(local_to_grid?"True":"False");
    if(!service_urls)throw std::invalid_argument("Null account URL dictionary");auto enumerated=service_urls;std::string urls;
    for(auto [key,value]:*enumerated)urls+=key+'*'+framework::service_url_object_text(value)+';';(*result)["ServiceURLs"]=urls;return result;
}
}
