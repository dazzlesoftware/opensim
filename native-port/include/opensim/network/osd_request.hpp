// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/osd_json.hpp"
namespace opensim::network {
Value osd_timed_request(const std::string& url,const Value& data,const std::string& method,int timeout_milliseconds,bool compressed=false,bool keepalive=false);
Value osd_delete_request(const std::string& url,int timeout_milliseconds=10000);
Value canonical_osd_response(std::string_view body);
Value osd_request(const std::string& url,const Value& data,const std::string& method,bool compressed=false,bool keepalive=false);
}
