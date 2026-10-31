// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/friends_connector.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/network/rest_forms.hpp"
namespace opensim::services {
namespace {

std::string value_text(const network::ServiceXmlValue& value){return value.list?"System.Collections.Generic.Dictionary`2[System.String,System.Object]":value.text;}
bool boolean(std::string text){auto chars=framework::utf16_replacing(text);auto white=[](char16_t c){return c==0||framework::unicode_whitespace(c);};while(!chars.empty()&&white(chars.front()))chars.erase(chars.begin());while(!chars.empty()&&white(chars.back()))chars.pop_back();for(auto& c:chars)if(c>='A'&&c<='Z')c+=32;return chars==u"true";}
}
FriendsConnector::FriendsConnector(std::optional<std::string> uri,Request request):request_(request?std::move(request):network::rest_forms_transport()){if(!uri)throw std::invalid_argument("Null friends URI");uri_=*uri;while(uri_.ends_with('/'))uri_.pop_back();}
FriendsConnector::FriendsConnector(const server::IniConfig& config,Request request){if(!config.has_section("FriendsService"))throw std::invalid_argument("FriendsService section missing");uri_=config.get("FriendsService","FriendsServerURI");if(uri_.empty())throw std::invalid_argument("Friends server URI missing");if(request)request_=std::move(request);else{auto type=config.get("FriendsService","AuthType",config.get("Network","AuthType","None"));std::optional<std::pair<std::string,std::string>> auth;if(type=="BasicHttpAuthentication")auth={{config.get("FriendsService","HttpAuthUsername",config.get("Network","HttpAuthUsername")),config.get("FriendsService","HttpAuthPassword",config.get("Network","HttpAuthPassword"))}};request_=network::rest_forms_transport(auth);}}
std::vector<FriendInfo> FriendsConnector::get(const framework::Uuid& principal){return get(Fields{{"PRINCIPALID",principal.str()},{"METHOD","getfriends"}});}
std::vector<FriendInfo> FriendsConnector::get(const std::optional<std::string>& principal){return get(Fields{{"PRINCIPALID",principal},{"METHOD","getfriends_string"}});}
std::vector<FriendInfo> FriendsConnector::get(const Fields& fields){auto body=network::build_service_form(fields);try{auto reply=request_(uri_+"/friends",body);if(reply.empty())return {};auto root=network::parse_service_xml(reply);for(const auto& [key,value]:root.fields)if(key=="result"&&!value.list){auto text=value.text;for(auto& c:text)if(c>='A'&&c<='Z')c+=32;if(text=="null")return {};break;}std::vector<FriendInfo> result;for(const auto& [key,value]:root.fields)if(value.list){AccountValues pairs;for(const auto& [name,field]:value.fields)pairs[name]=value_text(field);result.emplace_back(pairs);}return result;}catch(const std::exception&){return {};}}
bool FriendsConnector::mutation(const Fields& fields){std::string reply;try{reply=request_(uri_+"/friends",network::build_service_form(fields));}catch(const std::exception&){return false;}if(reply.empty())return false;auto root=network::parse_service_xml(reply);for(const auto& [key,value]:root.fields)if(key=="Result")return boolean(value_text(value));return false;}
bool FriendsConnector::store(const std::optional<std::string>& principal,const std::optional<std::string>& friend_id,std::int32_t flags){return mutation({{"PrincipalID",principal},{"Friend",friend_id},{"MyFlags",std::to_string(flags)},{"METHOD","storefriend"}});}
bool FriendsConnector::erase(const framework::Uuid& principal,const std::optional<std::string>& friend_id){return mutation({{"PRINCIPALID",principal.str()},{"FRIEND",friend_id},{"METHOD","deletefriend"}});}
bool FriendsConnector::erase(const std::optional<std::string>& principal,const std::optional<std::string>& friend_id){if(!principal)throw std::invalid_argument("Null friend principal");return mutation({{"PRINCIPALID",principal},{"FRIEND",friend_id},{"METHOD","deletefriend_string"}});}
}
