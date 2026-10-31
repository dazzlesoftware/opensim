// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/terrain.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <mutex>
#include <stdexcept>
namespace opensim::scene {
namespace {
int source_int(float value){
 if(!std::isfinite(value)||value>=2147483648.f||value< -2147483648.f)return std::numeric_limits<std::int32_t>::min();
 return static_cast<int>(value);
}
}
Terrain::Terrain(std::uint32_t width,std::uint32_t height):width_(width),height_(height){
 auto points=static_cast<std::uint64_t>(width)*height;if(points>heights_.max_size())throw std::length_error("Terrain dimensions overflow");heights_.resize(static_cast<std::size_t>(points),0.f);
 auto bits=static_cast<std::uint64_t>(patches_x())*patches_y();if(bits>std::numeric_limits<std::int32_t>::max())throw std::length_error("Terrain taint dimensions overflow");taints_.resize(static_cast<std::size_t>((bits+31)/32),0);
}
Terrain::Terrain(const Terrain& other):width_(other.width_),height_(other.height_){std::shared_lock lock(other.mutex_);heights_=other.heights_;taints_=other.taints_;tainted_count_=other.tainted_count_;}
std::size_t Terrain::index(int x,int y)const{if(x<0||y<0||static_cast<std::uint32_t>(x)>=width_||static_cast<std::uint32_t>(y)>=height_)throw std::out_of_range("Terrain coordinate");return static_cast<std::size_t>(x)*height_+static_cast<std::size_t>(y);}
std::size_t Terrain::patch_index(std::uint32_t px,std::uint32_t py)const{if(px>=patches_x()||py>=patches_y())throw std::out_of_range("Terrain patch");return static_cast<std::size_t>(px)*patch_size*height_+py*patch_size;}
float Terrain::get(int x,int y)const{std::shared_lock lock(mutex_);return heights_[index(x,y)];}
void Terrain::set(int x,int y,float value){std::unique_lock lock(mutex_);auto i=index(x,y);if(heights_[i]!=value){heights_[i]=value;taint_set_unlocked(x/16+(y/16)*static_cast<int>(patches_x()),true);}}
void Terrain::clear_land(float value){std::unique_lock lock(mutex_);std::fill(heights_.begin(),heights_.end(),value);}
float Terrain::height_at(float x,float y)const{
 std::shared_lock lock(mutex_);auto read=[&](int xx,int yy){return heights_[index(xx,yy)];};int ix,iy;int mix=static_cast<int>(width_)-1,miy=static_cast<int>(height_)-1;float dx,dy;
 if(x<=0){if(y<=0)return read(0,0);iy=source_int(y);if(iy>=miy)return read(0,miy);dy=y-iy;float h=read(0,iy);++iy;return h+(read(0,iy)-h)*dy;}
 ix=source_int(x);if(ix>=mix){if(y<=0)return read(mix,0);iy=source_int(y);if(y>=miy)return read(mix,miy);dy=y-iy;float h=read(mix,iy);++iy;return h+(read(mix,iy)-h)*dy;}
 dx=x-ix;if(y<=0){float h=read(ix,0);++ix;return h+(read(ix,0)-h)*dx;}
 iy=source_int(y);if(iy>=miy){float h=read(ix,miy);++ix;return h+(read(ix,miy)-h)*dx;}
 dy=y-iy;float h0=read(ix,iy),h1,h2;if(dy>dx){++iy;h2=read(ix,iy);h1=(h2-h0)*dy;++ix;h2=(read(ix,iy)-h2)*dx;}else{++ix;h2=read(ix,iy);h1=(h2-h0)*dx;++iy;h2=(read(ix,iy)-h2)*dy;}return h0+h1+h2;
}
std::array<float,256> Terrain::patch(std::uint32_t px,std::uint32_t py,float sub,float premult)const{
 std::shared_lock lock(mutex_);auto start=patch_index(px,py);std::array<float,256> result{};std::size_t k=0;for(std::size_t y=0;y<16;++y)for(std::size_t x=0;x<16;++x)result[k++]=(heights_[start+x*height_+y]-sub)*premult;return result;
}
std::pair<float,float> Terrain::patch_min_max(std::uint32_t px,std::uint32_t py)const{
 std::shared_lock lock(mutex_);auto start=patch_index(px,py);float min=heights_[start],max=min;
 // Preserve TerrainData.GetPatchMinMax's first-column count (15): its final
 // point is omitted, while the remaining 15 columns each scan all16 points.
 for(std::size_t x=0;x<16;++x)for(std::size_t y=0;y<(x==0?15u:16u);++y){float v=heights_[start+x*height_+y];if(v>max)max=v;else if(v<min)min=v;}return {min,max};
}
std::vector<float> Terrain::serialized_floats()const{std::shared_lock lock(mutex_);std::vector<float> out;out.reserve(heights_.size());for(std::uint32_t y=0;y<height_;++y)for(std::uint32_t x=0;x<width_;++x)out.push_back(heights_[static_cast<std::size_t>(x)*height_+y]);return out;}
std::vector<float> Terrain::compressed_map()const{std::shared_lock lock(mutex_);return heights_;}
void Terrain::taint_set_unlocked(int bit,bool value){if(bit<0||static_cast<std::size_t>(bit/32)>=taints_.size())throw std::out_of_range("Terrain taint");auto& word=taints_[static_cast<std::size_t>(bit/32)];auto mask=std::uint32_t(1)<<(bit&31);bool old=(word&mask)!=0;if(old==value)return;if(value){word|=mask;++tainted_count_;}else{word&=~mask;--tainted_count_;}}
bool Terrain::taint_get_unlocked(int bit,bool clear){if(bit<0||static_cast<std::size_t>(bit/32)>=taints_.size())throw std::out_of_range("Terrain taint");auto& word=taints_[static_cast<std::size_t>(bit/32)];auto mask=std::uint32_t(1)<<(bit&31);if(!(word&mask))return false;if(clear){word^=mask;--tainted_count_;}return true;}
void Terrain::clear_taint(){std::unique_lock lock(mutex_);std::fill(taints_.begin(),taints_.end(),0);tainted_count_=0;}
void Terrain::taint_all(){std::unique_lock lock(mutex_);std::fill(taints_.begin(),taints_.end(),~std::uint32_t(0));tainted_count_=static_cast<std::int32_t>(patches_x()*patches_y());}
bool Terrain::tainted()const{std::shared_lock lock(mutex_);return tainted_count_>0;}
std::int32_t Terrain::tainted_count()const{std::shared_lock lock(mutex_);return tainted_count_;}
bool Terrain::tainted_at(int x,int y,bool clear){std::unique_lock lock(mutex_);return taint_get_unlocked(x/16+(y/16)*static_cast<int>(patches_x()),clear);}
bool Terrain::tainted_patch(int px,int py,bool clear){std::unique_lock lock(mutex_);return taint_get_unlocked(px+py*static_cast<int>(patches_x()),clear);}
int Terrain::next_taint(int start){std::unique_lock lock(mutex_);auto logical=patches_x()*patches_y();if(tainted_count_<=0||start<0||static_cast<std::uint32_t>(start)>=logical)return -1;for(std::size_t bit=static_cast<std::size_t>(start);bit<taints_.size()*32;++bit)if(taint_get_unlocked(static_cast<int>(bit),true))return static_cast<int>(bit);return -1;}
Terrain Terrain::clone()const{Terrain copy(width_,height_);std::shared_lock lock(mutex_);copy.heights_=heights_;return copy;}
}
