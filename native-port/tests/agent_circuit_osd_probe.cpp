#include "opensim/framework/agent_circuit.hpp"
#include "opensim/framework/avatar_appearance.hpp"
#include "osd_fixture_value.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>
using namespace opensim;
using network::Value;
using framework::AgentCircuitData;
std::string hex(const std::string& text) {
    std::ostringstream s;
    for (unsigned char b : text)
        s << std::hex << std::setw(2) << std::setfill('0') << unsigned(b);
    return s.str();
}
std::string token(const std::string& text) {
    return text == "." ? "" : fixture::unhex(text);
}
std::string canonical(const Value& v) {
    switch (v.kind) {
    case Value::Kind::Integer:
        return "i" + std::to_string(v.integer);
    case Value::Kind::Boolean:
        return v.boolean ? "b1" : "b0";
    case Value::Kind::String:
        return "s" + hex(v.text);
    case Value::Kind::Uuid:
        return "q" + v.text;
    case Value::Kind::Binary: {
        auto b = network::osd_binary(v);
        return "x" + hex(std::string(b->begin(), b->end()));
    }
    case Value::Kind::Real: {
        std::ostringstream s;
        s << 'r' << std::hex
          << (std::isnan(v.real) ? 0x7ff8000000000000ull : std::bit_cast<std::uint64_t>(v.real));
        return s.str();
    }
    case Value::Kind::Array: {
        std::string s = "[";
        for (auto& item : v.array)
            s += canonical(item) + ",";
        return s + "]";
    }
    case Value::Kind::Map: {
        std::string s = "{";
        for (auto& [key, item] : v.map)
            s += hex(key) + "=" + canonical(item) + ",";
        return s + "}";
    }
    default:
        return "u";
    }
}
int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "ignorables") {
        int start = -1, last = -1;
        for (int n = 0; n <= 0x10ffff; ++n) {
            bool equal = false;
            if (n < 0xd800 || n > 0xdfff) {
                std::string s = "fa";
                if (n < 128)
                    s += static_cast<char>(n);
                else if (n < 0x800) {
                    s += static_cast<char>(0xc0 | (n >> 6));
                    s += static_cast<char>(0x80 | (n & 63));
                } else if (n < 0x10000) {
                    s += static_cast<char>(0xe0 | (n >> 12));
                    s += static_cast<char>(0x80 | ((n >> 6) & 63));
                    s += static_cast<char>(0x80 | (n & 63));
                } else {
                    s += static_cast<char>(0xf0 | (n >> 18));
                    s += static_cast<char>(0x80 | ((n >> 12) & 63));
                    s += static_cast<char>(0x80 | ((n >> 6) & 63));
                    s += static_cast<char>(0x80 | (n & 63));
                }
                s += "lse";
                equal = !network::osd_boolean(Value(s));
            }
            if (equal) {
                if (start < 0)
                    start = n;
                last = n;
            } else if (start >= 0) {
                std::cout << std::hex << start << ' ' << last << '\n';
                start = -1;
            }
        }
        if (start >= 0)
            std::cout << std::hex << start << ' ' << last << '\n';
        return 0;
    }
    AgentCircuitData c;
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string op;
        in >> op;
        std::string status = "OK", out = "-";
        try {
            if (op == "reset")
                c = AgentCircuitData{};
            else if (op == "null-strings") {
                c.caps_path.reset();
                c.service_session_id.reset();
                c.children_cap_seeds = std::make_shared<AgentCircuitData::ChildrenSeeds>();
                (*c.children_cap_seeds)[42] = std::nullopt;
            } else if (op == "empty")
                c.unpack(Value::object({}));
            else if (op == "precedence") {
                Value old;
                old.kind = Value::Kind::Array;
                old.array = {Value("old"), Value("obsolete")};
                c.unpack(Value::object({{"appearance_serial", Value(99)},
                                        {"packed_appearance", Value::object({{"serial", Value(17)}})},
                                        {"service_urls", old},
                                        {"serviceurls", Value::object({{"new", Value("current")}})}}));
            } else if (op == "set") {
                std::string key, text;
                in >> key;
                std::getline(in, text);
                if (!text.empty())
                    text.erase(0, 1);
                auto value = fixture::build(text);
                auto map = Value::object({});
                if (key == "all")
                    for (auto k : {"agent_id",
                                   "base_folder",
                                   "caps_path",
                                   "child",
                                   "circuit_code",
                                   "first_name",
                                   "last_name",
                                   "inventory_folder",
                                   "secure_session_id",
                                   "session_id",
                                   "service_session_id",
                                   "client_ip",
                                   "viewer",
                                   "channel",
                                   "mac",
                                   "id0",
                                   "teleport_flags",
                                   "start_pos",
                                   "far",
                                   "appearance_serial"})
                        map.map[k] = value;
                else
                    map.map[key] = value;
                c.unpack(map);
            } else if (op == "seeds") {
                Value seeds;
                seeds.kind = Value::Kind::Array;
                std::string handle, seed;
                while (in >> handle >> seed) {
                    auto pair = Value::object({});
                    if (handle != "-")
                        pair.map["handle"] = Value(token(handle));
                    if (seed != "-")
                        pair.map["seed"] = Value(token(seed));
                    seeds.array.push_back(pair);
                }
                c.unpack(Value::object({{"children_seeds", seeds}}));
            } else if (op == "seed-edit") {
                std::uint64_t handle;
                int remove;
                in >> handle >> remove;
                if (!c.children_cap_seeds)
                    c.children_cap_seeds = std::make_shared<AgentCircuitData::ChildrenSeeds>();
                if (remove)
                    c.children_cap_seeds->erase(handle);
                else
                    (*c.children_cap_seeds)[handle] = "changed";
            } else if (op == "urls") {
                int mode;
                in >> mode;
                auto map = Value::object({});
                Value old;
                old.kind = Value::Kind::Array;
                auto modern = Value::object({});
                std::string k, v;
                while (in >> k >> v) {
                    auto key = token(k), value = token(v);
                    old.array.emplace_back(key);
                    old.array.emplace_back(value);
                    modern.set(key, Value(value));
                }
                if (mode != 1)
                    map.map["service_urls"] = old;
                if (mode != 0)
                    map.map["serviceurls"] = modern;
                c.unpack(map);
            } else if (op == "url-edit") {
                std::string key;
                int remove;
                in >> key >> remove;
                if (!c.service_urls)
                    c.service_urls = std::make_shared<AgentCircuitData::ServiceUrls>();
                if (remove)
                    c.service_urls->erase(key);
                else
                    (*c.service_urls)[key] = std::string("changed");
            } else if (op == "objects") {
                c.service_urls = std::make_shared<AgentCircuitData::ServiceUrls>();
                auto& urls = *c.service_urls;
                urls["null"] = std::any{};
                urls["boolean"] = true;
                urls["integer"] = std::int32_t{-7};
                urls["long"] = std::int64_t{-9223372036854775807ll};
                urls["uuid"] = framework::Uuid{};
                urls["float"] = .5f;
                urls["double"] = 1e22;
                urls["custom"] = framework::ServiceUrlStringifiable{[] { return "custom-value"; }};
                urls["floatinf"] = std::numeric_limits<float>::infinity();
                urls["doubleinf"] = -std::numeric_limits<double>::infinity();
            } else if (op == "counter-object") {
                c.service_urls = std::make_shared<AgentCircuitData::ServiceUrls>();
                auto counter = std::make_shared<int>(0);
                (*c.service_urls)["counter"] =
                    framework::ServiceUrlStringifiable{[counter] { return std::to_string(++*counter); }};
            } else if (op == "mutating-object") {
                c.service_urls = std::make_shared<AgentCircuitData::ServiceUrls>();
                std::weak_ptr<AgentCircuitData::ServiceUrls> urls = c.service_urls;
                (*c.service_urls)["first"] = framework::ServiceUrlStringifiable{[urls] {
                    auto p = urls.lock();
                    (*p)["added"] = std::string("value");
                    return "changed";
                }};
            } else if (op == "switching-object") {
                c.service_urls = std::make_shared<AgentCircuitData::ServiceUrls>();
                (*c.service_urls)["first"] = framework::ServiceUrlStringifiable{[&c] {
                    c.service_urls = std::make_shared<AgentCircuitData::ServiceUrls>();
                    (*c.service_urls)["new"] = std::string("value");
                    return "old-value";
                }};
            } else if (op == "appearance") {
                c.appearance = std::make_shared<framework::AvatarAppearance>();
                c.appearance->serial = 17;
                c.appearance->hover_z = .25f;
                c.appearance->set_wearable(16, std::make_shared<framework::AvatarWearable>(
                                                   framework::Uuid("00000000-0000-0000-0000-000000000003"),
                                                   framework::Uuid("00000000-0000-0000-0000-000000000004")));
            } else if (op == "pack") {
                std::string bits;
                in >> bits;
                framework::EntityTransferContext ctx;
                ctx.outbound_version =
                    std::bit_cast<float>(static_cast<std::uint32_t>(std::stoul(bits, nullptr, 16)));
                out = canonical(c.pack(&ctx));
            } else if (op == "roundtrip")
                c.unpack(network::parse_llsd_xml(network::llsd_xml(c.pack())));
            else if (op == "xml-order") {
                auto map = network::parse_llsd_xml(
                    "<llsd><map><key>serviceurls</key><map><key>z</key><string>Z</string><key>a</key><string>A</string><key>m</key><string>M</string></map></map></llsd>");
                c.unpack(network::parse_llsd_xml(network::llsd_xml(map)));
            }
        } catch (const std::exception&) {
            status = "EX";
        }
        std::string state;
        try {
            state = canonical(c.pack()) + "|" + std::to_string(c.teleport_flags) + "|" +
                    (c.children_cap_seeds ? "1" : "0") + (c.service_urls ? "1" : "0");
        } catch (const std::exception&) {
            state = "STATEEX";
        }
        std::cout << status << '|' << out << '|' << state << '\n';
    }
}
