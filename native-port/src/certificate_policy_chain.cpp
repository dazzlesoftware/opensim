// Adapted from runtime v8.0.31 X509Certificates/CertificatePolicy.cs and its
// generated PolicyInformation/PolicyConstraints/CertificatePolicyMapping ASN.1.
// .NET Foundation MIT license: third_party/dotnet_collation/LICENSE.
#include "opensim/framework/certificate_policy_chain.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <set>
#include <span>
namespace opensim::framework {
namespace {
using Bytes = std::span<const std::uint8_t>;
using Oids = std::optional<std::set<std::string>>;
[[noreturn]] void corrupt() {
    throw CertificatePolicyError();
}
void decrement(std::int32_t& value) {
    value = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(value) - std::uint32_t{1});
}
struct Der {
    Bytes data;
    bool empty() const {
        return data.empty();
    }
    unsigned peek() const {
        if (empty())
            corrupt();
        return data.front();
    }
    Bytes take(std::optional<unsigned> expected = {}) {
        if (data.size() < 2)
            corrupt();
        std::size_t at = 1;
        auto tag = data[0];
        if ((tag & 31) == 31) {
            if (at == data.size() || data[at] == 0x80)
                corrupt();
            std::uint64_t number = 0;
            bool done = false;
            while (at < data.size()) {
                auto b = data[at++];
                number = (number << 7) | (b & 127);
                if (number > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
                    corrupt();
                if (!(b & 128)) {
                    done = true;
                    break;
                }
            }
            if (!done || number < 31)
                corrupt();
        }
        if (expected && tag != *expected)
            corrupt();
        if (at == data.size())
            corrupt();
        std::size_t length = data[at++];
        if (length & 128) {
            auto count = length & 127;
            if (!count || count > sizeof(std::size_t) || count > data.size() - at || data[at] == 0)
                corrupt();
            length = 0;
            for (std::size_t i = 0; i < count; ++i) {
                if (length > (std::numeric_limits<std::size_t>::max() >> 8))
                    corrupt();
                length = (length << 8) | data[at++];
            }
            if (length < 128)
                corrupt();
        }
        if (length > data.size() - at)
            corrupt();
        auto value = data.subspan(at, length);
        data = data.subspan(at + length);
        return value;
    }
    void end() const {
        if (!empty())
            corrupt();
    }
    Der sequence() {
        return Der{take(0x30)};
    }
    std::int32_t integer(unsigned tag = 2) {
        auto value = take(tag);
        if (value.empty() || value.size() > 4)
            corrupt();
        if (value.size() > 1 && ((value[0] == 0 && value[1] < 128) || (value[0] == 255 && value[1] >= 128)))
            corrupt();
        std::uint32_t number = (value[0] & 128) ? 0xffffffffu : 0;
        for (auto b : value)
            number = (number << 8) | b;
        return std::bit_cast<std::int32_t>(number);
    }
    std::string oid() {
        auto bytes = take(6);
        if (bytes.empty())
            corrupt();
        std::vector<std::string> arcs;
        std::string decimal = "0";
        bool start = true;
        unsigned bits = 0;
        for (auto b : bytes) {
            if (start) {
                if (b == 128)
                    corrupt();
                bits = 0;
                for (unsigned v = b & 127; v; v >>= 1)
                    ++bits;
            } else
                bits += 7;
            if (bits > 128)
                corrupt();
            unsigned carry = b & 127;
            for (auto it = decimal.rbegin(); it != decimal.rend(); ++it) {
                unsigned n = unsigned(*it - '0') * 128 + carry;
                *it = char('0' + n % 10);
                carry = n / 10;
            }
            while (carry) {
                decimal.insert(decimal.begin(), char('0' + carry % 10));
                carry /= 10;
            }
            if (!(b & 128)) {
                arcs.push_back(decimal);
                decimal.assign(1, '0');
                start = true;
            } else
                start = false;
        }
        if (!start || arcs.empty())
            corrupt();
        auto first = std::move(arcs.front());
        unsigned subtract = 80;
        std::string result(1, '2');
        if (first.size() <= 2) {
            auto n = std::stoul(first);
            if (n < 40) {
                result.front() = '0';
                subtract = 0;
            } else if (n < 80) {
                result.front() = '1';
                subtract = 40;
            }
        }
        unsigned borrow = subtract;
        for (auto it = first.rbegin(); it != first.rend() && borrow; ++it) {
            int digit = *it - '0' - int(borrow % 10);
            borrow /= 10;
            if (digit < 0) {
                digit += 10;
                ++borrow;
            }
            *it = char('0' + digit);
        }
        if (borrow)
            corrupt();
        auto zero = first.find_first_not_of('0');
        if (zero == first.npos)
            first.assign(1, '0');
        else
            first.erase(0, zero);
        result.push_back('.');
        result += first;
        for (std::size_t i = 1; i < arcs.size(); ++i) {
            result.push_back('.');
            result += arcs[i];
        }
        return result;
    }
};
Der sequence(const std::vector<std::uint8_t>& bytes) {
    Der reader{bytes};
    auto value = reader.sequence();
    reader.end();
    return value;
}
std::set<std::string> eku(const std::vector<std::uint8_t>& bytes) {
    auto reader = sequence(bytes);
    std::set<std::string> result;
    while (!reader.empty())
        result.insert(reader.oid());
    return result;
}
std::set<std::string> policies(const std::vector<std::uint8_t>& bytes) {
    auto reader = sequence(bytes);
    std::set<std::string> result;
    while (!reader.empty()) {
        auto item = reader.sequence();
        result.insert(item.oid());
        if (!item.empty())
            item.take();
        item.end();
    }
    return result;
}
struct Policy {
    Oids application, certificate;
    std::optional<std::vector<std::pair<std::string, std::string>>> mappings;
    bool implicit_app = true, implicit_cert = true, any_app = false, any_cert = false;
    std::optional<std::int32_t> inhibit_any, inhibit_mapping, require_explicit;
};
Policy read(const CertificatePolicyData& data) {
    Policy policy;
    if (data.application_cert_policies)
        policy.application = policies(*data.application_cert_policies);
    if (data.cert_policies)
        policy.certificate = policies(*data.cert_policies);
    if (data.cert_policy_mappings) {
        policy.mappings.emplace();
        auto reader = sequence(*data.cert_policy_mappings);
        while (!reader.empty()) {
            auto pair = reader.sequence();
            auto issuer = pair.oid(), subject = pair.oid();
            pair.end();
            policy.mappings->emplace_back(std::move(issuer), std::move(subject));
        }
    }
    if (data.cert_policy_constraints) {
        auto reader = sequence(*data.cert_policy_constraints);
        if (!reader.empty() && (reader.peek() & 0xdf) == 0x80)
            policy.require_explicit = reader.integer(0x80);
        if (!reader.empty() && (reader.peek() & 0xdf) == 0x81)
            policy.inhibit_mapping = reader.integer(0x81);
        reader.end();
    }
    if (data.enhanced_key_usage && !policy.application)
        policy.application = eku(*data.enhanced_key_usage);
    if (data.inhibit_any_policy) {
        Der reader{*data.inhibit_any_policy};
        policy.inhibit_any = reader.integer();
        reader.end();
    }
    policy.implicit_app = !policy.application;
    policy.implicit_cert = !policy.certificate;
    if (policy.application)
        policy.any_app = policy.application->erase("2.5.29.32.0") != 0;
    if (policy.certificate)
        policy.any_cert = policy.certificate->erase("2.5.29.32.0") != 0;
    return policy;
}
}
struct CertificatePolicyChain::State {
    std::vector<Policy> policies;
    bool fail_all_certificate = false;
};
CertificatePolicyChain::CertificatePolicyChain(const std::vector<CertificatePolicyData>& chain) {
    if (chain.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        throw std::length_error("Certificate chain exceeds native managed-length boundary");
    auto state = std::make_shared<State>();
    for (const auto& data : chain)
        state->policies.push_back(read(data));
    auto explicit_depth = static_cast<std::int32_t>(chain.size()), any_depth = explicit_depth,
         mapping_depth = explicit_depth;
    for (auto it = state->policies.rbegin(); it != state->policies.rend(); ++it) {
        auto& policy = *it;
        if (!policy.certificate && explicit_depth <= 0)
            state->fail_all_certificate = true;
        if (any_depth <= 0) {
            policy.implicit_cert = false;
            policy.any_cert = false;
        } else
            decrement(any_depth);
        if (mapping_depth <= 0)
            policy.mappings.reset();
        else
            decrement(any_depth); // Pinned source decrements this counter twice, unchecked.
        if (explicit_depth <= 0) {
            policy.implicit_cert = false;
            policy.implicit_app = false;
        } else
            decrement(explicit_depth);
        if (policy.inhibit_any)
            any_depth = std::min(any_depth, *policy.inhibit_any);
        if (policy.inhibit_mapping)
            mapping_depth = std::min(mapping_depth, *policy.inhibit_mapping);
        if (policy.require_explicit)
            explicit_depth = std::min(explicit_depth, *policy.require_explicit);
    }
    state_ = std::move(state);
}
bool CertificatePolicyChain::matches_application_policy(const Oid& oid) const {
    for (auto it = state_->policies.rbegin(); it != state_->policies.rend(); ++it)
        if (!it->implicit_app && !it->any_app &&
            (!oid || !it->application || !it->application->contains(*oid)))
            return false;
    return true;
}
bool CertificatePolicyChain::matches_certificate_policy(const Oid& oid) const {
    if (state_->fail_all_certificate)
        return false;
    auto current = oid;
    for (auto it = state_->policies.rbegin(); it != state_->policies.rend(); ++it) {
        const auto& policy = *it;
        auto before = current;
        if (policy.mappings && before)
            for (const auto& pair : *policy.mappings)
                if (pair.first == *before)
                    current = pair.second;
        if (!policy.implicit_cert && !policy.any_cert &&
            (!before || !policy.certificate || !policy.certificate->contains(*before)))
            return false;
    }
    return true;
}
bool CertificatePolicyChain::matches_application_policies(const std::vector<Oid>& oids) const {
    for (const auto& oid : oids)
        if (!matches_application_policy(oid))
            return false;
    return true;
}
bool CertificatePolicyChain::matches_certificate_policies(const std::vector<Oid>& oids) const {
    for (const auto& oid : oids)
        if (!matches_certificate_policy(oid))
            return false;
    return true;
}
}
