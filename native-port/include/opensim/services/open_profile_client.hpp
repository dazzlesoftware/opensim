// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/user_profile_models.hpp"
#include <functional>
#include <stdexcept>
namespace opensim::services {
std::u16string profile_boxed_object_text(const network::Value&);
class OpenProfileError : public std::runtime_error {
  public:
    OpenProfileError(std::string type, std::u16string text)
        : runtime_error(framework::utf8(text)), source_type(std::move(type)), message(std::move(text)) {}
    std::string source_type;
    std::u16string message;
};
// XMLRPCRequester owns HTTP, XML-RPC fault handling and Hashtable validation.
// A requester is mandatory until that transport is bound by the native host.
class OpenProfileClient {
  public:
    using Text = std::optional<std::u16string>;
    using Requester = std::function<std::shared_ptr<network::Value>(const network::Value&,
                                                                    std::u16string_view, const Text&)>;
    using ObjectText = std::function<Text(const network::Value&)>;
    OpenProfileClient(Text server_uri, Requester, ObjectText = {});
    bool request_avatar_properties(std::shared_ptr<framework::UserProfileProperties>&);

  private:
    Text server_uri_;
    Requester request_;
    ObjectText object_text_;
};
}
