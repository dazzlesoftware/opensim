// Shared view of source-backed System.Uri8.0.31 constructor/getters.
#include "opensim/framework/config_uri_components.hpp"
#include "opensim/framework/config_generic_uri_object.hpp"
#include "opensim/framework/text.hpp"
namespace opensim::framework {
ConfigUriComponentsError::ConfigUriComponentsError(std::string type, std::u16string text, std::uint32_t h,
                                                   std::optional<std::u16string> p)
    : runtime_error(utf8(text)), source_type(std::move(type)), message(std::move(text)), hresult(h),
      parameter(std::move(p)) {}
struct ConfigUriComponents::State {
    ConfigUriObjectPtr object;
    std::shared_ptr<ConfigUriComponentsBackend> backend;
    bool windows;
};
ConfigUriComponents::ConfigUriComponents(std::shared_ptr<State> state) : state_(std::move(state)) {}
std::shared_ptr<ConfigUriComponents>
ConfigUriComponents::from_object(ConfigUriObjectPtr object,
                                 std::shared_ptr<ConfigUriComponentsBackend> backend, bool windows) {
    if (!backend || !backend->unicode.normalize_nfc || !backend->lower_invariant || !backend->idn_ascii)
        throw std::invalid_argument(
            "URI components require normalization, invariant lowercase and IDN providers");
    if (!object || !object->is_absolute() || !config_generic_uri_owns(object))
        throw std::invalid_argument("URI component view requires native generic absolute object");
    return std::shared_ptr<ConfigUriComponents>(new ConfigUriComponents(
        std::make_shared<State>(State{std::move(object), std::move(backend), windows})));
}
ConfigUriObjectPtr ConfigUriComponents::source_object() const {
    return state_->object;
}
std::shared_ptr<ConfigUriComponents> ConfigUriComponents::rebind(ConfigUriObjectPtr object) const {
    return from_object(std::move(object), state_->backend, state_->windows);
}
std::shared_ptr<ConfigUriComponents>
ConfigUriComponents::absolute(ConfigUriText input, std::shared_ptr<ConfigUriComponentsBackend> backend) {
    if (!input)
        throw ConfigUriComponentsError("System.ArgumentNullException",
                                       u"Value cannot be null. (Parameter 'uriString')", 0x80004003,
                                       u"uriString");
    if (!backend || !backend->unicode.normalize_nfc || !backend->lower_invariant || !backend->idn_ascii)
        throw std::invalid_argument(
            "URI components require normalization, invariant lowercase and IDN providers");
#ifdef _WIN32
    constexpr bool windows = true;
#else
    constexpr bool windows = false;
#endif
    auto object =
        config_generic_uri_create(std::move(input), ConfigUriObjectKind::Absolute, backend, windows);
    return from_object(std::move(object), std::move(backend), windows);
}
ConfigUriText ConfigUriComponents::original_string() const {
    return state_->object->original_string();
}
ConfigUriText ConfigUriComponents::scheme() const {
    return state_->object->scheme();
}
bool ConfigUriComponents::is_http() const {
    auto s = *scheme();
    return s == u"http" || s == u"https";
}
ConfigUriText ConfigUriComponents::host() const {
    return config_generic_uri_component(state_->object, ConfigGenericUriComponent::Host);
}
ConfigUriText ConfigUriComponents::idn_host() const {
    return config_generic_uri_component(state_->object, ConfigGenericUriComponent::IdnHost);
}
ConfigUriText ConfigUriComponents::dns_safe_host() const {
    return config_generic_uri_component(state_->object, ConfigGenericUriComponent::DnsSafeHost);
}
std::int32_t ConfigUriComponents::port() const {
    return config_generic_uri_port(state_->object);
}
ConfigUriText ConfigUriComponents::user_info() const {
    return config_generic_uri_component(state_->object, ConfigGenericUriComponent::UserInfo);
}
ConfigUriText ConfigUriComponents::absolute_path() const {
    return config_generic_uri_component(state_->object, ConfigGenericUriComponent::AbsolutePath);
}
ConfigUriText ConfigUriComponents::query() const {
    return config_generic_uri_component(state_->object, ConfigGenericUriComponent::Query);
}
ConfigUriText ConfigUriComponents::fragment() const {
    return state_->object->fragment();
}
ConfigUriText ConfigUriComponents::absolute_uri() const {
    return state_->object->absolute_uri();
}
std::shared_ptr<ConfigUriComponents> ConfigUriComponents::resolve(std::shared_ptr<ConfigUriComponents> base,
                                                                  ConfigUriText relative) {
    if (!base)
        throw ConfigUriComponentsError("System.ArgumentNullException",
                                       u"Value cannot be null. (Parameter 'baseUri')", 0x80004003,
                                       u"baseUri");
    auto create = [backend = base->state_->backend, windows = base->state_->windows](auto value, auto kind) {
        return config_generic_uri_create(std::move(value), kind, backend, windows);
    };
    return base->rebind(config_generic_uri_combine_string(base->state_->object, std::move(relative), create));
}
}
