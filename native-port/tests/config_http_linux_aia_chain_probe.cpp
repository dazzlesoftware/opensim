#include "opensim/framework/config_http_linux_aia.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace opensim::framework;
CertificateDer bytes(const std::string& text) {
    CertificateDer out;
    if (text != "_")
        for (std::size_t i = 0; i < text.size(); i += 2)
            out.push_back(static_cast<std::uint8_t>(std::stoul(text.substr(i, 2), nullptr, 16)));
    return out;
}
std::string hex(std::span<const std::uint8_t> value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto c : value)
        out << std::setw(2) << unsigned(c);
    return out.str();
}
std::string hex(std::u16string_view value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto c : value)
        out << std::setw(4) << unsigned(c);
    return out.str();
}
std::string flags(const std::vector<std::uint32_t>& values) {
    std::string result;
    for (auto n : values) {
        if (!result.empty())
            result += ',';
        result += std::to_string(n);
    }
    return result.empty() ? "-" : result;
}
int main(int argc, char** argv) {
    const bool http = argc == 2 && std::string_view(argv[1]) == "--http-downloads";
    auto uris = native_config_uri_object_backend();
    ConfigXmlNativeHttpServices services;
    services.error = [](std::string, int) {
        return std::make_exception_ptr(std::runtime_error("Opaque AIA download failure"));
    };
    std::unique_ptr<ConfigHttpX509ResourceClient> client;
    if (http)
        client = std::make_unique<ConfigHttpX509ResourceClient>(services);
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string value;
        in >> value;
        auto leaf = bytes(value);
        ConfigHttpLinuxCertificateStores stores;
        ConfigHttpLinuxCertificatePolicy policy;
        policy.application_policies.clear();
        policy.check_name = false;
        policy.verification_unix_seconds = 1893456000;
        auto list = [&] {
            std::size_t count;
            in >> count;
            std::vector<CertificateDer> result;
            while (count--) {
                in >> value;
                result.push_back(bytes(value));
            }
            return result;
        };
        policy.custom_roots = list();
        auto extra = list(), manual = list();
        int mode, timeout;
        std::size_t count;
        in >> mode >> timeout >> count;
        std::vector<std::pair<std::u16string, std::optional<CertificateDer>>> replies;
        while (count--) {
            std::string uri, reply;
            in >> uri >> reply;
            std::u16string text;
            for (std::size_t i = 0; i < uri.size(); i += 4)
                text += static_cast<char16_t>(std::stoul(uri.substr(i, 4), nullptr, 16));
            replies.emplace_back(std::move(text), reply == "_" ? std::nullopt : std::optional{bytes(reply)});
        }
        if (!in)
            return 2;
        ConfigHttpLinuxCertificateChain chain(leaf, stores, policy);
        const int first = chain.find_first(extra);
        int last = first;
        std::vector<CertificateDer> downloaded;
        std::vector<std::size_t> selected;
        std::vector<std::string> calls;
        if (mode) {
            for (const auto& der : manual)
                last = chain.add_issuer(der);
            if (mode == 1)
                selected = chain.selected_added_issuers();
        }
        if (mode != 1 && (last == 2 || last == 20)) {
            if (http) {
                auto aia = config_http_linux_find_chain_via_assets(
                    chain, *uris, std::chrono::milliseconds(timeout),
                    ConfigHttpLinuxIssuerAssetDownload{[&](auto uri, auto duration) {
                        calls.push_back(hex(uri) + "/" + std::to_string(duration.count()));
                        return config_http_linux_download_certificate(*client, uri, duration.count() * 10000);
                    }});
                last = aia.native_error;
                for (auto& cert : aia.downloaded)
                    downloaded.push_back(std::move(cert.der));
            } else {
                auto aia = config_http_linux_find_chain_via_aia(
                    chain, *uris, std::chrono::milliseconds(timeout),
                    [&](auto uri, auto duration) -> std::optional<CertificateDer> {
                        calls.push_back(hex(uri) + "/" + std::to_string(duration.count()));
                        for (const auto& [address, reply] : replies)
                            if (address == uri)
                                return reply;
                        throw std::runtime_error("Unmapped issuer URL");
                    });
                last = aia.native_error;
                downloaded = std::move(aia.downloaded);
            }
        }
        auto result = chain.finish(u"");
        std::cout << first << ' ' << last << " D:";
        if (downloaded.empty())
            std::cout << '-';
        for (std::size_t i = 0; i < downloaded.size(); ++i)
            std::cout << (i ? "," : "") << hex(downloaded[i]);
        std::cout << " I:";
        if (selected.empty())
            std::cout << '-';
        for (std::size_t i = 0; i < selected.size(); ++i)
            std::cout << (i ? "," : "") << selected[i];
        std::cout << " F:" << flags(result.status);
        for (const auto& element : result.elements)
            std::cout << ' ' << hex(element.certificate) << ':' << flags(element.status);
        std::cout << " U:";
        if (calls.empty())
            std::cout << '-';
        for (std::size_t i = 0; i < calls.size(); ++i)
            std::cout << (i ? "," : "") << calls[i];
        std::cout << '\n';
    }
}
