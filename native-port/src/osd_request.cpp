// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/osd_request.hpp"
#include "opensim/network/osd_service_http.hpp"
#include "opensim/network/web_http_clients.hpp"
#include "opensim/network/sdk_osd_stream.hpp"
#include "opensim/network/gzip.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/stream_reader_text.hpp"
#include <httplib.h>
#include <atomic>
namespace opensim::network {
namespace {
Value error(std::string text) {
    return Value::object({{"Success", "False"}, {"Message", "Service request failed: " + text}});
}

}
Value canonical_osd_response(std::string_view body) {
    auto lower = std::string(body);
    for (auto& c : lower)
        if (c >= 'A' && c <= 'Z')
            c += 32;
    const bool boolean = lower == "true" || lower == "false";
    bool success = lower != "false";
    Value parsed = Value::object({});
    if (!boolean)
        try {
            auto value = deserialize_sdk_osd_text(framework::utf16_replacing(body));
            if (value.kind == Value::Kind::Map)
                parsed = std::move(value);
        } catch (const std::exception&) {
        }
    Value result = Value::object({});
    result.set("Success", Value(success));
    result.set("success", Value(success));
    result.set("_RawResult", Value(std::string(body)));
    result.set("_Result", std::move(parsed));
    return result;
}
namespace {
Value send_osd_request(const std::string& url, const Value* data, const std::string& method, bool compressed,
                       bool keepalive, int timeout_milliseconds) {
    static std::atomic<std::uint32_t> request_number{0};
    const auto number = request_number.fetch_add(1);
    try {
        const auto scheme = url.find("://");
        if (scheme == std::string::npos)
            return error("Invalid URI");
        const auto end = url.find_first_of("/?#", scheme + 3);
        auto target = end == std::string::npos ? std::string("/") : url.substr(end);
        if (target.starts_with('?'))
            target.insert(0, "/");
        if (auto fragment = target.find('#'); fragment != std::string::npos)
            target.resize(fragment);
        if (target.empty())
            target = "/";
        httplib::Client client(url.substr(0, end));
        if (!client.is_valid())
            return error("Invalid URI");
        client.set_connection_timeout(std::chrono::milliseconds(timeout_milliseconds));
        client.set_read_timeout(std::chrono::milliseconds(timeout_milliseconds));
        client.set_write_timeout(std::chrono::milliseconds(timeout_milliseconds));
        client.set_max_timeout(timeout_milliseconds);
        client.set_follow_location(true);
        client.set_keep_alive(keepalive);
        httplib::Request request;
        request.method = method;
        request.path = target;
        if (data) {
            request.body = osd_json(*data);
            request.headers.emplace("Content-Type", "application/json");
        }
        request.headers.emplace("opensim-request-id", std::to_string(number));
        request.headers.emplace("Connection", keepalive ? "Keep-Alive" : "close");
        if (keepalive)
            request.headers.emplace("Keep-Alive", "timeout=30, max=10");
        if (data && compressed) {
            request.body = gzip_compress(request.body);
            request.headers.emplace("X-Content-Encoding", "gzip");
        }
        auto response = client.send(request);
        if (!response)
            return error("[499] " + httplib::to_string(response.error()));
        if (response->status < 200 || response->status >= 300) {
            const auto status = std::to_string(response->status);
            const auto suffix = framework::trim_utf8(response->reason).empty()
                                    ? std::string(".")
                                    : " (" + response->reason + ").";
            return error("[" + status + "] Response status code does not indicate success: " + status +
                         suffix);
        }
        auto decoded = framework::decode_stream_reader_text(
            {reinterpret_cast<const std::uint8_t*>(response->body.data()), response->body.size()});
        return canonical_osd_response(framework::utf8(decoded.text));
    } catch (const std::exception& e) {
        return error(e.what());
    }
}
}
Value osd_request(const std::string& url, const Value& data, const std::string& method, bool compressed,
                  bool keepalive) {
    return send_osd_request(url, &data, method, compressed, keepalive, 30000);
}
Value osd_timed_request(const std::string& url, const Value& data, const std::string& method,
                        int timeout_milliseconds, bool compressed, bool keepalive) {
    return send_osd_request(url, &data, method, compressed, keepalive, timeout_milliseconds);
}
Value osd_json_rpc_request(const std::string& url, const Value& data, int timeout_milliseconds) {
    static auto requester = [] {
        auto clients = std::make_shared<WebHttpClients>(native_web_http_handler_factory());
        clients->setup(false, false, nullptr, 32);
        OsdServiceNativeHttp transport(clients, clients->shared_handler.load()->error_factory());
        OsdServiceServices services;
        services.next_request_number = [] {
            static std::atomic<std::int32_t> number{0};
            return number.fetch_add(1, std::memory_order_relaxed);
        };
        services.debug_level = [] { return 0; };
        services.debug = services.info = [](const auto&) {};
        transport.bind(services);
        return OsdServiceRequester(std::move(services));
    }();
    return requester.send(framework::utf16_replacing(url), &data, u"POST", timeout_milliseconds, false, true);
}
Value osd_delete_request(const std::string& url, int timeout_milliseconds) {
    return send_osd_request(url, nullptr, "DELETE", false, false, timeout_milliseconds);
}

}
