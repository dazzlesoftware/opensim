// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/object_add.hpp"
namespace opensim::scene {
namespace {
framework::Vector3 vector(const network::lludp::FieldValue& v){auto& a=std::get<std::vector<double>>(v);return {static_cast<float>(a.at(0)),static_cast<float>(a.at(1)),static_cast<float>(a.at(2))};}
}
ObjectAddRequest object_add_request(const network::lludp::Packet& p){
 if(!p.spec||p.spec->name!="ObjectAdd")throw std::invalid_argument("ObjectAdd packet required");
 auto& a=p.block("AgentData");auto& d=p.block("ObjectData");ObjectAddRequest out;
 out.object=SceneObject::primitive(std::get<framework::Uuid>(a.at("AgentID")),vector(d.at("RayEnd")),vector(d.at("Scale")));out.object.group=std::get<framework::Uuid>(a.at("GroupID"));
 auto& q=std::get<std::vector<double>>(d.at("Rotation"));out.object.rotation={static_cast<float>(q.at(0)),static_cast<float>(q.at(1)),static_cast<float>(q.at(2)),static_cast<float>(q.at(3))};
 auto number=[&](const char* key){return std::get<std::uint64_t>(d.at(key));};auto signed_number=[&](const char* key){return std::get<std::int64_t>(d.at(key));};auto& s=out.object.shape;
 s.pcode=static_cast<std::uint8_t>(number("PCode"));s.state=static_cast<std::uint8_t>(number("State"));
 #define UNSIGNED_FIELD(member,field) s.member=static_cast<decltype(s.member)>(number(field))
 #define SIGNED_FIELD(member,field) s.member=static_cast<decltype(s.member)>(signed_number(field))
 UNSIGNED_FIELD(path_curve,"PathCurve");UNSIGNED_FIELD(profile_curve,"ProfileCurve");UNSIGNED_FIELD(path_begin,"PathBegin");UNSIGNED_FIELD(path_end,"PathEnd");UNSIGNED_FIELD(path_scale_x,"PathScaleX");UNSIGNED_FIELD(path_scale_y,"PathScaleY");UNSIGNED_FIELD(path_shear_x,"PathShearX");UNSIGNED_FIELD(path_shear_y,"PathShearY");UNSIGNED_FIELD(path_revolutions,"PathRevolutions");UNSIGNED_FIELD(profile_begin,"ProfileBegin");UNSIGNED_FIELD(profile_end,"ProfileEnd");UNSIGNED_FIELD(profile_hollow,"ProfileHollow");
 SIGNED_FIELD(path_twist,"PathTwist");SIGNED_FIELD(path_twist_begin,"PathTwistBegin");SIGNED_FIELD(path_radius_offset,"PathRadiusOffset");SIGNED_FIELD(path_taper_x,"PathTaperX");SIGNED_FIELD(path_taper_y,"PathTaperY");SIGNED_FIELD(path_skew,"PathSkew");
 #undef UNSIGNED_FIELD
 #undef SIGNED_FIELD
 out.ray_start=vector(d.at("RayStart"));out.ray_end=vector(d.at("RayEnd"));out.ray_target=std::get<framework::Uuid>(d.at("RayTargetID"));out.bypass_raycast=static_cast<std::uint8_t>(number("BypassRaycast"));out.ray_end_is_intersection=static_cast<std::uint8_t>(number("RayEndIsIntersection"));out.add_flags=static_cast<std::uint32_t>(number("AddFlags"));return out;
}
}
