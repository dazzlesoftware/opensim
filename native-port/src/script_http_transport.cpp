// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/script_http_transport.hpp"
#include "opensim/framework/config_http_headers.hpp"
#include "opensim/framework/config_http_location.hpp"
#include "opensim/framework/config_generic_uri_object.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/text.hpp"
#include <httplib.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <stdexcept>

namespace opensim::network {
namespace {

using Clock = std::chrono::steady_clock;
using namespace opensim::framework;

ConfigUriText text(std::string_view value) {
    return std::make_shared<const std::u16string>(utf16_replacing(value));
}

bool header_is(std::string_view name, std::string_view expected) {
    return std::ranges::equal(name, expected, [](char actual, char other) {
        return httplib::detail::case_ignore::to_lower(actual) == httplib::detail::case_ignore::to_lower(other);
    });
}

bool redirect_status(int status) {
    return status == 301 || status == 302 || status == 303 || status == 307;
}

bool is_content_header(std::string_view name) {
    constexpr std::array<std::string_view, 10> content_headers{
        "content-disposition", "content-encoding", "content-language", "content-length",
        "content-location", "content-md5", "content-range", "content-type", "expires", "last-modified",
    };
    return std::ranges::any_of(content_headers, [&](auto header) { return header_is(name, header); });
}

std::string response_text(std::string_view bytes) {
    const auto decoded = utf16_replacing(bytes);
    std::u16string normalized;
    normalized.reserve(decoded.size());
    for (std::size_t index = 0; index < decoded.size(); ++index) {
        const auto value = decoded[index];
        if (value == u'\r' && index + 1 < decoded.size() && decoded[index + 1] == u'\n')
            ++index;
        normalized += value == u'\r' || value == u'\f' || value == u'\u0085' ||
                      value == u'\u2028' || value == u'\u2029' ? u'\n' : value;
    }
    return utf8(normalized);
}

std::int32_t narrow_length(std::int64_t value) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value));
}

}

ScriptHttpTransport::ScriptHttpTransport(std::shared_ptr<const framework::OutboundUrlFilter> filter,
                                         WebHttpClients::HandlerFactory handlers, std::int32_t timeout_ms,
                                         std::int32_t maximum_connections,
                                         std::shared_ptr<framework::ConfigXmlProxy> proxy)
    : filter_(std::move(filter)), uris_(framework::native_config_uri_object_backend()) {
    if (!filter_ || !handlers)
        throw std::invalid_argument("Script HTTP requires a URL filter and HTTP handler factory");
    timeout_ms = std::clamp(timeout_ms, 200, 60000);
    WebHttpHandlerSettings settings;
    settings.allow_auto_redirect = false;
    settings.no_verify_cert_chain = true;
    settings.use_proxy = bool(proxy);
    settings.proxy = std::move(proxy);
    settings.pool.connect_timeout = std::chrono::milliseconds(timeout_ms);
    settings.pool.max_connections_per_server = std::min(maximum_connections, 10);
    verified_ = std::make_shared<WebHttpClient>(handlers(settings), timeout_ms, 32768);
    settings.no_verify_cert_hostname = true;
    unverified_ = std::make_shared<WebHttpClient>(handlers(settings), timeout_ms, 32768);
}

std::optional<ScriptHttpReply> ScriptHttpTransport::send(
    const ScriptHttpRequest& request, const std::function<bool()>& cancelled) const {
    auto current = request;
    for (unsigned redirects = 0;; ++redirects) {
        auto attempt = send_attempt(current, redirects, cancelled);
        if (auto* reply = std::get_if<ScriptHttpReply>(&attempt))
            return std::move(*reply);
        if (auto* next = std::get_if<ConfigUriObjectPtr>(&attempt))
            current.uri = std::move(*next);
        else
            return std::nullopt;
    }
}

ScriptHttpAttempt ScriptHttpTransport::send_attempt(
    const ScriptHttpRequest& request, unsigned redirects, const std::function<bool()>& cancelled) const {
    const auto stopped = [&] { return cancelled && cancelled(); };
    if (stopped())
        return {};
    if (trim_utf8(request.method).empty())
        return ScriptHttpReply{499,
            "The value cannot be an empty string or composed entirely of whitespace. (Parameter 'method')"};
    if (!httplib::detail::fields::is_token(request.method))
        return ScriptHttpReply{499, "The format of the HTTP method is invalid."};
    const auto& uri = request.uri;
    const auto& client = request.verify_certificate ? verified_ : unverified_;
    const ConfigHttpLocationAdapter locations(*uris_);
    ConfigXmlHttpOperation operation;
    operation.method = request.method;
    constexpr std::array<std::string_view, 9> known_methods{
        "get", "head", "post", "put", "delete", "connect", "options", "trace", "patch",
    };
    if (std::ranges::any_of(known_methods, [&](auto method) { return header_is(request.method, method); })) {
        for (auto& character : operation.method)
            if (character >= 'a' && character <= 'z')
                character -= 'a' - 'A';
    }
    operation.body = request.body;
    const auto add_header = [&](std::string_view name, std::string_view value) {
        const auto existing = std::ranges::find_if(operation.headers, [&](const auto& header) {
            return header_is(header.first, name);
        });
        if (existing == operation.headers.end()) {
            operation.headers.emplace_back(name, text(value));
            return;
        }
        const auto separator = header_is(name, "user-agent") ? u" " :
                               header_is(name, "cookie") ? u"; " : u", ";
        existing->second = nini_writer_text(*existing->second + separator + *text(value));
    };
    bool has_content_headers = false;
    std::vector<std::int64_t> content_lengths;
    std::vector<std::u16string> transfer_encodings;
    for (const auto& [name, value] : request.headers) {
        // HttpRequestHeaders rejects Allow as content-only. The managed script
        // worker does not route it to HttpContentHeaders.
        if (!httplib::detail::fields::is_field_name(name) || header_is(name, "allow"))
            continue;
        has_content_headers |= is_content_header(name);
        if (header_is(name, "content-length")) {
            if (const auto length = config_http_content_length({*text(value)}))
                content_lengths.push_back(*length);
            continue;
        }
        // Keep Host values separate for the typed Host parser in the transport.
        if (header_is(name, "host"))
            operation.headers.emplace_back(name, text(value));
        else if (header_is(name, "transfer-encoding"))
            transfer_encodings.push_back(*text(value));
        else
            add_header(name, value);
    }
    for (const auto& coding : config_http_disable_chunked_request_encoding(transfer_encodings))
        add_header("Transfer-Encoding", utf8(coding));
    add_header("Connection", "close");
    if (request.pragma_no_cache)
        add_header("Pragma", "no-cache");
    if (!request.body.empty() || has_content_headers) {
        const auto body_size = static_cast<std::int64_t>(request.body.size());
        if (!content_lengths.empty() && content_lengths.front() != body_size) {
            const auto promised = content_lengths.front();
            operation.validate_request_content = [body_size, promised] {
                if (promised < body_size)
                    throw std::runtime_error(
                        "Unable to write content to request stream; content would exceed Content-Length.");
                throw std::runtime_error("Sent " + std::to_string(body_size) +
                    " request content bytes, but Content-Length promised " + std::to_string(promised) + ".");
            };
        }
        if (!request.body.empty())
            add_header("Content-Type", request.mime_type);
        if (content_lengths.empty() || !request.body.empty())
            content_lengths.push_back(body_size);
        std::string length_header;
        for (const auto length : content_lengths) {
            if (!length_header.empty())
                length_header += ", ";
            length_header += std::to_string(length);
        }
        operation.headers.emplace_back("Content-Length", text(length_header));
    }
    operation.request_interrupted = stopped;

    ConfigHttpResponseHead head;
    std::optional<Clock::time_point> deadline;
    bool body_timed_out = false;
    const auto timed_out = [&] { return deadline && Clock::now() >= *deadline; };
    operation.response_headers = [&](const ConfigHttpResponseHead& response) { head = response; };
    operation.body_interrupted = [&] { return stopped() || timed_out(); };
    ScriptHttpReply reply{499, {}};
    try {
        auto body = client->send_headers(uri, operation);
        if (stopped())
            return {};
        reply.status = head.status == 0 ? 499 : head.status;
        if (body) {
            std::vector<std::u16string> lengths;
            for (const auto& header : head.headers)
                if (header_is(header.name, "content-length"))
                    lengths.push_back(header.value);
            const auto length = config_http_content_length(lengths);
            const auto kib = length ? narrow_length(*length / 1024) : 0;
            const auto receive_ms = !length ? 30000 : kib > 55000 ? 60000 : kib + 5000;
            if (receive_ms < -1)
                throw std::out_of_range(
                    "Specified argument was out of the range of valid values. (Parameter 'millisecondsDelay')");
            if (receive_ms >= 0)
                deadline = Clock::now() + std::chrono::milliseconds(receive_ms);
            auto size = length ? narrow_length(*length) : -1;
            if (size < 0 || size > request.max_body_bytes)
                size = request.max_body_bytes;
            if (size < 0)
                throw std::overflow_error("Arithmetic operation resulted in an overflow.");
            std::string bytes(static_cast<std::size_t>(size), '\0');
            auto remaining = std::span(reinterpret_cast<std::uint8_t*>(bytes.data()), bytes.size());
            while (!remaining.empty() && !stopped()) {
                const auto count = body->read(remaining);
                if (count <= 0)
                    break;
                remaining = remaining.subspan(static_cast<std::size_t>(count));
            }
            body_timed_out = timed_out();
            deadline.reset();
            bytes.resize(bytes.size() - remaining.size());
            body->close();
            reply.body = response_text(bytes);
        }
    } catch (const ConfigXmlHttpError& error) {
        reply = {error.status.value_or(499), error.what()};
    } catch (const std::exception& error) {
        reply = {499, error.what()};
    }
    if (stopped())
        return {};
    if (body_timed_out || timed_out())
        return ScriptHttpReply{408, "Request timeout"};
    if (!redirect_status(reply.status))
        return reply;
    if (redirects >= 10)
        return ScriptHttpReply{499, "Number of redirects exceeded max redirects"};
    ConfigUriObjectPtr next;
    for (const auto& header : head.headers) {
        if (header_is(header.name, "location")) {
            next = locations.parse_value(std::make_shared<const std::u16string>(header.value));
            break;
        }
    }
    if (!next)
        return ScriptHttpReply{499, "HTTP redirect code but no location header"};
    if (!next->is_absolute()) {
        const auto previous = uri->http_components();
        auto absolute = *previous->scheme() + u"://" + *previous->dns_safe_host() + u":" +
            *text(std::to_string(previous->port())) + u"/" + *next->original_string();
        next = uris_->try_create(std::make_shared<const std::u16string>(std::move(absolute)),
                                ConfigUriObjectKind::RelativeOrAbsolute);
        if (!next)
            return ScriptHttpReply{499, "HTTP redirect code but invalid location header"};
    }
    const auto target = next->http_components();
    const bool allowed = target ? filter_->allows(*target) :
        config_generic_uri_owns(next) &&
            filter_->allows(utf8(*config_generic_uri_component(next, ConfigGenericUriComponent::Host)),
                            config_generic_uri_port(next));
    if (!allowed)
        return ScriptHttpReply{499, "URL from HTTP redirect blocked: " + utf8(*next->absolute_uri())};
    return next;
}

}
