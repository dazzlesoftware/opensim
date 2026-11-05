// Consumed runtime v8.0.31 OpenSslX509ChainProcessor.FindHttpAiaRecord /
// FindChainViaAia and generated AccessDescription/GeneralName decoders.
// .NET Foundation MIT license: third_party/dotnet_collation/LICENSE.
#include "opensim/framework/config_http_linux_aia.hpp"
#include "opensim/framework/text.hpp"
#include <openssl/x509v3.h>
#include <openssl/err.h>
#include <algorithm>
#include <climits>
#include <limits>
namespace opensim::framework {
namespace {
using Bytes = std::span<const std::uint8_t>;
struct Invalid {};
[[noreturn]] void invalid() {
    throw Invalid{};
}
struct Value {
    unsigned tag;
    Bytes bytes, encoded;
};
struct Der {
    Bytes bytes;
    unsigned peek() const {
        if (bytes.empty())
            invalid();
        return bytes.front();
    }
    Value read() {
        if (bytes.size() < 2)
            invalid();
        std::size_t at = 1;
        auto tag = bytes.front();
        if ((tag & 31) == 31) {
            if (bytes[at] == 0x80)
                invalid();
            std::uint64_t number = 0;
            bool done = false;
            while (at < bytes.size()) {
                auto b = bytes[at++];
                number = (number << 7) | (b & 127);
                if (number > INT_MAX)
                    invalid();
                if (!(b & 128)) {
                    done = true;
                    break;
                }
            }
            if (!done || number < 31)
                invalid();
        }
        if (at == bytes.size())
            invalid();
        std::size_t length = bytes[at++];
        if (length & 128) {
            auto count = length & 127;
            if (!count || count > sizeof(std::size_t) || count > bytes.size() - at || bytes[at] == 0)
                invalid();
            length = 0;
            for (std::size_t i = 0; i < count; ++i) {
                if (length > (std::numeric_limits<std::size_t>::max() >> 8))
                    invalid();
                length = (length << 8) | bytes[at++];
            }
            if (length < 128)
                invalid();
        }
        if (length > bytes.size() - at)
            invalid();
        Value value{tag, bytes.subspan(at, length), bytes.first(at + length)};
        bytes = bytes.subspan(at + length);
        return value;
    }
    Der sequence(unsigned tag = 0x30) {
        auto v = read();
        if (v.tag != tag)
            invalid();
        return {v.bytes};
    }
    void end() const {
        if (!bytes.empty())
            invalid();
    }
};
std::string oid(Value value, unsigned tag = 6) {
    if (value.tag != tag || value.bytes.empty())
        invalid();
    bool start = true;
    unsigned bits = 0;
    for (auto b : value.bytes) {
        if (start) {
            if (b == 128)
                invalid();
            bits = 0;
            for (unsigned n = b & 127; n; n >>= 1)
                ++bits;
        } else
            bits += 7;
        if (bits > 128)
            invalid();
        start = !(b & 128);
    }
    if (!start)
        invalid();
    // The content has already passed the runtime OID size/canonical checks.
    // Decode a universal-tagged copy for context-specific RegisteredId as well.
    std::vector<std::uint8_t> encoded(value.encoded.begin(), value.encoded.end());
    encoded.front() = 6;
    const auto* at = encoded.data();
    std::unique_ptr<ASN1_OBJECT, decltype(&ASN1_OBJECT_free)> object(
        d2i_ASN1_OBJECT(nullptr, &at, static_cast<long>(encoded.size())), ASN1_OBJECT_free);
    if (!object) {
        ERR_clear_error();
        invalid();
    }
    int size = OBJ_obj2txt(nullptr, 0, object.get(), 1);
    if (size <= 0)
        invalid();
    std::string result(static_cast<std::size_t>(size) + 1, '\0');
    OBJ_obj2txt(result.data(), size + 1, object.get(), 1);
    result.resize(size);
    return result;
}
std::u16string ia5(Value value, unsigned tag) {
    if (value.tag != tag)
        invalid();
    std::u16string result;
    for (auto b : value.bytes) {
        if (b > 127)
            invalid();
        result.push_back(b);
    }
    return result;
}
void directory_string(Der& reader) {
    auto value = reader.read();
    if ((value.tag & 0xdf) == 28)
        return; // UniversalString is opaque in the generated decoder.
    if (value.tag == 20)
        return; // Every byte sequence is valid UTF8-or-Latin1 Teletex.
    if (value.tag == 12) {
        try {
            utf16(std::string_view(reinterpret_cast<const char*>(value.bytes.data()), value.bytes.size()));
        } catch (const std::invalid_argument&) {
            invalid();
        }
        return;
    }
    if (value.tag == 30) {
        if (value.bytes.size() % 2)
            invalid();
        for (std::size_t i = 0; i < value.bytes.size(); i += 2) {
            unsigned c = (value.bytes[i] << 8) | value.bytes[i + 1];
            if (c >= 0xd800 && c <= 0xdfff)
                invalid();
        }
        return;
    }
    if (value.tag == 19) {
        constexpr std::string_view extra = " '()+,-./:=?";
        for (auto b : value.bytes)
            if (!(b >= 'A' && b <= 'Z') && !(b >= 'a' && b <= 'z') && !(b >= '0' && b <= '9') &&
                extra.find(static_cast<char>(b)) == extra.npos)
                invalid();
        return;
    }
    invalid();
}
std::optional<std::u16string> general_name(Der& reader) {
    const auto tag = reader.peek();
    switch (tag & 0xdf) {
    case 0x80: {
        auto other = reader.sequence(0xa0);
        oid(other.read());
        auto value = other.sequence(0xa0);
        value.read();
        value.end();
        other.end();
        break;
    }
    case 0x81:
        ia5(reader.read(), 0x81);
        break;
    case 0x82:
        ia5(reader.read(), 0x82);
        break;
    case 0x83:
        reader.read();
        break;
    case 0x84: {
        auto name = reader.sequence(0xa4);
        name.read();
        name.end();
        break;
    }
    case 0x85: {
        auto edi = reader.sequence(0xa5);
        if (!edi.bytes.empty() && (edi.peek() & 0xdf) == 0x80) {
            auto assigner = edi.sequence(0xa0);
            directory_string(assigner);
            assigner.end();
        }
        auto party = edi.sequence(0xa1);
        directory_string(party);
        party.end();
        edi.end();
        break;
    }
    case 0x86:
        return ia5(reader.read(), 0x86);
    case 0x87:
        if (reader.read().tag != 0x87)
            invalid();
        break;
    case 0x88:
        oid(reader.read(), 0x88);
        break;
    default:
        invalid();
    }
    return {};
}
std::optional<CertificateDer> aia_extension(Bytes der) {
    if (der.size() > LONG_MAX)
        throw ConfigHttpLinuxCertificateError();
    const auto* at = der.data();
    std::unique_ptr<X509, decltype(&X509_free)> certificate(
        d2i_X509(nullptr, &at, static_cast<long>(der.size())), X509_free);
    if (!certificate)
        throw ConfigHttpLinuxCertificateError();
    int index = X509_get_ext_by_NID(certificate.get(), NID_info_access, -1);
    if (index < 0)
        return {};
    auto* extension = X509_get_ext(certificate.get(), index);
    auto* value = X509_EXTENSION_get_data(extension);
    if (!value || value->length < 0)
        throw ConfigHttpLinuxCertificateError();
    if (!value->length)
        return CertificateDer{};
    return CertificateDer(value->data, value->data + value->length);
}
}
std::optional<std::u16string> config_http_linux_find_http_aia_record(Bytes extension, std::string_view record,
                                                                     const ConfigUriObjectBackend& uris) {
    try {
        Der input{extension};
        auto sequence = input.sequence();
        input.end();
        while (!sequence.bytes.empty()) {
            auto entry = sequence.sequence();
            auto method = oid(entry.read());
            auto location = general_name(entry);
            entry.end();
            if (method != record || !location)
                continue;
            auto uri = uris.try_create(std::make_shared<const std::u16string>(*location),
                                       ConfigUriObjectKind::Absolute);
            if (uri && *uri->scheme() == u"http")
                return location;
        }
    } catch (const Invalid&) {
    }
    return {};
}
namespace {
template <class Result, class Download, class Add>
Result find_chain(ConfigHttpLinuxCertificateChain& chain, const ConfigUriObjectBackend& uris,
                  std::chrono::milliseconds timeout, const Download& download, Add add) {
    Result result{X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT, {}};
    std::uintptr_t previous = 0;
    const auto first_added = chain.issuer_addition_count();
    auto incomplete = [](int error) {
        return error == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT ||
               error == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY;
    };
    while (incomplete(result.native_error)) {
        auto identity = chain.missing_issuer_identity();
        if (identity == previous)
            break;
        previous = identity;
        auto child = chain.missing_issuer_child();
        if (!child)
            break;
        auto extension = aia_extension(*child);
        if (!extension || extension->empty())
            break;
        auto uri = config_http_linux_find_http_aia_record(*extension, "1.3.6.1.5.5.7.48.2", uris);
        if (!uri)
            break;
        auto issuer = download(*uri, timeout);
        if (!issuer)
            break;
        result.native_error = add(*issuer);
        result.downloaded.push_back(std::move(*issuer));
    }
    if (result.native_error == X509_V_OK && !result.downloaded.empty()) {
        auto selected = chain.selected_added_issuers();
        decltype(result.downloaded) retained;
        for (std::size_t i = 0; i < result.downloaded.size(); ++i)
            if (std::find(selected.begin(), selected.end(), i + first_added) != selected.end())
                retained.push_back(std::move(result.downloaded[i]));
        result.downloaded = std::move(retained);
    }
    return result;
}
}
ConfigHttpLinuxAiaResult config_http_linux_find_chain_via_aia(ConfigHttpLinuxCertificateChain& chain,
                                                              const ConfigUriObjectBackend& uris,
                                                              std::chrono::milliseconds timeout,
                                                              const ConfigHttpLinuxIssuerDownload& download) {
    return find_chain<ConfigHttpLinuxAiaResult>(chain, uris, timeout, download,
                                                [&](const auto& issuer) { return chain.add_issuer(issuer); });
}
ConfigHttpLinuxAiaAssetResult
config_http_linux_find_chain_via_assets(ConfigHttpLinuxCertificateChain& chain,
                                        const ConfigUriObjectBackend& uris, std::chrono::milliseconds timeout,
                                        const ConfigHttpLinuxIssuerAssetDownload& download) {
    return find_chain<ConfigHttpLinuxAiaAssetResult>(chain, uris, timeout, download, [&](const auto& issuer) {
        return chain.add_issuer_aux(issuer.auxiliary_der);
    });
}
ConfigHttpLinuxAiaAssetResult
config_http_linux_find_chain_via_assets(ConfigHttpLinuxCertificateChain& chain,
                                        const ConfigUriObjectBackend& uris, std::chrono::milliseconds timeout,
                                        const ConfigHttpX509ResourceClient& client) {
    return config_http_linux_find_chain_via_assets(
        chain, uris, timeout, ConfigHttpLinuxIssuerAssetDownload{[&](auto uri, auto duration) {
            if (duration.count() <= 0)
                return std::optional<ConfigHttpLinuxDownloadedCertificate>{};
            const auto count = duration.count();
            const auto ticks = count > INT64_MAX / 10000 ? INT64_MAX : count * 10000;
            return config_http_linux_download_certificate(client, uri, ticks);
        }});
}
}
