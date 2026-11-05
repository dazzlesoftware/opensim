// Copyright (c) Contributors, http://opensimulator.org/
// C++ adaptation of XMLRPCRequester in OpenSim/Framework/WebUtil.cs.
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/xml_rpc_requester.hpp"
#include "opensim/framework/tick_count.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/nini_xml_text_writer.hpp"
#include "opensim/framework/nini_xml_config.hpp"
#include <exception>
namespace opensim::network {
namespace {
using Text = std::u16string;
Text number(std::int32_t n) {
    return framework::utf16(std::to_string(n));
}
[[noreturn]] void null_reference() {
    throw NwcXmlRpcError("System.NullReferenceException",
                         u"Object reference not set to an instance of an object.");
}
const char* type(const Value& v) {
    switch (v.view().kind) {
    case Value::Kind::Map:
        return "System.Collections.Hashtable";
    case Value::Kind::Array:
        return "System.Collections.ArrayList";
    case Value::Kind::Integer:
        return "System.Int32";
    case Value::Kind::Long:
        return "System.Int64";
    case Value::Kind::Boolean:
        return "System.Boolean";
    case Value::Kind::Real:
        return "System.Double";
    case Value::Kind::Date:
        return "System.DateTime";
    case Value::Kind::Binary:
        return "System.Byte[]";
    default:
        return "System.String";
    }
}
class HttpClient final : public XmlRpcRequesterClient {
    std::shared_ptr<NwcXmlRpcHttpTransport> transport_;
    std::int32_t timeout_;
    bool closed_ = false;

  public:
    HttpClient(std::shared_ptr<NwcXmlRpcHttpTransport> t, std::int32_t n)
        : transport_(std::move(t)), timeout_(n > 0 ? n : 30000) {
        if (!transport_)
            throw std::invalid_argument("XML-RPC HTTP transport is required");
    }
    std::shared_ptr<NwcXmlRpcResponse> send(const std::shared_ptr<NwcXmlRpcRequest>& r,
                                            const std::optional<Text>& u) override {
        if (closed_)
            throw NwcXmlRpcError(
                "System.ObjectDisposedException",
                u"Cannot access a disposed object.\r\nObject name: 'System.Net.Http.HttpClient'.");
        return transport_->send(r, u, timeout_);
    }
    void close() override {
        closed_ = true;
    }
};
}
std::shared_ptr<XmlRpcRequesterClient> xml_rpc_http_client(std::shared_ptr<NwcXmlRpcHttpTransport> t,
                                                           std::int32_t timeout) {
    return std::make_shared<HttpClient>(std::move(t), timeout);
}
std::u16string xml_rpc_binary_to_ascii(std::u16string text) {
    for (auto& c : text)
        if (c < 32 || c > 127)
            c = u'.';
    return text;
}
XmlRpcRequester::XmlRpcRequester(XmlRpcRequesterServices s) : services_(std::move(s)) {
    auto& p = services_;
    if (!p.request_number || !p.set_request_number || !p.debug_level || !p.client || !p.debug || !p.error ||
        !p.info)
        throw std::invalid_argument("XMLRPCRequester requires counter, diagnostic and client services");
    if (!p.compact_xml)
        p.compact_xml = compact_nwc_xml_rpc_diagnostic;
    if (!p.outgoing_detail) {
        auto level = p.debug_level;
        auto log = p.debug;
        p.outgoing_detail = [level, log](auto id, const auto& text) {
            xml_rpc_log_detail(level, log, u"SEND " + number(id) + u": ", text);
        };
    }
    if (!p.response_detail) {
        auto level = p.debug_level;
        auto log = p.debug;
        p.response_detail = [level, log](auto id, const auto& text) {
            xml_rpc_log_detail(level, log, u"RESPONSE " + number(id) + u": ", text);
        };
    }
    if (!p.tick_count)
        p.tick_count = framework::environment_tick_count;
    if (!p.elapsed)
        p.elapsed = [](auto start) {
            return framework::environment_tick_count_subtract(framework::environment_tick_count(), start);
        };
    if (!p.request_text)
        p.request_text = [](const auto& r) {
            return framework::utf16_replacing(nwc_xml_rpc_request_bytes(r, true));
        };
    if (!p.response_text)
        p.response_text = [](const auto& r) {
            if (!r)
                null_reference();
            return framework::utf16_replacing(nwc_xml_rpc_response_bytes(r, true));
        };
    if (!p.exception_message)
        p.exception_message = [](std::exception_ptr e) -> Text {
            try {
                std::rethrow_exception(e);
            } catch (const NwcXmlRpcError& ex) {
                return ex.message;
            } catch (const framework::XmlTextWriterError& ex) {
                return ex.message;
            } catch (const framework::XmlEncoderError& ex) {
                return ex.message;
            } catch (const framework::NiniXmlProviderError& ex) {
                return ex.message;
            } catch (const std::exception& ex) {
                return framework::utf16_replacing(ex.what());
            }
        };
}
std::shared_ptr<Value> XmlRpcRequester::send_request(std::shared_ptr<Value> data,
                                                     const std::optional<Text>& method,
                                                     const std::optional<Text>& url) {
    auto& s = services_;
    const auto reqnum = s.request_number();
    s.set_request_number(framework::wrapping_add(reqnum, 1));
    if (s.debug_level() >= 3)
        s.debug(u"[LOGHTTP]: HTTP OUT " + number(reqnum) + u" XML-RPC '" + method.value_or(u"") + u"' to " +
                url.value_or(u""));
    const auto start = s.tick_count();
    std::optional<Text> response_text;
    std::shared_ptr<XmlRpcRequesterClient> client;
    std::shared_ptr<Value> result;
    std::exception_ptr pending;
    try {
        auto args = std::make_shared<NwcXmlRpcRequest::Parameters>();
        args->push_back(std::move(data));
        auto req = std::make_shared<NwcXmlRpcRequest>(method, args);
        if (s.debug_level() >= 5) {
            auto text = s.request_text(req);
            text = s.compact_xml(text);
            s.outgoing_detail(reqnum, text);
        }
        client = s.client(-1);
        if (!client)
            null_reference();
        auto response = client->send(req, url);
        try {
            if (s.debug_level() >= 5) {
                response_text = s.response_text(response);
                response_text = s.compact_xml(*response_text);
                s.response_detail(reqnum, *response_text);
            }
        } catch (...) {
            s.error(u"[LOGHTTP]: Error parsing XML-RPC response: " +
                    s.exception_message(std::current_exception()));
        }
        if (!response)
            null_reference();
        if (response->is_fault) {
            // Evaluate both getters even if the logger discards the message.
            const auto code = response->fault_code();
            const auto text = response->fault_string();
            s.debug(u"[LOGHTTP]: XML-RPC request " + number(reqnum) + u" '" + method.value_or(u"") +
                    u"' to " + url.value_or(u"") + u" FAILED: FaultCode=" + number(code) + u", " +
                    text.value_or(u""));
        } else {
            result = response->value;
            if (result && (result->view().kind == Value::Kind::NullReference ||
                           result->view().kind == Value::Kind::Undefined))
                result.reset();
            if (result && result->view().kind != Value::Kind::Map)
                throw NwcXmlRpcError("System.InvalidCastException",
                                     u"Unable to cast object of type '" + framework::utf16(type(*result)) +
                                         u"' to type 'System.Collections.Hashtable'.");
        }
    } catch (...) {
        pending = std::current_exception();
    }
    // Do not put this in a noexcept C++ destructor: source Dispose can throw,
    // suppress the pending exception, and prevent the remaining finally body.
    if (client)
        client->close();
    auto elapsed = s.elapsed(start);
    if (elapsed > 3000)
        s.info(u"[LOGHTTP]: Slow XML-RPC request " + number(reqnum) + u" '" + method.value_or(u"") +
               u"' to " + url.value_or(u"") + u" took " + number(elapsed) + u"ms, " +
               (response_text ? response_text->substr(0, 200) : u""));
    else if (s.debug_level() >= 4)
        s.debug(u"[LOGHTTP]: HTTP OUT " + number(reqnum) + u" took " + number(elapsed) + u"ms");
    if (pending)
        std::rethrow_exception(pending);
    return result;
}
}
