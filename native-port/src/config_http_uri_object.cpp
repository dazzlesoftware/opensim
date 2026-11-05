#include "opensim/framework/config_generic_uri_object.hpp"
#include "opensim/framework/config_http_uri_object.hpp"
#include "opensim/framework/config_uri_relative.hpp"
#include "opensim/framework/config_uri_iri.hpp"
#include "opensim/framework/config_uri_unescape.hpp"
#include "opensim/framework/config_file_uri_path.hpp"
#include "opensim/framework/config_file_uri_canonical.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <mutex>
namespace opensim::framework {
namespace {
using Text = ConfigUriText;
using Object = ConfigUriObjectPtr;
using Kind = ConfigUriObjectKind;
Text t(std::u16string value) {
    if (value.empty())
        return runtime_empty_text();
    return std::make_shared<const std::u16string>(std::move(value));
}
[[noreturn]] void format(std::u16string message) {
    throw ConfigUriComponentsError("System.UriFormatException", std::move(message), 0x80131537);
}
[[noreturn]] void nullarg(std::u16string name) {
    throw ConfigUriComponentsError("System.ArgumentNullException",
                                   u"Value cannot be null. (Parameter '" + name + u"')", 0x80004003, name);
}
[[noreturn]] void missing() {
    throw std::logic_error("Native URI object requires the file/other-scheme SDK provider");
}
void kind_check(Kind kind) {
    auto n = static_cast<std::int32_t>(kind);
    if (n < 0 || n > 2)
        throw ConfigUriComponentsError("System.ArgumentException",
                                       u"The value '" + utf16(std::to_string(n)) +
                                           u"' passed for the UriKind parameter is invalid.",
                                       0x80070057);
}
bool http(std::u16string_view scheme) {
    return scheme == u"http" || scheme == u"https";
}
[[noreturn]] void scan_error(int error) {
    switch (error) {
    case 1:
        format(u"Invalid URI: The format of the URI could not be determined.");
    case 2:
        format(u"Invalid URI: The URI scheme is not valid.");
    case 3:
        format(u"Invalid URI: The Authority/Host could not be parsed.");
    case 4:
        format(u"Invalid URI: The URI is empty.");
    case 5:
        format(u"Invalid URI: The Uri scheme is too long.");
    case 6:
        format(u"Invalid URI: The Uri string is too long.");
    case 7:
        format(u"Invalid URI: A Dos path must be rooted, for example, 'c:\\\\'.");
    default:
        format(u"Invalid URI: The hostname could not be parsed.");
    }
}
class Http final : public ConfigUriObject {
    std::shared_ptr<ConfigUriComponents> value_;
    ConfigUriObjectPtr source_;

  public:
    Http(std::shared_ptr<ConfigUriComponents> value, ConfigUriObjectPtr source)
        : value_(std::move(value)), source_(std::move(source)) {
    }
    bool is_absolute() const override {
        return true;
    }
    Text original_string() const override {
        return source_->original_string();
    }
    Text scheme() const override {
        return source_->scheme();
    }
    Text fragment() const override {
        return source_->fragment();
    }
    Text absolute_uri() const override {
        return source_->absolute_uri();
    }
    Text local_path() const override {
        return source_->local_path();
    }
    Text to_string() const override {
        return source_->to_string();
    }
    std::shared_ptr<ConfigUriComponents> http_components() const override {
        return value_;
    }
    Object source_object() const {
        return source_;
    }
    Object clone_source() const {
        auto source = config_generic_uri_clone(source_);
        return std::make_shared<Http>(value_->rebind(source), std::move(source));
    }
};
struct Backend {
    ConfigHttpUriObjectOptions options;
    explicit Backend(ConfigHttpUriObjectOptions o) : options(std::move(o)) {
        if (!options.components)
            options.components = native_config_uri_components_backend();
    }
    Object wrap(std::shared_ptr<ConfigUriComponents> value) {
        auto source = value->source_object();
        return std::make_shared<Http>(std::move(value), std::move(source));
    }
    Object retain_http_components(Object result) {
        if (result && result->is_absolute() && http(*result->scheme()) && !result->http_components()) {
            auto components = ConfigUriComponents::from_object(result, options.components, options.windows);
            return std::make_shared<Http>(std::move(components), std::move(result));
        }
        return result;
    }
    bool foreign(Text value, Kind kind) {
        if (!value)
            return false;
        auto scan = config_file_uri_scan_scheme(*value, options.windows);
        return !scan.error && !http(scan.scheme) &&
               !config_file_uri_implicit_kind_is_relative(scan, *value, static_cast<std::int32_t>(kind),
                                                          options.windows);
    }
    Object create(Text value, Kind kind) {
        if (!value)
            nullarg(u"uriString");
        kind_check(kind);
        auto scan = config_file_uri_scan_scheme(*value, options.windows);
        if (scan.error) {
            if (scan.error > 4 || kind == Kind::Absolute)
                scan_error(scan.error);
            auto parsed =
                config_uri_has_iri_characters(*value) ? t(config_uri_escape_unescape_iri(*value, 0)) : value;
            return config_uri_relative_object(value, parsed);
        }
        if (config_file_uri_implicit_kind_is_relative(scan, *value, static_cast<std::int32_t>(kind),
                                                      options.windows))
            return config_uri_relative_object(value, value);
        if (!http(scan.scheme)) {
            if (!options.other_schemes.create)
                missing();
            return options.other_schemes.create(value, kind);
        }
        auto source = config_generic_uri_create(value, kind, options.components, options.windows);
        if (!source->is_absolute())
            return source;
        return wrap(ConfigUriComponents::from_object(std::move(source), options.components, options.windows));
    }
    Object try_create(Text value, Kind kind) {
        if (!value)
            return {};
        kind_check(kind);
        auto scan = config_file_uri_scan_scheme(*value, options.windows);
        if (scan.error > 0 && scan.error <= 4 && kind != Kind::Absolute)
            return config_uri_relative_object(value, value);
        if (foreign(value, kind)) {
            if (!options.other_schemes.try_create)
                missing();
            return options.other_schemes.try_create(value, kind);
        }
        try {
            return create(value, kind);
        } catch (const ConfigUriComponentsError& e) {
            if (e.source_type == "System.UriFormatException")
                return {};
            throw;
        }
    }
    Object combine_string(Object base, Text relative) {
        if (!base)
            nullarg(u"baseUri");
        auto native = std::dynamic_pointer_cast<const Http>(base);
        if (!native) {
            if (!options.other_schemes.combine_string)
                missing();
            return options.other_schemes.combine_string(base, relative);
        }
        auto constructor = [this](auto text, auto kind) { return create(text, kind); };
        return retain_http_components(
            config_generic_uri_combine_string(native->source_object(), relative, constructor));
    }
    Object combine_uri(Object base, Object relative) {
        if (!base)
            nullarg(u"baseUri");
        if (!base->is_absolute())
            throw ConfigUriComponentsError(
                "System.ArgumentOutOfRangeException",
                u"Specified argument was out of the range of valid values. (Parameter 'baseUri')", 0x80131502,
                u"baseUri");
        if (!relative)
            throw ConfigUriComponentsError("System.NullReferenceException",
                                           u"Object reference not set to an instance of an object.",
                                           0x80004003);
        if (relative->is_absolute()) {
            if (auto native = std::dynamic_pointer_cast<const Http>(relative))
                return native->clone_source();
            if (!options.other_schemes.combine_uri)
                missing();
            return options.other_schemes.combine_uri(base, relative);
        }
        if (auto native = std::dynamic_pointer_cast<const Http>(base)) {
            auto constructor = [this](auto text, auto kind) { return create(text, kind); };
            return retain_http_components(
                config_generic_uri_combine_uri(native->source_object(), relative, constructor));
        }
        if (!options.other_schemes.combine_uri)
            missing();
        return options.other_schemes.combine_uri(base, relative);
    }
    Object with_fragment(Object uri, Text value) {
        if (!uri)
            nullarg(u"uri");
        auto native = std::dynamic_pointer_cast<const Http>(uri);
        if (!native) {
            if (!options.other_schemes.with_fragment)
                missing();
            return options.other_schemes.with_fragment(uri, value);
        }
        auto constructor = [this](auto text, auto kind) {
            return config_generic_uri_create(text, kind, options.components, options.windows);
        };
        auto source = config_generic_uri_with_fragment(native->source_object(), value, constructor);
        auto components = ConfigUriComponents::from_object(source, options.components, options.windows);
        return std::make_shared<Http>(std::move(components), std::move(source));
    }
};
}
ConfigUriObjectBackend native_http_uri_object_backend(ConfigHttpUriObjectOptions options) {
    auto state = std::make_shared<Backend>(std::move(options));
    ConfigUriObjectBackend b;
    b.create = [state](auto v, auto k) { return state->create(v, k); };
    b.try_create = [state](auto v, auto k) { return state->try_create(v, k); };
    b.combine_uri = [state](auto a, auto v) { return state->combine_uri(a, v); };
    b.combine_string = [state](auto a, auto v) { return state->combine_string(a, v); };
    b.with_fragment = [state](auto a, auto f) { return state->with_fragment(a, f); };
    return b;
}
}
