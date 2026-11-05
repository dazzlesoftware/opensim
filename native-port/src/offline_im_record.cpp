// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/offline_im_record.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/framework/vector_text.hpp"
#include "opensim/framework/text.hpp"
#include <charconv>
#include <sstream>
#include <pugixml.hpp>

namespace opensim::services {
namespace {
constexpr std::pair<const char*, const char*> fields[] = {
    {"FromAgentID", "fromAgentID"}, {"FromAgentName", "fromAgentName"}, {"ToAgentID", "toAgentID"},
    {"Dialog", "dialog"}, {"FromGroup", "fromGroup"}, {"Message", "message"},
    {"SessionID", "imSessionID"}, {"Offline", "offline"}, {"EstateID", "ParentEstateID"},
    {"RegionID", "RegionID"}, {"Timestamp", "timestamp"}
};
}
network::ServiceXmlValue read_legacy_offline_im(std::string_view xml)
{
    pugi::xml_document document;
    if (!document.load_buffer(xml.data(), xml.size(), pugi::parse_default | pugi::parse_ws_pcdata_single))
        throw std::runtime_error("Invalid offline message XML");
    const auto message = document.document_element();
    if (std::string_view(message.name()) != "GridInstantMessage")
        throw std::runtime_error("Unknown offline message XML");

    auto result = network::ServiceXmlValue::object({});
    for (const auto& [wire, stored] : fields) {
        if (const auto field = message.child(stored)) {
            const std::string_view value = field.child_value();
            if (std::string_view(wire) == "FromGroup") {
                const auto flag = framework::trim_utf8(value);
                if (flag != "true" && flag != "false" && flag != "1" && flag != "0")
                    throw std::runtime_error("Invalid legacy offline message group flag");
                result.fields.emplace_back(wire, flag == "true" || flag == "1" ? "True" : "False");
            } else if (std::string_view(wire).ends_with("ID") && std::string_view(wire) != "EstateID") {
                framework::Uuid id;
                if (!framework::try_parse_sdk_uuid(framework::trim_utf8(value), id))
                    throw std::runtime_error("Invalid legacy offline message UUID");
                result.fields.emplace_back(wire, id.str());
            } else {
                result.fields.emplace_back(wire, std::string(value));
            }
        }
    }
    if (const auto position = message.child("Position")) {
        result.fields.emplace_back("Position", std::string("<") + position.child_value("X") + ", " +
            position.child_value("Y") + ", " + position.child_value("Z") + ">");
    }
    const auto bucket = message.child("binaryBucket");
    if (bucket) {
        const auto binary = network::parse_llsd_xml(std::string("<llsd><binary>") + bucket.child_value() + "</binary></llsd>");
        const auto bytes = network::osd_binary(binary);
        constexpr char digits[] = "0123456789ABCDEF";
        std::string hex;
        for (auto byte : *bytes) {
            if (!hex.empty())
                hex.push_back(' ');
            hex.push_back(digits[byte >> 4]);
            hex.push_back(digits[byte & 15]);
        }
        result.fields.emplace_back("BinaryBucket", std::move(hex));
    }
    if (result.fields.empty())
        throw std::runtime_error("Empty legacy offline message");
    return result;
}

std::string write_legacy_offline_im(const network::ServiceXmlValue& message)
{
    pugi::xml_document document;
    auto root = document.append_child("GridInstantMessage");
    for (const auto& [name, field] : message.fields) {
        if (name == "BinaryBucket") {
            std::vector<std::uint8_t> bytes;
            std::string_view input(field.text);
            while (!input.empty()) {
                unsigned value{};
                if (input.size() < 2)
                    throw std::invalid_argument("Invalid offline message bucket");
                const auto end = input.data() + 2;
                const auto parsed = std::from_chars(input.data(), end, value, 16);
                if (parsed.ec != std::errc{} || parsed.ptr != end)
                    throw std::invalid_argument("Invalid offline message bucket");
                bytes.push_back(static_cast<std::uint8_t>(value));
                input.remove_prefix(2);
                if (input.starts_with(' '))
                    input.remove_prefix(1);
            }
            const auto binary = network::osd_from_binary(std::make_shared<std::vector<std::uint8_t>>(std::move(bytes)));
            root.append_child("binaryBucket").text().set(network::binary_base64(binary).c_str());
        } else if (name == "Position") {
            const auto vector = framework::parse_sdk_vector3(field.text);
            auto position = root.append_child("Position");
            position.append_child("X").text().set(vector.x);
            position.append_child("Y").text().set(vector.y);
            position.append_child("Z").text().set(vector.z);
        } else {
            for (const auto& [wire, stored] : fields) {
                if (name == wire) {
                    const auto value = name == "FromGroup" ? (field.text == "True" ? "true" : "false") : field.text;
                    root.append_child(stored).text().set(value.c_str());
                    break;
                }
            }
        }
    }
    std::ostringstream output;
    document.save(output, "", pugi::format_raw);
    return output.str();
}
}
