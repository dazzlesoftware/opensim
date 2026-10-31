#include "opensim/scene/object_update.hpp"
#include <iostream>
#include <iomanip>
#include <stdexcept>
using namespace opensim;using namespace scene;
static SceneObject object(int seed,int mode){auto o=SceneObject::primitive(framework::Uuid("11223344-5566-7788-99aa-bbccddeeff00"),{seed*.25f,-seed*.125f,23});o.id=framework::Uuid("00112233-4455-6677-8899-aabbccddeeff");o.local_id=720001+seed;o.velocity={seed*.03125f,0,1};o.acceleration={1,-2,0};o.angular_velocity={.5f,0,-.25f};o.rotation={seed*.01f,-.2f,.3f,(mode&4)?-.4f:.4f};if(mode&8)o.rotation={0,0,0,0};if(mode&16)o.rotation={.01f,.02f,.03f,.99995f};o.flags=0x12347;
 if(mode&32){auto& s=o.shape;s.state=seed&255;s.path_curve=32;s.profile_curve=5;s.path_begin=123;s.path_end=456;s.path_scale_x=115;s.path_scale_y=133;s.path_shear_x=17;s.path_shear_y=255;s.path_twist=-123;s.path_twist_begin=127;s.path_radius_offset=-17;s.path_taper_x=-31;s.path_taper_y=29;s.path_revolutions=33;s.path_skew=-44;s.profile_begin=1234;s.profile_end=2345;s.profile_hollow=3333;}
 if(mode&64)o.texture_entry={1,0,2,255,17};if(mode&128)o.texture_entry.clear();if(mode&256){o.scale={-0.f,0,-0.f};o.position={-0.f,0,0};o.velocity={0,-0.f,0};o.acceleration={0,0,-0.f};o.angular_velocity={0,-0.f,0};}return o;}
static ObjectUpdateOptions options(const SceneObject& o,int seed,int mode){ObjectUpdateOptions x;x.pseudo_crc=0xaabb0000u+seed;x.material=mode&32?7:3;x.click_action=mode&32?2:0;if(mode&2)x.generated_client_flags=0xfff0fff3;x.recipient=mode&1?o.owner:framework::Uuid{};x.create_selected=mode&4;return x;}
int main(int argc,char**){try{if(argc>1){int seed,mode;while(std::cin>>seed>>mode){auto o=object(seed,mode);auto p=object_update(o,0x123456789abcdef0ull,seed*.03125f,options(o,seed,mode));for(auto b:network::lludp::encode_datagram(p))std::cout<<std::hex<<std::setfill('0')<<std::setw(2)<<unsigned(b);std::cout<<"\n";}return 0;}
 auto o=object(3,0);auto p=object_update(o,123,.5f);auto& b=p.block("ObjectData");if(std::get<std::vector<std::uint8_t>>(b.at("ObjectData")).size()!=60||std::get<std::uint64_t>(b.at("Material"))!=3||std::get<std::uint64_t>(b.at("PathCurve"))!=16||!std::get<std::vector<std::uint8_t>>(b.at("NameValue")).empty())throw std::runtime_error("prim fields");
 auto wire=network::lludp::encode_datagram(p);auto back=network::lludp::decode_datagram(wire);if(std::get<framework::Uuid>(back.block("ObjectData").at("FullID"))!=o.id)throw std::runtime_error("wire identity");
 for(int mode=0;mode<3;mode++){o=object(1,0);if(mode==0)o.is_attachment=true;if(mode==1)o.shape.pcode=255;if(mode==2)o.parent_id=7;bool threw=false;try{object_update(o,0,1);}catch(const std::invalid_argument&){threw=true;}if(!threw)throw std::runtime_error("unsupported object profile");}
 std::cout<<"PASS: prim fields, zero-wire round trip and unsupported-profile rejection\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
