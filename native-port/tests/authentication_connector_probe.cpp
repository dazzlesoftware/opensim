// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/authentication_connector.hpp"
#include "opensim/framework/filesystem.hpp"
#include <iostream>
#include <sstream>
using namespace opensim;using namespace services;
std::string hex(const std::optional<std::string>& s){if(!s)return "-";std::string out;for(unsigned char c:*s){out+="0123456789abcdef"[c>>4];out+="0123456789abcdef"[c&15];}return out.empty()?".":out;}
std::optional<std::string> decode(const std::string& s){if(s=="-")return {};if(s==".")return std::string{};std::string r;for(size_t i=0;i<s.size();i+=2)r+=char(std::stoi(s.substr(i,2),nullptr,16));return r;}
int main(int argc,char** argv){std::string line;while(std::getline(std::cin,line)){std::istringstream in(line);int mode,lifetime;std::string p,v,r;in>>mode>>lifetime>>p>>v>>r;auto principal=decode(p),value=decode(v),reply=decode(r);std::string trace,out;framework::Uuid real("00000000-0000-0000-0000-000000000007");try{
 AuthenticationConnector::Request request=[&](const auto& uri,const auto& body){trace+=hex(uri)+":"+hex(body)+";";if(reply==std::optional<std::string>("#throw"))throw std::runtime_error("transport");return reply.value_or("");};
 std::unique_ptr<AuthenticationConnector> connector;std::string profile=argc>1?argv[1]:"uri";
 if(profile=="--network")connector=std::make_unique<AuthenticationConnector>(server::IniConfig::load(framework::utf8_path(argv[2])));
 else if(profile=="config")connector=std::make_unique<AuthenticationConnector>(server::IniConfig::load(framework::utf8_path(argv[2])),request);
 else connector=std::make_unique<AuthenticationConnector>(profile=="null"?std::optional<std::string>{}:profile=="default"?std::optional<std::string>(""):std::optional<std::string>("http://auth.invalid/base///"),request);
 if((profile=="config"||profile=="--network")&&argc>3)connector->initialise(server::IniConfig::load(framework::utf8_path(argv[3])));
 framework::Uuid id;if(principal)framework::try_parse_sdk_uuid(*principal,id);
 if(mode==0||mode==1)out=hex(mode?connector->authenticate(id,value,lifetime,real):connector->authenticate(id,value,lifetime));
 else if(mode==2)out=connector->verify(id,value,lifetime)?"T":"F";
 else if(mode==3)out=connector->release(id,value)?"T":"F";
 else if(mode==4)out=connector->set_password(id,value)?"T":"F";
 else if(mode==5)out=connector->get_auth_info(id)?"Y":"N";
 else out=connector->set_auth_info(value?std::make_shared<AuthInfo>():nullptr)?"T":"F";
 }catch(...){out="E";}std::cout<<trace<<'|'<<out<<'|'<<real.str()<<'\n';}}
