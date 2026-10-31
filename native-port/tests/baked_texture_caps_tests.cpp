// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/baked_texture_caps.hpp"
#include <iostream>
#include <regex>
using namespace opensim;using namespace services;
namespace {
std::string hex(std::string_view v){std::string r;for(unsigned char c:v){r+="0123456789abcdef"[c>>4];r+="0123456789abcdef"[c&15];}return r;}
struct Cache final:framework::IAssetCache {
 std::function<void(std::shared_ptr<framework::AssetBase>)> save;
 void cache(const std::shared_ptr<framework::AssetBase>& a,bool)override{save(a);}
 void cache_negative(const std::string&)override{}bool get(const std::string&,std::shared_ptr<framework::AssetBase>&)override{return true;}
 std::shared_ptr<framework::AssetBase> get_cached(const std::string&)override{return {};}
 bool get_from_memory(const std::string&,std::shared_ptr<framework::AssetBase>&)override{return true;}
 bool check(const std::string&)override{return false;}void expire(const std::string&)override{}void clear()override{}
};
std::string probe(int kind,int mode,int phase){std::string trace;auto add=[&](auto s){trace+="|"+std::string(s);};std::shared_ptr<framework::AssetBase> saved;BakedTextureCapsBackend b;std::shared_ptr<BakedTextureUpload> held;
 auto cache=std::make_shared<Cache>();cache->save=[&](auto a){add("C");saved=a;if(mode&2)throw std::runtime_error("cache");};if(!(mode&1))b.cache=cache;
 b.new_id=[] {return framework::Uuid("11223344-5566-7788-99aa-000000000002");};b.timer=[&](auto,int n){add("T"+std::to_string(n));if((n==0&&(mode&16))||(n!=0&&(mode&64)))throw std::runtime_error("timer");};
 b.remove_upload=[&](auto){add("R");if(mode&8)throw std::runtime_error("remove");};b.dispose_timer=[&](auto){add("D");if(mode&32)throw std::runtime_error("dispose");};b.register_upload=[&](auto u){add("A");if(mode&4)throw std::runtime_error("register");held=u;};b.log=[&](auto){add("L");};
 network::HttpRequest request;request.method="POST";request.remote_ip="127.0.0.1";request.body=std::string("raw\0\xff",5);std::string origin=request.remote_ip;
 if(phase==1)request.remote_ip="127.0.0.2";if(phase==2)request.method="GET";if(phase==3)request.body.clear();if(phase==4)request.body.assign(6000001,0);if(phase==5)request.body.assign(6000000,0);if(phase==8)origin=request.remote_ip="::1";if(phase==9)request.remote_ip="::1";if(phase==10){origin="fe80::1%2";request.remote_ip="fe80::1%3";}
 network::HttpResponse response;try{if(kind==0){BakedTextureCaps caps("/cap",framework::Uuid("11223344-5566-7788-99aa-000000000001"),"http://host:9000",b);response=caps.handle(request);}else{BakedTextureUpload upload("/upload",framework::Uuid("11223344-5566-7788-99aa-000000000001"),origin,b);if(phase==6||phase==7){upload.expire();response.status=0;}if(phase!=6)response=upload.handle(request);}}catch(...){response.status=500;}
 static const std::regex uuid("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}");auto body=response.body;std::string normalized;std::size_t cursor=0;for(auto it=std::sregex_iterator(body.begin(),body.end(),uuid);it!=std::sregex_iterator();++it){normalized+=body.substr(cursor,static_cast<std::size_t>(it->position())-cursor);auto value=it->str();normalized+=value==framework::Uuid{}.str()?value:"ID";cursor=static_cast<std::size_t>(it->position()+it->length());}normalized+=body.substr(cursor);
 std::string asset="none";if(saved){auto data=saved->data;asset=saved->name().value_or("")+"|"+std::to_string(saved->type())+"|"+saved->creator_id().value_or("")+"|"+(saved->local()?"1":"0")+(saved->temporary()?"1":"0")+"|"+std::to_string(saved->flags())+"|"+(data->size()>100?"bytes:"+std::to_string(data->size()):hex(std::string_view(reinterpret_cast<const char*>(data->data()),data->size())));}
 return std::to_string(response.status)+"|"+hex(normalized)+trace+"|"+asset+"|id-match:"+(saved?(!saved->full_id().is_zero()&&response.body.find(saved->full_id().str())!=std::string::npos?"1":"0"):"none");
}
}
int main(int argc,char** argv){try{if(argc>1&&std::string_view(argv[1])=="--probe"){int k,m,p;while(std::cin>>k>>m>>p)std::cout<<probe(k,m,p)<<'\n';return 0;}for(auto [k,m,p,prefix]:std::vector<std::tuple<int,int,int,std::string>>{{0,0,3,"200|"},{1,0,0,"200|"},{1,0,1,"401|"},{1,0,4,"413|"},{1,1,0,"400|"},{1,2,0,"400|"},{1,16,0,"500|"}})if(!probe(k,m,p).starts_with(prefix))throw std::runtime_error("Baked texture source status");std::cout<<"PASS: baked texture metadata, binary, flags and failure checks\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
