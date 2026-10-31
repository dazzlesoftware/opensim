// Connected viewer-protocol probe; uses production codecs over actual UDP.
#include "opensim/network/udp_socket.hpp"
#include "opensim/network/lludp.hpp"
#include "opensim/scene/terrain_layer.hpp"
#include "opensim/framework/uuid.hpp"
#include "legacy_inventory_probe.hpp"
#include "task_inventory_probe.hpp"
#include "inventory_script_rez_probe.hpp"
#include <condition_variable>
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <optional>
#include <iostream>
#include <set>
using namespace opensim;using namespace framework;using namespace network;
int main(int argc,char** argv){try{
 if(argc<6||argc>11)throw std::invalid_argument("agent session circuit host port [state message] [touch message] [--walk] [legacy metadata] required");
 bool has_touch=argc>7&&std::string(argv[7]).size()>0,walk=argc>8&&std::string(argv[8])=="--walk";
 Uuid agent(argv[1]),session(argv[2]);auto code=static_cast<std::uint32_t>(std::stoul(argv[3]));UdpEndpoint destination{argv[4],static_cast<std::uint16_t>(std::stoul(argv[5]))};
 std::mutex mutex;std::condition_variable changed;std::vector<lludp::Packet> packets;std::atomic<std::uint32_t> sequence=1;UdpSocket* sending=nullptr;
 UdpSocket socket({"127.0.0.1",0},[&](const auto& from,auto bytes){auto p=lludp::decode_datagram(bytes,65536,false);if(p.reliable){auto ack=lludp::make_packet("PacketAck");ack.sequence=sequence++;auto b=lludp::make_block(ack.spec->blocks[0]);b["ID"]=std::uint64_t(p.sequence);ack.blocks["Packets"].push_back(std::move(b));sending->send_to(from,lludp::encode_datagram(ack));}{std::lock_guard lock(mutex);packets.push_back(std::move(p));}changed.notify_all();});sending=&socket;socket.start();
 auto send=[&](const char* name){auto p=lludp::make_packet(name);p.sequence=sequence++;if(p.blocks.contains("AgentData")){p.block("AgentData")["AgentID"]=agent;p.block("AgentData")["SessionID"]=session;}return p;};
 auto transmit=[&](const auto& p){socket.send_to(destination,lludp::encode_datagram(p));};
 int wait_index=0;auto wait=[&](auto predicate){++wait_index;std::unique_lock lock(mutex);if(!changed.wait_for(lock,std::chrono::seconds(12),[&]{return predicate(packets);})){
  std::set<std::string> names;for(auto& p:packets){names.insert(p.spec->name);if(p.spec->name=="ChatFromSimulator"||p.spec->name=="AlertMessage"){auto& b=p.block(p.spec->name=="AlertMessage"?"AlertData":"ChatData");auto& bytes=std::get<std::vector<std::uint8_t>>(b.at("Message"));std::cerr<<"Message: "<<std::string(bytes.begin(),bytes.end())<<'\n';}}
  std::string seen;for(auto& name:names)seen+=name+" ";throw std::runtime_error("Viewer session timeout at wait "+std::to_string(wait_index)+": "+seen);
 }};
 auto has=[](const auto& list,const char* name){return std::any_of(list.begin(),list.end(),[&](const auto& p){return p.spec->name==name;});};
 auto use=send("UseCircuitCode");use.block("CircuitCode")["ID"]=agent;use.block("CircuitCode")["SessionID"]=session;use.block("CircuitCode")["Code"]=std::uint64_t(code);transmit(use);wait([&](const auto& p){return has(p,"RegionHandshake");});
 auto reply=send("RegionHandshakeReply");transmit(reply);
 if(argc>9&&std::string_view(argv[9]).size()){tests::LegacyInventoryProbeIo io;io.make=send;io.send=transmit;io.wait=[&](auto predicate){wait(predicate);};io.snapshot=[&]{std::lock_guard lock(mutex);return packets;};tests::run_legacy_inventory_probe(agent,std::filesystem::path(argv[9]),io);}
 auto complete=send("CompleteAgentMovement");complete.block("AgentData")["CircuitCode"]=std::uint64_t(code);transmit(complete);
 scene::Terrain terrain;terrain.clear_land();std::vector<scene::TerrainPatchCoordinate> patches;for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x)patches.push_back({x,y});auto expected=scene::terrain_layer_packets(terrain,patches);
 wait([&](const auto& list){std::set<std::uint32_t> ids;for(auto& p:list)if(p.spec->name=="LayerData")ids.insert(p.sequence);return has(list,"AgentMovementComplete")&&ids.size()>=expected.size();});
 {std::lock_guard lock(mutex);std::set<std::uint32_t> seen;std::size_t i=0;for(auto& p:packets)if(p.spec->name=="LayerData"&&seen.insert(p.sequence).second){if(i>=expected.size()||std::get<std::vector<std::uint8_t>>(p.block("LayerData").at("Data"))!=expected[i].data||std::get<std::uint64_t>(p.block("LayerID").at("Type"))!=expected[i].type)throw std::runtime_error("Terrain stream content mismatch");++i;}}
 wait([&](const auto& list){for(auto& p:list)if(p.spec->name=="ObjectUpdate")for(auto& b:p.blocks.at("ObjectData"))if(std::get<Uuid>(b.at("FullID"))==agent&&std::get<std::uint64_t>(b.at("PCode"))==47)return true;return false;});
 std::uint32_t avatar_local=0;{std::lock_guard lock(mutex);for(auto& p:packets)if(p.spec->name=="ObjectUpdate")for(auto& b:p.blocks.at("ObjectData"))if(std::get<Uuid>(b.at("FullID"))==agent&&std::get<std::uint64_t>(b.at("PCode"))==47){avatar_local=static_cast<std::uint32_t>(std::get<std::uint64_t>(b.at("ID")));break;}}if(avatar_local<720001)throw std::runtime_error("Missing shared avatar local ID");
 auto wearable_request=send("AgentWearablesRequest");transmit(wearable_request);wait([&](const auto& list){return has(list,"AgentWearablesUpdate")&&has(list,"AvatarAppearance");});
 auto add=send("ObjectAdd");auto& shape=add.block("ObjectData");shape["PCode"]=std::uint64_t(9);shape["PathCurve"]=std::uint64_t(16);shape["ProfileCurve"]=std::uint64_t(1);shape["PathScaleX"]=shape["PathScaleY"]=std::uint64_t(100);shape["Scale"]=std::vector<double>{.5,.5,.5};shape["Rotation"]=std::vector<double>{0,0,0,1};shape["RayStart"]=std::vector<double>{128,128,70};shape["RayEnd"]=std::vector<double>{128,128,21};shape["RayEndIsIntersection"]=std::uint64_t(1);shape["AddFlags"]=std::uint64_t(2);transmit(add);
 auto primitive=[&](const auto& p){return p.spec->name=="ObjectUpdate"&&!p.blocks.at("ObjectData").empty()&&std::get<std::uint64_t>(p.block("ObjectData").at("PCode"))==9&&std::get<std::uint64_t>(p.block("ObjectData").at("ID"))==avatar_local+1;};
 wait([&](const auto& list){return std::any_of(list.begin(),list.end(),primitive);});
 auto real=[](const auto& bytes,std::size_t offset){std::uint32_t bits=0;for(int i=0;i<4;++i)bits|=std::uint32_t(bytes.at(offset+i))<<(8*i);return std::bit_cast<float>(bits);};std::uint32_t local=0;Uuid object_id;
 {std::lock_guard lock(mutex);for(auto& p:packets)if(primitive(p)){auto& b=p.block("ObjectData");local=static_cast<std::uint32_t>(std::get<std::uint64_t>(b.at("ID")));object_id=std::get<Uuid>(b.at("FullID"));auto& data=std::get<std::vector<std::uint8_t>>(b.at("ObjectData"));if(local!=avatar_local+1||real(data,8)!=21.25f||(std::get<std::uint64_t>(b.at("UpdateFlags"))&2)==0||std::get<std::vector<double>>(b.at("Scale"))!=std::vector<double>{.5,.5,.5})throw std::runtime_error("ObjectAdd did not preserve source rez/shape/local ID/selection");break;}}
 auto move=send("ObjectPosition");auto block=lludp::make_block(move.spec->blocks.at(1));block["ObjectLocalID"]=std::uint64_t(local);block["Position"]=std::vector<double>{130,129,22};move.blocks["ObjectData"].push_back(std::move(block));transmit(move);
 wait([&](const auto& list){for(auto& p:list)if(primitive(p)){auto& bytes=std::get<std::vector<std::uint8_t>>(p.block("ObjectData").at("ObjectData"));if(real(bytes,0)==130&&real(bytes,4)==129&&real(bytes,8)==22)return true;}return false;});
 // Derive the actual spawn height from the server's movement completion.
 std::vector<double> avatar_position;{std::lock_guard lock(mutex);for(auto& p:packets)if(p.spec->name=="AgentMovementComplete"){avatar_position=std::get<std::vector<double>>(p.block("Data").at("Position"));break;}}
 move.sequence=sequence++;move.block("ObjectData")["Position"]=std::vector<double>{avatar_position.at(0)+2,avatar_position.at(1)+1,avatar_position.at(2)};transmit(move);
 wait([&](const auto& list){for(auto& p:list)if(primitive(p)&&real(std::get<std::vector<std::uint8_t>>(p.block("ObjectData").at("ObjectData")),8)==static_cast<float>(avatar_position.at(2)))return true;return false;});
 tests::LegacyInventoryProbeIo task_io;task_io.make=send;task_io.send=transmit;task_io.wait=[&](auto predicate){wait(predicate);};task_io.snapshot=[&]{std::lock_guard lock(mutex);return packets;};
 tests::check_empty_task_inventory(object_id,local,task_io);
 auto rez=send("RezScript");rez.block("UpdateBlock")["ObjectLocalID"]=std::uint64_t(0xdead);auto& inventory=rez.block("InventoryBlock");inventory["FolderID"]=object_id;inventory["OwnerID"]=inventory["CreatorID"]=agent;inventory["Type"]=inventory["InvType"]=std::int64_t(10);inventory["Name"]=std::vector<std::uint8_t>{'N','e','w',' ','S','c','r','i','p','t',0};inventory["BaseMask"]=inventory["OwnerMask"]=inventory["NextOwnerMask"]=std::uint64_t(0x7fffffff);transmit(rez);
 auto chat=[&](const auto& list,const std::string& message){for(auto& p:list)if(p.spec->name=="ChatFromSimulator"){auto& b=p.block("ChatData");auto& bytes=std::get<std::vector<std::uint8_t>>(b.at("Message"));std::string text(bytes.begin(),std::find(bytes.begin(),bytes.end(),0));if(text==message&&std::get<Uuid>(b.at("SourceID"))==object_id&&std::get<Uuid>(b.at("OwnerID"))==agent)return true;}return false;};
 wait([&](const auto& list){return chat(list,argc>6?argv[6]:"Script running");});
 std::function<void(Uuid)> verify_script_asset;if(argc>10&&std::string_view(argv[10])=="--verify-script-assets")verify_script_asset=[](Uuid){std::string result;if(!std::getline(std::cin,result)||result!="OK")throw std::runtime_error("Live Contents script asset verification failed");};
 Uuid first_script;bool first_script_has_instance=true,first_script_running=true;std::function<void(Uuid,Uuid)> verify_script_running;if(argc>10)verify_script_running=[&](Uuid object,Uuid item){if(first_script.is_zero())first_script=item;if(item==first_script&&!first_script_has_instance)return;std::cout<<"Script running query "<<object.str()<<' '<<item.str()<<' '<<((item!=first_script||first_script_running)?1:0)<<std::endl;std::string result;if(!std::getline(std::cin,result)||result!="OK")throw std::runtime_error("Live script running EventQueue verification failed");};
 auto first_task_file=tests::check_task_inventory_download(agent,object_id,local,1,task_io,verify_script_asset,verify_script_running);
 auto second_start=task_io.snapshot().size();rez.sequence=sequence++;transmit(rez);
 wait([&](const auto& list){std::vector<lludp::Packet> added(list.begin()+std::min(second_start,list.size()),list.end());return chat(added,argc>6?argv[6]:"Script running");});
 auto second_task_file=tests::check_task_inventory_download(agent,object_id,local,2,task_io,verify_script_asset,verify_script_running);if(first_task_file==second_task_file)throw std::runtime_error("Task mutation did not invalidate filename cache");
 if(has_touch){auto grab=send("ObjectGrab");grab.block("ObjectData")["LocalID"]=std::uint64_t(local);transmit(grab);wait([&](const auto& list){return chat(list,argv[7]);});}
 wait([&](const auto& list){for(auto& p:list)if(primitive(p)){auto flags=std::get<std::uint64_t>(p.block("ObjectData").at("UpdateFlags"));if((flags&0x40)!=0&&(flags&0x800)==0&&bool(flags&0x80)==has_touch)return true;}return false;});
 if(argc>10&&std::string_view(argv[10])=="--verify-script-assets"){
  auto previous_file=second_task_file;
  for(int mode=1;mode<=4;++mode){auto after=task_io.snapshot().size();std::cout<<"Script save query "<<object_id.str()<<' '<<first_script.str()<<' '<<mode<<std::endl;std::string result;if(!std::getline(std::cin,result)||result!="OK")throw std::runtime_error("Live script save HTTP verification failed");auto serial=mode+2;
   wait([&](const auto& list){for(auto n=after;n<list.size();++n)if(list[n].spec->name=="ObjectProperties")for(const auto& b:list[n].blocks.at("ObjectData"))if(std::get<Uuid>(b.at("ObjectID"))==object_id&&std::get<std::int64_t>(b.at("InventorySerial"))==serial)return true;return false;});
   auto properties=task_io.snapshot();bool checked=false;for(auto n=after;n<properties.size();++n)if(properties[n].spec->name=="ObjectProperties")for(const auto& b:properties[n].blocks.at("ObjectData"))if(std::get<Uuid>(b.at("ObjectID"))==object_id&&std::get<std::int64_t>(b.at("InventorySerial"))==serial){if(!properties[n].reliable||!properties[n].zerocoded||std::get<Uuid>(b.at("CreatorID"))!=agent||std::get<Uuid>(b.at("OwnerID"))!=agent||std::get<Uuid>(b.at("LastOwnerID"))!=agent||std::get<std::uint64_t>(b.at("BaseMask"))!=0x9e000||std::get<std::uint64_t>(b.at("OwnerMask"))!=0x9e000||std::get<std::uint64_t>(b.at("NextOwnerMask"))!=0x82000||std::get<std::uint64_t>(b.at("GroupMask"))!=0||std::get<std::uint64_t>(b.at("EveryoneMask"))!=0||std::get<std::uint64_t>(b.at("CreationDate"))==0||std::get<std::uint64_t>(b.at("CreationDate"))%1000000)throw std::runtime_error("ObjectProperties actual identity/permissions/date/serial mismatch");checked=true;}if(!checked)throw std::runtime_error("Script save missed actual object properties");
   if(mode==1||mode==4)wait([&](const auto& list){std::vector<lludp::Packet> added(list.begin()+std::min(after,list.size()),list.end());return chat(added,mode==1?"Saved native":"Recovered native");});
   if(mode==3){std::this_thread::sleep_for(std::chrono::milliseconds(300));auto list=task_io.snapshot();std::vector<lludp::Packet> added(list.begin()+std::min(after,list.size()),list.end());if(chat(added,"Stopped native should not execute"))throw std::runtime_error("Stopped script executed state_entry");}
   first_script_has_instance=mode!=2;first_script_running=mode!=3;
   auto filename=tests::check_task_inventory_download(agent,object_id,local,static_cast<std::int16_t>(serial),task_io,verify_script_asset,verify_script_running,2);if(filename==previous_file)throw std::runtime_error("Script asset update retained stale inventory file");previous_file=std::move(filename);
  }
  auto count_recovered=[&](){auto list=task_io.snapshot();std::set<std::uint32_t> ids;for(const auto& p:list)if(chat(std::vector<lludp::Packet>{p},"Recovered native"))ids.insert(p.sequence);return ids.size();};
  auto query_running=[&]{auto p=send("GetScriptRunning");p.block("Script")["ObjectID"]=object_id;p.block("Script")["ItemID"]=first_script;transmit(p);verify_script_running(object_id,first_script);};
  auto set_running=[&](bool value){auto p=send("SetScriptRunning");p.block("Script")["ObjectID"]=object_id;p.block("Script")["ItemID"]=first_script;p.block("Script")["Running"]=value;transmit(p);first_script_running=value;query_running();};
  auto reset_script=[&]{auto p=send("ScriptReset");p.block("Script")["ObjectID"]=object_id;p.block("Script")["ItemID"]=first_script;transmit(p);query_running();};
  auto recovered=count_recovered();set_running(false);reset_script();std::this_thread::sleep_for(std::chrono::milliseconds(300));if(count_recovered()!=recovered)throw std::runtime_error("Reset incorrectly started stopped script");
  set_running(true);wait([&](const auto& list){std::set<std::uint32_t> ids;for(const auto& p:list)if(chat(std::vector<lludp::Packet>{p},"Recovered native"))ids.insert(p.sequence);return ids.size()>recovered;});if(count_recovered()!=recovered+1)throw std::runtime_error("Start after stopped reset replayed state entry");
  recovered=count_recovered();reset_script();wait([&](const auto& list){std::set<std::uint32_t> ids;for(const auto& p:list)if(chat(std::vector<lludp::Packet>{p},"Recovered native"))ids.insert(p.sequence);return ids.size()>recovered;});
  auto unchanged=tests::check_task_inventory_download(agent,object_id,local,6,task_io,verify_script_asset,verify_script_running,2);if(unchanged!=previous_file)throw std::runtime_error("Script start/stop/reset mutated task inventory serial or asset cache");
  std::cout<<"Verified viewer stop/start/reset, stopped reset state_entry and unchanged inventory serial\n";

 }
 if(argc>9&&std::string_view(argv[9]).size())tests::run_inventory_script_rez_probe(agent,object_id,local,std::filesystem::path(argv[9]),task_io);
 if(walk){
  auto animation=[&](const char* id,std::size_t after=0){Uuid expected(id);wait([&](const auto& list){for(auto i=after;i<list.size();++i){const auto& p=list[i];if(p.spec->name=="AvatarAnimation"&&std::get<Uuid>(p.block("Sender").at("ID"))==agent)for(const auto& entry:p.blocks.at("AnimationList"))if(std::get<Uuid>(entry.at("AnimID"))==expected&&std::get<std::int64_t>(entry.at("AnimSequenceID"))>0)return true;}return false;});};
  animation("2408fe9e-df1d-1d7d-f4ff-1384fa7b350f");
  struct Motion {float x=0,y=0,z=0,vx=0,vy=0,vz=0;};
  auto motion=[&](const auto& p)->std::optional<Motion>{if(p.spec->name!="ObjectUpdate")return {};for(auto& b:p.blocks.at("ObjectData"))if(std::get<Uuid>(b.at("FullID"))==agent&&std::get<std::uint64_t>(b.at("PCode"))==47){auto& bytes=std::get<std::vector<std::uint8_t>>(b.at("ObjectData"));return Motion{real(bytes,16),real(bytes,20),real(bytes,24),real(bytes,28),real(bytes,32),real(bytes,36)};}return {};};
  auto mark=[&]{std::lock_guard lock(mutex);return packets.size();};
  auto latest=[&](){std::lock_guard lock(mutex);for(auto p=packets.rbegin();p!=packets.rend();++p)if(auto m=motion(*p))return *m;throw std::runtime_error("Missing physics avatar");};
  auto control=[&](std::uint32_t flags,bool turned=false){auto p=send("AgentUpdate");auto& b=p.block("AgentData");b["BodyRotation"]=turned?std::vector<double>{0,0,.70710677,.70710677}:std::vector<double>{0,0,0,1};b["HeadRotation"]=b["BodyRotation"];b["CameraAtAxis"]=std::vector<double>{1,0,0};b["ControlFlags"]=std::uint64_t(flags);transmit(p);};
  auto observe=[&](std::size_t after,auto predicate){wait([&](const auto& list){for(auto i=after;i<list.size();++i)if(auto m=motion(list[i]);m&&predicate(*m))return true;return false;});};
  auto after=mark();control(0);observe(after,[](const Motion& m){return m.z>21&&m.z<23&&std::abs(m.vz)<.1f;});auto ground=latest();
  after=mark();control(1u|0x400u);observe(after,[&](const Motion& m){return m.x>ground.x+1.2f&&std::abs(m.y-ground.y)<.15f&&m.z>21;});animation("6ed24bd8-91aa-4b12-ccc7-c97c857ab4e0",after);auto forward=latest();
  after=mark();control(1u|0x400u,true);observe(after,[&](const Motion& m){return m.y>forward.y+1.0f;});
  auto run=send("SetAlwaysRun");run.block("AgentData")["AlwaysRun"]=true;transmit(run);after=mark();control(1u|0x400u,true);observe(after,[](const Motion& m){return m.vy>4.f;});animation("05ddbff8-aaa9-92a1-2b74-8fe77a29b445",after);
  after=mark();control(0,true);observe(after,[](const Motion& m){return std::abs(m.vx)<.1f&&std::abs(m.vy)<.1f&&std::abs(m.vz)<.1f;});ground=latest();
  after=mark();control(0x10u|0x8000u,true);observe(after,[&](const Motion& m){return m.z>ground.z+1.f;});animation("2305bd75-1ca9-b03b-1faa-b176b8a8c49e",after);control(0,true);
  after=mark();observe(after,[&](const Motion& m){return m.z<ground.z+.3f&&std::abs(m.vz)<.2f;});ground=latest();
  after=mark();control(0x2000u|0x10u|0x1000u,true);observe(after,[&](const Motion& m){return m.z>ground.z+2.f;});control(0x2000u,true);
  after=mark();observe(after,[&](const Motion& m){return m.z>ground.z+1.f&&std::abs(m.vz)<.15f;});animation("4ae8016b-31b9-03bb-c401-b1ea941db41d",after);
  std::cout<<"Verified actual native walking, yaw turn, running, stopping, jump, flight, hover and actual animation UUIDs\n";
 }
 auto logout=send("LogoutRequest");transmit(logout);wait([&](const auto& p){return has(p,"LogoutReply");});socket.stop();std::cout<<"PASS: connected region handshake, root transition, avatar, "<<expected.size()<<" terrain packets, primitive creation/edit, native LSL execution and logout\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
