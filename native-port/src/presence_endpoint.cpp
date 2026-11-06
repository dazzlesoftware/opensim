/*
Copyright (c) Contributors, http://opensimulator.org/
See CONTRIBUTORS.TXT for a full list of copyright holders.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenSimulator Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "opensim/server/presence_endpoint.hpp"
#include "opensim/framework/text.hpp"
namespace opensim::server {
using namespace network;
namespace {
std::string text(const FormValue& value) {
    if (const auto* scalar = std::get_if<std::string>(&value))
        return *scalar;
    return "System.Collections.Generic.List`1[System.String]";
}
ServiceXmlValue info(const services::PresenceInfo& value) {
    return ServiceXmlValue::object({{"UserID", value.user_id}, {"RegionID", value.region_id.str()}});
}
}
HttpResponse PresenceEndpoint::handle(const HttpRequest& request) {
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
    response.body = service_result(false);
    try {
        const auto body = read_service_body(request.body);
        const auto fields = parse_service_form(framework::trim_utf8(body));
        const auto method = fields.find("METHOD");
        if (method == fields.end() || !std::holds_alternative<std::string>(method->second))
            return response;
        const auto& name = std::get<std::string>(method->second);
        if (name == "login")
            return response;
        if (name == "logout" && fields.contains("SessionID"))
            response.body = service_result(service_.logout(framework::Uuid(text(fields.at("SessionID")))));
        else if (name == "logoutregion" && fields.contains("RegionID"))
            response.body =
                service_result(service_.logout_region(framework::Uuid(text(fields.at("RegionID")))));
        else if (name == "report" && fields.contains("SessionID") && fields.contains("RegionID"))
            response.body = service_result(service_.report(framework::Uuid(text(fields.at("SessionID"))),
                                                           framework::Uuid(text(fields.at("RegionID")))));
        else if (name == "getagent" && fields.contains("SessionID")) {
            const auto value = service_.agent(framework::Uuid(text(fields.at("SessionID"))));
            response.body = service_xml(
                ServiceXmlValue::object({{"result", value ? info(*value) : ServiceXmlValue("null")}}));
        } else if (name == "getagents" && fields.contains("uuids")) {
            if (const auto* users = std::get_if<std::vector<std::string>>(&fields.at("uuids"))) {
                const auto values = service_.agents(*users);
                auto result = ServiceXmlValue::object({});
                if (values.empty())
                    result.fields.push_back({"result", "null"});
                else
                    for (std::size_t i = 0; i < values.size(); ++i)
                        result.fields.push_back({"presence" + std::to_string(i), info(values[i])});
                response.body = service_xml(result);
            }
        }
    } catch (...) {
        response.body = service_result(false);
    }
    return response;
}
}
