#include "opensim/framework/config_http_environment_proxy_native.hpp"
#include "opensim/framework/config_http_uri_object.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/config_generic_uri_object.hpp"
#include "opensim/framework/config_uri_components.hpp"
#include "opensim/framework/config_uri_unescape.hpp"
#include "opensim/framework/config_uri_escape.hpp"
#include "opensim/framework/runtime_environment.hpp"
#include "opensim/framework/nini_writer.hpp"
#include "opensim/framework/text.hpp"
#include <array>
namespace opensim::framework {
namespace {
using Text = ConfigUriText;
using Object = ConfigUriObjectPtr;
Text t(std::u16string value) {
    return nini_writer_text(std::move(value));
}
[[noreturn]] void required(const char* value) {
    throw ConfigHttpProxyProviderError(value);
}
class Builder : public ConfigHttpProxyUriBuilder {
    Text scheme_, host_, user_, password_;
    std::uint16_t port_;
    ConfigUriObjectBackend uris_;

  public:
    Builder(Text scheme, Text host, std::uint16_t port, ConfigUriObjectBackend uris)
        : scheme_(std::move(scheme)), host_(std::move(host)), port_(port), uris_(std::move(uris)) {
    }
    void user_name(Text value) override {
        user_ = std::move(value);
    }
    void password(Text value) override {
        password_ = std::move(value);
    }
    Object uri() override {
        auto host = *host_;
        if (host.find(u':') != std::u16string::npos && (host.empty() || host.front() != u'['))
            host = u"[" + host + u"]";
        bool user = user_ && !user_->empty(), password = password_ && !password_->empty();
        if (!user && password)
            throw ConfigUriComponentsError("System.UriFormatException",
                                           u"Invalid URI: The username:password construct is badly formed.",
                                           0x80131537);
        std::u16string value = *scheme_ + u"://";
        if (user) {
            value += *user_;
            if (password)
                value += u":" + *password_;
            value += u'@';
        }
        value += host + u":" + utf16(std::to_string(port_)) + u"/";
        return uris_.create(t(std::move(value)), ConfigUriObjectKind::Absolute);
    }
};
std::u16string comparison_url(const Object& object) {
    auto value = object->http_components();
    if (!value)
        required("Proxy URI equality requires the complete nonHTTP SDK provider");
    return *value->scheme() + u"://" + *value->host() + u":" + utf16(std::to_string(value->port())) +
           config_uri_unescape(*value->absolute_path(), u'?', u'#', 0xffff, 3, true, false) +
           config_uri_unescape(*value->query(), u'#', 0xffff, 0xffff, 3, true, true);
}
}
std::shared_ptr<ConfigXmlCredentials> config_http_default_network_credentials() {
    static auto value = std::make_shared<ConfigHttpNetworkCredential>(t(u""), t(u""), t(u""), true);
    return value;
}
bool config_http_proxy_dotnet8_is_digit(char16_t c) {
    constexpr std::array<char16_t, 37> starts = {
        0x0030, 0x0660, 0x06f0, 0x07c0, 0x0966, 0x09e6, 0x0a66, 0x0ae6, 0x0b66, 0x0be6,
        0x0c66, 0x0ce6, 0x0d66, 0x0de6, 0x0e50, 0x0ed0, 0x0f20, 0x1040, 0x1090, 0x17e0,
        0x1810, 0x1946, 0x19d0, 0x1a80, 0x1a90, 0x1b50, 0x1bb0, 0x1c40, 0x1c50, 0xa620,
        0xa8d0, 0xa900, 0xa9d0, 0xa9f0, 0xaa50, 0xabf0, 0xff10};
    for (auto start : starts)
        if (c >= start && c <= start + 9)
            return true;
    return false;
}
bool config_http_proxy_http_uri_equal(Object a, Object b) {
    if (!a)
        throw ConfigUriComponentsError("System.NullReferenceException",
                                       u"Object reference not set to an instance of an object.", 0x80004003);
    if (!b)
        return false;
    if (a == b || a->original_string() == b->original_string())
        return true;
    if (a->is_absolute() != b->is_absolute())
        return false;
    if (!a->is_absolute())
        return *a->original_string() == *b->original_string();
    auto av = a->http_components(), bv = b->http_components();
    if (!av || !bv)
        required("Proxy URI equality requires the complete nonHTTP SDK provider");
    if (*av->host() != *bv->host() || av->port() != bv->port())
        return false;
    return comparison_url(a) == comparison_url(b);
}
std::shared_ptr<ConfigHttpEnvironmentProxyBackend>
native_config_http_environment_proxy_backend(ConfigHttpEnvironmentProxyNativeOptions options) {
    if (!options.uris.create)
        options.uris = *native_config_uri_object_backend();
    auto b = std::make_shared<ConfigHttpEnvironmentProxyBackend>();
    b->environment = options.environment ? options.environment : runtime_environment_get;
    b->absolute_uri = [uris = options.uris](Text text) {
        return uris.create(std::move(text), ConfigUriObjectKind::Absolute);
    };
    b->new_builder = [uris = options.uris, other = options.other_builder](
                         Text scheme, Text host,
                         std::uint16_t port) -> std::shared_ptr<ConfigHttpProxyUriBuilder> {
        if (*scheme != u"http" && *scheme != u"https" && other)
            return other(scheme, host, port);
        return std::make_shared<Builder>(scheme, host, port, uris);
    };
    b->unescape_data = [](Text value) {
        if (!value)
            throw ConfigUriComponentsError("System.ArgumentNullException",
                                           u"Value cannot be null. (Parameter 'stringToUnescape')",
                                           0x80004003, u"stringToUnescape");
        return t(config_uri_unescape(*value, 0xffff, 0xffff, 0xffff, 10, true, false));
    };
    b->escape_data = [](Text value) {
        if (!value)
            throw ConfigUriComponentsError("System.ArgumentNullException",
                                           u"Value cannot be null. (Parameter 'stringToEscape')", 0x80004003,
                                           u"stringToEscape");
        return t(config_uri_escape(*value, false, ConfigUriEscapeSet::Unreserved));
    };
    b->user_info = [other = options.other_user_info](Object object) {
        auto v = object->http_components();
        if (v)
            return v->user_info();
        if (other)
            return other(object);
        if (config_generic_uri_owns(object))
            return config_generic_uri_component(object, ConfigGenericUriComponent::UserInfo);
        required("Proxy UserInfo requires its native or foreign URI provider");
    };
    b->host = [other = options.other_host](Object object) {
        auto v = object->http_components();
        if (v)
            return v->host();
        if (other)
            return other(object);
        if (config_generic_uri_owns(object))
            return config_generic_uri_component(object, ConfigGenericUriComponent::Host);
        required("Proxy Host requires its native or foreign URI provider");
    };
    b->uri_equal = [other = options.other_uri_equal](Object a, Object c) {
        if (!a || !c || a == c || a->original_string() == c->original_string() || !a->is_absolute() ||
            !c->is_absolute() || (a->http_components() && c->http_components()))
            return config_http_proxy_http_uri_equal(a, c);
        if (!other)
            required("Proxy Uri.Equals requires the complete nonHTTP SDK provider");
        return other(a, c);
    };
    auto casing = options.casing ? *options.casing : file_system_name::pinned_case_backend();
    if (!casing.ordinal_ignore_case_ends_with)
        required("Environment proxy requires ordinal Unicode casing");
    b->ordinal_ends_with = casing.ordinal_ignore_case_ends_with;
    b->ordinal_equal = [ends = casing.ordinal_ignore_case_ends_with](auto a, auto c) {
        return a.size() == c.size() && ends(a, c);
    };
    b->white_space = unicode_whitespace;
    b->is_digit = config_http_proxy_dotnet8_is_digit;
    b->network_credential = options.network_credential
                                ? options.network_credential
                                : decltype(options.network_credential)([](Text u, Text p, Text d) {
                                      return std::make_shared<ConfigHttpNetworkCredential>(
                                          std::move(u), std::move(p), std::move(d));
                                  });
    b->default_network_credentials = options.default_network_credentials
                                         ? options.default_network_credentials
                                         : config_http_default_network_credentials;
    return b;
}
}
