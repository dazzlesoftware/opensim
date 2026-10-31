#include "opensim/scene/avatar_update.hpp"
#include <iostream>
#include <iomanip>
#include <stdexcept>
using namespace opensim;using namespace scene;
static AvatarUpdate avatar(int seed,int mode){AvatarUpdate a;a.id=framework::Uuid("00112233-4455-6677-8899-aabbccddeeff");a.local_id=720001+seed;a.parent_id=mode&2?900001:0;a.position={seed*.25f,-seed*.125f,23.f};a.velocity={seed*.03125f,0,1};a.size={.45f,.6f,1.9f+seed*.001f};a.rotation={seed*.01f,-.2f,.3f,(mode&4)?-.4f:.4f};if(mode&8)a.rotation={0,0,0,0};if(mode&16)a.rotation={.01f,.02f,.03f,.99995f};a.flying=mode&1;a.sitting=mode&2;a.hide_title=mode&32;a.collision_plane={0,0,1,-23};a.first_name="First"+std::to_string(seed);a.last_name="Last";a.group_title="Title";if(mode&64){a.first_name+="é";a.last_name="雪";a.group_title="世界";}if(mode&128)a.group_title.push_back(0);if(mode&256){a.size={-0.f,0,-0.f};a.position={-0.f,0,0};a.velocity={0,-0.f,0};a.collision_plane={-0.f,0,0,0};}return a;}
int main(int argc,char**){try{if(argc>1){int seed,mode;while(std::cin>>seed>>mode){auto p=avatar_object_update(avatar(seed,mode),0x123456789abcdef0ull,seed*.03125f);for(auto b:network::lludp::encode_datagram(p))std::cout<<std::hex<<std::setfill('0')<<std::setw(2)<<unsigned(b);std::cout<<"\n";}return 0;}
 auto a=avatar(3,0);auto p=avatar_object_update(a,123,.5f);auto& b=p.block("ObjectData");auto bytes=std::get<std::vector<std::uint8_t>>(b.at("ObjectData"));if(bytes.size()!=76||std::get<std::uint64_t>(b.at("PCode"))!=47||std::get<std::uint64_t>(b.at("PathCurve"))!=0)throw std::runtime_error("avatar fields");
 auto wire=network::lludp::encode_datagram(p);auto back=network::lludp::decode_datagram(wire);if(std::get<framework::Uuid>(back.block("ObjectData").at("FullID"))!=a.id)throw std::runtime_error("wire identity");
 std::cout<<"PASS: avatar object fields and zero-coded wire round trip\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
