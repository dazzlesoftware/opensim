#include "opensim/services/login_response.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/services/login_agent_factory.hpp"
#include "opensim/framework/identity.hpp"
#include <iostream>
#include <stdexcept>
using namespace opensim;using network::Value;
void check(bool passed,const char* message){if(!passed)throw std::runtime_error(message);}
int main(){try{
 services::LoginResponse response({},[]{return 1700000000.;});
 check(response.to_llsd().get("reason").as_string()=="Internal Error","source null library-root LLSD failure");
 response.inventory_library_root=std::make_shared<Value>();response.inventory_library_root->kind=Value::Kind::Array;
 auto llsd=response.to_llsd();check(llsd.get("agent_id").kind==Value::Kind::Uuid&&!llsd.contains("http_port")&&!llsd.contains("region_size_x"),"LLSD UUID and omitted source fields");
 auto first=response.to_xml_rpc();response.to_xml_rpc();check(first.get("login-flags").view().array.size()==2,"prior response retains shared flags array");
 auto json=network::parse_osd_json(network::osd_json(first,true));check(json.get("login-flags").array.size()==2,"JSON resolves live aliases");
 auto rpc=network::parse_xml_rpc_response(network::xml_rpc_response(first));check(rpc.get("ui-config").array.size()==2&&rpc.get("http_port").kind==Value::Kind::Integer,"XML-RPC resolves live aliases and retains integer fields");
 auto round=network::parse_llsd_xml(network::llsd_xml(first));check(round.get("global-textures").array.size()==2,"LLSD writer resolves live aliases");
 response.add_classified(10,"Extra");check(first.get("classified_categories").view().array.size()==10&&llsd.get("classified_categories").array.size()==9,"raw response lists alias while OSD conversions copy");
 response.add_ui("custom","live");check(first.get("ui-config").view().array[0].get("custom").as_string()=="live","all prior UI entries share the source dictionary");
 auto target=std::make_shared<Value>(std::int32_t{7});auto alias=Value::alias(target);check(network::osd_integer(alias)==7&&network::osd_boolean(alias),"scalar conversion resolves references");
 auto cycle=std::make_shared<Value>();cycle->reference=cycle;bool threw=false;try{cycle->set("key","value");}catch(const std::invalid_argument&){threw=true;}cycle->reference.reset();check(threw,"reference cycles have a bounded failure");
 // Connect the constructor to actual SQLite models, the agent factory and the bundled library.
 services::AccountStore accounts(":memory:",":memory:",true);const auto principal=accounts.create_user("Constructor","Resident","secret",framework::ZeroUuid,0);auto account=*accounts.find_by_id(std::string(framework::ZeroUuid),principal);const framework::Uuid owner(principal);
 services::InventoryFolderStore store(":memory:",true);services::InventoryService inventory(store);check(inventory.create_user_inventory(owner),"create actual inventory for response assembly");auto skeleton=inventory.skeleton(owner);auto root=inventory.root(owner);services::InventoryItem gesture;gesture.id=framework::Uuid::random();gesture.asset_id=framework::Uuid::random();gesture.owner=owner;gesture.parent=root->id;gesture.name="Active";gesture.asset_type=21;gesture.inventory_type=21;gesture.flags=1;check(inventory.add_item(gesture),"persist active gesture");
 services::SqliteGridUserStore grid_store(":memory:",true);services::GridUserService users(grid_store);services::GridRegion region;region.id=framework::Uuid::random();region.x=51200;region.y=25600;region.size_x=512;region.size_y=256;region.internal_endpoint=services::RegionEndpoint{"0.0.0.0",9000};region.external_host="203.0.113.5";region.set_server_uri("http://destination:9001/");users.set_home(principal,region.id,{128,128,30},{0,1,0});auto info=users.info(principal);
 services::LoginAgentFactory factory(services::LoginAgentFactory::Config{},std::nullopt,[&](auto& a){return accounts.store_account(a);},[]{return "seed";});services::LoginAgentDetails details;details.session=framework::Uuid::random();details.secure_session=framework::Uuid::random();details.circuit=65537;details.position={128,128,30};auto circuit=factory.make_agent(account,nullptr,details);
 auto retained_library=services::LoginResponseLibrary::bind(services::LibraryService(std::string(OPENSIM_REPOSITORY_ROOT)+"/bin/inventory/Libraries.xml"));check(retained_library->root()&&retained_library->folders()->size()==19,"library binding owns the shared service state");
 services::LibraryService library(std::string(OPENSIM_REPOSITORY_ROOT)+"/bin/inventory/Libraries.xml");services::LoginResponseInput input;input.account=&account;input.circuit=circuit.get();input.grid_user=&*info;input.destination=input.home=&region;input.skeleton=services::LoginResponseLibrary::Folders{};for(const auto& f:*skeleton)input.skeleton->push_back(std::make_shared<services::LoginInventoryFolder>(f));input.gestures=std::vector<std::shared_ptr<services::InventoryItem>>{};for(const auto& i:inventory.active_gestures(owner))input.gestures->push_back(std::make_shared<services::InventoryItem>(i));input.friends=std::vector<std::shared_ptr<services::LoginFriendInfo>>{};input.library=services::LoginResponseLibrary::bind(library);input.where="home";input.message="Welcome";input.look_at={0,1,0};input.dst_zone="none";input.map_url=input.profile_url=input.openid_url=input.search_url=input.currency=input.classified_fee=input.destinations_url=input.avatars_url="";input.max_agent_groups=60;
 services::LoginResponseEnvironment environment;services::bind_login_response_network(environment);services::LoginResponse assembled(input,environment,{},[]{return 1700000000.;});auto assembled_rpc=network::parse_xml_rpc_response(network::xml_rpc_response(assembled.to_xml_rpc()));auto assembled_llsd=network::parse_llsd_xml(network::llsd_xml(assembled.to_llsd()));
 check(assembled_rpc.get("agent_id").as_string()==principal&&assembled_rpc.get("session_id").as_string()==details.session.str()&&assembled_rpc.get("circuit_code").integer==65537,"account and factory circuit feed response identity");
 check(assembled_rpc.get("inventory-root").array.at(0).get("folder_id").as_string()==root->id.str()&&assembled_llsd.get("inventory-skeleton").array.size()==skeleton->size(),"real SQLite inventory root and skeleton survive response serialization");
 check(assembled_llsd.get("gestures").array.size()==1&&assembled_llsd.get("gestures").array.at(0).get("item_id").as_string()==gesture.id.str(),"real active gestures feed the response");
 check(assembled_llsd.get("inventory-skel-lib").array.size()==library.ordered_folders().size()&&assembled_llsd.get("inventory-skel-lib").array.size()==19,"bound bundled library populates LLSD response");
 check(assembled_rpc.get("seed_capability").as_string()=="http://destination:9001/CAPS/seed0000/"&&assembled_rpc.get("sim_port").integer==9000&&assembled_rpc.get("http_port").integer==0,"constructor preserves source seed, simulator endpoint and untouched HTTP-port behavior");
 check(!users.info(principal)->online,"response construction alone does not mark a user online or admit an agent");
 std::cout<<"PASS: login response wire serialization and shared state\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
