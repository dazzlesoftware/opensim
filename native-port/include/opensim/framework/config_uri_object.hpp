// Shared consumed System.Uri object boundary. Native scheme providers implement
// the actual getters; absent file/HTTP operations must never be approximated.
#pragma once
#include "opensim/framework/config_uri.hpp"
#include <cstdint>
namespace opensim::framework {
class ConfigUriComponents;
class ConfigUriObject {
public:
    virtual ~ConfigUriObject() = default;
    virtual bool is_absolute() const = 0;
    virtual ConfigUriText original_string() const = 0;
    virtual ConfigUriText scheme() const = 0;
    virtual ConfigUriText fragment() const = 0;
    virtual ConfigUriText absolute_uri() const = 0;
    // Simple-syntax Uri.ToString uses the source V1 display format (32767);
    // relative instances return their retained parse string.
    virtual ConfigUriText to_string() const = 0;
    virtual ConfigUriText local_path() const = 0;
    // Optional concrete consumed HTTP components, never supplied for opaque/file URIs.
    virtual std::shared_ptr<ConfigUriComponents> http_components() const {
        return {};
    }
};
using ConfigUriObjectPtr = std::shared_ptr<const ConfigUriObject>;
enum class ConfigUriObjectKind : std::int32_t { RelativeOrAbsolute = 0, Absolute = 1, Relative = 2 };
struct ConfigUriObjectBackend {
    // new Uri(text,kind), with original typed constructor failures.
    std::function<ConfigUriObjectPtr(ConfigUriText, ConfigUriObjectKind)> create;
    // Uri.TryCreate(text,kind), null on ordinary parsing failure.
    std::function<ConfigUriObjectPtr(ConfigUriText, ConfigUriObjectKind)> try_create;
    // These overloads differ; Header Location uses the Uri-object overload.
    std::function<ConfigUriObjectPtr(ConfigUriObjectPtr, ConfigUriObjectPtr)> combine_uri;
    std::function<ConfigUriObjectPtr(ConfigUriObjectPtr, ConfigUriText)> combine_string;
    // new UriBuilder(uri) { Fragment=fragment }.Uri, preserving builder semantics.
    std::function<ConfigUriObjectPtr(ConfigUriObjectPtr, ConfigUriText)> with_fragment;
};
}
