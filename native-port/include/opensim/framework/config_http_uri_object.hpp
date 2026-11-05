// Native HTTP/relative System.Uri operations; file/other schemes are delegated.
#pragma once
#include "opensim/framework/config_uri_object.hpp"
#include "opensim/framework/config_uri_components.hpp"
namespace opensim::framework {
struct ConfigHttpUriObjectOptions {
 std::shared_ptr<ConfigUriComponentsBackend> components;
 ConfigUriObjectBackend other_schemes;
 // Scheme scan/implicit file behavior follows the selected source platform.
#ifdef _WIN32
 bool windows=true;
#else
 bool windows=false;
#endif
};
ConfigUriObjectBackend native_http_uri_object_backend(ConfigHttpUriObjectOptions={});
}
