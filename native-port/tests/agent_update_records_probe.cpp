// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/agent_update_records.hpp"
#include "opensim/framework/animation.hpp"
#include "opensim/network/osd_json.hpp"
#include <iostream>
using namespace opensim;
using network::Value;
std::string hex(std::string_view s) {
    std::string out;
    const char* h = "0123456789abcdef";
    for (unsigned char c : s) {
        out += h[c >> 4];
        out += h[c & 15];
    }
    return out;
}
std::string decode(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); i += 2)
        out += static_cast<char>(std::stoi(s.substr(i, 2), nullptr, 16));
    return out;
}
framework::Uuid id(int n) {
    return framework::Uuid("00000000-0000-0000-0000-00000000000" + std::to_string(n));
}
Value typed(std::string type) {
    if (type == "binary")
        return network::osd_from_binary(std::make_shared<std::vector<std::uint8_t>>(
            std::initializer_list<std::uint8_t>{0x12, 0x34, 0x56, 0x78}));
    if (type == "unsigned")
        return network::osd_from_unsigned(0xffffffffu);
    if (type == "long")
        return Value::long_integer(0x1234567887654321LL);
    Value v;
    if (type == "uuid") {
        v.kind = Value::Kind::Uuid;
        v.text = id(7).str();
    } else if (type == "date") {
        v.kind = Value::Kind::Date;
        v.text = "2026-10-29T12:34:56Z";
    } else if (type == "uri") {
        v.kind = Value::Kind::Uri;
        v.text = "http://example.com/path";
    }
    return v;
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        auto command = network::parse_osd_json(decode(line));
        auto data = command.get("data");
        int profile = network::osd_integer(command.get("profile"));
        bool group = network::osd_string(command.get("model")) == "group",
             animation = network::osd_string(command.get("model")) == "animation", failed = false;
        auto anim = std::make_unique<framework::Animation>(profile & 1 ? id(7) : framework::Uuid{},
                                                           profile & 1 ? -1 : 0,
                                                           profile & 1 ? id(8) : framework::Uuid{});
        framework::AgentGroupData g(profile & 1 ? id(7) : framework::Uuid{},
                                    profile & 1 ? ~std::uint64_t{} : 0, profile & 1);
        framework::ControllerData c(profile & 1 ? id(7) : framework::Uuid{},
                                    profile & 1 ? id(8) : framework::Uuid{},
                                    profile & 1 ? ~std::uint32_t{} : 0, profile & 1 ? 0x80000000u : 0);
        if (command.contains("typed"))
            data.set(network::osd_string(command.get("field")),
                     typed(network::osd_string(command.get("typed"))));
        const Value* input = network::osd_boolean(command.get("null_map")) ? nullptr : &data;
        try {
            if (profile & 8) {
                auto& raw = command.get("raw").array;
                if (animation)
                    anim = std::make_unique<framework::Animation>(
                        id(7), static_cast<std::int32_t>(std::stoll(network::osd_string(raw.at(0)))), id(8));
                else if (group)
                    g = framework::AgentGroupData(id(7), std::stoull(network::osd_string(raw.at(0))),
                                                  network::osd_boolean(raw.at(1)));
                else
                    c = framework::ControllerData(
                        id(7), id(8), static_cast<std::uint32_t>(std::stoull(network::osd_string(raw.at(0)))),
                        static_cast<std::uint32_t>(std::stoull(network::osd_string(raw.at(1)))));
            } else if (animation) {
                if (profile & 2)
                    anim = std::make_unique<framework::Animation>(input);
                else
                    anim->unpack(input);
                if (profile & 4)
                    anim->unpack(command.get("next"));
            } else if (group) {
                if (profile & 2)
                    g = framework::AgentGroupData(input);
                else
                    g.unpack(input);
                if (profile & 4)
                    g.unpack(command.get("next"));
            } else {
                if (profile & 2)
                    c = framework::ControllerData(input);
                else
                    c.unpack(input);
                if (profile & 4)
                    c.unpack(command.get("next"));
            }
        } catch (const std::exception&) {
            failed = true;
        }
        std::cout << failed << "|"
                  << hex(network::osd_json(animation ? anim->pack()
                                           : group   ? g.pack()
                                                     : c.pack(),
                                           true));
        if (animation) {
            framework::Animation same(anim->animation_id, anim->sequence_number, anim->object_id),
                different(anim->animation_id, anim->sequence_number ^ 1, anim->object_id);
            std::cout << "|" << hex(anim->str()) << "|" << anim->equals(nullptr) << anim->equals(anim.get())
                      << (*anim == same) << (*anim == different) << "|"
                      << (anim->identity_hash() == anim->identity_hash());
        }
        std::cout << "\n";
    }
}
