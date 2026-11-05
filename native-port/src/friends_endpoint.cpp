// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/friends_endpoint.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/culture_prefix.hpp"
namespace opensim::server {
using namespace network;
namespace {
std::string text(const FormValue& v) {
    if (auto s = std::get_if<std::string>(&v))
        return *s;
    return "System.Collections.Generic.List`1[System.String]";
}
std::string result(bool success) {
    auto xml = service_result(success);
    xml.replace(xml.find("<result>"), 8, "<Result>");
    xml.replace(xml.find("</result>"), 9, "</Result>");
    if (!success) {
        auto at = xml.find("</ServerResponse>");
        auto newline = xml.find("\r\n") != std::string::npos ? "\r\n" : "\n";
        xml.insert(at, std::string("  <Message>") + newline + "  </Message>" + newline);
    }
    return xml;
}
std::string package(const FriendsOperations::Friends& friends) {
    if (!friends || friends->empty())
        return service_xml(ServiceXmlValue::object({{"result", "null"}}));
    auto root = ServiceXmlValue::object({});
    std::size_t index = 0;
    for (const auto& info : *friends) {
        if (!info)
            throw std::invalid_argument("Null friend");
        auto entry = ServiceXmlValue::object({});
        for (const auto& [key, value] : info->to_key_value_pairs())
            if (value.has_value() && value.type() != typeid(std::nullptr_t))
                entry.fields.push_back({key, framework::service_url_object_text(value)});
        root.fields.push_back({"friend" + std::to_string(index++), std::move(entry)});
    }
    return service_xml(root);
}
}
HttpResponse FriendsEndpoint::handle(const HttpRequest& request) {
    HttpResponse response;
    response.content_type = "application/xml";
    response.headers["Cache-Control"] = "no-store";
    if (request.method != "POST") {
        response.status = 405;
        response.headers["Allow"] = "POST";
        return response;
    }
    if (!auth_.authenticate(request, response))
        return response;
    response.body = result(false);
    try {
        auto fields = parse_service_form(framework::trim_utf8(read_service_body(request.body)));
        auto method = fields.find("METHOD");
        if (method == fields.end())
            return response;
        auto get = [&](const char* key) {
            auto found = fields.find(key);
            return found == fields.end() ? std::string{} : text(found->second);
        };
        auto name = text(method->second);
        if (name == "status" && status_) {
            framework::Uuid from, to;
            const auto value = framework::trim_utf8(get("Online"));
            const bool online = framework::invariant_equals_ignore_case(value, "true");
            const bool valid = framework::try_parse_sdk_uuid(get("FromID"), from) &&
                               framework::try_parse_sdk_uuid(get("ToID"), to) &&
                               (online || framework::invariant_equals_ignore_case(value, "false"));
            response.body = service_xml(
                ServiceXmlValue::object({{"RESULT", valid && status_(to, from, online) ? "True" : "False"}}));
        } else if (name == "grant_rights" && rights_) {
            framework::Uuid from, to;
            std::int32_t previous = 0, granted = 0;
            const bool valid = framework::try_parse_sdk_uuid(get("FromID"), from) &&
                               framework::try_parse_sdk_uuid(get("ToID"), to) &&
                               services::parse_account_integer(get("UserFlags"), previous) &&
                               services::parse_account_integer(get("Rights"), granted);
            response.body = service_xml(ServiceXmlValue::object(
                {{"RESULT", valid && rights_(to, from, previous, granted) ? "True" : "False"}}));
        } else if (name == "friendship_terminated" && terminate_) {
            framework::Uuid from, to;
            const bool valid = framework::try_parse_sdk_uuid(get("FromID"), from) &&
                               framework::try_parse_sdk_uuid(get("ToID"), to);
            response.body = service_xml(
                ServiceXmlValue::object({{"RESULT", valid && terminate_(to, from) ? "True" : "False"}}));
        } else if (((name == "friendship_approved" || name == "friendship_denied") && answer_) ||
                   (name == "friendship_offered" && offer_)) {
            framework::Uuid from, to;
            const bool valid = framework::try_parse_sdk_uuid(get("FromID"), from) &&
                               framework::try_parse_sdk_uuid(get("ToID"), to);
            const bool delivered =
                valid && (name == "friendship_offered"
                              ? offer_(to, from, get("FromName"), get("Message"))
                              : answer_(to, from, get("FromName"), name == "friendship_approved"));
            response.body = service_xml(ServiceXmlValue::object({{"RESULT", delivered ? "True" : "False"}}));
        } else if (name == "getfriends" || name == "deletefriend") {
            framework::Uuid id;
            framework::try_parse_sdk_uuid(get("PRINCIPALID"), id);
            response.body = name == "getfriends" ? package(operations_.get(id))
                                                 : result(operations_.erase(id, get("FRIEND")));
        } else if (name == "getfriends_string")
            response.body = package(operations_.get_string(get("PRINCIPALID")));
        else if (name == "deletefriend_string")
            response.body = result(operations_.erase_string(get("PRINCIPALID"), get("FRIEND")));
        else if (name == "storefriend") {
            std::int32_t flags = 0;
            services::parse_account_integer(get("MyFlags"), flags);
            response.body = result(operations_.store(get("PrincipalID"), get("Friend"), flags));
        }
    } catch (...) {
        response.body = result(false);
    }
    return response;
}
}
