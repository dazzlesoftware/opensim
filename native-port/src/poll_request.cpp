// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/poll_request.hpp"
#include "opensim/framework/agent_circuit.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/network/osd_value.hpp"
#include <bit>
#include <chrono>
namespace opensim::services {
CapabilityPollHandler::CapabilityPollHandler(RequestMethod incoming,server::PollName path,HasEvents has,GetEvents get,GetEvents no,Drop discard,framework::Uuid key,std::int32_t timeout):request(std::move(incoming)),url(std::move(path)),has_events(std::move(has)),get_events(std::move(get)),no_events(std::move(no)),drop(std::move(discard)),id(key),timeout_ms(timeout){}
}
namespace opensim::server {
namespace {
template<class T>T& require(const std::shared_ptr<T>& value){if(!value)throw std::invalid_argument("Null poll dependency");return *value;}
template<class T>T cast(const std::any& value){if(auto p=std::any_cast<T>(&value))return *p;throw std::invalid_argument("Invalid poll response value type");}
PollName text(const std::any& value){if(poll_object_null(value))return {};return cast<std::string>(value);}
template<class T>std::shared_ptr<T> reference(const std::any& value){if(poll_object_null(value))return {};return cast<std::shared_ptr<T>>(value);}
PollName object_text(const std::any& value){if(poll_object_null(value))throw std::invalid_argument("Null poll header value");if(auto p=std::any_cast<PollStringObject>(&value))return p->to_string();if(auto p=std::any_cast<PollOpaqueObject>(&value))return p->type_name;if(std::any_cast<PollBytes>(&value))return "System.Byte[]";if(std::any_cast<PollTablePtr>(&value))return "System.Collections.Hashtable";if(std::any_cast<PollResponsePtr>(&value))return "OpenSim.Framework.Servers.HttpServer.OSHttpResponse";return framework::service_url_object_text(value);}
}
bool poll_object_null(const std::any& value){if(!value.has_value()||value.type()==typeid(std::nullptr_t))return true;if(auto p=std::any_cast<PollBytes>(&value))return !*p;if(auto p=std::any_cast<PollTablePtr>(&value))return !*p;if(auto p=std::any_cast<PollResponsePtr>(&value))return !*p;return false;}
bool PollTable::contains(const std::string& name)const{for(const auto& [key,value]:entries_)if(auto s=std::any_cast<std::string>(&key);s&&*s==name)return true;return false;}
std::any PollTable::get(const std::string& name)const{for(const auto& [key,value]:entries_)if(auto s=std::any_cast<std::string>(&key);s&&*s==name)return value;return {};}
void PollTable::set(const std::string& name,std::any value){for(auto& [key,existing]:entries_)if(auto s=std::any_cast<std::string>(&key);s&&*s==name){existing=std::move(value);++version_;return;}entries_.emplace_back(name,std::move(value));++version_;}
void PollTable::add_key(std::any key,std::any value){if(poll_object_null(key))throw std::invalid_argument("Null Hashtable key");if(auto s=std::any_cast<std::string>(&key)){set(*s,std::move(value));return;}entries_.emplace_back(std::move(key),std::move(value));++version_;}
void PollTable::for_each_key(const std::function<void(const std::any&)>& visitor)const{const auto version=version_;for(std::size_t i=0;i<entries_.size();++i){auto key=entries_[i].first;visitor(key);if(version_!=version)throw std::runtime_error("Hashtable changed during enumeration");}}
std::int32_t PollRequestBackend::tick_count()const{auto count=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(count));}
PollServiceHttpRequest::PollServiceHttpRequest(std::shared_ptr<services::CapabilityPollHandler> args,std::shared_ptr<PollIncomingRequest> incoming,PollRequestBackend& backend):poll_args(std::move(args)),request(std::move(incoming)),request_time(backend.tick_count()),request_id(backend.random_id()),backend_(backend){}
void PollServiceHttpRequest::do_http_grunt_work(PollTablePtr data){
    auto body=require(request).body();if(require(body).can_read())require(require(request).body()).dispose();
    if(require(data).contains("h")){auto response=reference<PollResponse>(data->get("h"));try{require(response).send();}catch(...){}require(poll_args).requests_handled++;return;}
    auto response=backend_.make_response(request);
    if(!data){send_no_content_error(response);return;}
    std::int32_t code=200,start=0,length=-1;PollName response_string,content_type;PollBytes bytes;
    try{
        if(!poll_object_null(data->get("int_response_code")))code=cast<std::int32_t>(data->get("int_response_code"));
        if(!poll_object_null(data->get("bin_response_data"))){bytes=reference<std::vector<std::uint8_t>>(data->get("bin_response_data"));data->set("bin_response_data",{});if(!poll_object_null(data->get("bin_start")))start=cast<std::int32_t>(data->get("bin_start"));if(!poll_object_null(data->get("int_bytes")))length=cast<std::int32_t>(data->get("int_bytes"));}
        else response_string=text(data->get("str_response_string"));content_type=text(data->get("content_type"));
    }catch(...){send_no_content_error(response);return;}
    if(code==301){require(response).redirect(text(data->get("str_redirect_location")),301);response->keep_alive(false);require(poll_args).requests_handled++;response->send();return;}
    require(response).status_code(code);
    if(data->contains("http_protocol_version"))response->protocol_version(text(data->get("http_protocol_version")));
    if(data->contains("keepalive"))response->keep_alive(cast<bool>(data->get("keepalive")));
    if(data->contains("keepaliveTimeout"))response->keep_alive_timeout(cast<std::int32_t>(data->get("keepaliveTimeout")));
    if(data->contains("error_status_text"))response->status_description(text(data->get("error_status_text")));
    if(data->contains("access_control_allow_origin"))response->add_header("Access-Control-Allow-Origin",text(data->get("access_control_allow_origin")));
    response->add_header("Content-Type",!content_type||content_type->empty()?PollName("text/html"):content_type);
    if(data->contains("headers")){auto headers=reference<PollTable>(data->get("headers"));require(headers).for_each_key([&](const std::any& item){auto name=cast<std::string>(item);response->add_header(name,object_text(headers->get(name)));});}
    if(!bytes){if(!response_string||response_string->empty())bytes=std::make_shared<std::vector<std::uint8_t>>();else if(content_type&&content_type->find("image")==content_type->npos&&content_type->find("x-shockwave-flash")==content_type->npos&&content_type->find("application/x-oar")==content_type->npos&&content_type->find("application/vnd.ll.mesh")==content_type->npos){auto value=framework::utf8(framework::utf16_replacing(*response_string));bytes=std::make_shared<std::vector<std::uint8_t>>(value.begin(),value.end());}else{network::Value encoded;encoded.kind=network::Value::Kind::Binary;encoded.text=*response_string;bytes=network::osd_binary(encoded);}response->utf8_encoding();}
    auto size=static_cast<std::int32_t>(bytes->size());if(start<0||start>size)start=0;
    if(length<0)length=size;else if(std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(length)+std::bit_cast<std::uint32_t>(start))>size)length=size-start;
    response->content_length(length);
    try{if(length>0){response->buffer_start(start);response->buffer_length(length);response->buffer(bytes);}bytes=nullptr;response->send();}
    catch(const std::exception& error){auto socket=dynamic_cast<const PollSocketError*>(&error);if(!socket||!socket->connection_reset)backend_.warn("[POLL SERVICE WORKER THREAD]: Error ",error);}
    require(poll_args).requests_handled++;
}
void PollServiceHttpRequest::send_no_content_error(PollResponsePtr response){require(response).content_length(0);response->utf8_encoding();response->status_code(500);try{response->send();}catch(...){} }
void PollServiceHttpRequest::do_http_stop(){auto response=backend_.make_response(request);auto body=require(request).body();if(require(body).can_read())require(require(request).body()).dispose();require(response).content_length(0);response->utf8_encoding();response->keep_alive(false);response->status_code(503);try{response->send();}catch(...){} }
}
