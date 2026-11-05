// Consumed runtime v8.0.31 ChainPal.OpenSsl/OpenSslX509ChainProcessor,
// OpenSslX509CertificateReader.GetPolicyData and native pal_x509.c. MIT .NET
// Foundation license: third_party/dotnet_collation/LICENSE.
#include "opensim/framework/config_http_linux_certificate.hpp"
#include "opensim/framework/config_http_linux_certificate_name.hpp"
#include <openssl/x509v3.h>
#include <openssl/err.h>
#include <algorithm>
#include <climits>
#include <ctime>
#include <set>
namespace opensim::framework {
namespace {
[[noreturn]] void crypto() {
    throw ConfigHttpLinuxCertificateError();
}
using Cert = std::unique_ptr<X509, decltype(&X509_free)>;
Cert decode(std::span<const std::uint8_t> der) {
    if (der.empty() || der.size() > LONG_MAX)
        crypto();
    auto at = der.data();
    Cert value(d2i_X509(nullptr, &at, static_cast<long>(der.size())), X509_free);
    if (!value)
        crypto();
    return value;
}
CertificateDer encode(X509* cert) {
    auto count = i2d_X509(cert, nullptr);
    if (count <= 0)
        crypto();
    CertificateDer result(static_cast<std::size_t>(count));
    auto at = result.data();
    if (i2d_X509(cert, &at) != count)
        crypto();
    return result;
}
CertificateDer name_der(X509_NAME* name) {
    auto count = i2d_X509_NAME(name, nullptr);
    if (count <= 0)
        crypto();
    CertificateDer result(static_cast<std::size_t>(count));
    auto at = result.data();
    if (i2d_X509_NAME(name, &at) != count)
        crypto();
    return result;
}
bool self_issued(X509* cert) {
    return name_der(X509_get_subject_name(cert)) == name_der(X509_get_issuer_name(cert));
}
bool incomplete(int code) {
    return code == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT ||
           code == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY;
}
struct Stack {
    STACK_OF(X509) * value;
    explicit Stack(STACK_OF(X509) * data) : value(data) {
        if (!value)
            crypto();
    }
    Stack() : Stack(sk_X509_new_null()) {
    }
    Stack(const Stack&) = delete;
    Stack& operator=(const Stack&) = delete;
    ~Stack() {
        sk_X509_pop_free(value, X509_free);
    }
    void add(Cert cert) {
        if (!sk_X509_push(value, cert.get()))
            crypto();
        cert.release();
    }
    void clear() {
        while (auto cert = sk_X509_pop(value))
            X509_free(cert);
    }
};
std::uint32_t status(int code) {
    switch (code) {
    case X509_V_OK:
        return 0;
    case X509_V_ERR_CERT_NOT_YET_VALID:
    case X509_V_ERR_CERT_HAS_EXPIRED:
    case X509_V_ERR_ERROR_IN_CERT_NOT_BEFORE_FIELD:
    case X509_V_ERR_ERROR_IN_CERT_NOT_AFTER_FIELD:
        return 1;
    case X509_V_ERR_CERT_REVOKED:
        return 4;
    case X509_V_ERR_UNABLE_TO_DECODE_ISSUER_PUBLIC_KEY:
    case X509_V_ERR_CERT_SIGNATURE_FAILURE:
        return 8;
    case X509_V_ERR_INVALID_PURPOSE:
        return 16;
    case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
    case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
    case X509_V_ERR_CERT_UNTRUSTED:
        return 32;
    case X509_V_ERR_UNABLE_TO_GET_CRL:
    case X509_V_ERR_UNABLE_TO_DECRYPT_CRL_SIGNATURE:
    case X509_V_ERR_CRL_SIGNATURE_FAILURE:
    case X509_V_ERR_CRL_NOT_YET_VALID:
    case X509_V_ERR_CRL_HAS_EXPIRED:
    case X509_V_ERR_ERROR_IN_CRL_LAST_UPDATE_FIELD:
    case X509_V_ERR_ERROR_IN_CRL_NEXT_UPDATE_FIELD:
    case X509_V_ERR_UNABLE_TO_GET_CRL_ISSUER:
    case X509_V_ERR_KEYUSAGE_NO_CRL_SIGN:
    case X509_V_ERR_UNHANDLED_CRITICAL_CRL_EXTENSION:
        return 64;
    case X509_V_ERR_INVALID_EXTENSION:
        return 256;
    case X509_V_ERR_INVALID_POLICY_EXTENSION:
    case X509_V_ERR_NO_EXPLICIT_POLICY:
        return 512;
    case X509_V_ERR_PATH_LENGTH_EXCEEDED:
    case X509_V_ERR_KEYUSAGE_NO_CERTSIGN:
    case X509_V_ERR_INVALID_NON_CA:
    case X509_V_ERR_KEYUSAGE_NO_DIGITAL_SIGNATURE:
    case X509_V_ERR_INVALID_CA:
        return 1024;
    case X509_V_ERR_UNSUPPORTED_NAME_SYNTAX:
        return 2048;
    case X509_V_ERR_SUBTREE_MINMAX:
        return 4096;
    case X509_V_ERR_PERMITTED_VIOLATION:
        return 16384;
    case X509_V_ERR_EXCLUDED_VIOLATION:
        return 32768;
    case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
    case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
    case X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE:
        return 65536;
    case X509_V_ERR_CERT_REJECTED:
        return 67108864;
    case X509_V_ERR_UNHANDLED_CRITICAL_EXTENSION:
        return 134217728;
    case X509_V_ERR_OUT_OF_MEM:
        throw std::bad_alloc();
    default:
        crypto();
    }
}
void unique(std::vector<std::uint32_t>& values, std::uint32_t value) {
    if (std::find(values.begin(), values.end(), value) == values.end())
        values.push_back(value);
}
bool contains(const std::vector<std::uint32_t>& values, std::uint32_t value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}
void remove(std::vector<std::uint32_t>& values, std::uint32_t value) {
    auto at = std::find(values.begin(), values.end(), value);
    if (at != values.end())
        values.erase(at);
}
CertificatePolicyData policy_data(X509* cert) {
    CertificatePolicyData result;
    for (int i = 0; i < X509_get_ext_count(cert); ++i) {
        auto ext = X509_get_ext(cert, i);
        if (!ext)
            crypto();
        auto object = X509_EXTENSION_get_object(ext);
        auto count = OBJ_obj2txt(nullptr, 0, object, 1);
        if (count <= 0)
            crypto();
        std::string oid(static_cast<std::size_t>(count) + 1, '\0');
        if (OBJ_obj2txt(oid.data(), static_cast<int>(oid.size()), object, 1) != count)
            crypto();
        oid.resize(count);
        CertificatePolicyData::Bytes* target = nullptr;
        if (oid == "1.3.6.1.4.1.311.21.10")
            target = &result.application_cert_policies;
        else if (oid == "2.5.29.32")
            target = &result.cert_policies;
        else if (oid == "2.5.29.33")
            target = &result.cert_policy_mappings;
        else if (oid == "2.5.29.36")
            target = &result.cert_policy_constraints;
        else if (oid == "2.5.29.37")
            target = &result.enhanced_key_usage;
        else if (oid == "2.5.29.54")
            target = &result.inhibit_any_policy;
        if (target) {
            auto data = X509_EXTENSION_get_data(ext);
            if (!data || data->length < 0)
                crypto();
            CertificateDer bytes;
            if (data->length) {
                if (!data->data)
                    crypto();
                bytes.assign(data->data, data->data + data->length);
            }
            *target = std::move(bytes);
        }
    }
    return result;
}
struct WorkingErrors {
    std::vector<std::set<int>> codes;
    std::exception_ptr failure;
};
int collect(int ok, X509_STORE_CTX* context) noexcept {
    if (ok)
        return ok;
    auto errors = static_cast<WorkingErrors*>(X509_STORE_CTX_get_app_data(context));
    try {
        auto code = X509_STORE_CTX_get_error(context);
        if (code != X509_V_OK && code != X509_V_ERR_CRL_NOT_YET_VALID &&
            code != X509_V_ERR_DIFFERENT_CRL_SCOPE) {
            auto depth = X509_STORE_CTX_get_error_depth(context);
            if (depth < 0)
                crypto();
            if (errors->codes.size() <= static_cast<std::size_t>(depth))
                errors->codes.resize(static_cast<std::size_t>(depth) + 1);
            errors->codes[depth].insert(code);
        }
        return 1;
    } catch (...) {
        errors->failure = std::current_exception();
        return -1;
    }
}
}
struct ConfigHttpLinuxCertificateChain::State {
    Cert leaf;
    std::unique_ptr<X509_STORE, decltype(&X509_STORE_free)> store{X509_STORE_new(), X509_STORE_free};
    Stack untrusted;
    std::unique_ptr<X509_STORE_CTX, decltype(&X509_STORE_CTX_free)> context{X509_STORE_CTX_new(),
                                                                            X509_STORE_CTX_free};
    ConfigHttpLinuxCertificatePolicy policy;
    std::vector<X509*> added_issuers;
    bool found = false, finished = false;
    int error = 0;
    State(std::span<const std::uint8_t> der, const ConfigHttpLinuxCertificateStores& stores,
          ConfigHttpLinuxCertificatePolicy options)
        : leaf(decode(der)), policy(std::move(options)) {
        ERR_clear_error();
        if (!store || !context)
            crypto();
        auto trust = [&](const CertificateDer& item) {
            auto cert = decode(item);
            if (!X509_STORE_add_cert(store.get(), cert.get()))
                crypto();
        };
        for (const auto& item : stores.user_intermediates)
            untrusted.add(decode(item));
        if (policy.custom_roots) {
            for (const auto& item : *policy.custom_roots) {
                auto cert = decode(item);
                if (self_issued(cert.get())) {
                    if (!X509_STORE_add_cert(store.get(), cert.get()))
                        crypto();
                } else
                    untrusted.add(std::move(cert));
            }
        } else
            for (const auto& item : stores.trusted)
                trust(item);
        for (const auto& item : stores.system_intermediates)
            untrusted.add(decode(item));
        auto when = policy.verification_unix_seconds.value_or(static_cast<std::int64_t>(std::time(nullptr)));
        X509_VERIFY_PARAM_set_time(X509_STORE_get0_param(store.get()), static_cast<time_t>(when));
        reset();
    }
    void reset() {
        ERR_clear_error();
        X509_STORE_CTX_cleanup(context.get());
        if (!X509_STORE_CTX_init(context.get(), store.get(), leaf.get(), untrusted.value))
            crypto();
        X509_STORE_CTX_set_flags(context.get(), X509_V_FLAG_CHECK_SS_SIGNATURE);
    }
    int verify() {
        ERR_clear_error();
        auto value = X509_verify_cert(context.get());
        error = X509_STORE_CTX_get_error(context.get());
        if (value < 0 && error == X509_V_OK)
            crypto();
        return error;
    }
};
ConfigHttpLinuxCertificateChain::ConfigHttpLinuxCertificateChain(
    std::span<const std::uint8_t> der, const ConfigHttpLinuxCertificateStores& stores,
    ConfigHttpLinuxCertificatePolicy policy)
    : state_(std::make_unique<State>(der, stores, std::move(policy))) {
}
ConfigHttpLinuxCertificateChain::~ConfigHttpLinuxCertificateChain() = default;
int ConfigHttpLinuxCertificateChain::find_first(const std::vector<CertificateDer>& extra) {
    auto& s = *state_;
    if (s.found || s.finished)
        throw std::logic_error("Certificate path already evaluated");
    s.found = true;
    s.verify();
    if (incomplete(s.error) && !extra.empty()) {
        for (const auto& item : extra)
            s.untrusted.add(decode(item));
        s.reset();
        s.verify();
    }
    return s.error;
}
std::optional<CertificateDer> ConfigHttpLinuxCertificateChain::missing_issuer_child() const {
    const auto& s = *state_;
    if (!s.found || s.finished || !incomplete(s.error))
        return {};
    auto cert = X509_STORE_CTX_get_current_cert(s.context.get());
    if (!cert)
        crypto();
    return encode(cert);
}
std::uintptr_t ConfigHttpLinuxCertificateChain::missing_issuer_identity() const {
    const auto& s = *state_;
    if (!s.found || s.finished || !incomplete(s.error))
        return 0;
    return reinterpret_cast<std::uintptr_t>(X509_STORE_CTX_get_current_cert(s.context.get()));
}
int ConfigHttpLinuxCertificateChain::add_issuer(std::span<const std::uint8_t> der) {
    auto& s = *state_;
    if (!s.found || s.finished || !incomplete(s.error))
        throw std::logic_error("Certificate path has no missing issuer");
    auto cert = decode(der);
    auto* identity = cert.get();
    s.untrusted.add(std::move(cert));
    s.added_issuers.push_back(identity);
    s.reset();
    return s.verify();
}
int ConfigHttpLinuxCertificateChain::add_issuer_aux(std::span<const std::uint8_t> bytes) {
    auto& s = *state_;
    if (!s.found || s.finished || !incomplete(s.error))
        throw std::logic_error("Certificate path has no missing issuer");
    if (bytes.empty() || bytes.size() > LONG_MAX)
        crypto();
    auto* at = bytes.data();
    Cert cert(d2i_X509_AUX(nullptr, &at, static_cast<long>(bytes.size())), X509_free);
    if (!cert)
        crypto();
    auto* identity = cert.get();
    s.untrusted.add(std::move(cert));
    s.added_issuers.push_back(identity);
    s.reset();
    return s.verify();
}
std::vector<std::size_t> ConfigHttpLinuxCertificateChain::selected_added_issuers() const {
    const auto& s = *state_;
    if (!s.found || s.finished)
        throw std::logic_error("Certificate path is not ready");
    std::vector<std::size_t> result;
    auto* chain = X509_STORE_CTX_get0_chain(s.context.get());
    if (!chain)
        crypto();
    for (std::size_t i = 0; i < s.added_issuers.size(); ++i)
        for (int j = 0; j < sk_X509_num(chain); ++j)
            if (s.added_issuers[i] == sk_X509_value(chain, j)) {
                result.push_back(i);
                break;
            }
    return result;
}
std::size_t ConfigHttpLinuxCertificateChain::issuer_addition_count() const {
    return state_->added_issuers.size();
}
ConfigHttpLinuxCertificateResult ConfigHttpLinuxCertificateChain::finish(std::u16string_view host) {
    auto& s = *state_;
    if (!s.found || s.finished)
        throw std::logic_error("Certificate path is not ready");
    s.finished = true;
    // Commit only the selected chain, in reverse order, before the all-errors
    // verification pass. Unselected alternatives cannot change the committed path.
    Stack selected(X509_STORE_CTX_get1_chain(s.context.get())), committed;
    while (auto cert = sk_X509_pop(selected.value)) {
        if (cert == s.leaf.get())
            X509_free(cert);
        else
            committed.add(Cert(cert, X509_free));
    }
    s.untrusted.clear();
    while (auto cert = sk_X509_shift(committed.value))
        s.untrusted.add(Cert(cert, X509_free));
    WorkingErrors errors;
    if (s.error != X509_V_OK) {
        s.reset();
        X509_STORE_CTX_set_app_data(s.context.get(), &errors);
        X509_STORE_CTX_set_verify_cb(s.context.get(), collect);
        s.verify();
        if (errors.failure)
            std::rethrow_exception(errors.failure);
    }
    auto raw = X509_STORE_CTX_get0_chain(s.context.get());
    if (!raw)
        crypto();
    ConfigHttpLinuxCertificateResult result;
    std::vector<CertificatePolicyData> data;
    bool signature = false;
    auto count = sk_X509_num(raw);
    bool has_policy = !s.policy.application_policies.empty() || !s.policy.certificate_policies.empty();
    for (int i = 0; i < count; ++i) {
        ConfigHttpLinuxChainElement element;
        auto cert = sk_X509_value(raw, i);
        element.certificate = encode(cert);
        auto previous_signature = signature;
        if (static_cast<std::size_t>(i) < errors.codes.size())
            for (auto code : errors.codes[i]) {
                auto flag = status(code);
                unique(element.status, flag);
                unique(result.status, flag);
                signature |= flag == 8;
            }
        if (i == count - 1 && !previous_signature && signature && !contains(result.status, 65536) &&
            !contains(result.status, 32)) {
            remove(element.status, 8);
            remove(result.status, 8);
        }
        if (contains(element.status, 64)) {
            unique(element.status, 16777216);
            unique(result.status, 16777216);
        }
        if (has_policy)
            data.push_back(policy_data(cert));
        result.elements.push_back(std::move(element));
    }
    if (has_policy) {
        CertificatePolicyChain policy(data);
        if (!policy.matches_application_policies(s.policy.application_policies) ||
            !policy.matches_certificate_policies(s.policy.certificate_policies)) {
            unique(result.status, 16);
            for (auto& element : result.elements)
                unique(element.status, 16);
        }
    }
    if (!result.status.empty())
        result.errors |= 4;
    if (s.policy.check_name && !config_http_linux_certificate_name(encode(s.leaf.get()), host))
        result.errors |= 2;
    return result;
}
ConfigHttpLinuxCertificateResult
config_http_linux_certificate_errors(std::span<const std::uint8_t> leaf,
                                     const std::vector<CertificateDer>& extra,
                                     const ConfigHttpLinuxCertificateStores& stores, std::u16string_view host,
                                     ConfigHttpLinuxCertificatePolicy policy) {
    if (leaf.empty()) {
        ConfigHttpLinuxCertificateResult result;
        result.errors = 1;
        return result;
    }
    ConfigHttpLinuxCertificateChain chain(leaf, stores, std::move(policy));
    chain.find_first(extra);
    return chain.finish(host);
}
}
