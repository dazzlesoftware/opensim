// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/atomic_shared_ptr.hpp"
#include "opensim/network/script_http_transport.hpp"
#include "opensim/framework/config_http_environment_proxy.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/config_uri_network_identity.hpp"
#include "opensim/framework/dotnet_regex_interpreter.hpp"
#include "opensim/framework/execution_stack.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <array>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace opensim::network {
namespace {
using namespace framework;

std::string machine_name(bool domain) {
#ifdef _WIN32
    std::array<wchar_t, 256> buffer{};
    DWORD size = static_cast<DWORD>(buffer.size());
    const auto kind = domain ? ComputerNameDnsDomain : ComputerNameDnsHostname;
    if (!GetComputerNameExW(kind, buffer.data(), &size))
        return {};
    return utf8(std::u16string(buffer.begin(), buffer.begin() + size));
#else
    std::array<char, 256> buffer{};
    const auto result = domain ? getdomainname(buffer.data(), buffer.size() - 1)
                               : gethostname(buffer.data(), buffer.size() - 1);
    return result == 0 ? std::string(buffer.data()) : std::string{};
#endif
}

bool ascii_equal(std::string_view a, std::string_view b) {
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c; };
    return std::ranges::equal(a, b, [&](char left, char right) { return lower(left) == lower(right); });
}

bool is_local(const ConfigUriObjectPtr& target, const std::string& host) {
    if (native_config_uri_network_identity(target).loopback)
        return true;
    if (const auto ip = IpAddress::parse(host)) {
        const auto name = machine_name(false);
        const auto addresses = name.empty() ? std::nullopt : HostResolver::system_lookup(name);
        if (!addresses)
            return false;
        return std::ranges::any_of(*addresses, [&](const auto& address) {
            return address->v6() == ip->v6() && address->bytes() == ip->bytes() &&
                   (!ip->v6() || address->scope_id() == ip->scope_id());
        });
    }
    const auto dot = host.find('.');
    return dot == host.npos || ascii_equal(std::string_view(host).substr(dot), "." + machine_name(true));
}

class ScriptHttpProxy final : public ConfigHttpProxy {
public:
    ScriptHttpProxy(ConfigUriObjectPtr address, std::string_view exceptions) : address_(std::move(address)) {
        if (exceptions.empty())
            return;
        for (;;) {
            const auto separator = exceptions.find(';');
            auto expression = std::make_shared<const std::u16string>(utf16_replacing(exceptions.substr(0, separator)));
            constexpr int ignore_case_and_invariant = 1 | 512;
            auto parsed = dotnet_regex_parse(expression, ignore_case_and_invariant,
                                             [] { return std::u16string{}; }, sufficient_execution_stack);
            bypass_.emplace_back(std::move(expression), std::move(parsed));
            if (separator == exceptions.npos)
                break;
            exceptions.remove_prefix(separator + 1);
        }
    }

    ConfigUriObjectPtr get_proxy(ConfigUriObjectPtr target) const override {
        return is_bypassed(target) ? target : address_;
    }

    bool is_bypassed(ConfigUriObjectPtr target) const override {
        const auto components = target->http_components();
        if (!components)
            return false;
        const auto host = utf8(*components->host());
        if (is_local(target, host))
            return true;
        if (bypass_.empty())
            return false;
        const auto scheme = *components->scheme();
        auto origin = scheme + u"://" + *components->host();
        const auto port = components->port();
        if (port != (scheme == u"https" ? 443 : 80))
            origin += u":" + utf16(std::to_string(port));
        const auto input = std::make_shared<const std::u16string>(std::move(origin));
        return std::ranges::any_of(bypass_, [&](const auto& pattern) {
            DotnetRegexInterpreter runner(pattern.second, pattern.first, sufficient_execution_stack);
            return runner.scan(input, 0, static_cast<std::int32_t>(input->size()), 0, -1,
                               DotnetRegexRunnerMode::ExistenceRequired).success;
        });
    }

    std::shared_ptr<ConfigXmlCredentials> credentials() const override { return credentials_.load(); }
    void credentials(std::shared_ptr<ConfigXmlCredentials> value) override { credentials_.store(std::move(value)); }

private:
    ConfigUriObjectPtr address_;
    std::vector<std::pair<DotnetRegexNodeText, DotnetRegexParsedPattern>> bypass_;
    opensim::framework::AtomicSharedPtr<ConfigXmlCredentials> credentials_;
};
}

std::shared_ptr<framework::ConfigXmlProxy> make_script_http_proxy(std::string address, std::string_view exceptions) {
    if (address.empty())
        return {};
    if (address.find("://") == address.npos)
        address.insert(0, "http://");
    const auto uris = framework::native_config_uri_object_backend();
    return std::make_shared<ScriptHttpProxy>(
        uris->create(std::make_shared<const std::u16string>(framework::utf16_replacing(address)),
                     framework::ConfigUriObjectKind::Absolute),
        exceptions);
}

}
