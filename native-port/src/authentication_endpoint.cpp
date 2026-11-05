// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/authentication_endpoint.hpp"
#include "opensim/services/user_account.hpp"
namespace opensim::server {
using namespace network;
namespace {
std::string text(const FormValue& value) {
    if (auto p = std::get_if<std::string>(&value))
        return *p;
    return "System.Collections.Generic.List`1[System.String]";
}
std::string result(bool success, const std::optional<std::string>& token = {}) {
    auto xml = service_result(success);
    auto at = xml.find("<result>");
    xml.replace(at, 8, "<Result>");
    at = xml.find("</result>");
    xml.replace(at, 9, "</Result>");
    if (token) {
        auto t = service_xml(ServiceXmlValue::object({{"Token", *token}}));
        auto first = t.find("<Token>");
        if (!token->empty() && first != std::string::npos) {
            auto end = t.find("</Token>", first) + 8;
            auto tail = xml.find("</ServerResponse>");
            xml.insert(tail, "  " + t.substr(first, end - first) +
#ifdef _WIN32
                                 "\r\n"
#else
                                 "\n"
#endif
            );
        } else {
            auto tail = xml.find("</ServerResponse>");
            xml.insert(tail,
#ifdef _WIN32
                       "  <Token>\r\n  </Token>\r\n"
#else
                       "  <Token>\n  </Token>\n"
#endif
            );
        }
    }
    return xml;
}
}
HttpResponse AuthenticationEndpoint::handle(const HttpRequest& request) {
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
    if (!request.path.starts_with("/auth"))
        return response;
    auto path = std::string_view(request.path).substr(5);
    auto start = path.find_first_not_of("/?&");
    if (start == path.npos)
        return response;
    path.remove_prefix(start);
    auto end = path.find_first_of("/?&");
    if (path.substr(0, end) != "plain")
        return response;
    auto fields = parse_service_form(read_service_body(request.body));
    int lifetime = 30;
    if (fields.contains("LIFETIME")) {
        lifetime = services::account_integer(text(fields.at("LIFETIME")));
        if (lifetime > 30)
            lifetime = 30;
    }
    response.body = result(false);
    if (!fields.contains("METHOD") || !fields.contains("PRINCIPAL"))
        return response;
    framework::Uuid principal;
    if (!framework::try_parse_sdk_uuid(text(fields.at("PRINCIPAL")), principal))
        return response;
    auto method = text(fields.at("METHOD"));
    if (method == "authenticate" && fields.contains("PASSWORD")) {
        auto token = operations_.authenticate(principal, text(fields.at("PASSWORD")), lifetime);
        if (!token || !token->empty())
            response.body = result(true, token.value_or(""));
    } else if (method == "setpassword" && allow_password_ && fields.contains("PASSWORD"))
        response.body = result(operations_.set_password(principal, text(fields.at("PASSWORD"))));
    else if (method == "verify" && fields.contains("TOKEN"))
        response.body = result(operations_.verify(principal, text(fields.at("TOKEN")), lifetime));
    else if (method == "release" && fields.contains("TOKEN"))
        response.body = result(operations_.release(principal, text(fields.at("TOKEN"))));
    else if (method == "getauthinfo" && allow_get_) {
        auto info = operations_.get_info(principal);
        if (info) {
            auto value = ServiceXmlValue::object({});
            for (auto& [key, item] : info->to_key_value_pairs())
                if (item.has_value())
                    value.fields.push_back({key, framework::service_url_object_text(item)});
            response.body = service_xml(ServiceXmlValue::object({{"result", std::move(value)}}));
        }
    } else if (method == "setauthinfo" && allow_set_) {
        auto info = operations_.get_info(principal);
        if (info) {
            auto set = [&](const char* name, auto& out) {
                if (fields.contains(name))
                    out = text(fields.at(name));
            };
            set("AccountType", info->account_type);
            set("PasswordHash", info->password_hash);
            set("PasswordSalt", info->password_salt);
            set("WebLoginKey", info->web_login_key);
            response.body = result(operations_.set_info(info));
        }
    }
    return response;
}
}
