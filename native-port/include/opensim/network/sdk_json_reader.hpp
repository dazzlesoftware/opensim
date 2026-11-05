// Derived from bundled OpenMetaverse.StructuredData LitJson; BSD license:
// native-port/protocol/LIBOPENMETAVERSE-LICENSE.txt.
#pragma once
#include "opensim/network/rpc.hpp"
#include <stdexcept>
namespace opensim::network {
class SdkJsonError : public std::invalid_argument {
  public:
    explicit SdkJsonError(std::u16string);
    std::u16string message;
};
class SdkJsonValueError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
Value read_sdk_json(std::u16string_view);
}
