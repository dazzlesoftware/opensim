// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/rpc.hpp"
#include <functional>
#include <optional>
#include <stdexcept>
namespace opensim::server {
class JsonRpcMembers {
public:
    virtual ~JsonRpcMembers() = default;
    virtual network::Value serialize_members() = 0;
    virtual void deserialize_members(const network::Value&) = 0;
};
class JsonRpcRequestArgumentError : public std::invalid_argument {
public:
    explicit JsonRpcRequestArgumentError(std::string name)
        : std::invalid_argument("Value cannot be null. (Parameter '" + name + "')"),
          parameter(std::move(name)) {}
    std::string parameter;
};
struct JsonRpcRequestLog {
    std::u16string message;
    std::optional<std::u16string> exception;
};
class JsonRpcRequestManager {
public:
    using Text = std::optional<std::u16string>;
    using Post =
        std::function<std::shared_ptr<network::Value>(const Text&, const network::Value&, int, bool)>;
    using NewId = std::function<std::u16string()>;
    using Logger = std::function<void(const JsonRpcRequestLog&)>;
    explicit JsonRpcRequestManager(Post = {}, NewId = {}, Logger = {});
    bool request(std::shared_ptr<network::Value>& data, const Text& method, const Text& uri, const Text& id);
    bool request_object(std::shared_ptr<JsonRpcMembers>& parameters, const Text& method, const Text& uri,
                        const Text& id);

private:
    Post post_;
    NewId new_id_;
    Logger logger_;
    void log(std::u16string, const std::optional<std::u16string>& exception = {}) const;
};
}
