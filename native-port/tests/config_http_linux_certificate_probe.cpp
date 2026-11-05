#include "opensim/framework/config_http_linux_certificate.hpp"
#include "opensim/framework/config_http_linux_trust_stores.hpp"
#include "opensim/framework/config_uri_components.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace opensim::framework;
CertificateDer bytes(const std::string& value) {
    CertificateDer out;
    if (value != "_")
        for (std::size_t i = 0; i < value.size(); i += 2)
            out.push_back(static_cast<std::uint8_t>(std::stoul(value.substr(i, 2), nullptr, 16)));
    return out;
}
std::string hex(std::span<const std::uint8_t> value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto b : value)
        out << std::setw(2) << unsigned(b);
    return out.str();
}
std::string text_hex(std::u16string_view value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto c : value)
        out << std::setw(4) << unsigned(c);
    return out.str();
}
std::string flags(const std::vector<std::uint32_t>& value) {
    std::string out;
    for (auto n : value) {
        if (!out.empty())
            out += ',';
        out += std::to_string(n);
    }
    return out.empty() ? "-" : out;
}
int main(int argc, char**) {
    const bool cached = argc > 1;
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            std::istringstream in(line);
            std::string value;
            in >> value;
            auto leaf = bytes(value);
            ConfigHttpLinuxCertificateStores stores;
            ConfigHttpLinuxCertificatePolicy policy;
            auto list = [&] {
                std::size_t count;
                in >> count;
                std::vector<CertificateDer> values;
                while (count--) {
                    in >> value;
                    values.push_back(bytes(value));
                }
                return values;
            };
            auto extra = list();
            stores.trusted = list();
            stores.user_intermediates = list();
            stores.system_intermediates = list();
            int custom;
            in >> custom;
            if (custom)
                policy.custom_roots = list();
            std::int64_t when;
            int check;
            in >> when >> check >> value;
            policy.verification_unix_seconds = when;
            policy.check_name = check != 0;
            std::u16string host;
            if (value != "_")
                for (std::size_t i = 0; i < value.size(); i += 4)
                    host += static_cast<char16_t>(std::stoul(value.substr(i, 4), nullptr, 16));
            auto oids = [&] {
                std::size_t count;
                in >> count;
                std::vector<CertificatePolicyChain::Oid> values;
                while (count--) {
                    in >> value;
                    values.push_back(value == "-" ? std::nullopt : std::optional<std::string>{value});
                }
                return values;
            };
            policy.application_policies = oids();
            policy.certificate_policies = oids();
            auto issuers = list();
            if (!in)
                return 2;
            ConfigHttpLinuxCertificateResult result;
            std::string trace;
            if (leaf.empty())
                result.errors = 1;
            else if (cached && issuers.empty())
                result = config_http_linux_cached_certificate_errors(leaf, extra, host, policy);
            else {
                if (cached)
                    stores = config_http_linux_default_certificate_stores(policy.custom_roots.has_value());
                ConfigHttpLinuxCertificateChain chain(leaf, stores, policy);
                chain.find_first(extra);
                for (const auto& issuer : issuers) {
                    auto child = chain.missing_issuer_child();
                    if (!child)
                        return 3;
                    if (!trace.empty())
                        trace += ',';
                    trace += hex(*child);
                    chain.add_issuer(issuer);
                }
                result = chain.finish(host);
            }
            std::cout << result.errors << ' ' << flags(result.status)
                      << " T:" << (trace.empty() ? "-" : trace);
            for (const auto& element : result.elements)
                std::cout << ' ' << hex(element.certificate) << ':' << flags(element.status);
            std::cout << '\n';
        } catch (const CertificatePolicyError& e) {
            std::cout << "E " << e.source_type << ' ' << e.hresult << ' '
                      << text_hex(utf16_replacing(e.what())) << '\n';
        } catch (const ConfigHttpLinuxCertificateError& e) {
            std::cout << "E " << e.source_type << ' ' << e.hresult << ' '
                      << text_hex(utf16_replacing(e.what())) << '\n';
        } catch (const ConfigUriComponentsError& e) {
            std::cout << "E " << e.source_type << ' ' << e.hresult << ' ' << text_hex(e.message) << '\n';
        } catch (const ConfigHttpLinuxStoreError& e) {
            std::cout << "E " << e.source_type << ' ' << e.hresult << ' ' << text_hex(e.message) << '\n';
        }
    }
}
