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
#include "opensim/server/grid_user_endpoint.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/vector_text.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>
namespace opensim::server {
using namespace network;
namespace {
std::string text(const FormValue& value) {
    if (const auto* scalar = std::get_if<std::string>(&value))
        return *scalar;
    return "System.Collections.Generic.List`1[System.String]";
}
// OpenSim's server sets the invariant culture. DateTime.ToString uses this
// Gregorian UTC date without a timezone suffix, including pre-epoch values.
std::string date(std::int32_t seconds) {
    using namespace std::chrono;
    const sys_seconds instant{std::chrono::seconds{seconds}};
    const auto day = floor<days>(instant);
    const year_month_day calendar{day};
    const hh_mm_ss time{instant - day};
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setfill('0') << std::setw(2) << unsigned(calendar.month()) << '/' << std::setw(2)
        << unsigned(calendar.day()) << '/' << std::setw(4) << int(calendar.year()) << ' ' << std::setw(2)
        << time.hours().count() << ':' << std::setw(2) << time.minutes().count() << ':' << std::setw(2)
        << time.seconds().count();
    return out.str();
}
ServiceXmlValue info(const services::GridUserInfo& value) {
    return ServiceXmlValue::object({{"UserID", value.user_id},
                                    {"HomeRegionID", value.home_region.str()},
                                    {"HomePosition", framework::vector3_string(value.home_position)},
                                    {"HomeLookAt", framework::vector3_string(value.home_look_at)},
                                    {"LastRegionID", value.last_region.str()},
                                    {"LastPosition", framework::vector3_string(value.last_position)},
                                    {"LastLookAt", framework::vector3_string(value.last_look_at)},
                                    {"Online", value.online ? "True" : "False"},
                                    {"Login", date(value.login)},
                                    {"Logout", date(value.logout)}});
}
bool unpack(const Form& fields, std::string& user, framework::Uuid& region, framework::Vector3& position,
            framework::Vector3& look) {
    if (!fields.contains("UserID") || !fields.contains("RegionID"))
        return false;
    user = text(fields.at("UserID"));
    try {
        region = framework::Uuid(text(fields.at("RegionID")));
    } catch (...) {
        return false;
    }
    position = {128, 128, 70};
    look = {};
    if (fields.contains("Position"))
        framework::parse_vector3(text(fields.at("Position")), position);
    if (fields.contains("LookAt"))
        framework::parse_vector3(text(fields.at("LookAt")), look);
    return true;
}
}
HttpResponse GridUserEndpoint::handle(const HttpRequest& request) {
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
        if (method == fields.end())
            return response;
        const auto name = text(method->second);
        if (name == "loggedin" && fields.contains("UserID"))
            response.body = service_xml(
                ServiceXmlValue::object({{"result", info(service_.logged_in(text(fields.at("UserID"))))}}));
        else if (name == "getgriduserinfo" && fields.contains("UserID")) {
            const auto value = service_.info(text(fields.at("UserID")));
            if (value)
                response.body = service_xml(ServiceXmlValue::object({{"result", info(*value)}}));
        } else if (name == "getgriduserinfos" && fields.contains("AgentIDs")) {
            if (const auto* users = std::get_if<std::vector<std::string>>(&fields.at("AgentIDs"))) {
                const auto values = service_.info(*users);
                auto result = ServiceXmlValue::object({});
                if (values.empty())
                    result.fields.push_back({"result", "null"});
                else {
                    std::size_t index = 0;
                    for (const auto& value : values)
                        if (value)
                            result.fields.push_back({"griduser" + std::to_string(index++), info(*value)});
                }
                response.body = service_xml(result);
            }
        } else if (name == "loggedout" || name == "sethome" || name == "setposition") {
            std::string user;
            framework::Uuid region;
            framework::Vector3 position, look;
            if (unpack(fields, user, region, position, look)) {
                const bool success =
                    name == "loggedout" ? service_.logged_out(user, {}, region, position, look)
                    : name == "sethome" ? service_.set_home(user, region, position, look)
                                        : service_.set_last_position(user, {}, region, position, look);
                response.body = service_result(success);
            }
        }
    } catch (...) {
        response.body = service_result(false);
    }
    return response;
}
}
