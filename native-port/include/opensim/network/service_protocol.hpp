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
#pragma once
#include "opensim/network/http_server.hpp"
#include "opensim/server/config.hpp"
#include <variant>
#include <optional>
#include <vector>
namespace opensim::network {
using FormValue=std::variant<std::string,std::vector<std::string>>;
struct Form:std::map<std::string,FormValue> {
    using std::map<std::string,FormValue>::map;
    std::vector<std::string> insertion_order;
    std::vector<std::string> ordered_keys()const;
};
Form parse_service_form(std::string_view text);
std::string service_url_decode(std::string_view text);
std::string service_url_encode(std::string_view text);
struct ServiceXmlValue {
    std::string text;
    std::vector<std::pair<std::string,ServiceXmlValue>> fields;
    bool list=false;
    ServiceXmlValue()=default;
    ServiceXmlValue(std::string value):text(std::move(value)){}
    ServiceXmlValue(const char* value):text(value){}
    static ServiceXmlValue object(std::vector<std::pair<std::string,ServiceXmlValue>> fields);
};
std::string service_xml_name(std::string_view value);
std::string service_xml(const ServiceXmlValue& value);
std::string service_result(bool success);
std::string read_service_body(std::string_view bytes);
ServiceXmlValue parse_service_xml(std::string_view text);
std::string build_service_form(const std::vector<std::pair<std::string,std::optional<std::string>>>& fields);
class ServiceAuth {
public:
    explicit ServiceAuth(const server::IniConfig& config,const std::string& section);
    bool authenticate(const HttpRequest& request,HttpResponse& response)const;
private:
    bool allow_script_requests_,basic_;
    std::string username_,password_;
};
}
