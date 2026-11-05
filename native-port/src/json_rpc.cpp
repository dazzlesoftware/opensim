// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/json_rpc.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/network/sdk_json_reader.hpp"
#include "opensim/network/sdk_llsd_xml.hpp"
#include "opensim/framework/stream_reader_text.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include <algorithm>
namespace opensim::server {
using namespace network;
namespace {
[[noreturn]] void null_reference() {
    throw std::runtime_error("Object reference not set to an instance of an object.");
}
Value& required(const std::shared_ptr<Value>& value) {
    if (!value)
        null_reference();
    return *value;
}
JsonRpcError& required(const std::shared_ptr<JsonRpcError>& value) {
    if (!value)
        null_reference();
    return *value;
}
JsonRpcResponse& required(const std::shared_ptr<JsonRpcResponse>& value) {
    if (!value)
        null_reference();
    return *value;
}
Value text_value(const std::optional<std::u16string>& text) {
    auto value = text.value_or(u"");
    Value result(framework::utf8(value));
    result.sdk_string = std::make_shared<const std::u16string>(std::move(value));
    return result;
}
}
JsonRpcParseError::JsonRpcParseError(std::string text)
    : std::runtime_error(text), source_message(framework::utf16_replacing(text)) {
}
JsonRpcParseError::JsonRpcParseError(std::u16string text)
    : std::runtime_error(framework::utf8(text)), source_message(std::move(text)) {
}
std::int32_t JsonRpcError::code() const {
    return osd_integer(required(values).get("code"));
}
void JsonRpcError::code(std::int32_t code) {
    required(values).set("code", Value(code));
}
std::optional<std::u16string> JsonRpcError::message() const {
    auto& map = required(values);
    if (!map.contains("message"))
        return {};
    return osd_string_utf16(map.get("message"));
}
void JsonRpcError::message(const std::optional<std::u16string>& text) {
    required(values).set("message", text_value(text));
}
std::u16string JsonRpcResponse::id() const {
    return osd_string_utf16(required(reply).get("id"));
}
void JsonRpcResponse::id(const std::optional<std::u16string>& text) {
    required(reply).set("id", text_value(text));
}
std::u16string JsonRpcResponse::version() const {
    return osd_string_utf16(required(reply).get("jsonrpc"));
}
void JsonRpcResponse::version(const std::optional<std::u16string>& text) {
    required(reply).set("jsonrpc", text_value(text));
}
std::string JsonRpcResponse::serialize() {
    if (result)
        required(reply).set("result", Value::alias(result));
    if (required(error).code() != 0)
        required(reply).set("error", Value::alias(error->values));
    try {
        return osd_json(required(reply));
    } catch (...) {
        return {};
    }
}
JsonRpcDispatcher::JsonRpcDispatcher(Decoder decoder, Logger logger, Disposer disposer)
    : decoder_(std::move(decoder)), logger_(std::move(logger)), disposer_(std::move(disposer)) {
    default_decoder_ = !decoder_;
    if (!decoder_)
        decoder_ = [](std::string_view bytes) {
            auto text = framework::decode_stream_reader_text(
                {reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
            try {
                return parse_osd_json_utf16(text.text);
            } catch (const SdkJsonError& e) {
                throw JsonRpcParseError(e.message);
            }
        };
}
void JsonRpcDispatcher::logger(Logger value) {
    std::lock_guard lock(mutex_);
    logger_ = std::move(value);
}
bool JsonRpcDispatcher::add(std::u16string method, Handler handler) {
    std::lock_guard lock(mutex_);
    if (handlers_.contains(method))
        return false;
    handlers_[method] = std::move(handler);
    return true;
}
JsonRpcDispatcher::Handler JsonRpcDispatcher::get(std::u16string_view method) const {
    std::lock_guard lock(mutex_);
    auto found = handlers_.find(std::u16string(method));
    return found == handlers_.end() ? Handler{} : found->second;
}
void JsonRpcDispatcher::remove(std::u16string_view method) {
    std::lock_guard lock(mutex_);
    handlers_.erase(std::u16string(method));
}
std::vector<std::u16string> JsonRpcDispatcher::keys() const {
    std::lock_guard lock(mutex_);
    std::vector<std::u16string> result;
    for (auto& entry : handlers_)
        result.push_back(entry.first);
    return result;
}
HttpResponse JsonRpcDispatcher::handle(const HttpRequest& request) {
    auto response = std::make_shared<JsonRpcResponse>();
    std::optional<Value> decoded;
    auto decode = [&] {
        if (!default_decoder_)
            return decoder_(request.body);
        Value input;
        try {
            input = decoder_(request.body);
        } catch (...) {
            if (disposer_)
                disposer_();
            throw;
        }
        if (disposer_)
            disposer_();
        return input;
    };
    try {
        auto input = decode();
        if (input.view().kind != Value::Kind::Map)
            throw std::invalid_argument("JSON-RPC request requires OSDMap");
        decoded = std::move(input);
    } catch (const JsonRpcParseError& e) {
        response->error->code(JsonRpcErrorCode::InternalError);
        response->error->message(e.source_message);
    }
    if (!default_decoder_ && disposer_)
        disposer_();
    if (decoded) {
        auto& map = *decoded;
        if (map.contains("id"))
            response->id(osd_string_utf16(map.get("id")));
        if (map.contains("jsonrpc") && osd_string_utf16(map.get("jsonrpc")) == u"2.0") {
            response->version(u"2.0");
            auto name = osd_string_utf16(map.get("method"));
            Handler method;
            bool defined = false;
            if (!name.empty() && !std::all_of(name.begin(), name.end(), framework::unicode_whitespace)) {
                std::lock_guard lock(mutex_);
                auto found = handlers_.find(name);
                if (found != handlers_.end()) {
                    defined = true;
                    method = found->second;
                }
            }
            if (defined) {
                try {
                    if (!method)
                        null_reference();
                    if (!method(map, response) && required(required(response).error).code() == 0)
                        required(required(response).error).code(JsonRpcErrorCode::InternalError);
                } catch (const std::exception& e) {
                    auto message = "[BASE HTTP SERVER]: Json-Rpc Handler Error method " +
                                   framework::utf8(name) + " - " + e.what();
                    Logger log;
                    {
                        std::lock_guard lock(mutex_);
                        log = logger_;
                    }
                    if (log)
                        log(message);
                    auto& error = required(required(response).error);
                    error.code(JsonRpcErrorCode::InternalError);
                    error.message(framework::utf16_replacing(message));
                }
            } else {
                response->error->code(JsonRpcErrorCode::InvalidRequest);
                response->error->message(u"No handler defined for " + name);
            }
        } else {
            response->error->code(JsonRpcErrorCode::InvalidRequest);
            response->error->message(u"Must be valid json-rpc 2.0 see: http://www.jsonrpc.org/specification");
        }
    }
    HttpResponse result;
    result.status = 200;
    result.content_type = "text/html;charset=UTF-8";
    result.keep_alive = true;
    if (auto connection = request.headers.find("connection"); connection != request.headers.end())
        result.keep_alive = !framework::invariant_equals_ignore_case(connection->second, "close");
    result.body = required(response).serialize();
    return result;
}
}
