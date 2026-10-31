// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/object_update.hpp"
#include <bit>
#include <cmath>
#include <stdexcept>
namespace opensim::scene {
namespace {
void real(std::vector<std::uint8_t>& b,float f){auto u=std::bit_cast<std::uint32_t>(f);for(int i=0;i<4;i++)b.push_back(static_cast<std::uint8_t>(u>>(8*i)));}
void vector(std::vector<std::uint8_t>& b,framework::Vector3 v){if(v==framework::Vector3{})b.insert(b.end(),12,0);else{real(b,v.x);real(b,v.y);real(b,v.z);}}
void quaternion(std::vector<std::uint8_t>& b,framework::Quaternion q){float norm=(q.x*q.x+q.y*q.y)+(q.z*q.z+q.w*q.w);if(norm>1.e-6f){float m=(q.w<0?-1.f:1.f)/std::sqrt(norm);real(b,m*q.x);real(b,m*q.y);real(b,m*q.z);}else b.insert(b.end(),12,0);}
}
network::lludp::Packet object_update(const SceneObject& o,std::uint64_t region,float dilation,const ObjectUpdateOptions& options){
 if(o.shape.pcode!=9||o.is_attachment||o.parent_id!=0)throw std::invalid_argument("object_update requires a plain nonattachment root prim");
 using namespace network;auto p=lludp::make_packet("ObjectUpdate");auto& r=p.block("RegionData");r["RegionHandle"]=region;
 r["TimeDilation"]=std::uint64_t(dilation<=0||std::isnan(dilation)?0:dilation>=1?65535:static_cast<std::uint16_t>(dilation*65535.f));
 auto b=lludp::make_block(p.spec->blocks.at(1));b["ID"]=std::uint64_t(o.local_id);b["State"]=std::uint64_t(o.shape.state);b["FullID"]=o.id;b["CRC"]=std::uint64_t(options.pseudo_crc);b["PCode"]=std::uint64_t(o.shape.pcode);b["Material"]=std::uint64_t(options.material);b["ClickAction"]=std::uint64_t(options.click_action);
 b["Scale"]=o.scale==framework::Vector3{}?std::vector<double>{0,0,0}:std::vector<double>{o.scale.x,o.scale.y,o.scale.z};b["ParentID"]=std::uint64_t(o.parent_id);
 std::uint32_t flags=options.generated_client_flags.value_or(o.flags)&~2u;if(options.recipient==o.owner&&options.create_selected)flags|=2u;b["UpdateFlags"]=std::uint64_t(flags);
 std::vector<std::uint8_t> data;data.reserve(60);vector(data,o.position);vector(data,o.velocity);vector(data,o.acceleration);quaternion(data,o.rotation);vector(data,o.angular_velocity);b["ObjectData"]=std::move(data);
 const auto& s=o.shape;
 b["PathCurve"]=std::uint64_t(s.path_curve);b["ProfileCurve"]=std::uint64_t(s.profile_curve);b["PathBegin"]=std::uint64_t(s.path_begin);b["PathEnd"]=std::uint64_t(s.path_end);b["PathScaleX"]=std::uint64_t(s.path_scale_x);b["PathScaleY"]=std::uint64_t(s.path_scale_y);b["PathShearX"]=std::uint64_t(s.path_shear_x);b["PathShearY"]=std::uint64_t(s.path_shear_y);
 b["PathTwist"]=std::int64_t(s.path_twist);b["PathTwistBegin"]=std::int64_t(s.path_twist_begin);b["PathRadiusOffset"]=std::int64_t(s.path_radius_offset);b["PathTaperX"]=std::int64_t(s.path_taper_x);b["PathTaperY"]=std::int64_t(s.path_taper_y);b["PathRevolutions"]=std::uint64_t(s.path_revolutions);b["PathSkew"]=std::int64_t(s.path_skew);b["ProfileBegin"]=std::uint64_t(s.profile_begin);b["ProfileEnd"]=std::uint64_t(s.profile_end);b["ProfileHollow"]=std::uint64_t(s.profile_hollow);b["TextureEntry"]=o.texture_entry;
 // Plain-root source NameValue/Data are empty; object name/description are
 // delivered separately through ObjectProperties, not injected into NameValue.
 p.blocks["ObjectData"].push_back(std::move(b));return p;
}
}
