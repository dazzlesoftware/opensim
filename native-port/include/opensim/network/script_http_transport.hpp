// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/outbound_url_filter.hpp"
#include "opensim/network/web_http_clients.hpp"
#include <variant>

namespace opensim::network {

std::shared_ptr<framework::ConfigXmlProxy> make_script_http_proxy(std::string address, std::string_view exceptions);

struct ScriptHttpRequest {
    framework::ConfigUriObjectPtr uri;
    std::string method = "GET";
    std::string mime_type = "text/plain;charset=utf-8";
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
    std::int32_t max_body_bytes = 2048;
    bool verify_certificate = true;
    bool pragma_no_cache = false;
};

struct ScriptHttpReply {
    int status;
    std::string body;
};

using ScriptHttpAttempt = std::variant<std::monostate, ScriptHttpReply, framework::ConfigUriObjectPtr>;

class ScriptHttpTransport {
public:
    ScriptHttpTransport(std::shared_ptr<const framework::OutboundUrlFilter> filter,
                        WebHttpClients::HandlerFactory handlers, std::int32_t timeout_ms = 30000,
                        std::int32_t maximum_connections = 8,
                        std::shared_ptr<framework::ConfigXmlProxy> proxy = {});

    // Initial URL validation runs at submission. Redirect targets are filtered on the worker.
    std::optional<ScriptHttpReply> send(const ScriptHttpRequest& request,
                                        const std::function<bool()>& cancelled = {}) const;
    ScriptHttpAttempt send_attempt(const ScriptHttpRequest& request, unsigned redirects,
                                   const std::function<bool()>& cancelled = {}) const;

private:
    std::shared_ptr<const framework::OutboundUrlFilter> filter_;
    std::shared_ptr<framework::ConfigUriObjectBackend> uris_;
    std::shared_ptr<WebHttpClient> verified_;
    std::shared_ptr<WebHttpClient> unverified_;
};

}
