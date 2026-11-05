#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/config_generic_uri_object.hpp"
#include "opensim/framework/config_http_uri_object.hpp"
namespace opensim::framework {
std::shared_ptr<ConfigUriObjectBackend>
native_config_uri_object_backend(ConfigNativeUriObjectOptions options) {
    auto result = std::make_shared<ConfigUriObjectBackend>();
    auto components =
        options.components ? std::move(options.components) : native_config_uri_components_backend();
    ConfigHttpUriObjectOptions http;
    http.components = components;
    http.windows = options.windows;
    auto windows = options.windows;
    http.other_schemes.create = [components, windows](auto value, auto kind) {
        return config_generic_uri_create(value, kind, components, windows);
    };
    http.other_schemes.try_create = [components, windows](auto value, auto kind) {
        return config_generic_uri_try_create(value, kind, components, windows);
    };
    // This constructor-only backend contains no resolution callbacks. Retaining
    // its create operation makes copied outer callbacks self-contained without a
    // reference back to the outer backend (and therefore without a cycle).
    auto create = native_http_uri_object_backend(http).create;
    http.other_schemes.combine_string = [create](auto base, auto relative) {
        return config_generic_uri_combine_string(base, relative, create);
    };
    http.other_schemes.combine_uri = [create](auto base, auto relative) {
        return config_generic_uri_combine_uri(base, relative, create);
    };
    http.other_schemes.with_fragment = [create](auto uri, auto fragment) {
        return config_generic_uri_with_fragment(uri, fragment, create);
    };
    *result = native_http_uri_object_backend(std::move(http));
    return result;
}
}
