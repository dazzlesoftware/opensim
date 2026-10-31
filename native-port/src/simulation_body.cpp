// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/simulation_body.hpp"
#include "opensim/network/service_protocol.hpp"
#include "opensim/network/osd_json.hpp"
namespace opensim::network {
std::optional<Value> simulation_json_body(const HttpRequest& request){try{auto header=[&](std::string_view wanted){std::string result;bool found=false;for(const auto& [key,value]:request.headers){auto lower=key;for(auto& c:lower)if(c>='A'&&c<='Z')c+=32;if(lower==wanted){if(found)result+=',';result+=value;found=true;}}return result;};auto body=request.content_type=="application/x-gzip"||header("content-encoding")=="gzip"||header("x-content-encoding")=="gzip"?inflate_gzip(request.body):request.body;auto value=parse_osd_json(read_service_body(body));if(value.kind!=Value::Kind::Map)return {};return value;}catch(const std::exception&){return {};}}
}
