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
#include "opensim/server/grid_endpoint.hpp"
#include "opensim/framework/text.hpp"
#include <stdexcept>
#include "opensim/framework/http_host.hpp"
namespace opensim::server {
using namespace network;
namespace {
std::string text(const FormValue& value) {
    if (const auto* scalar = std::get_if<std::string>(&value))
        return *scalar;
    return "System.Collections.Generic.List`1[System.String]";
}
std::string value(const Form& fields, const std::string& name) {
    const auto found = fields.find(name);
    return found == fields.end() ? "" : text(found->second);
}
framework::Uuid uuid(const Form& fields, const std::string& name) {
    try {
        return framework::Uuid(value(fields, name));
    } catch (...) {
        return {};
    }
}
int integer(const Form& fields, const std::string& name) {
    try {
        return services::parse_region_integer(value(fields, name));
    } catch (...) {
        return 0;
    }
}
ServiceXmlValue info(const services::GridRegion& region) {
    auto result = ServiceXmlValue::object({});
    for (const auto& [key, value] : region.fields())
        if (value)
            result.fields.push_back({key, *value});
    return result;
}
std::string single(const std::optional<services::GridRegion>& region) {
    return service_xml(
        ServiceXmlValue::object({{"result", region ? info(*region) : ServiceXmlValue("null")}}));
}
std::string list(const std::vector<services::GridRegion>& regions) {
    auto result = ServiceXmlValue::object({});
    if (regions.empty())
        result.fields.push_back({"result", "null"});
    else
        for (std::size_t i = 0; i < regions.size(); ++i)
            result.fields.push_back({"region" + std::to_string(i), info(regions[i])});
    return service_xml(result);
}
std::string result(bool success, const std::string& message = "") {
    auto xml = service_result(success);
    const auto first = xml.find("<result>");
    xml.replace(first, 8, "<Result>");
    const auto last = xml.find("</result>");
    xml.replace(last, 9, "</Result>");
    if (!success) {
        auto escaped = service_xml(ServiceXmlValue::object({{"Message", message}}));
        const auto start = escaped.find("<Message>");
        escaped = escaped.substr(start, escaped.find("</Message>", start) + 10 - start);
        // DocToBytes uses XmlWriter indentation and newline normalization.
#ifdef _WIN32
        const std::string newline = "\r\n";
#else
        const std::string newline = "\n";
#endif
        std::string normalized;
        for (std::size_t i = 0; i < escaped.size(); ++i) {
            if (escaped[i] == '\r') {
                if (i + 1 < escaped.size() && escaped[i + 1] == '\n')
                    ++i;
                normalized += newline;
            } else if (escaped[i] == '\n')
                normalized += newline;
            else
                normalized += escaped[i];
        }
        if (message.empty())
            normalized = "<Message>" + newline + "  </Message>";
        const auto end = xml.find("</ServerResponse>");
        xml.insert(end, "  " + normalized + newline);
    }
    return xml;
}
}
ServiceXmlValue grid_extra_features(const IniConfig& config) {
    auto result = ServiceXmlValue::object({});
    if (config.boolean("GridService", "SuppressConsoleCommands", false) ||
        !config.has_section("GridService") || !config.has_section("LoginService"))
        return result;
    const auto add = [&](const std::string& key, const std::string& value) {
        if (!value.empty())
            result.fields.push_back({key, value});
    };
    add("search-server-url", config.get("LoginService", "SearchURL"));
    auto map = config.get("LoginService", "MapTileURL");
    if (!map.empty()) {
        map = framework::trim_utf8(map);
        if (!map.ends_with('/'))
            map += '/';
        add("map-server-url", map);
    }
    add("destination-guide-url", config.get("LoginService", "DestinationGuide"));
    const auto sections = [&](const std::string& key, std::initializer_list<const char*> names) {
        std::string value;
        for (const auto* section : names)
            value = config.get(section, key, value);
        return value;
    };
    add("GridURL", sections("GatekeeperURI", {"Startup", "Hypergrid"}));
    auto name = sections("GridName", {"Const", "Hypergrid"});
    if (name.empty())
        name = sections("gridname", {"GridInfo", "GridInfoService"});
    add("GridName", name);
    auto nick = sections("GridNick", {"Const", "Hypergrid"});
    if (nick.empty())
        nick = sections("gridnick", {"GridInfo", "GridInfoService"});
    add("GridNick", nick);
    add("GridStatus", sections("GridStatus", {"GridInfo", "GridInfoService"}));
    add("GridStatusRSS", sections("GridStatusRSS", {"GridInfo", "GridInfoService"}));
    result.fields.push_back({"ExportSupported", config.get("GridService", "ExportSupported", "true")});
    const auto aliases = sections("GatekeeperURIAlias", {"Const, Startup", "Hypergrid", "GatekeeperService"});
    if (!framework::trim_utf8(aliases).empty()) {
        std::string normalized;
        std::size_t start = 0;
        while (start <= aliases.size()) {
            const auto end = aliases.find(',', start);
            framework::HttpHost host(aliases.substr(start, end == std::string::npos ? end : end - start));
            if (host.valid()) {
                normalized += host.uri;
                if (end != std::string::npos)
                    normalized += ',';
            }
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
        if (!normalized.empty())
            result.fields.push_back({"GridURLAlias", normalized});
    }
    return result;
}
HttpResponse GridEndpoint::handle(const HttpRequest& request) {
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
        const auto body = read_service_body(request.body);
        const auto fields = parse_service_form(framework::trim_utf8(body));
        const auto method = value(fields, "METHOD");
        const auto scope = uuid(fields, "SCOPEID"), id = uuid(fields, "REGIONID");
        if (method == "register") {
            if (integer(fields, "VERSIONMIN") > 1 || integer(fields, "VERSIONMAX") < 1)
                return response;
            std::optional<services::GridRegion> region;
            try {
                services::RegionFields data;
                for (const auto& [key, item] : fields)
                    data[key] = text(item);
                region = services::GridRegion::from_fields(data);
            } catch (...) {
            }
            const auto error =
                region ? service_.register_region(scope, *region) : "Error communicating with grid service";
            response.body = result(error.empty(), error);
        } else if (method == "deregister")
            response.body = result(service_.deregister_region(id));
        else if (method == "get_neighbours")
            response.body = list(service_.neighbours(scope, id));
        else if (method == "get_region_by_uuid")
            response.body = single(service_.by_id(scope, id));
        else if (method == "get_region_by_position")
            response.body = single(service_.by_position(scope, integer(fields, "X"), integer(fields, "Y")));
        else if (method == "get_region_by_name" || method == "get_localregion_by_name") {
            std::optional<services::GridRegion> region;
            const auto found = fields.find("NAME");
            if (found != fields.end())
                if (const auto* name = std::get_if<std::string>(&found->second))
                    region = method == "get_region_by_name" ? service_.by_name(scope, *name)
                                                            : service_.local_by_name(scope, *name);
            response.body = single(region);
        } else if (method == "get_regions_by_name")
            response.body = list(service_.names(scope, value(fields, "NAME"), integer(fields, "MAX")));
        else if (method == "get_region_range")
            response.body = list(service_.range(scope, integer(fields, "XMIN"), integer(fields, "XMAX"),
                                                integer(fields, "YMIN"), integer(fields, "YMAX")));
        else if (method == "get_default_regions")
            response.body = list(service_.defaults(scope));
        else if (method == "get_default_hypergrid_regions")
            response.body = list(service_.hypergrid_defaults(scope));
        else if (method == "get_fallback_regions")
            response.body = list(service_.fallback(scope, integer(fields, "X"), integer(fields, "Y")));
        else if (method == "get_online_regions") {
            const auto max = integer(fields, "MC");
            response.body =
                list(max > 0 ? service_.online(scope, integer(fields, "X"), integer(fields, "Y"), max)
                             : std::vector<services::GridRegion>{});
        } else if (method == "get_hyperlinks")
            response.body = list(service_.hyperlinks(scope));
        else if (method == "get_region_flags")
            response.body =
                service_xml(ServiceXmlValue::object({{"result", std::to_string(service_.flags(scope, id))}}));
        else if (method == "get_grid_extra_features")
            response.body = service_xml(features_());
    } catch (...) {
        response.body = result(false);
    }
    return response;
}
}
