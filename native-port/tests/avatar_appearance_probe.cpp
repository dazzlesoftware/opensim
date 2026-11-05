#include "opensim/framework/avatar_appearance.hpp"
#include "opensim/network/osd_value.hpp"
#include <bit>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace opensim;
using network::Value;
using framework::AvatarAppearance;
using framework::Uuid;
Uuid id(unsigned n) {
    std::ostringstream s;
    s << "00000000-0000-0000-0000-" << std::hex << std::setw(12) << std::setfill('0') << n;
    return Uuid(s.str());
}
template <class T> std::string hex(const T& bytes) {
    std::ostringstream s;
    for (auto b : bytes)
        s << std::hex << std::setw(2) << std::setfill('0') << unsigned(b);
    return s.str();
}
std::string number(float n) {
    auto bits = std::bit_cast<std::uint32_t>(n);
    if (std::isnan(n))
        bits = 0x7fc00000;
    std::ostringstream s;
    s << std::hex << bits;
    return s.str();
}
std::string canonical(const Value& v) {
    switch (v.kind) {
    case Value::Kind::Integer:
        return "i" + std::to_string(v.integer);
    case Value::Kind::Real: {
        auto bits = std::bit_cast<std::uint64_t>(v.real);
        if (std::isnan(v.real))
            bits = 0x7ff8000000000000ull;
        std::ostringstream s;
        s << 'r' << std::hex << bits;
        return s.str();
    }
    case Value::Kind::Uuid:
        return "q" + v.text;
    case Value::Kind::Binary:
        return "x" + hex(*network::osd_binary(v));
    case Value::Kind::Array: {
        std::string s = "[";
        for (auto& n : v.array)
            s += canonical(n) + ",";
        return s + "]";
    }
    case Value::Kind::Map: {
        std::string s = "{";
        for (auto& [k, n] : v.map)
            s += k + "=" + canonical(n) + ",";
        return s + "}";
    }
    default:
        return "u";
    }
}
std::string snapshot(AvatarAppearance& a) {
    std::string s = std::to_string(a.serial) + ":" + number(a.avatar_height) + ":" + number(a.hover_z);
    for (auto v : {a.avatar_size(), a.avatar_box_size()})
        s += ":" + number(v.x) + "," + number(v.y) + "," + number(v.z);
    s +=
        ":" + number(a.avatar_feet_offset()) + ":" + (a.visual_params ? hex(*a.visual_params) : "null") + ":";
    if (!a.wearables)
        s += "null";
    else
        for (auto& w : *a.wearables) {
            if (!w) {
                s += "null;";
                continue;
            }
            s += '[';
            for (std::size_t i = 0; i < w->count(); ++i) {
                auto x = w->at(static_cast<int>(i));
                s += x.item.str() + "," + x.asset.str() + ";";
            }
            s += ']';
        }
    s += ':';
    s += a.texture ? hex(a.texture->bytes()) : "null";
    s += ':';
    for (auto& attachment : a.attachments())
        s += std::to_string(attachment.point) + "," + attachment.item.str() + "," + attachment.asset.str() +
             ";";
    s += ':';
    if (!a.cache_items)
        s += "null";
    else
        for (auto& c : *a.cache_items)
            s += c ? std::to_string(c->texture_index) + "," + c->cache_id.str() + "," + c->texture_id.str() +
                         ";"
                   : "null;";
    return s;
}
int main() {
    auto a = std::make_shared<AvatarAppearance>();
    std::shared_ptr<AvatarAppearance> saved;
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string op;
        in >> op;
        std::string status = "OK", output = "-";
        try {
            if (op == "reset")
                a = std::make_shared<AvatarAppearance>();
            else if (op == "description") {
                auto text = a->description();
                output = hex(std::vector<std::uint8_t>(text.begin(), text.end()));
            } else if (op == "initialized") {
                int mode;
                in >> mode;
                if (mode == 0)
                    a = std::make_shared<AvatarAppearance>(nullptr, nullptr, nullptr);
                else {
                    saved = a;
                    a = std::make_shared<AvatarAppearance>(a->wearables, a->texture, a->visual_params);
                }
            } else if (op == "saved-alias") {
                output = saved ? std::to_string(saved->wearables == a->wearables) +
                                     std::to_string(saved->texture == a->texture) +
                                     std::to_string(saved->visual_params == a->visual_params) +
                                     std::to_string(saved->cache_items == a->cache_items)
                               : "-";
            } else if (op == "copy") {
                int w, b;
                in >> w >> b;
                saved = a;
                a = std::make_shared<AvatarAppearance>(a.get(), w != 0, b != 0);
            } else if (op == "copy-null")
                a = std::make_shared<AvatarAppearance>(static_cast<AvatarAppearance*>(nullptr));
            else if (op == "map-null")
                a = std::make_shared<AvatarAppearance>(static_cast<Value*>(nullptr));
            else if (op == "size") {
                std::uint32_t x, y, z;
                in >> std::hex >> x >> y >> z;
                a->set_size({std::bit_cast<float>(x), std::bit_cast<float>(y), std::bit_cast<float>(z)});
            } else if (op == "scalar") {
                int serial;
                std::uint32_t h, z;
                in >> serial >> std::hex >> h >> z;
                a->serial = serial;
                a->avatar_height = std::bit_cast<float>(h);
                a->hover_z = std::bit_cast<float>(z);
            } else if (op == "clear-wearables")
                a->clear_wearables();
            else if (op == "wear") {
                int index, mode;
                unsigned item, asset;
                in >> index >> mode >> item >> asset;
                std::shared_ptr<framework::AvatarWearable> w;
                if (mode == 1)
                    w = a->wearables->at(index);
                else if (mode != 2)
                    w = std::make_shared<framework::AvatarWearable>(id(item), id(asset));
                a->set_wearable(index, w);
            } else if (op == "add-wear") {
                int index;
                unsigned item, asset;
                in >> index >> item >> asset;
                a->wearables->at(index)->add(id(item), id(asset));
            } else if (op == "visual") {
                int mode, count, n;
                in >> mode >> count >> n;
                AvatarAppearance::Bytes bytes;
                if (mode != 2)
                    bytes = std::make_shared<std::vector<std::uint8_t>>(count, static_cast<std::uint8_t>(n));
                if (mode == 1)
                    a->visual_params = bytes;
                else
                    output = a->set_visual_params(bytes) ? "1" : "0";
            } else if (op == "visual-null")
                a->visual_params.reset();
            else if (op == "textures") {
                int mode, index;
                unsigned n;
                in >> mode >> index >> n;
                if (mode == 2)
                    a->texture.reset();
                else {
                    auto entry =
                        mode == 1
                            ? a->texture
                            : std::make_shared<framework::TextureEntry>(AvatarAppearance::default_texture_id);
                    if (index >= 0)
                        entry->get_face(index)->texture_id(id(n));
                    output = a->set_texture_entries(entry) ? "1" : "0";
                }
            } else if (op == "reset-appearance")
                a->reset_appearance();
            else if (op == "reset-bakes")
                a->reset_baked_textures();
            else if (op == "attach") {
                int point;
                unsigned item, asset;
                in >> point >> item >> asset;
                output = a->set_attachment(point, id(item), id(asset)) ? "1" : "0";
            } else if (op == "append" || op == "replace") {
                int point;
                unsigned item, asset;
                in >> point >> item >> asset;
                auto t = std::make_shared<framework::AvatarAttachment>(point, id(item), id(asset));
                if (op == "append")
                    a->append_attachment(t);
                else
                    a->replace_attachment(t);
            } else if (op == "detach") {
                unsigned n;
                in >> n;
                output = a->detach_attachment(id(n)) ? "1" : "0";
            } else if (op == "remove") {
                int point;
                unsigned n;
                in >> point >> n;
                output = a->remove_attachment(point, id(n)) ? "1" : "0";
            } else if (op == "lookup") {
                unsigned n;
                in >> n;
                auto t = a->attachment_for_item(id(n));
                output = std::to_string(a->attachpoint(id(n))) + ":" +
                         (t ? std::to_string(t->point) + "," + t->asset.str() : "null");
            } else if (op == "mutate-attachment") {
                unsigned n;
                int point;
                in >> n >> point;
                auto t = a->attachment_for_item(id(n));
                if (t)
                    t->point = point;
            } else if (op == "clear-attachments")
                a->clear_attachments();
            else if (op == "can") {
                std::uint32_t bits;
                in >> std::hex >> bits;
                output = a->can_teleport(std::bit_cast<float>(bits)) ? "1" : "0";
            } else if (op == "cache") {
                a->cache_items =
                    std::make_shared<framework::WearableCacheItems>(framework::WearableCacheItem::defaults());
                for (unsigned i : {8u, 20u, 40u, 44u}) {
                    (*a->cache_items)[i]->cache_id = id(i);
                    (*a->cache_items)[i]->texture_id = id(i + 1);
                }
            } else if (op == "pack") {
                std::string bits;
                in >> bits;
                framework::EntityTransferContext ctx;
                if (bits == "null")
                    output = canonical(a->pack());
                else {
                    ctx.outbound_version =
                        std::bit_cast<float>(static_cast<std::uint32_t>(std::stoul(bits, nullptr, 16)));
                    output = canonical(a->pack(&ctx));
                }
            } else if (op == "notecard") {
                int flag;
                in >> flag;
                output = canonical(a->pack_for_notecard(flag != 0));
            } else if (op == "unpack") {
                std::string kind;
                in >> kind;
                if (kind == "null")
                    a->unpack(nullptr);
                else {
                    auto map = Value::object({});
                    if (kind == "packed")
                        map = a->pack();
                    else if (kind == "bad-wear")
                        map.map["wearables"].kind = Value::Kind::Array,
                        map.map["wearables"].array.push_back(Value(3));
                    else if (kind == "extra-only")
                        map.map["wrbls8"].kind = Value::Kind::Array,
                        map.map["wrbls8"].array.push_back(Value(3));
                    else if (kind == "height") {
                        Value v;
                        v.kind = Value::Kind::Real;
                        v.real = .25;
                        map.map["height"] = v;
                    } else if (kind == "cache-extra") {
                        auto row = [](int index, unsigned id_value) {
                            Value cache(id(id_value).str());
                            cache.kind = Value::Kind::Uuid;
                            return Value::object(
                                {{"textureindex", Value(index)}, {"cacheid", cache}, {"textureid", cache}});
                        };
                        Value baked, extra;
                        baked.kind = extra.kind = Value::Kind::Array;
                        baked.array.push_back(row(8, 888));
                        extra.array.push_back(row(40, 999));
                        map.map["bakedcache"] = baked;
                        map.map["bc8"] = extra;
                    }
                    a->unpack(&map);
                }
            } else if (op == "saved-mutate" && saved) {
                saved->visual_params->at(0) ^= 255;
                saved->wearables->at(0)->clear();
                if (saved->cache_items)
                    (*saved->cache_items)[8]->texture_id = id(999);
            } else if (op == "assets" && saved)
                a->get_assets_from(*saved);
        } catch (const std::exception&) {
            status = "EX";
        }
        std::string state;
        try {
            state = snapshot(*a);
        } catch (const std::exception&) {
            state = "STATEEX";
        }
        std::cout << status << '|' << output << '|' << state << '\n';
    }
}
