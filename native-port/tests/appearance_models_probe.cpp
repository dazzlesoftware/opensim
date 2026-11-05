// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/avatar_attachment.hpp"
#include "opensim/framework/entity_transfer_context.hpp"
#include "osd_fixture_value.hpp"
#include <iomanip>
#include <iostream>
using namespace opensim;
using network::Value;
framework::Uuid id(int n) {
    std::ostringstream out;
    out << "00000000-0000-0000-0000-" << std::hex << std::setw(12) << std::setfill('0') << n;
    return framework::Uuid(out.str());
}
Value uuid(int n) {
    Value value(id(n).str());
    value.kind = Value::Kind::Uuid;
    return value;
}
std::string bits(float value) {
    std::ostringstream out;
    out << std::hex << std::setw(8) << std::setfill('0') << std::bit_cast<std::uint32_t>(value);
    return out.str();
}
std::string snapshot(const framework::AvatarAttachment& attachment,
                     const framework::EntityTransferContext& context) {
    const auto a = attachment.pack(), c = context.pack();
    const bool a_types = a.kind == Value::Kind::Map && a.map.size() == 3 &&
                         a.get("point").kind == Value::Kind::Integer &&
                         a.get("item").kind == Value::Kind::Uuid && a.get("asset").kind == Value::Kind::Uuid;
    const bool c_types = c.kind == Value::Kind::Map && c.map.size() == 3 &&
                         c.get("InboundVersion").kind == Value::Kind::Real &&
                         c.get("OutboundVersion").kind == Value::Kind::Real &&
                         c.get("WearablesCount").kind == Value::Kind::Integer;
    return std::to_string(attachment.point) + "|" + attachment.item.str() + "|" + attachment.asset.str() +
           "|" + std::to_string(a_types) + ":" + std::to_string(a.get("point").integer) + ":" +
           a.get("item").text + ":" + a.get("asset").text + "|" + bits(context.inbound_version) + ":" +
           bits(context.outbound_version) + ":" + std::to_string(context.wearables_count) + "|" +
           std::to_string(c_types) + ":" + bits(static_cast<float>(c.get("InboundVersion").real)) + ":" +
           bits(static_cast<float>(c.get("OutboundVersion").real)) + ":" +
           std::to_string(c.get("WearablesCount").integer);
}
int main() {
    framework::AvatarAttachment attachment(17, id(1), id(2));
    framework::EntityTransferContext context;
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            const auto space = line.find(' ');
            const auto command = line.substr(0, space),
                       text = space == std::string::npos ? std::string{} : line.substr(space + 1);
            std::string a_status = "AOK", c_status = "COK";
            if (command == "reset") {
                attachment = framework::AvatarAttachment(17, id(1), id(2));
                context = framework::EntityTransferContext{};
            } else if (command == "copy") {
                auto previous = attachment;
                attachment = framework::AvatarAttachment(previous);
                ++previous.point;
            } else {
                Value a = Value::object({}), c = Value::object({});
                if (command == "wrong")
                    a.kind = c.kind = Value::Kind::Array;
                else if (command == "missing") {
                    a.map["item"] = uuid(1);
                    Value real;
                    real.kind = Value::Kind::Real;
                    real.real = .55;
                    c.map["OutboundVersion"] = real;
                } else if (command == "set" || command == "partial" || command == "ctor") {
                    const auto value = fixture::build(text);
                    a.map["point"] = value;
                    c.map["InboundVersion"] = value;
                    if (command != "partial") {
                        a.map["item"] = a.map["asset"] = value;
                        c.map["OutboundVersion"] = c.map["WearablesCount"] = value;
                    }
                }
                try {
                    if (command == "null")
                        attachment.unpack(static_cast<const Value*>(nullptr));
                    else if (command == "ctor")
                        attachment = framework::AvatarAttachment(a);
                    else
                        attachment.unpack(a);
                } catch (const std::exception&) {
                    a_status = "AEX";
                }
                try {
                    if (command == "null")
                        context.unpack(static_cast<const Value*>(nullptr));
                    else {
                        if (command == "ctor")
                            context = framework::EntityTransferContext{};
                        context.unpack(c);
                    }
                } catch (const std::exception&) {
                    c_status = "CEX";
                }
            }
            std::cout << a_status << '|' << c_status << '|' << snapshot(attachment, context) << '\n';
        } catch (const std::exception&) {
            std::cout << "EX\n";
        }
    }
    return 0;
}
