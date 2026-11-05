#include "opensim/framework/config_http_windows_proxy_bypass.hpp"
#include "opensim/framework/config_http_environment_proxy_native.hpp"
#include "opensim/framework/config_generic_uri_object.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
namespace opensim::framework {
ConfigHttpWindowsProxyBypassBackend native_config_http_windows_proxy_bypass_backend() {
    auto environment = native_config_http_environment_proxy_backend();
    return {file_system_name::pinned_case_backend(), environment->white_space,
            native_config_uri_network_identity,
            [](auto uri) {
                if (uri)
                    if (auto http = uri->http_components())
                        return http->idn_host();
                if (!uri)
                    throw ConfigUriComponentsError("System.NullReferenceException",
                                                   u"Object reference not set to an instance of an object.",
                                                   0x80004003);
                if (!uri->is_absolute())
                    throw ConfigUriComponentsError("System.InvalidOperationException",
                                                   u"This operation is not supported for a relative URI.",
                                                   0x80131509);
                if (config_generic_uri_owns(uri))
                    return config_generic_uri_component(uri, ConfigGenericUriComponent::IdnHost);
#ifdef _WIN32
                constexpr bool windows = true;
#else
                constexpr bool windows = false;
#endif
                return config_generic_uri_component(
                    config_generic_uri_create(uri->original_string(), ConfigUriObjectKind::Absolute,
                                              native_config_uri_components_backend(), windows),
                    ConfigGenericUriComponent::IdnHost);
            },
            native_config_http_windows_local_addresses};
}
bool config_http_windows_proxy_star_match(std::u16string_view input, std::u16string_view pattern,
                                          const file_system_name::CasingBackend& casing) {
    if (!casing.upper_invariant)
        throw ConfigHttpProxyProviderError("Windows proxy wildcard requires Char.ToUpperInvariant provider");
    std::size_t n = 0, i = 0, star_n = 0, star_i = 0;
    bool star = false;
    while (n < input.size()) {
        if (i < pattern.size() && pattern[i] == u'*') {
            star_n = n;
            star_i = ++i;
            star = true;
            continue;
        }
        if (i < pattern.size() && (pattern[i] == input[n] ||
                                   casing.upper_invariant(pattern[i]) == casing.upper_invariant(input[n]))) {
            ++n;
            ++i;
            continue;
        }
        if (!star)
            return false;
        n = ++star_n;
        i = star_i;
    }
    while (i < pattern.size() && pattern[i] == u'*')
        ++i;
    return i == pattern.size();
}
ConfigHttpWindowsProxyBypass::ConfigHttpWindowsProxyBypass(ConfigUriText text,
                                                           ConfigHttpWindowsProxyBypassBackend backend)
    : backend_(std::move(backend)) {
    if (!backend_.white_space || !backend_.casing.ordinal_ignore_case_ends_with)
        throw ConfigHttpProxyProviderError(
            "Windows proxy bypass requires whitespace and ordinal casing providers");
    if (!text || std::all_of(text->begin(), text->end(), backend_.white_space))
        return;
    auto starts = [&](std::size_t i, std::u16string_view value) {
        return text->size() - i >= value.size() &&
               backend_.casing.ordinal_ignore_case_ends_with(
                   std::u16string_view(*text).substr(i, value.size()), value);
    };
    for (std::size_t i = 0; i < text->size();) {
        while (i < text->size() && (*text)[i] == u' ')
            ++i;
        if (starts(i, u"http://"))
            i += 7;
        else if (starts(i, u"https://"))
            i += 8;
        if (i < text->size() && (*text)[i] == u'[')
            ++i;
        auto start = i;
        while (i < text->size() && (*text)[i] != u' ' && (*text)[i] != u';' && (*text)[i] != u']')
            ++i;
        if (i != start) {
            if (starts(start, u"<local>"))
                local_ = true;
            else
                patterns_.push_back(nini_writer_text(text->substr(start, i - start)));
        }
        if (i < text->size() && (*text)[i] != u';') {
            while (i < text->size() && (*text)[i] != u';')
                ++i;
        }
        if (i < text->size() && (*text)[i] == u';')
            ++i;
    }
    if (local_) {
        if (!backend_.local_addresses)
            throw ConfigHttpProxyProviderError("Windows <local> bypass requires interface address provider");
        addresses_ = backend_.local_addresses();
    }
}
bool ConfigHttpWindowsProxyBypass::bypasses(ConfigUriObjectPtr uri) const {
    if (local_) {
        if (!backend_.identity || !backend_.idn_host)
            throw ConfigHttpProxyProviderError("Windows local bypass requires URI identity providers");
        auto identity = backend_.identity(uri);
        if (identity.loopback)
            return true;
        if (identity.host_name_type == 3 || identity.host_name_type == 4) {
            auto address = IpAddress::parse(utf8(*backend_.idn_host(uri)));
            if (address)
                for (auto& local : addresses_)
                    if (local.v6() == address->v6() && local.bytes() == address->bytes() &&
                        (!local.v6() || local.scope_id() == address->scope_id()))
                        return true;
        }
        if (identity.host_name_type != 4 && backend_.idn_host(uri)->find(u'.') == std::u16string::npos)
            return true;
    }
    if (!patterns_.empty() && !backend_.idn_host)
        throw ConfigHttpProxyProviderError("Windows wildcard bypass requires URI IdnHost provider");
    for (auto& pattern : patterns_) {
        if (config_http_windows_proxy_star_match(*backend_.idn_host(uri), *pattern, backend_.casing))
            return true;
    }
    return false;
}
}
