// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/flotsam_memory_cache.hpp"
#include "opensim/services/region_asset_runtime.hpp"
#include <filesystem>
#include <fstream>
#include <bit>
#include <iostream>
#include <sstream>
#include <map>
#include <chrono>
using namespace opensim;using namespace services;
namespace {
std::string key(int n){return n==7?framework::Uuid{}.str():"key"+std::to_string(n);}
std::string show(const std::shared_ptr<framework::AssetBase>& a){if(!a)return "-";std::string result=a->name().value_or("")+":";for(auto b:*a->data){result+="0123456789abcdef"[b>>4];result+="0123456789abcdef"[b&15];}return result;}
struct Probe {
 std::int64_t now=0;int serial=0;std::unique_ptr<FlotsamMemoryCache> cache;std::map<int,std::shared_ptr<framework::AssetBase>> held;
 std::string run(const std::string& line){std::istringstream input(line);std::string op,result;int n=0;input>>op;if(op=="init"){int memory,negative,negms;double seconds;input>>memory>>negative>>seconds>>negms;cache.reset();now=0;serial=0;held.clear();cache=std::make_unique<FlotsamMemoryCache>(FlotsamMemoryCache::Options{memory!=0,negative!=0,seconds,negms},[this]{return now;},false);result="init";}else if(op=="advance"){std::int64_t delta;input>>delta;now+=delta;cache->pump();result="advance";}else{
  input>>n;std::shared_ptr<framework::AssetBase> a;if(op=="cache"){a=std::make_shared<framework::AssetBase>(std::optional<std::string>(key(n)),"asset"+std::to_string(++serial),std::int8_t(0),"creator");a->data=std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{static_cast<std::uint8_t>(serial),0,255});held[n]=a;cache->cache(a);result="cache";}
  else if(op=="null"){cache->cache({});result="null";}else if(op=="drop"){held.erase(n);result="drop";}else if(op=="mutate"){auto it=held.find(n);if(it!=held.end()){it->second->name(it->second->name().value_or("")+"X");++it->second->data->at(0);}result="mutate";}
  else if(op=="negative"){cache->cache_negative(key(n));result="negative";}else if(op=="get"||op=="from"){auto ok=op=="get"?cache->get(key(n),a):cache->get_from_memory(key(n),a);result=std::string(ok?"1:":"0:")+show(a);}
  else if(op=="cached")result=show(cache->get_cached(key(n)));else if(op=="check")result=cache->check(key(n))?"1":"0";else if(op=="expire"){cache->expire(key(n));result="expire";}else if(op=="clear"){cache->clear();result="clear";}else throw std::runtime_error("Unknown cache operation");
 }auto stats=cache->statistics();return result+"|"+std::to_string(stats.requests)+","+std::to_string(stats.memory_hits)+","+std::to_string(stats.weak_hits);}
};
}
int main(int argc,char** argv){try{if(argc>1&&std::string_view(argv[1])=="--config-probe"){
 auto directory=std::filesystem::temp_directory_path()/("opensim-cache-config-"+framework::Uuid::random().str());std::filesystem::create_directory(directory);struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove_all(path,ec);}} cleanup{directory};auto file=directory/"config.ini";std::string seconds,negative;
 while(std::cin>>seconds>>negative){try{std::ofstream out(file);auto db=(directory/"assets.db").generic_u8string();out<<"[Modules]\nAssetCaching=FlotsamAssetCache\nAssetServices=LocalAssetServicesConnector\n[AssetCache]\nFileCacheEnabled=false\nMemoryCacheEnabled=true\n";if(seconds!=".")out<<"MemoryCacheTimeout="<<seconds<<'\n';if(negative!=".")out<<"NegativeCacheTimeout="<<negative<<'\n';out<<"[AssetService]\nLocalServiceModule=OpenSim.Services.AssetService.dll:AssetService\nStorageProvider=OpenSim.Data.SQLite.dll\nConnectionString=URI=file:"<<std::string(reinterpret_cast<const char*>(db.data()),db.size())<<";version=3;Pooling=false\nAssetLoaderEnabled=false\n";out.close();auto events=std::make_shared<SceneCapabilityEvents>(SceneCapabilityEvents::Error{});RegionAssetRuntime assets(server::IniConfig::load(file),events);auto cache=std::dynamic_pointer_cast<FlotsamMemoryCache>(assets.asset_cache());if(!cache)throw std::runtime_error("No real configured cache");auto settings=cache->settings();std::cout<<std::bit_cast<std::uint64_t>(settings.memory_seconds)<<'|'<<settings.negative_milliseconds<<'\n';assets.stop();}catch(...){std::cout<<"ERR\n";}}return 0;}
if(argc>1&&std::string_view(argv[1])=="--probe"){Probe p;std::string s;while(std::getline(std::cin,s))std::cout<<p.run(s)<<'\n';return 0;}
 Probe p;p.run("init 1 1 0.5 120");p.run("cache 1");p.run("drop 1");if(p.run("get 1").find("1:asset1:")!=0)throw std::runtime_error("Strong memory retention");p.run("negative 1");if(p.run("get 1").find("0:-")!=0||p.run("cached 1").find("asset1:")!=0)throw std::runtime_error("Source negative cache getters");p.run("advance 2000");if(p.run("get 1").find("0:-")!=0)throw std::runtime_error("Strict expiry boundary");p.run("advance 2000");if(p.run("get 1").find("1:-")!=0)throw std::runtime_error("Periodic expiry");
 auto asset=std::make_shared<framework::AssetBase>(framework::Uuid::random(),"background",std::int8_t(0),"creator");auto id=asset->id();std::weak_ptr<framework::AssetBase> weak=asset;FlotsamMemoryCache real({true,true,.001,120});real.cache(asset);asset.reset();auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(!weak.expired()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(10));if(!weak.expired())throw std::runtime_error("Actual periodic worker expiry");
 std::cout<<"PASS: memory/negative cache semantics and real worker expiry\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
