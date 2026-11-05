// Native composition of the consumed System.Uri constructor/getter operations.
#pragma once
#include "opensim/framework/config_uri_object.hpp"
#include "opensim/framework/config_uri_components.hpp"
namespace opensim::framework {
struct ConfigNativeUriObjectOptions {
 std::shared_ptr<ConfigUriComponentsBackend> components;
#ifdef _WIN32
 bool windows=true;
#else
 bool windows=false;
#endif
};
// Operation callbacks retain their constructor dependencies and remain usable
// when copied independently of the returned backend owner.
std::shared_ptr<ConfigUriObjectBackend> native_config_uri_object_backend(ConfigNativeUriObjectOptions={});
}
