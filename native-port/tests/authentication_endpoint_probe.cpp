// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/authentication_endpoint.hpp"
#include "opensim/framework/filesystem.hpp"
#include "opensim/network/osd_json.hpp"
#include "osd_fixture_value.hpp"
#include <iostream>
using namespace opensim;
std::string hex(std::string_view s){constexpr char d[]="0123456789abcdef";std::string r;for(unsigned char c:s){r+=d[c>>4];r+=d[c&15];}return r;}
std::string info(const std::shared_ptr<services::AuthInfo>& a){if(!a)return "null";std::string r;for(auto& [k,v]:a->to_key_value_pairs())r+=hex(k)+"="+(!v.has_value()?"N":v.type()==typeid(framework::Uuid)?"U"+std::any_cast<framework::Uuid>(v).str():"S"+hex(std::any_cast<std::string>(v)))+",";return r;}
std::string record(const std::shared_ptr<services::AuthenticationRecord>& a){if(!a)return "null";std::string r=a->principal.str()+":";if(!a->data)return r+"null";for(auto& [k,v]:*a->data)r+=hex(k)+"="+(!v.has_value()?"N":v.type()==typeid(std::string)?"S"+hex(std::any_cast<std::string>(v)):"I"+std::to_string(std::any_cast<int>(v)))+",";return r;}
int main(int argc,char** argv){try{auto base=framework::utf8_path(argv[1]);std::string line;while(std::getline(std::cin,line)){auto j=network::parse_osd_json(line);auto kind=j.get("op").text;std::string status="OK",value,trace;
 if(kind=="endpoint"){
  auto config=server::IniConfig::load(base/std::to_string(j.get("flags").integer));server::AuthenticationOperations ops;auto touch=[&](auto id){if(id.str().back()=='9')throw std::runtime_error("callback");return id.str().back()!='8';};
  ops.authenticate=[&](auto id,auto p,int life)->std::optional<std::string>{trace+="auth:"+id.str()+":"+hex(p)+":"+std::to_string(life)+";";if(!touch(id))return "";if(id.str().back()=='2')return {};if(id.str().back()=='1')return std::string("\0\x01\x0b\x1f",4)+"\xef\xbf\xbe\xef\xbf\xbf";return "token<&>\r\n\tè©¦é¨“";};
  ops.verify=[&](auto id,auto t,int life){trace+="verify:"+id.str()+":"+hex(t)+":"+std::to_string(life)+";";return touch(id);};
  ops.release=[&](auto id,auto t){trace+="release:"+id.str()+":"+hex(t)+";";return touch(id);};
  ops.set_password=[&](auto id,auto p){trace+="password:"+id.str()+":"+hex(p)+";";return touch(id);};
  ops.get_info=[&](auto id)->std::shared_ptr<services::AuthInfo>{trace+="info:"+id.str()+";";if(!touch(id))return {};auto a=std::make_shared<services::AuthInfo>();a->principal=id.str().back()=='4'?framework::Uuid("00000000-0000-0000-0000-000000000008"):id.str().back()=='5'?framework::Uuid("00000000-0000-0000-0000-000000000009"):id;if(!id.is_zero()){a->account_type="Type<&>";a->password_hash="hash\r\n\t";a->password_salt="salt";a->web_login_key="è©¦é¨“ ðŸ¦Š";}return a;};
  ops.set_info=[&](auto a){trace+="setinfo:"+info(a)+";";return touch(a->principal);};
  server::AuthenticationEndpoint endpoint(std::move(ops),config);try{value=hex(endpoint.handle({"POST",j.get("path").text,fixture::unhex(j.get("body").text)}).body);}catch(...){status="EX";}
 }else if(kind=="get"){
  int mode=j.get("mode").integer;std::shared_ptr<services::AuthenticationRecord> row;if(mode){row=std::make_shared<services::AuthenticationRecord>();row->principal=framework::Uuid("00000000-0000-0000-0000-000000000011");if(mode!=1){row->data=std::make_shared<framework::OrderedDictionary<std::string,std::any>>();const char* keys[]={"accountType","passwordHash","passwordSalt","webLoginKey"};for(int i=0;i<4;++i){if(mode==i+3)continue;(*row->data)[keys[i]]=mode==i+7?std::any(17):mode==i+11?std::any{}:std::any(std::string("value<&>è©¦é¨“\r\n\t"));}}}trace="get:00000000-0000-0000-0000-000000000000;";try{value=info(services::authentication_info(row));}catch(...){status="EX";}
 }else{
  int mode=j.get("mode").integer,outcome=j.get("outcome").integer;std::shared_ptr<services::AuthInfo> a;std::shared_ptr<services::AuthenticationRecord> stored;if(mode>=0){a=std::make_shared<services::AuthInfo>();a->principal=framework::Uuid("00000000-0000-0000-0000-000000000011");if(!(mode&1))a->account_type="Type";if(!(mode&2))a->password_hash="hash";if(!(mode&4))a->password_salt="salt";if(!(mode&8))a->web_login_key="<&>è©¦é¨“\r\n\t";}try{stored=services::authentication_record(a);trace="store;";if(outcome==2)throw std::runtime_error("callback");value=outcome==0?"true":"false";}catch(...){status="EX";}trace+=record(stored);
 }std::cout<<status<<'|'<<value<<'|'<<hex(trace)<<'\n';}return 0;}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
