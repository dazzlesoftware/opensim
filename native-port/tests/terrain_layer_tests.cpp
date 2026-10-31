#include "opensim/scene/terrain_layer.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
using namespace opensim::scene;
static void fill(Terrain& t,int mode,int seed){for(unsigned y=0;y<t.height();y++)for(unsigned x=0;x<t.width();x++){
 float f=21.f;if(mode==1)f=float(int((x*17+y*31+seed*13)%397)-198)*.25f;
 if(mode==2)f=21.f+float((x+y+seed)%3)*.001f;
 if(mode==3)f=float(x)*.125f-float(y)*.0625f+seed;
 t.set(x,y,f);}}
int main(int argc,char**){try{if(argc>1){unsigned w,h,n;int mode,seed;while(std::cin>>w>>h>>mode>>seed>>n){Terrain t(w,h);fill(t,mode,seed);std::vector<TerrainPatchCoordinate> p(n);for(auto& v:p)std::cin>>v.x>>v.y;auto result=terrain_layer_packets(t,p);for(const auto& packet:result){std::cout<<unsigned(packet.type)<<":";for(auto b:packet.data)std::cout<<std::hex<<std::setfill('0')<<std::setw(2)<<unsigned(b);std::cout<<std::dec<<";";}std::cout<<"\n";}return 0;}
 Terrain t(256,256);t.clear_land();std::vector<TerrainPatchCoordinate> p{{0,0}};auto a=terrain_layer_packets(t,p);
 if(a.size()!=1||a[0].type!=76||a[0].data.size()!=14||a[0].data[0]!=8||a[0].data[1]!=1||a[0].data[2]!=16||a[0].data[3]!=76)throw std::runtime_error("flat patch framing");
 auto empty=terrain_layer_packets(t,{});if(empty[0].data.size()!=5||empty[0].data.back()!=97)throw std::runtime_error("empty stream");
 p={{16,0}};bool threw=false;try{terrain_layer_packets(t,p);}catch(const std::out_of_range&){threw=true;}if(!threw)throw std::runtime_error("patch bounds");
 std::cout<<"PASS: terrain layer framing, empty stream, bounds\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
