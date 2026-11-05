// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/rpc.hpp"
#include <stdexcept>
namespace opensim::network {
class SdkNotationError : public std::runtime_error {
  public:
    explicit SdkNotationError(std::string text) : std::runtime_error(text), message(std::move(text)) {}
    std::string message;
};
Value deserialize_sdk_llsd_notation(std::string_view bytes);
Value deserialize_sdk_llsd_notation_text(std::u16string_view text);
}
