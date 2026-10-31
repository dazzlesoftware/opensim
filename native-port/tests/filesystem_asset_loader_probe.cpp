// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/filesystem_asset_loader.hpp"
#include "opensim/framework/sha1.hpp"
#include <iostream>
#include <sstream>
using namespace opensim;using namespace services;
std::string hex(std::string_view s){std::string result;for(unsigned char c:s){result+="0123456789abcdef"[c>>4];result+="0123456789abcdef"[c&15];}return result;}
std::string unhex(const std::string& s){if(s=="."||s=="N")return "";std::string out;for(size_t i=0;i<s.size();i+=2)out+=char(std::stoi(s.substr(i,2),nullptr,16));return out;}
struct Files:AssetLoaderFiles {std::string trace,fail;int randoms=0;void event(std::string value){trace+=value+";";if(value==fail)throw std::runtime_error("injected");}framework::Uuid random_uuid()override{event("random");std::array<uint8_t,16> data{};uint32_t n=static_cast<uint32_t>(++randoms);for(int i=0;i<4;++i){data[15-i]=static_cast<uint8_t>(n);n>>=8;}return framework::Uuid::from_bytes(data);}void info(const std::string& text)override{event("info:"+text);}void error(const std::string& text)override{auto value=text;auto at=value.find(" : ");if(value.starts_with("[ASSETS]: Error loading ")&&at!=value.npos)value.resize(at+3),value+="XML";event("error:"+value);}};
std::string snapshot(const AssetService::Asset& a){return a->full_id().str()+":"+hex(a->name().value_or(""))+":"+std::to_string(a->type())+":"+hex(a->creator_id().value_or(""))+":"+(a->data?framework::sha1_hex(*a->data):"N")+":"+(a->data?std::to_string(a->data->size()):"N")+":"+hex(a->description().value_or(""))+":"+(a->local()?"1":"0")+(a->temporary()?"1":"0");}
int main(){std::string line;while(std::getline(std::cin,line)){std::istringstream in(line);std::string path,failure;int mode;in>>path>>mode>>failure;path=unhex(path);auto files=std::make_shared<Files>();files->fail=failure=="-"?"":unhex(failure);FilesystemAssetLoader loader(files);std::string values,result="OK";bool reentered=false;std::function<void(AssetService::Asset)> action;if(mode!=3)action=[&](auto asset){values+=snapshot(asset)+";";if(mode==1)throw std::runtime_error("callback");if(mode==2&&!reentered){reentered=true;loader.for_each(path,action);}};try{loader.for_each(path,action);}catch(...){result="EX";}std::cout<<result<<'|'<<values<<'|'<<hex(files->trace)<<'|'<<files->randoms<<'\n';}}
