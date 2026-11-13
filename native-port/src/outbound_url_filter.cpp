// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/outbound_url_filter.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <charconv>
#include <iterator>
#include <utility>

namespace opensim::framework {
namespace {

std::uint32_t network_order(const IpAddress& address) {
    const auto bytes = address.bytes();
    std::uint32_t result = 0;
    for (std::size_t index = 0; index < 4; ++index)
        result = (result << 8) | bytes[index];
    return result;
}

std::optional<std::vector<HostResolver::Address>> lookup_host(const std::string& host) {
    if (const auto literal = IpAddress::parse(host)) {
        if (literal->unspecified())
            return std::nullopt;
        return std::vector{std::make_shared<IpAddress>(*literal)};
    }
    return HostResolver::system_lookup(host);
}

std::optional<std::pair<std::uint32_t, std::uint32_t>> parse_ipv4_network(std::string_view entry) {
    // The bundled LukeSkywalker parser keeps only IPv4 digits and separators.
    std::string cleaned;
    std::ranges::copy_if(entry, std::back_inserter(cleaned), [](char c) {
        return (c >= '0' && c <= '9') || c == '.' || c == '/' || c == ' ';
    });
    const auto first = cleaned.find_first_not_of(' ');
    if (first == cleaned.npos)
        return std::nullopt;
    auto fields = std::string_view(cleaned).substr(first);
    const auto separator = fields.find_first_of("/ ");
    if (separator == fields.npos)
        return std::nullopt;
    const auto address = IpAddress::parse(std::string(fields.substr(0, separator)));
    const bool space_separator = fields[separator] == ' ';
    fields.remove_prefix(separator + 1);
    if (space_separator)
        fields.remove_prefix(std::min(fields.find_first_not_of(' '), fields.size()));
    const auto suffix = fields.substr(0, fields.find_first_of("/ "));
    if (!address || suffix.empty())
        return std::nullopt;

    std::uint32_t mask = 0;
    unsigned prefix = 0;
    const auto* end = suffix.data() + suffix.size();
    const auto parsed = std::from_chars(suffix.data(), end, prefix);
    if (parsed.ec == std::errc{} && parsed.ptr == end && prefix <= 255) {
        if (prefix > 32)
            return std::nullopt;
        if (prefix != 0)
            mask = 0xffffffffu << (32 - prefix);
    } else {
        const auto netmask = IpAddress::parse(std::string(suffix));
        if (!netmask)
            return std::nullopt;
        mask = network_order(*netmask);
        const auto host_bits = ~mask;
        if ((host_bits & (host_bits + 1u)) != 0)
            return std::nullopt;
    }
    return std::pair{network_order(*address) & mask, mask};
}

}

OutboundUrlFilter::OutboundUrlFilter(std::string_view disallow, std::string_view exceptions,
                                     HostResolver::Lookup lookup, InvalidEntry invalid_entry)
    : lookup_(lookup ? std::move(lookup) : HostResolver::Lookup(lookup_host)),
      disallowed_(parse(disallow, invalid_entry)), exceptions_(parse(exceptions, invalid_entry)) {}

std::vector<OutboundUrlFilter::Rule> OutboundUrlFilter::parse(
    std::string_view entries, const InvalidEntry& invalid_entry) const {
    std::vector<Rule> rules;
    const auto uris = native_config_uri_object_backend();
    while (!entries.empty()) {
        const auto separator = entries.find('|');
        const auto raw = entries.substr(0, separator);
        entries.remove_prefix(separator == entries.npos ? entries.size() : separator + 1);
        if (raw.empty())
            continue;

        const auto entry = trim_utf8(raw);
        if (entry.find('/') != entry.npos) {
            if (const auto network = parse_ipv4_network(entry))
                rules.push_back({network->first, network->second, std::nullopt});
            else if (invalid_entry)
                invalid_entry(entry, true);
            continue;
        }

        const auto uri = uris->try_create(std::make_shared<const std::u16string>(utf16("http://" + entry)),
                                          ConfigUriObjectKind::Absolute);
        const auto endpoint = uri ? uri->http_components() : nullptr;
        if (!endpoint) {
            if (invalid_entry)
                invalid_entry(entry, false);
            continue;
        }
        const auto host_component = endpoint->host();
        const auto host = host_component ? utf8(*host_component) : std::string{};
        const auto addresses = lookup_(host);
        if (!addresses)
            throw std::runtime_error("Unable to resolve outbound filter endpoint: " + host);
        for (const auto& address : *addresses)
            if (address && !address->v6())
                rules.push_back({network_order(*address), 0xffffffffu, endpoint->port()});
    }
    return rules;
}

bool OutboundUrlFilter::allows(const ConfigUriComponents& url) const {
    const auto host = url.host();
    return allows(host ? utf8(*host) : std::string{}, url.port());
}

bool OutboundUrlFilter::allows(std::string_view host, std::int32_t port) const {
    std::optional<std::vector<HostResolver::Address>> addresses;
    try {
        addresses = lookup_(std::string(host));
    } catch (...) {
        return true;
    }
    if (!addresses)
        return true;

    bool has_ipv4 = false;
    for (const auto& address : *addresses) {
        if (!address || address->v6())
            continue;
        has_ipv4 = true;
        const auto number = network_order(*address);
        const auto matches = [&](const Rule& rule) {
            return (number & rule.mask) == rule.address && (!rule.port || *rule.port == port);
        };
        if (std::ranges::any_of(disallowed_, matches) && !std::ranges::any_of(exceptions_, matches))
            return false;
    }
    return has_ipv4;
}

}
