// Copyright (c) Contributors, http://opensimulator.org/
// C++ adaptation of WebUtil.ServiceOSDRequest in OpenSim/Framework/WebUtil.cs.
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/osd_service_requester.hpp"
#include "opensim/network/xml_rpc_requester.hpp"
#include "opensim/network/sdk_osd_stream.hpp"
#include "opensim/network/gzip.hpp"
#include "opensim/framework/tick_count.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include <bit>
namespace opensim::network {
namespace {
using Text = std::u16string;
using Nullable = std::optional<Text>;
Text number(std::int32_t n) {
    return framework::utf16(std::to_string(n));
}
[[noreturn]] void null_reference() {
    throw OsdServiceError("System.NullReferenceException",
                          u"Object reference not set to an instance of an object.");
}
Value boxed_text(Text text) {
    Value v(framework::utf8(text));
    v.sdk_string = std::make_shared<const Text>(std::move(text));
    return v;
}
Value error_map(Text message) {
    auto result = Value::object({});
    result.set("Success", Value("False"));
    result.set("Message", boxed_text(u"Service request failed: " + message));
    return result;
}
}
OsdServiceError::OsdServiceError(std::string type, Text text, std::optional<int> code)
    : runtime_error(framework::utf8(text)), source_type(std::move(type)), message(std::move(text)),
      status(code) {
}
std::int32_t osd_service_receive_timeout(std::optional<std::int64_t> length, bool keepalive) {
    if (!length)
        return keepalive ? 5000 : 30000;
    auto t = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(*length / 1024));
    return t > 55000 ? 60000 : framework::wrapping_add(t, 5000);
}
Value canonical_osd_response_utf16(std::u16string_view text) {
    auto bytes = framework::utf8(text);
    bool is_false = framework::ordinal_equals_ignore_case(bytes, "false"),
         is_true = framework::ordinal_equals_ignore_case(bytes, "true");
    auto parsed = Value::object({});
    if (!is_true && !is_false) {
        try {
            auto value = deserialize_sdk_osd_text(text);
            if (value.kind == Value::Kind::Map)
                parsed = std::move(value);
        } catch (...) {
        }
    }
    auto result = Value::object({});
    result.set("Success", Value(!is_false));
    result.set("success", Value(!is_false));
    result.set("_RawResult", boxed_text(Text(text)));
    result.set("_Result", std::move(parsed));
    return result;
}
OsdServiceRequester::OsdServiceRequester(OsdServiceServices services) : services_(std::move(services)) {
    auto& s = services_;
    const bool has_counter = s.next_request_number || (s.request_number && s.set_request_number);
    if (!has_counter || !s.debug_level || !s.debug || !s.info || !s.client || !s.request || !s.reader ||
        !s.cancellation)
        throw std::invalid_argument(
            "ServiceOSDRequest requires counter, diagnostics and actual HTTP/reader/cancellation factories");
    if (!s.tick_count)
        s.tick_count = framework::environment_tick_count;
    if (!s.elapsed)
        s.elapsed = [](auto before) {
            return framework::environment_tick_count_subtract(framework::environment_tick_count(), before);
        };
    if (!s.outgoing_detail) {
        auto level = s.debug_level;
        auto log = s.debug;
        s.outgoing_detail = [level, log](const auto& method, auto id, const auto& text) {
            xml_rpc_log_detail(level, log, method.value_or(u"") + u" " + number(id) + u": ", text);
        };
    }
    if (!s.response_detail) {
        auto level = s.debug_level;
        auto log = s.debug;
        s.response_detail = [level, log](auto id, const auto& text) {
            xml_rpc_log_detail(level, log, u"RESPONSE " + number(id) + u": ", text);
        };
    }
    if (!s.serialize_text)
        s.serialize_text = [](const auto& data) { return framework::utf16_replacing(osd_json(data)); };
    if (!s.serialize_bytes)
        s.serialize_bytes = [](const auto& data) { return osd_json(data); };
    if (!s.utf8_bytes)
        s.utf8_bytes = [](const auto& text) { return framework::utf8(text); };
    if (!s.compress)
        s.compress = gzip_compress;
    if (!s.canonicalize)
        s.canonicalize = [](const auto& text) { return canonical_osd_response_utf16(text); };
    if (!s.describe)
        s.describe = [](std::exception_ptr error) -> OsdServiceException {
            try {
                std::rethrow_exception(error);
            } catch (const OsdServiceError& e) {
                return {e.message, e.source_type == "System.Net.Http.HttpRequestException", e.status};
            } catch (const std::exception& e) {
                return {framework::utf16_replacing(e.what()), false, {}};
            }
        };
}
Value OsdServiceRequester::send(const Nullable& url, const Value* data, const Nullable& method,
                                std::int32_t timeout, bool compressed, bool rpc, bool keepalive) {
    auto& s = services_;
    const auto id = s.next_request_number ? s.next_request_number() : s.request_number();
    if (!s.next_request_number)
        s.set_request_number(framework::wrapping_add(id, 1));
    if (s.debug_level() >= 3)
        s.debug(u"[LOGHTTP]: HTTP OUT " + number(id) + u" JSON-RPC " + method.value_or(u"") + u" to " +
                url.value_or(u""));
    Text error = u"unknown error";
    auto ticks = s.tick_count();
    std::int32_t sent = 0, received = 0;
    auto body_timed_out = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<OsdServiceResponse> response;
    std::shared_ptr<OsdServiceHttpRequest> request;
    std::shared_ptr<OsdServiceHttpClient> client;
    std::optional<Value> result;
    std::exception_ptr pending;
    try {
        try {
            client = s.client(timeout);
            request = s.request(method, url);
            if (!request)
                null_reference();
            if (data) {
                std::string bytes;
                if (s.debug_level() >= 5) {
                    auto text = s.serialize_text(*data);
                    s.outgoing_detail(method, id, text);
                    bytes = s.utf8_bytes(text);
                } else
                    bytes = s.serialize_bytes(*data);
                if (!bytes.empty()) {
                    if (compressed) {
                        bytes = s.compress(bytes);
                        request->headers.emplace_back("X-Content-Encoding", u"gzip");
                    }
                    sent = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(bytes.size()));
                    request->body = std::move(bytes);
                    request->headers.emplace_back("Content-Type",
                                                  rpc ? u"application/json-rpc" : u"application/json");
                    request->headers.emplace_back("Content-Length", number(sent));
                }
            }
            // ExpectContinue and TransferEncodingChunked are explicitly false in the
            // source. Adapters must not generate Expect or Transfer-Encoding headers.
            if (keepalive) {
                request->headers.emplace_back("Keep-Alive", u"timeout=30, max=10");
                request->headers.emplace_back("Connection", u"Keep-Alive");
            } else
                request->headers.emplace_back("Connection", u"close");
            request->headers.emplace_back("opensim-request-id", number(id));
            if (!client)
                null_reference();
            response = client->send(request);
            if (!response)
                null_reference();
            response->ensure_success();
            auto stream = response->stream();
            if (stream) {
                auto reader = s.reader(stream);
                std::exception_ptr reader_error;
                try {
                    auto timer =
                        s.cancellation(osd_service_receive_timeout(response->content_length(), keepalive));
                    std::exception_ptr timer_error;
                    Text text;
                    try {
                        if (!timer)
                            null_reference();
                        timer->on_cancel([body_timed_out, stream] {
                            body_timed_out->store(true);
                            stream->close();
                        });
                        if (!reader)
                            null_reference();
                        text = reader->read_to_end();
                    } catch (...) {
                        timer_error = std::current_exception();
                    }
                    if (timer)
                        timer->close();
                    if (timer_error)
                        std::rethrow_exception(timer_error);
                    if (s.debug_level() >= 5)
                        s.response_detail(id, text);
                    received = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(text.size()));
                    result = s.canonicalize(text);
                } catch (...) {
                    reader_error = std::current_exception();
                }
                if (reader)
                    reader->close();
                if (reader_error)
                    std::rethrow_exception(reader_error);
            }
        } catch (...) {
            result.reset();
            auto ex = s.describe(std::current_exception());
            if (ex.http_request)
                error = u"[" + number(ex.status.value_or(499)) + u"] " + ex.message;
            else {
                error = ex.message;
                s.debug(u"[WEB UTIL]: Exception making request: " + error);
            }
        }
    } catch (...) {
        pending = std::current_exception();
    }
    // C# finally disposal is outside the error-map catch and can escape/mask it.
    if (request)
        request->close();
    if (response)
        response->close();
    if (client)
        client->close();
    auto elapsed = s.elapsed(ticks);
    if (body_timed_out->load())
        error = u"[WEB UTIL]: receive timeout " + number(id) + u" " + method.value_or(u"") + u" " +
                url.value_or(u"") + u" (" + number(elapsed) + u"ms); " + error;
    else if (elapsed > 3000)
        s.info(u"[WEB UTIL]: SvcOSD " + number(id) + u" " + method.value_or(u"") + u" " + url.value_or(u"") +
               u" took " + number(elapsed) + u"ms, " + number(sent) + u"/" + number(received) + u"bytes");
    else if (s.debug_level() >= 4)
        s.debug(u"[LOGHTTP]: HTTP OUT " + number(id) + u" took " + number(elapsed) + u"ms");
    if (pending)
        std::rethrow_exception(pending);
    if (result)
        return std::move(*result);
    s.debug(u"[LOGHTTP]: request " + number(id) + u" " + method.value_or(u"") + u" to " + url.value_or(u"") +
            u" FAILED: " + error);
    return error_map(std::move(error));
}
}
