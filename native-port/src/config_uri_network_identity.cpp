#include "opensim/framework/config_uri_network_identity.hpp"
#include "opensim/framework/config_generic_uri_object.hpp"
namespace opensim::framework {
ConfigUriNetworkIdentity native_config_uri_network_identity(ConfigUriObjectPtr uri) {
    if (!uri)
        throw ConfigUriComponentsError("System.NullReferenceException",
                                       u"Object reference not set to an instance of an object.", 0x80004003);
    if (!uri->is_absolute())
        throw ConfigUriComponentsError("System.InvalidOperationException",
                                       u"This operation is not supported for a relative URI.", 0x80131509);
    if (config_generic_uri_owns(uri))
        return config_generic_uri_network_identity(uri);
    if (auto components = uri->http_components())
        return config_generic_uri_network_identity(components->source_object());
#ifdef _WIN32
    constexpr bool windows = true;
#else
    constexpr bool windows = false;
#endif
    auto mirror = config_generic_uri_create(uri->original_string(), ConfigUriObjectKind::Absolute,
                                            native_config_uri_components_backend(), windows);
    return config_generic_uri_network_identity(mirror);
}
}
