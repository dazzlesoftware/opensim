// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/wearable_cache_item.hpp"
#include "opensim/network/osd_value.hpp"
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace opensim;using namespace framework;using network::Value;
Uuid id(int n){std::ostringstream out;out<<"00000000-0000-0000-0000-"<<std::hex<<std::setw(12)<<std::setfill('0')<<n;return Uuid(out.str());}
Value uuid(int n){Value value(id(n).str());value.kind=Value::Kind::Uuid;return value;}
std::string hex(const std::string& data){constexpr char alphabet[]="0123456789abcdef";std::string result;for(unsigned char c:data){result+=alphabet[c>>4];result+=alphabet[c&15];}return result;}
std::string tree(const Value& value){
    if(value.kind==Value::Kind::Array){std::string out="a[";for(const auto& child:value.array)out+=tree(child)+";";return out+"]";}
    if(value.kind==Value::Kind::Map){std::string out="m{";for(const auto& [key,child]:value.map)out+=key+":"+tree(child)+";";return out+"}";}
    if(value.kind==Value::Kind::Uuid)return "q"+network::osd_uuid(value).str();if(value.kind==Value::Kind::Binary){const auto bytes=network::osd_binary(value);return "x"+hex(std::string(bytes->begin(),bytes->end()));}if(value.kind==Value::Kind::String)return "s"+hex(value.text);if(value.kind==Value::Kind::Integer)return "i"+std::to_string(value.integer);return "u";
}
std::string items(const WearableCacheItems& values){std::string out=std::to_string(values.size())+"[";for(const auto& value:values)out+=value?std::to_string(value->texture_index)+":"+value->cache_id.str()+":"+value->texture_id.str()+":"+std::to_string(static_cast<bool>(value->texture_asset))+";":"null;";return out+"]";}
struct Cache:IAssetCache {
    int seed;std::string trace;std::vector<std::shared_ptr<AssetBase>> stored;std::shared_ptr<AssetBase> shared;
    explicit Cache(int profile):seed(profile){shared=std::make_shared<AssetBase>(id(300),profile&16?std::optional<std::string>{}:std::optional<std::string>{"Cached \xe8\xa9\xa6\xe9\xa8\x93"},std::int8_t{0},profile&16?std::optional<std::string>{}:std::optional<std::string>{"creator&name"});if(profile%3!=0)shared->data=std::make_shared<std::vector<std::uint8_t>>(profile%3==1?std::initializer_list<std::uint8_t>{1,2,3,4}:std::initializer_list<std::uint8_t>{});if(profile&32)shared->metadata(nullptr);}
    void cache(const std::shared_ptr<AssetBase>& asset,bool replace=false)override{trace+="cache:"+asset->id()+":"+std::to_string(replace)+";";stored.push_back(asset);if(seed&8)throw std::runtime_error("cache");}
    void cache_negative(const std::string&)override{}
    bool check(const std::string& key)override{trace+="check:"+key+";";if(seed%5==3)throw std::runtime_error("check");return seed%5!=0;}
    bool get(const std::string& key,std::shared_ptr<AssetBase>& result)override{trace+="get:"+key+";";if(seed%5==4)throw std::runtime_error("get");result=seed%5==1?nullptr:shared;return seed%5!=2;}
    std::shared_ptr<AssetBase> get_cached(const std::string&)override{return shared;}bool get_from_memory(const std::string&,std::shared_ptr<AssetBase>& result)override{result=shared;return true;}void expire(const std::string&)override{}void clear()override{}
    std::string state()const{std::string out=trace+"|";for(const auto& asset:stored){out+=asset->id()+":"+hex(asset->name().value_or(""))+":"+hex(asset->creator_id().value_or(""))+":"+std::to_string(asset->type())+":"+std::to_string(asset->temporary())+":"+std::to_string(asset->local())+":"+std::to_string(asset->flags())+":";out+=asset->data?hex(std::string(asset->data->begin(),asset->data->end())):"null";out+=';';}return out;}
};
Value row(std::uint32_t index,int cache=10,int texture=20){return Value::object({{"textureindex",network::osd_from_unsigned(index)},{"cacheid",uuid(cache)},{"textureid",uuid(texture)}});}
Value array(std::vector<Value> rows){Value value;value.kind=Value::Kind::Array;value.array=std::move(rows);return value;}
int main(int argc,char** argv){const int seed=std::stoi(argv[1]);
    const auto observe=[&](const std::string& label,const std::function<std::string(Cache&)>& action){Cache cache(seed);std::string result;try{result=action(cache);}catch(const std::exception&){result="EX";}std::cout<<label<<'|'<<result<<'|'<<cache.state()<<'\n';};
    observe("defaults",[](Cache&){return items(WearableCacheItem::defaults());});
    auto input=row(8);input.map["assetdata"]=network::osd_from_binary(std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{7,8,9}));input.map["assetcreator"]=Value("map creator");input.map["assetname"]=Value("map name");
    observe("from-map",[&](Cache& cache){return items(WearableCacheItem::from_osd(input,&cache));});
    observe("from-array",[&](Cache& cache){return items(WearableCacheItem::from_osd(array({input,row(44)}),&cache));});
    observe("from-array-no-cache",[&](Cache&){return items(WearableCacheItem::from_osd(array({input}),nullptr));});
    observe("from-empty-map",[](Cache& cache){return items(WearableCacheItem::from_osd(Value::object({}),&cache));});
    observe("from-wrong",[](Cache& cache){return items(WearableCacheItem::from_osd(Value(1),&cache));});
    observe("from-null",[](Cache& cache){return items(WearableCacheItem::from_osd(static_cast<const Value*>(nullptr),&cache));});
    observe("from-partial-error",[&](Cache& cache){return items(WearableCacheItem::from_osd(array({input,Value(1)}),&cache));});
    observe("from-alias",[&](Cache& cache){auto copy=input;const auto data=network::osd_binary(copy.get("assetdata"));auto result=WearableCacheItem::from_osd(copy,&cache);data->front()^=85;const bool aliased=!cache.stored.empty()&&cache.stored.back()->data==data;data->front()^=85;return items(result)+":"+std::to_string(aliased);});
    observe("baked-from-array",[&](Cache&){return items(WearableCacheItem::baked_from_osd(array({row(8,10,20),row(0,11,21),row(44,12,22),row(45,13,23),row(4294967295u,14,24),row(8,15,25),Value::object({})})));});
    observe("baked-from-map",[&](Cache&){return items(WearableCacheItem::baked_from_osd(input));});
    observe("baked-from-wrong",[](Cache&){return items(WearableCacheItem::baked_from_osd(Value(1)));});
    observe("baked-from-null",[](Cache&){return items(WearableCacheItem::baked_from_osd(static_cast<const Value*>(nullptr)));});
    observe("baked-from-error",[&](Cache&){return items(WearableCacheItem::baked_from_osd(array({row(0),Value(1)})));});
    const int lengths[]={0,5,9,29,45,50};WearableCacheItems values;
    for(int i=0;i<lengths[seed%6];++i){auto value=std::make_shared<WearableCacheItem>();value->texture_index=static_cast<std::uint32_t>(i);value->cache_id=id(100+i%3);value->texture_id=id(200+i%4);value->texture_asset=std::make_shared<AssetBase>();values.push_back(value);}
    observe("to",[&](Cache& cache){return tree(WearableCacheItem::to_osd(values,&cache));});
    observe("to-no-cache",[&](Cache&){return tree(WearableCacheItem::to_osd(values));});
    observe("to-null",[](Cache& cache){return tree(WearableCacheItem::to_osd(nullptr,&cache));});
    observe("to-alias",[&](Cache& cache){auto packed=WearableCacheItem::to_osd(values,&cache);if(cache.shared->data&&!cache.shared->data->empty())cache.shared->data->front()^=85;return tree(packed);});
    for(const auto& [start,end]:std::vector<std::pair<int,int>>{{-1,-1},{0,6},{6,11},{3,3},{5,2},{99,100},{-999,0},{0,2147483647}})observe("baked-to:"+std::to_string(start)+":"+std::to_string(end),[&](Cache&){const auto packed=WearableCacheItem::baked_to_osd(values,start,end);return packed?tree(*packed):"null";});
    for(const auto& [start,end]:std::vector<std::pair<int,int>>{{0,0},{2,1},{0,1}})observe("baked-null:"+std::to_string(start)+":"+std::to_string(end),[&](Cache&){const auto packed=WearableCacheItem::baked_to_osd(nullptr,start,end);return packed?tree(*packed):"null";});
    observe("search-index",[&](Cache&){auto copy=values;auto found=WearableCacheItem::search_index(0,&copy);if(found)found->texture_index=777;return std::to_string(static_cast<bool>(found))+":"+items(copy);});
    observe("search-cache",[&](Cache&){auto copy=values;auto found=WearableCacheItem::search_cache(id(101),&copy);if(found)found->texture_index=777;return std::to_string(static_cast<bool>(found))+":"+items(copy);});
    observe("search-texture",[&](Cache&){auto copy=values;auto found=WearableCacheItem::search_texture(id(202),&copy);if(found)found->texture_index=777;return std::to_string(static_cast<bool>(found))+":"+items(copy);});
    observe("search-missing",[&](Cache&){return WearableCacheItem::search_texture(id(999),&values)?"found":"null";});
    observe("search-null",[](Cache&){return WearableCacheItem::search_index(0,nullptr)?"found":"null";});
    observe("null-entry",[&](Cache& cache){auto copy=values;copy.insert(copy.begin(),nullptr);return tree(WearableCacheItem::to_osd(copy,&cache));});
    observe("search-before-null",[&](Cache&){auto copy=WearableCacheItem::defaults();copy[1]=nullptr;return WearableCacheItem::search_index(0,&copy)==copy[0]?"same":"other";});
    observe("search-at-null",[&](Cache&){auto copy=WearableCacheItem::defaults();copy[0]=nullptr;return WearableCacheItem::search_index(0,&copy)?"found":"null";});
    return 0;
}
