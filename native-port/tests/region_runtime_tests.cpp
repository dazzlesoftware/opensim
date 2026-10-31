#include "opensim/scene/region_runtime.hpp"
#include <iostream>
using namespace opensim;using namespace framework;using namespace services;using namespace scene;
struct FailingPresence:IPresenceService {
 NullPresenceStore store;PresenceService service{store};int region_logouts=0;
 bool login(const std::string& u,const Uuid& s,const Uuid& secure)override{return service.login(u,s,secure);}
 bool logout(const Uuid& s)override{return service.logout(s);}
 bool logout_region(const Uuid& r)override{++region_logouts;service.logout_region(r);throw std::runtime_error("controlled presence cleanup failure");}
 bool report(const Uuid& s,const Uuid& r)override{return service.report(s,r);}
 std::optional<PresenceInfo> agent(const Uuid& s)override{return service.agent(s);}
 std::vector<PresenceInfo> agents(const std::vector<std::string>& u)override{return service.agents(u);}
};
int main(int argc,char** argv){try{
 if(argc!=3)throw std::runtime_error("Expected original ubODE library and animation catalog paths");
 FailingPresence presence;auto listener=std::make_shared<server::RegistryCapabilityListener>(server::CapabilityListenerInfo{false,9000,0,std::string("127.0.0.1")});GridRegion region;region.id=Uuid::random();region.name="Cleanup";region.external_host="127.0.0.1";region.internal_endpoint=RegionEndpoint{"127.0.0.1",0};
 RegionPolicies policies;policies.physics_library=argv[1];policies.animations=std::make_shared<DefaultAnimations>(argv[2]);policies.access=[](auto,const auto&,auto&){return true;};policies.authorize=[](const auto&,auto&){return true;};
 {RegionRuntime runtime(region,presence,policies);auto bound=runtime.start(listener);auto circuit=std::make_shared<AgentCircuitData>();circuit->agent_id=Uuid::random();circuit->session_id=Uuid::random();circuit->secure_session_id=Uuid::random();circuit->circuit_code=42;circuit->caps_path="shutdown";presence.login(circuit->agent_id.str(),circuit->session_id,circuit->secure_session_id);LoginTransferContext context;std::string reason;
  if(!runtime.create_agent(nullptr,region,circuit,128,context,reason)||listener->routes.simple_keys().empty()||listener->routes.poll_keys().empty())throw std::runtime_error("Region admission did not own live capability routes");
  auto abandoned=std::make_shared<AgentCircuitData>(*circuit);abandoned->agent_id=Uuid::random();abandoned->session_id=Uuid::random();abandoned->secure_session_id=Uuid::random();abandoned->circuit_code=43;abandoned->caps_path="second-unconnected";
  presence.login(abandoned->agent_id.str(),abandoned->session_id,abandoned->secure_session_id);
  if(!runtime.create_agent(nullptr,region,abandoned,128,context,reason))throw std::runtime_error("Unconnected login reservation blocked subsequent admission");
  bool failed=false;try{runtime.stop();}catch(const std::runtime_error& e){failed=std::string(e.what())=="controlled presence cleanup failure";}
  if(!failed||runtime.capabilities()||!listener->routes.simple_keys().empty()||!listener->routes.poll_keys().empty())throw std::runtime_error("Cleanup failure skipped capability disposal");
  network::UdpSocket replacement({bound.address,bound.port},[](const auto&,auto){});replacement.start();replacement.stop();
  if(runtime.create_agent(nullptr,region,circuit,128,context,reason))throw std::runtime_error("Stopped region accepted new login");
 } // destructor repeats throwing provider cleanup without terminating the process.
 if(presence.region_logouts!=2)throw std::runtime_error("Destructor cleanup was not exercised");std::cout<<"PASS: region cleanup continues after provider failure; destructor does not terminate\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
