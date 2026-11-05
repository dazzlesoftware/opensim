// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/offline_im_endpoint.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/vector_text.hpp"
#include <charconv>

namespace opensim::server {
using namespace network;

HttpResponse OfflineImEndpoint::handle(const HttpRequest& request)
{
    HttpResponse response;
    response.content_type = "application/xml";
    if (request.method != "POST") {
        response.status = 405;
        response.headers["Allow"] = "POST";
        return response;
    }
    if (!auth_.authenticate(request, response))
        return response;
    response.body = process(request.body);
    return response;
}

std::string OfflineImEndpoint::process(std::string_view body)
{
    auto result = ServiceXmlValue::object({{"RESULT", "False"}});
    try {
        const auto form = parse_service_form(framework::trim_utf8(read_service_body(body)));
        const auto field = [&](const char* name) -> const std::string* {
            const auto item = form.find(name);
            return item == form.end() ? nullptr : std::get_if<std::string>(&item->second);
        };
        const auto* method = field("METHOD");
        if (method && *method == "GET") {
            const auto* principal = field("PrincipalID");
            framework::Uuid id;
            if (!form.contains("PrincipalID")) {
                result = ServiceXmlValue::object({{"RESULT", "NULL"}, {"REASON", "Bad network data"}});
            } else if (principal && framework::try_parse_sdk_uuid(*principal, id)) {
                auto messages = ServiceXmlValue::object({});
                for (auto& row : operations_.get(id))
                    messages.fields.emplace_back("im-" + std::to_string(messages.fields.size()), std::move(row));
                result = ServiceXmlValue::object({{"RESULT", std::move(messages)}});
            }
        } else if (method && *method == "STORE") {
            auto row = ServiceXmlValue::object({});
            framework::Uuid recipient;
            constexpr const char* message_fields[] = {
                "BinaryBucket", "Dialog", "FromAgentID", "FromAgentName", "FromGroup",
                "SessionID", "Message", "Offline", "EstateID", "Position", "RegionID",
                "Timestamp", "ToAgentID"
            };
            for (const std::string_view name : message_fields) {
                const auto key = std::string(name);
                const auto* value = field(key.c_str());
                std::string normalized = value ? *value : "";
                if (name.ends_with("ID") && name != "EstateID") {
                    framework::Uuid id;
                    if (value && !framework::try_parse_sdk_uuid(*value, id))
                        throw std::invalid_argument("Invalid offline message UUID");
                    if (name == "ToAgentID")
                        recipient = id;
                    normalized = id.str();
                } else if (name == "Dialog" || name == "Offline" || name == "EstateID" || name == "Timestamp") {
                    std::uint32_t number = 0;
                    if (value) {
                        const auto trimmed = framework::trim_utf8(*value);
                        std::string_view input(trimmed);
                        if (input.starts_with('+'))
                            input.remove_prefix(1);
                        const auto end = input.data() + input.size();
                        const auto parsed = std::from_chars(input.data(), end, number);
                        if (parsed.ec != std::errc{} || parsed.ptr != end ||
                            ((name == "Dialog" || name == "Offline") && number > 255))
                            throw std::invalid_argument("Invalid offline message number");
                    }
                    normalized = std::to_string(number);
                } else if (name == "Position") {
                    normalized = framework::vector3_string(value ? framework::parse_sdk_vector3(*value) : framework::Vector3{});
                } else if (name == "BinaryBucket") {
                    normalized.clear();
                    std::size_t digits = 0;
                    for (auto digit : value ? std::string_view(*value) : std::string_view{}) {
                        if (digit >= 'a' && digit <= 'f')
                            digit = static_cast<char>(digit - 'a' + 'A');
                        if (!((digit >= '0' && digit <= '9') || (digit >= 'A' && digit <= 'F')))
                            continue;
                        if (digits && digits % 2 == 0)
                            normalized.push_back(' ');
                        normalized.push_back(digit);
                        ++digits;
                    }
                    if (digits % 2 != 0)
                        normalized.resize(normalized.size() - (digits == 1 ? 1 : 2));
                } else if (name == "FromGroup") {
                    const auto flag = framework::trim_utf8(normalized);
                    const bool from_group = framework::ordinal_equals_ignore_case(flag, "true");
                    if (value && !from_group && !framework::ordinal_equals_ignore_case(flag, "false"))
                        throw std::invalid_argument("Invalid offline message group flag");
                    normalized = from_group ? "True" : "False";
                }
                row.fields.emplace_back(key, std::move(normalized));
            }
            const auto [stored, reason] = operations_.store(recipient, row);
            if (stored)
                result = ServiceXmlValue::object({{"RESULT", "True"}});
            else
                result.fields.emplace_back("REASON", reason);
        } else if (method && *method == "DELETE") {
            const auto* user = field("UserID");
            framework::Uuid id;
            if (user && framework::try_parse_sdk_uuid(*user, id)) {
                operations_.erase(id);
                result = ServiceXmlValue::object({{"RESULT", "True"}});
            }
        }
    } catch (const std::exception&) {
        result = ServiceXmlValue::object({{"RESULT", "False"}});
    }
    return service_xml(result);
}
}
