// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/simulation_body.hpp"
#include "opensim/network/service_protocol.hpp"
#include "opensim/network/osd_json.hpp"

namespace opensim::network {
std::string decode_rpc_request_body(const HttpRequest &request) {
    const auto is_gzip = [&](const char *name) {
        const auto header = request.headers.find(name);
        return header != request.headers.end() && header->second == "gzip";
    };
    if (!request.standard_gzip_decoded && (is_gzip("content-encoding") || is_gzip("x-content-encoding")))
        return read_service_body(inflate_gzip(request.body));
    return read_service_body(request.body);
}

std::optional<Value> simulation_json_body(const HttpRequest &request) {
    try {
        auto header = [&](std::string_view wanted) {
            std::string result;
            bool found = false;
            for (const auto &[key, value] : request.headers) {
                auto lower = key;
                for (auto &c : lower)
                    if (c >= 'A' && c <= 'Z')
                        c += 32;
                if (lower == wanted) {
                    if (found)
                        result += ',';
                    result += value;
                    found = true;
                }
            }
            return result;
        };
        const bool standard_gzip = header("content-encoding") == "gzip";
        const bool legacy_gzip = request.content_type == "application/x-gzip" || header("x-content-encoding") == "gzip";
        // The HTTP listener may already have decoded standard Content-Encoding.
        const bool needs_inflate = !request.standard_gzip_decoded && (standard_gzip || legacy_gzip);
        const auto body = needs_inflate ? inflate_gzip(request.body) : request.body;
        auto value = parse_osd_json(read_service_body(body));
        if (value.kind != Value::Kind::Map)
            return {};
        return value;
    } catch (const std::exception &) {
        return {};
    }
}
} // namespace opensim::network
