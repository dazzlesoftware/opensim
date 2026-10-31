// Real region owner-thread dispatch and shutdown cancellation, without a viewer.
#include "opensim/scene/region_runtime.hpp"
#include <future>
#include <iostream>
#include <thread>
using namespace opensim;using namespace scene;using namespace services;
int main(){try{
 NullPresenceStore store;PresenceService presence(store);RegionPolicies policies;policies.access=[](auto,const auto&,auto&){return true;};policies.authorize=[](const auto&,auto&){return true;};
 GridRegion region;region.id=framework::Uuid::random();region.name="Owner dispatch";region.external_host="127.0.0.1";region.internal_endpoint=RegionEndpoint{"127.0.0.1",0};RegionRuntime runtime(region,presence,std::move(policies));runtime.start(std::make_shared<server::RegistryCapabilityListener>(server::CapabilityListenerInfo{false,19009,0,{}}));
 auto agent=framework::Uuid::random(),item=framework::Uuid::random(),object=framework::Uuid::random();std::vector<std::uint8_t> bytes{'d','e','f','a','u','l','t'};
 auto operation=std::async(std::launch::async,[&]{return runtime.update_task_script(agent,item,object,true,bytes);});auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(operation.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready&&std::chrono::steady_clock::now()<deadline){runtime.poll();std::this_thread::yield();}if(operation.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)throw std::runtime_error("Owner did not complete queued update");if(!operation.get().empty())throw std::runtime_error("Missing source avatar did not return empty errors");
 std::vector<std::future<bool>> pending;for(int i=0;i<16;++i)pending.push_back(std::async(std::launch::async,[&]{try{runtime.update_task_script(agent,item,object,true,bytes);return false;}catch(const std::exception&){return true;}}));runtime.quiesce_script_updates();for(auto& task:pending)if(task.wait_for(std::chrono::seconds(3))!=std::future_status::ready||!task.get())throw std::runtime_error("Quiesce stranded or accepted a pending script update");
 bool rejected=false;try{runtime.update_task_script(agent,item,object,true,bytes);}catch(const std::exception&){rejected=true;}if(!rejected)throw std::runtime_error("Quiesced owner accepted an update");runtime.stop();std::cout<<"PASS: real owner-thread dispatch,16 concurrent cancellations and post-quiesce rejection\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
