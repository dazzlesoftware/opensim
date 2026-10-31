// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/avatar_endpoint.hpp"
#include "opensim/framework/text.hpp"
namespace opensim::server {
using namespace network;
namespace {
std::string text(const FormValue& value){if(auto s=std::get_if<std::string>(&value))return *s;return "System.Collections.Generic.List`1[System.String]";}
// StreamReader's default UTF-8 decoder detects UTF-8/16/32 byte-order marks.
AvatarOperations::Strings strings(const std::vector<std::string>& values){return {values.begin(),values.end()};}
}
HttpResponse AvatarEndpoint::handle(const HttpRequest& request){
    HttpResponse response;response.content_type="application/xml";response.headers["Cache-Control"]="no-store";
    if(request.method!="POST"){response.status=405;response.headers["Allow"]="POST";return response;}
    if(!auth_.authenticate(request,response))return response;
    response.body=service_result(false);
    try{
        auto body=read_service_body(request.body);
        const auto fields=parse_service_form(framework::trim_utf8(body));auto method=fields.find("METHOD");if(method==fields.end())return response;
        auto user=fields.find("UserID");framework::Uuid principal;if(user==fields.end()||!framework::try_parse_sdk_uuid(text(user->second),principal))return response;
        const auto name=text(method->second);
        if(name=="getavatar"){
            const auto avatar=operations_.get(principal);if(!avatar)return response;
            auto value=ServiceXmlValue::object({});const auto pairs=avatar->to_key_value_pairs();for(const auto& [key,item]:*pairs)if(item.has_value()&&item.type()!=typeid(std::nullptr_t))value.fields.push_back({key,framework::service_url_object_text(item)});
            response.body=service_xml(ServiceXmlValue::object({{"result",std::move(value)}}));
        }else if(name=="setavatar"){
            services::AccountValues values;for(const auto& key:fields.ordered_keys())if(key!="VERSIONMAX"&&key!="VERSIONMIN"&&key!="METHOD"&&key!="UserID")values[key]=text(fields.at(key));
            services::AvatarData avatar(values);response.body=service_result(operations_.set(principal,avatar));
        }else if(name=="resetavatar")response.body=service_result(operations_.reset(principal));
        else if(name=="setitems"||name=="removeitems"){
            auto names=fields.find("Names");if(names==fields.end())return response;auto keys=std::get_if<std::vector<std::string>>(&names->second);if(!keys)return response;
            auto converted=strings(*keys);
            if(name=="removeitems")response.body=service_result(operations_.remove_items(principal,converted));
            else {auto values=fields.find("Values");if(values==fields.end())return response;auto items=std::get_if<std::vector<std::string>>(&values->second);if(!items)return response;response.body=service_result(operations_.set_items(principal,converted,strings(*items)));}
        }
    }catch(...){response.body=service_result(false);}return response;
}
}
