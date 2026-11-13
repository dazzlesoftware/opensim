// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/config_uri_components.hpp"
#include "opensim/framework/host_resolver.hpp"
#include <string_view>

namespace opensim::framework {

class OutboundUrlFilter {
public:
    using InvalidEntry = std::function<void(std::string_view, bool network)>;
    static constexpr std::string_view default_disallow =
        "0.0.0.0/8|10.0.0.0/8|100.64.0.0/10|127.0.0.0/8|169.254.0.0/16|172.16.0.0/12|"
        "192.0.0.0/24|192.0.2.0/24|192.88.99.0/24|192.168.0.0/16|198.18.0.0/15|"
        "198.51.100.0/24|203.0.113.0/24|224.0.0.0/4|240.0.0.0/4|255.255.255.255/32";

    explicit OutboundUrlFilter(std::string_view disallow = default_disallow, std::string_view exceptions = {},
                               HostResolver::Lookup lookup = {}, InvalidEntry invalid_entry = {});
    bool allows(const ConfigUriComponents& url) const;
    bool allows(std::string_view host, std::int32_t port) const;

private:
    struct Rule {
        std::uint32_t address;
        std::uint32_t mask;
        std::optional<std::int32_t> port;
    };
    std::vector<Rule> parse(std::string_view entries, const InvalidEntry& invalid_entry) const;
    HostResolver::Lookup lookup_;
    std::vector<Rule> disallowed_, exceptions_;
};

}
