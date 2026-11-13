#include "opensim/framework/config_http_credential_cache.hpp"
#include "opensim/framework/config_uri_components.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>

namespace opensim::framework {
namespace {
bool equal(std::u16string_view left, std::u16string_view right) {
    return left.size() == right.size() &&
           file_system_name::pinned_case_backend().ordinal_ignore_case_ends_with(left, right);
}

void validate(std::u16string_view scheme, const ConfigHttpCredentialCache::Credential& credential) {
    if (credential && credential->is_default && !equal(scheme, u"Negotiate") &&
        !equal(scheme, u"NTLM") && !equal(scheme, u"Kerberos"))
        throw std::invalid_argument("Default credentials require Negotiate, NTLM or Kerberos");
}

bool matches(const ConfigUriComponents& uri, const ConfigUriComponents& prefix) {
    if (*uri.scheme() != *prefix.scheme() || *uri.host() != *prefix.host() || uri.port() != prefix.port())
        return false;
    const auto path = uri.absolute_path();
    const auto prefix_path = prefix.absolute_path();
    const auto length = prefix_path->rfind(u'/');
    return length != std::u16string::npos && length <= path->rfind(u'/') &&
           equal(std::u16string_view(*path).substr(0, length), std::u16string_view(*prefix_path).substr(0, length));
}
}

ConfigHttpCredentialCache::ConfigHttpCredentialCache(ConfigXmlUriObjects objects)
    : objects_(std::move(objects)) {}

void ConfigHttpCredentialCache::add(ConfigUriObjectPtr prefix, std::u16string scheme, Credential credential) {
    if (!prefix || !prefix->http_components())
        throw std::invalid_argument("Credential cache requires a typed HTTP URI prefix");
    validate(scheme, credential);
    std::lock_guard lock(mutex_);
    if (std::ranges::any_of(uris_, [&](const UriEntry& entry) {
            return equal(entry.scheme, scheme) && config_http_proxy_http_uri_equal(entry.prefix, prefix);
        }))
        throw std::invalid_argument("Duplicate credential URI and authentication scheme");
    uris_.push_back({std::move(prefix), std::move(scheme), std::move(credential)});
}

void ConfigHttpCredentialCache::add(std::u16string host, int port, std::u16string scheme, Credential credential) {
    if (host.empty() || port < 0)
        throw std::invalid_argument("Credential cache requires a host and nonnegative port");
    validate(scheme, credential);
    std::lock_guard lock(mutex_);
    if (std::ranges::any_of(hosts_, [&](const HostEntry& entry) {
            return entry.port == port && equal(entry.host, host) && equal(entry.scheme, scheme);
        }))
        throw std::invalid_argument("Duplicate credential host, port and authentication scheme");
    hosts_.push_back({std::move(host), port, std::move(scheme), std::move(credential)});
}

void ConfigHttpCredentialCache::remove(const ConfigUriObjectPtr& prefix, std::u16string_view scheme) {
    if (!prefix || !prefix->http_components())
        return;
    std::lock_guard lock(mutex_);
    std::erase_if(uris_, [&](const UriEntry& entry) {
        return equal(entry.scheme, scheme) && config_http_proxy_http_uri_equal(entry.prefix, prefix);
    });
}

void ConfigHttpCredentialCache::remove(std::u16string_view host, int port, std::u16string_view scheme) {
    std::lock_guard lock(mutex_);
    std::erase_if(hosts_, [&](const HostEntry& entry) {
        return entry.port == port && equal(entry.host, host) && equal(entry.scheme, scheme);
    });
}

ConfigHttpCredentialCache::Credential
ConfigHttpCredentialCache::get_credential(const ConfigUriObjectPtr& uri, std::u16string_view scheme) const {
    const auto address = uri ? uri->http_components() : nullptr;
    if (!address)
        return {};
    std::lock_guard lock(mutex_);
    Credential selected;
    std::size_t best_length = 0;
    for (const auto& entry : uris_) {
        if (!equal(entry.scheme, scheme) || !matches(*address, *entry.prefix->http_components()))
            continue;
        const auto length = entry.prefix->to_string()->size();
        if (length > best_length) {
            best_length = length;
            selected = entry.credential;
        }
    }
    return selected;
}

ConfigHttpCredentialCache::Credential
ConfigHttpCredentialCache::get_credential(std::u16string_view host, int port, std::u16string_view scheme) const {
    if (host.empty() || port < 0)
        throw std::invalid_argument("Credential cache requires a host and nonnegative port");
    std::lock_guard lock(mutex_);
    const auto entry = std::ranges::find_if(hosts_, [&](const HostEntry& entry) {
        return entry.port == port && equal(entry.host, host) && equal(entry.scheme, scheme);
    });
    return entry == hosts_.end() ? nullptr : entry->credential;
}

ConfigHttpCredentialCache::Credential ConfigHttpCredentialCache::get_credential(
    const std::shared_ptr<ConfigXmlUri>& uri, std::string_view scheme) const {
    return get_credential(objects_.get(uri), utf16(scheme));
}
}
