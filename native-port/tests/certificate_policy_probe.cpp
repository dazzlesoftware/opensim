#include "opensim/framework/certificate_policy_chain.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace opensim::framework;
CertificatePolicyData::Bytes bytes(const std::string& text) {
    if (text == "-")
        return {};
    std::vector<std::uint8_t> result;
    if (text != "_")
        for (std::size_t i = 0; i < text.size(); i += 2)
            result.push_back(static_cast<std::uint8_t>(std::stoul(text.substr(i, 2), nullptr, 16)));
    return result;
}
std::string hex(std::string_view text) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (unsigned char c : text)
        out << std::setw(4) << unsigned(c);
    return out.str();
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            std::istringstream in(line);
            std::size_t count;
            in >> count;
            std::vector<CertificatePolicyData> data;
            std::string text;
            for (std::size_t i = 0; i < count; ++i) {
                CertificatePolicyData item;
                for (auto target :
                     {&item.application_cert_policies, &item.cert_policies, &item.cert_policy_mappings,
                      &item.cert_policy_constraints, &item.enhanced_key_usage, &item.inhibit_any_policy}) {
                    in >> text;
                    *target = bytes(text);
                }
                data.push_back(std::move(item));
            }
            CertificatePolicyChain chain(data);
            char mode;
            in >> mode >> count;
            std::vector<CertificatePolicyChain::Oid> queries;
            for (std::size_t i = 0; i < count; ++i) {
                in >> text;
                queries.push_back(text == "-" ? std::nullopt
                                              : std::optional<std::string>{text == "_" ? "" : text});
            }
            bool result;
            switch (mode) {
            case 'A':
                result = chain.matches_application_policy(queries.at(0));
                break;
            case 'C':
                result = chain.matches_certificate_policy(queries.at(0));
                break;
            case 'a':
                result = chain.matches_application_policies(queries);
                break;
            case 'c':
                result = chain.matches_certificate_policies(queries);
                break;
            default:
                return 2;
            }
            std::cout << (result ? "1" : "0") << '\n';
        } catch (const CertificatePolicyError& e) {
            std::cout << "E " << e.source_type << ' ' << e.hresult << ' ' << hex(e.what()) << '\n';
        }
    }
}
