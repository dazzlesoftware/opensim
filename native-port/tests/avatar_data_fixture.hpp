// Shared avatar-state formatting for differential probes.
#pragma once
#include "opensim/services/avatar_data.hpp"
#include "osd_fixture_value.hpp"
#include <iostream>
#include <bit>
#include <cmath>
#include <iomanip>
using namespace opensim;
std::string hex(std::string_view text) {
    constexpr char d[] = "0123456789abcdef";
    std::string s;
    for (unsigned char c : text) {
        s += d[c >> 4];
        s += d[c & 15];
    }
    return s;
}
template <class T> std::string bytes(const T& b) {
    return hex({reinterpret_cast<const char*>(b.data()), b.size()});
}
std::string number(float f) {
    std::ostringstream s;
    s << std::hex << (std::isnan(f) ? 0x7fc00000u : std::bit_cast<std::uint32_t>(f));
    return s.str();
}
framework::Uuid id(int n) {
    std::ostringstream s;
    s << "00000000-0000-0000-0000-" << std::hex << std::setw(12) << std::setfill('0') << n;
    return framework::Uuid(s.str());
}
std::string appearance(const framework::AvatarAppearance& a) {
    std::string s = std::to_string(a.serial) + ":" + number(a.avatar_height) + ":" + number(a.hover_z);
    for (auto v : {a.avatar_size(), a.avatar_box_size()})
        s += ":" + number(v.x) + "," + number(v.y) + "," + number(v.z);
    s += ":" + number(a.avatar_feet_offset()) + ":" + (a.visual_params ? bytes(*a.visual_params) : "null") +
         ":";
    if (!a.wearables)
        s += "null";
    else
        for (auto w : *a.wearables) {
            if (!w) {
                s += "null;";
                continue;
            }
            s += '[';
            for (std::size_t i = 0; i < w->count(); ++i) {
                auto n = w->at(static_cast<int>(i));
                s += n.item.str() + "," + n.asset.str() + ";";
            }
            s += ']';
        }
    s += ':';
    s += a.texture ? bytes(a.texture->bytes()) : "null";
    s += ':';
    for (auto t : a.attachments())
        s += std::to_string(t.point) + "," + t.item.str() + "," + t.asset.str() + ";";
    s += ':';
    if (!a.cache_items)
        s += "null";
    else
        for (auto c : *a.cache_items)
            s += c ? std::to_string(c->texture_index) + "," + c->cache_id.str() + "," + c->texture_id.str() +
                         ";"
                   : "null;";
    return s;
}
