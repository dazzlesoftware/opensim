// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/http_server.hpp"
#include "opensim/network/rpc.hpp"
#include "opensim/framework/ordered_dictionary.hpp"
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
namespace opensim::server {
struct JsonRpcErrorCode {
    static constexpr int ParseError = -32700, InvalidRequest = -32600, MethodNotFound = -32601,
                         InvalidParams = -32602, InternalError = -32604;
};
struct JsonRpcError {
    std::shared_ptr<network::Value> values = std::make_shared<network::Value>(network::Value::object({}));
    std::shared_ptr<network::Value> data;
    std::int32_t code() const;
    void code(std::int32_t);
    std::optional<std::u16string> message() const;
    void message(const std::optional<std::u16string>&);
};
struct JsonRpcResponse {
    std::shared_ptr<network::Value> reply = std::make_shared<network::Value>(network::Value::object({}));
    std::shared_ptr<network::Value> result;
    std::shared_ptr<JsonRpcError> error = std::make_shared<JsonRpcError>();
    std::u16string id() const;
    void id(const std::optional<std::u16string>&);
    std::u16string version() const;
    void version(const std::optional<std::u16string>&);
    std::string serialize();
};
class JsonRpcParseError : public std::runtime_error {
public:
    explicit JsonRpcParseError(std::string);
    explicit JsonRpcParseError(std::u16string);
    std::u16string source_message;
};
class JsonRpcDispatcher {
public:
    using Handler = std::function<bool(const network::Value&, std::shared_ptr<JsonRpcResponse>&)>;
    using Decoder = std::function<network::Value(std::string_view)>;
    using Disposer = std::function<void()>;
    using Logger = std::function<void(std::string_view)>;
    explicit JsonRpcDispatcher(Decoder decoder = {}, Logger logger = {}, Disposer disposer = {});
    void logger(Logger);
    bool add(std::u16string method, Handler handler);
    Handler get(std::u16string_view method) const;
    void remove(std::u16string_view method);
    std::vector<std::u16string> keys() const;
    network::HttpResponse handle(const network::HttpRequest&);

private:
    Decoder decoder_;
    Logger logger_;
    Disposer disposer_;
    bool default_decoder_ = false;
    mutable std::mutex mutex_;
    framework::OrderedDictionary<std::u16string, Handler> handlers_;
};
}
