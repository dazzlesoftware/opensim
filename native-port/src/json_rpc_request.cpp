// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/json_rpc_request.hpp"
#include "opensim/network/osd_request.hpp"
#include "opensim/network/sdk_llsd_xml.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/uuid.hpp"
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
    return const_cast<Value&>(value->view());
}
Value text(const JsonRpcRequestManager::Text& source) {
    auto value = source.value_or(u"");
    Value result(framework::utf8(value));
    result.sdk_string = std::make_shared<const std::u16string>(std::move(value));
    return result;
}
Value boxed(const std::shared_ptr<Value>& value) {
    return value ? Value::alias(value) : Value::null_reference();
}
std::u16string json(const std::shared_ptr<Value>& value) {
    return framework::utf16_replacing(osd_json(required(value)));
}
std::shared_ptr<Value> child(const std::shared_ptr<Value>& owner, const char* name) {
    auto& map = required(owner);
    auto found = map.map.find(name);
    if (found == map.map.end())
        return {};
    auto& slot = found->second;
    if (slot.reference)
        return slot.reference;
    if (slot.kind == Value::Kind::NullReference)
        return {};
    auto value = std::make_shared<Value>(std::move(slot));
    slot = Value::alias(value);
    return value;
}
std::u16string prefix(const JsonRpcRequestManager::Text& method, const JsonRpcRequestManager::Text& uri) {
    return u"JsonRpc request '" + method.value_or(u"") + u"' to " + uri.value_or(u"");
}
Value envelope(const JsonRpcRequestManager::Text& method, const JsonRpcRequestManager::Text& id,
               Value parameters) {
    Value result;
    result.kind = Value::Kind::Map;
    result.set("jsonrpc", Value("2.0"));
    result.set("id", text(id));
    result.set("method", text(method));
    result.set("params", std::move(parameters));
    return result;
}
}
JsonRpcRequestManager::JsonRpcRequestManager(Post post, NewId id, Logger logger)
    : post_(std::move(post)), new_id_(std::move(id)), logger_(std::move(logger)) {
    if (!post_)
        post_ = [](const Text& uri, const Value& data, int timeout, bool) {
            return std::make_shared<Value>(
                osd_json_rpc_request(framework::utf8(uri.value_or(u"")), data, timeout));
        };
    if (!new_id_)
        new_id_ = [] { return framework::utf16_replacing(framework::Uuid::random().str()); };
}
void JsonRpcRequestManager::log(std::u16string message,
                                const std::optional<std::u16string>& exception) const {
    if (logger_)
        logger_({std::move(message), exception});
}
bool JsonRpcRequestManager::request(std::shared_ptr<Value>& data, const Text& method, const Text& uri,
                                    const Text& supplied_id) {
    Text id = supplied_id;
    if (!id || id->empty())
        id = new_id_();
    auto request = envelope(method, id, boxed(data));
    std::shared_ptr<Value> outer;
    try {
        outer = post_(uri, request, 10000, true);
    } catch (const std::exception& error) {
        log(prefix(method, uri) + u" failed", framework::utf16_replacing(error.what()));
        return false;
    }
    if (!required(outer).contains("_Result")) {
        log(prefix(method, uri) + u" returned an invalid response: " + json(outer));
        return false;
    }
    auto response = child(outer, "_Result");
    if (!response || required(response).kind != Value::Kind::Map) {
        log(prefix(method, uri) + u" returned an invalid response: " + json(outer));
        return false;
    }
    if (required(response).contains("error")) {
        data = child(response, "error");
        log(prefix(method, uri) + u" returned an error: " + json(data));
        return false;
    }
    data = std::move(response);
    return true;
}
bool JsonRpcRequestManager::request_object(std::shared_ptr<JsonRpcMembers>& parameters, const Text& method,
                                           const Text& uri, const Text& id) {
    if (!id)
        throw JsonRpcRequestArgumentError("jsonId");
    if (!uri)
        throw JsonRpcRequestArgumentError("uri");
    if (!method)
        throw JsonRpcRequestArgumentError("method");
    if (!parameters)
        throw JsonRpcRequestArgumentError("parameters");
    if (uri->empty() || std::all_of(uri->begin(), uri->end(), framework::unicode_whitespace))
        return false;
    auto request = envelope(method, id, parameters->serialize_members());
    std::shared_ptr<Value> outer;
    try {
        outer = post_(uri, request, 10000, true);
    } catch (const std::exception& error) {
        log(prefix(method, uri) + u" failed", framework::utf16_replacing(error.what()));
        return false;
    }
    if (!required(outer).contains("_Result")) {
        log(prefix(method, uri) + u" returned an invalid response: " + json(outer));
        return false;
    }
    auto response = child(outer, "_Result");
    if (!response || required(response).kind != Value::Kind::Map) {
        log(prefix(method, uri) + u" returned an invalid response: " + json(outer));
        return false;
    }
    if (required(response).contains("error")) {
        log(prefix(method, uri) + u" returned an error: " + json(child(response, "error")));
        return false;
    }
    if (!required(response).contains("result")) {
        log(prefix(method, uri) + u" returned an invalid response: " + json(response));
        return false;
    }
    auto result = child(response, "result");
    if (!result || required(result).kind != Value::Kind::Map) {
        log(prefix(method, uri) + u" returned an invalid response: " + json(response));
        return false;
    }
    if (!parameters)
        null_reference();
    parameters->deserialize_members(required(result));
    return true;
}
}
