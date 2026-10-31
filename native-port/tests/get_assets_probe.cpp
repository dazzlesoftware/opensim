// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/get_assets.hpp"
#include <iostream>
#include <sstream>
#include <thread>
using namespace opensim;using namespace server;
std::string hex(std::string_view text){std::string result;for(unsigned char c:text){result+="0123456789abcdef"[c>>4];result+="0123456789abcdef"[c&15];}return result;}
std::optional<std::string> text(const std::string& token){if(token=="N")return {};if(token==".")return "";std::string out;for(size_t i=0;i<token.size();i+=2)out+=char(std::stoi(token.substr(i,2),nullptr,16));return out;}
std::string token(const std::optional<std::string>& value){return value?"s"+hex(*value):"N";}
namespace opensim::server {struct HttpResponseProbe{static std::optional<std::string> range(const HttpResponseState& response){for(auto& [key,value]:response.headers_)if(key=="Content-Range")return value;return {};}};}
struct Request:AssetHttpRequest {int flags;std::optional<std::string> value;Request(std::string target,std::optional<std::string> range,int f):AssetHttpRequest({}),flags(f),value(std::move(range)){native.raw_target=std::move(target);}std::shared_ptr<AssetQueries> queries()override{if(flags&2)throw std::runtime_error("queries");if(flags&4)return {};return AssetHttpRequest::queries();}std::optional<std::string> range()override{if(flags&8)throw std::runtime_error("range");return value;}};
struct Service:AssetRetrievalService {std::shared_ptr<framework::AssetBase> asset;Complete saved;int mode,calls=0;std::string id;std::optional<std::string> foreign;bool store=true;
 void get(const std::string& key,const std::optional<std::string>& url,bool local,Complete done)override{++calls;id=key;foreign=url;store=local;saved=done;if(mode==3)throw std::runtime_error("before");if(mode==1)done({});if(mode==6){std::thread worker([&]{done(asset);});worker.join();}else done(asset);if(mode==2)done({});if(mode==4)throw std::runtime_error("after");}
};
int main(){std::string line;while(std::getline(std::cin,line)){try{std::istringstream input(line);std::string query,range,foreign,mime;int asset_mode,type,size,callback,start,length,priority,flags;input>>query>>range>>asset_mode>>type>>size>>callback>>start>>length>>priority>>foreign>>mime>>flags;
 auto service=std::make_shared<Service>();service->mode=callback;if(asset_mode!=1){service->asset=std::make_shared<framework::AssetBase>();service->asset->type(static_cast<int8_t>(type));if(mime!="N")service->asset->metadata()->content_type(text(mime));if(asset_mode!=2){service->asset->data=std::make_shared<std::vector<uint8_t>>();for(int i=0;i<size;++i)service->asset->data->push_back(uint8_t(i));}if(asset_mode==3)service->asset->metadata({});}
 auto response=std::make_shared<HttpResponseState>(std::shared_ptr<HttpResponseContext>{},"HTTP/1.1",HttpConnectionType::KeepAlive);response->status(207);response->raw_start(start);response->raw_length(length);response->priority(priority);response->raw_buffer(std::make_shared<std::vector<uint8_t>>(std::initializer_list<uint8_t>{0xaa}));response->content_type("initial/type");
 std::string warnings,result="OK",late="-";GetAssetsHandler handler((flags&1)?nullptr:service,[&](const auto& warning){warnings+=warning+";";if(flags&16)throw std::runtime_error("warn");if((flags&64)&&service->asset)service->asset->data={};if((flags&128)&&service->asset)service->asset->metadata({});if((flags&256)&&service->saved){try{service->saved({});}catch(...){}}});std::shared_ptr<Request> request;if(query!="N")request=std::make_shared<Request>(text(query).value(),text(range),flags);
 try{handler.handle(request,(flags&32)?nullptr:response,text(foreign));}catch(...){result="EX";}if((callback==3||callback==4||callback==5)&&service->saved){try{service->saved({});late="OK";}catch(...){late="EX";}}
 if((flags&1024)&&service->asset&&service->asset->data&&!service->asset->data->empty())(*service->asset->data)[0]=0xee;
 std::string bytes=response->raw_buffer()?hex(std::string(response->raw_buffer()->begin(),response->raw_buffer()->end())):"N";std::cout<<result<<'|'<<response->status()<<'|'<<int(response->connection())<<'|'<<response->raw_start()<<'|'<<response->raw_length()<<'|'<<response->priority()<<'|'<<token(response->content_type())<<'|'<<bytes<<'|'<<token(HttpResponseProbe::range(*response))<<'|'<<service->calls<<'|'<<hex(service->id)<<'|'<<token(service->foreign)<<'|'<<int(service->store)<<'|'<<hex(warnings)<<'|'<<late<<'\n';
 }catch(...){std::cout<<"FIXTURE_EX\n";}}}
