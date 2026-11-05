// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/sdk_quaternion.hpp"
#include "opensim/scene/avatar_update.hpp"
#include <bit>
#include <cmath>
namespace opensim::scene {
namespace {
void real(std::vector<std::uint8_t>& bytes, float f) {
    auto u = std::bit_cast<std::uint32_t>(f);
    for (int i = 0; i < 4; i++)
        bytes.push_back(static_cast<std::uint8_t>(u >> (8 * i)));
}
void vector(std::vector<std::uint8_t>& bytes, framework::Vector3 v) {
    if (v == framework::Vector3{})
        bytes.insert(bytes.end(), 12, 0);
    else {
        real(bytes, v.x);
        real(bytes, v.y);
        real(bytes, v.z);
    }
}
}
network::lludp::Packet avatar_object_update(const AvatarUpdate& a, std::uint64_t region, float dilation) {
    using namespace network;
    auto p = lludp::make_packet("ObjectUpdate");
    auto& r = p.block("RegionData");
    r["RegionHandle"] = region;
    r["TimeDilation"] = std::uint64_t(dilation <= 0 || std::isnan(dilation) ? 0
                                      : dilation >= 1 ? 65535
                                                      : static_cast<std::uint16_t>(dilation * 65535.f));
    auto b = lludp::make_block(p.spec->blocks.at(1));
    b["ID"] = std::uint64_t(a.local_id);
    b["FullID"] = a.id;
    b["PCode"] = std::uint64_t(47);
    b["Material"] = std::uint64_t(4);
    b["Scale"] = a.size == framework::Vector3{} ? std::vector<double>{0, 0, 0}
                                                : std::vector<double>{a.size.x, a.size.y, a.size.z};
    b["ParentID"] = std::uint64_t(a.parent_id);
    std::vector<std::uint8_t> data;
    data.reserve(76);
    if (a.collision_plane == std::array<float, 4>{})
        data.insert(data.end(), 16, 0);
    else
        for (auto f : a.collision_plane)
            real(data, f);
    vector(data, a.position);
    vector(data, a.velocity);
    data.insert(data.end(), 12, 0);
    auto q = a.rotation;
    if (!a.flying && !a.sitting) {
        q.x = 0;
        q.y = 0;
    }
    // Preserve the SDK CPU-dependent dot-product versus scalar sum.
    float norm = framework::sdk_quaternion_length_squared(q);
    if (norm > 1.e-6f) {
        float multiplier = (q.w < 0 ? -1.f : 1.f) / std::sqrt(norm);
        real(data, multiplier * q.x);
        real(data, multiplier * q.y);
        real(data, multiplier * q.z);
    } else
        data.insert(data.end(), 12, 0);
    data.insert(data.end(), 12, 0);
    b["ObjectData"] = std::move(data);
    auto name = "FirstName STRING RW SV " + a.first_name + "\nLastName STRING RW SV " + a.last_name +
                "\nTitle STRING RW SV " + (a.hide_title ? std::string{} : a.group_title);
    if (!name.empty() && name.back() == 0)
        name.pop_back(); // SDK retains one trailing terminator.
    std::vector<std::uint8_t> names(name.begin(), name.end());
    names.push_back(0);
    b["NameValue"] = std::move(names);
    p.blocks["ObjectData"].push_back(std::move(b));
    return p;
}
}
