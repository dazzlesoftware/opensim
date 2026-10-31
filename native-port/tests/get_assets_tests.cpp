// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/get_assets.hpp"
#include "opensim/framework/object_job_engine.hpp"
#include "opensim/framework/atomic_int32_property.hpp"
#include <future>
#include <iostream>
#include <thread>
using namespace opensim;
namespace {
int checks=0;
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
struct Service:server::AssetRetrievalService {
 std::shared_ptr<framework::AssetBase> asset;Complete saved;std::string id;std::optional<std::string> foreign;bool store=true;int calls=0;std::function<void()> before;
 void get(const std::string& value,const std::optional<std::string>& url,bool local,Complete callback)override{++calls;id=value;foreign=url;store=local;saved=callback;if(before)before();callback(asset);}
};
std::shared_ptr<server::HttpResponseState> response(){return std::make_shared<server::HttpResponseState>(std::shared_ptr<server::HttpResponseContext>{},"HTTP/1.1",server::HttpConnectionType::KeepAlive);}
std::shared_ptr<server::AssetHttpRequest> request(const std::string& query,const std::optional<std::string>& range={}){network::HttpRequest native;native.method="POST";native.raw_target="/asset?"+query;if(range)native.headers["range"]=*range;return std::make_shared<server::AssetHttpRequest>(std::move(native));}
void handler_tests(){
 const std::string uuid="00000000-0000-0000-0000-000000000123";
 auto service=std::make_shared<Service>();service->asset=std::make_shared<framework::AssetBase>();service->asset->type(0);service->asset->data=std::make_shared<std::vector<uint8_t>>(std::initializer_list<uint8_t>{0,1,2,3,4,5});
 std::vector<std::string> warnings;server::GetAssetsHandler handler(service,[&](const auto& text){warnings.push_back(text);});
 for(auto key:{"texture_id","sound_id","callcard_id","landmark_id","script_id","clothing_id","object_id","notecard_id","lsltext_id","lslbyte_id","txtr_tga_id","bodypart_id","snd_wav_id","img_tga_id","jpeg_id","animatn_id","gesture_id","mesh_id","settings_id","material_id"}){auto r=response();handler.handle(request(std::string(key)+"="+uuid),r,"https://foreign.invalid/assets");check(r->status()==200,"all modern query types delivered");check(r->raw_buffer()==service->asset->data&&r->raw_length()==6,"asset bytes retain reference");check(r->priority()==((std::string(key)=="texture_id"||std::string(key)=="mesh_id")?2:1),"requested type controls priority");check(service->id==uuid&&service->foreign=="https://foreign.invalid/assets"&&!service->store,"foreign service and no local storage forwarded");}
 for(auto query:std::vector<std::string>{"","unknown=value","Texture_id="+uuid,"texture_id=bad&mesh_id="+uuid,"texture_id=&mesh_id="+uuid,"texture_id="+uuid+"&TEXTURE_ID="+uuid}){auto r=response();int before=service->calls;handler.handle(request(query),r);check(r->status()==400&&service->calls==before,"invalid query stops before retrieval");}
 struct Range{const char* value;int status,start,length;};
 for(auto item:{Range{"bytes=1-3",206,1,3},Range{"bytes=-2",206,0,3},Range{"bytes=4-",206,4,2},Range{"bytes=9-",200,0,6},Range{"bytes=1-0",200,0,6},Range{"bytes=4-2",206,2,1},Range{"bytes=0-99",206,0,6},Range{"bytes=1-2,4-5",200,0,6}}){auto r=response();handler.handle(request("texture_id="+uuid,item.value),r);check(r->status()==item.status&&r->raw_start()==item.start&&r->raw_length()==item.length,"source range delivery behavior");}
 {auto r=response();r->raw_start(4);handler.handle(request("texture_id="+uuid,"bytes=999-"),r);check(r->raw_start()==4,"out of range leaves old buffer start");}
 {auto r=response();server::GetAssetsHandler disabled({});disabled.handle({},r);check(r->status()==503&&r->connection()==server::HttpConnectionType::Close,"missing service short circuits null request");}
 {auto r=response();service->asset.reset();handler.handle(request("texture_id="+uuid),r);check(r->status()==404,"missing asset");}
 service->asset=std::make_shared<framework::AssetBase>();service->asset->type(49);service->asset->data=std::make_shared<std::vector<uint8_t>>();
 {auto r=response();handler.handle(request("texture_id="+uuid),r);check(r->status()==404&&!warnings.empty(),"empty asset rejected and logged");}
 service->asset->data->push_back(7);
 {auto r=response();handler.handle(request("texture_id="+uuid),r);auto old=r->raw_buffer();bool threw=false;try{service->saved({});}catch(const std::exception&){threw=true;}check(threw&&r->raw_buffer()==old,"late callback throws without changing delivered bytes");check(r->status()==200&&r->priority()==2,"wrong asset type remains served");}
 auto query=server::asset_query_dictionary("/asset?flag&=empty&texture_id=one&TEXTURE_ID=two&mesh_id=a+b%26c");check(query->size()==2&&query->at("texture_id")=="one,two"&&query->at("mesh_id")=="a b&c","ordered query duplicate grouping and decoding");
 framework::AtomicInt32Property counter(INT32_MIN);check(counter--==INT32_MIN&&int32_t(counter)==INT32_MAX,"unchecked decrement wraps");check(--counter==INT32_MAX-1,"prefix decrement returns new value");
}
template<class F>void until(F predicate){auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!predicate()){if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("worker timeout");std::this_thread::yield();}}
void worker_tests(){
 framework::ObjectJobEngine inactive({},"inactive");check(!inactive.enqueue(1)&&inactive.count()==0,"null callback disables worker");
 std::atomic<int> calls=0,errors=0;{framework::ObjectJobEngine worker([&](std::any value){++calls;if(std::any_cast<int>(value)==1)throw std::runtime_error("expected");},"errors",5,1,[&](const auto&,const auto&){++errors;});worker.enqueue(1);worker.enqueue(2);until([&]{return calls==2&&errors==1;});check(true,"callback errors continue queue");until([&]{return worker.active_threads()==0;});worker.enqueue(3);until([&]{return calls==3;});check(true,"idle workers restart");worker.dispose();check(!worker.enqueue(4)&&worker.count()==0,"disposed worker rejects jobs");}
 std::promise<void> entered,release,finished;auto gate=release.get_future().share();auto completion=finished.get_future();auto worker=std::make_unique<framework::ObjectJobEngine>([&](std::any){entered.set_value();gate.wait();finished.set_value();},"blocked",1000,1);worker->enqueue(1);entered.get_future().wait();worker->enqueue(2);worker->cancel();check(worker->count()==0,"cancel drains pending queue");worker->enqueue(3);check(worker->count()==1,"cancelled token accepts later jobs without processing");worker.reset();check(completion.wait_for(std::chrono::milliseconds(0))==std::future_status::timeout,"worker disposal does not block on callback");release.set_value();check(completion.wait_for(std::chrono::seconds(5))==std::future_status::ready,"running callback survives owner disposal");
}
}
int main(){try{handler_tests();worker_tests();std::cout<<"PASS: "<<checks<<" asset handler and worker checks\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
