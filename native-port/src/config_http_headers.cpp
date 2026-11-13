// Source HttpConnection.ParseStatusLineCore/ParseHeadersCore/AddResponseHeader.
#include "opensim/framework/config_http_headers.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/ip_address.hpp"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#endif
#include <algorithm>
namespace opensim::framework {
ConfigHttpHeaderError::ConfigHttpHeaderError(std::string type, std::u16string text, std::uint32_t hr,
                                             int code, std::shared_ptr<const ConfigHttpHeaderError> nested)
    : runtime_error(utf8(text)), source_type(std::move(type)), message(std::move(text)), hresult(hr),
      request_error(code), inner(std::move(nested)) {
}
std::optional<std::u16string> config_http_request_host(const std::vector<std::u16string>& values) {
    static const auto uris = native_config_uri_object_backend();
    if (!values.empty()) {
        const auto& value = values.front();
        std::size_t start = 0;
        while (start < value.size() && (value[start] == u' ' || value[start] == u'\t'))
            ++start;
        auto end = value.find_first_of(u"/ \t\r,", start);
        if (end == value.npos)
            end = value.size();
        if (end == start || (end < value.size() && value[end] == u'/'))
            return {};
        auto rest = end;
        while (rest < value.size() && (value[rest] == u' ' || value[rest] == u'\t'))
            ++rest;
        if (rest != value.size())
            return {};
        auto host = value.substr(start, end - start);
        if (uris->try_create(std::make_shared<const std::u16string>(u"http://u@" + host + u"/"),
                             ConfigUriObjectKind::Absolute))
            return host;
    }
    return {};
}
std::u16string config_http_tls_host_key(std::u16string_view host) {
    auto colon = host.find(u':');
    if (colon != host.npos) {
        auto bracket = host.find(u']');
        if (bracket == host.npos)
            return std::u16string(host.substr(0, colon));
        colon = host.rfind(u':');
        if (colon > bracket)
            return std::u16string(host.substr(0, colon));
    }
    return std::u16string(host);
}
ConfigHttpTlsTargetNames config_http_tls_target_names(std::u16string_view target) {
    ConfigHttpTlsTargetNames names;
    names.normalized = config_http_normalize_tls_host(target);
    names.certificate_host = utf8(names.normalized);
#ifdef _WIN32
    names.sni_host = names.certificate_host;
    if (auto ip = IpAddress::parse(names.sni_host); ip && (ip->v6() || ip->str() == names.sni_host))
        names.sni_host.clear();
#else
    names.sni_host = utf8(target);
    if (IpAddress::parse(names.sni_host))
        names.sni_host.clear();
    if (auto ip = IpAddress::parse(names.certificate_host)) {
        if (ip->v6())
            ip->scope_id(0);
        names.certificate_host = ip->str();
    }
#endif
#if defined(__linux__)
    names.ignore_sni_error = true;
#endif
    return names;
}
#ifdef _WIN32
bool config_http_windows_certificate_name(std::span<const std::uint8_t> der, std::u16string_view host) {
    if (der.size() > MAXDWORD)
        return false;
    auto cert = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, der.data(),
                                             static_cast<DWORD>(der.size()));
    if (!cert)
        return false;
    struct CertGuard {
        PCCERT_CONTEXT value;
        ~CertGuard() {
            CertFreeCertificateContext(value);
        }
    } cert_guard{cert};
    CERT_CHAIN_PARA parameters{};
    parameters.cbSize = sizeof(parameters);
    PCCERT_CHAIN_CONTEXT chain = nullptr;
    auto flags = CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL | CERT_CHAIN_DISABLE_AIA |
                 CERT_CHAIN_DISABLE_AUTH_ROOT_AUTO_UPDATE;
    if (!CertGetCertificateChain(nullptr, cert, nullptr, nullptr, &parameters, flags, nullptr, &chain) ||
        !chain)
        return false;
    struct ChainGuard {
        PCCERT_CHAIN_CONTEXT value;
        ~ChainGuard() {
            CertFreeCertificateChain(value);
        }
    } chain_guard{chain};
    std::u16string name(host);
    SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl{};
    ssl.cbSize = sizeof(ssl);
    ssl.dwAuthType = AUTHTYPE_SERVER;
    ssl.pwszServerName = reinterpret_cast<wchar_t*>(name.data());
    CERT_CHAIN_POLICY_PARA policy{};
    policy.cbSize = sizeof(policy);
    policy.dwFlags = 4031;
    policy.pvExtraPolicyPara = &ssl;
    CERT_CHAIN_POLICY_STATUS status{};
    status.cbSize = sizeof(status);
    CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain, &policy, &status);
    return status.dwError != static_cast<DWORD>(CERT_E_CN_NO_MATCH);
}
#endif
namespace {
using Bytes = std::span<const std::uint8_t>;
std::u16string ascii(Bytes bytes) {
    std::u16string out;
    for (auto b : bytes)
        out += b < 128 ? char16_t(b) : u'?';
    return out;
}
[[noreturn]] void invalid(std::u16string msg, int code = 9) {
    throw ConfigHttpHeaderError("System.Net.Http.HttpRequestException", std::move(msg), 0x80131500, code);
}
[[noreturn]] void line_error(Bytes line) {
    invalid(u"Received an invalid status line: '" + ascii(line) + u"'.");
}
bool token(std::uint8_t c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           std::string_view("!#$%&'*+-.^_`|~").find(char(c)) != std::string_view::npos;
}
bool digit(std::uint8_t c) {
    return c >= '0' && c <= '9';
}
}
std::vector<std::uint8_t> config_http_encode_request_header(std::u16string_view text,
                                                            ConfigHttpHeaderEncoder encoder) {
    if (encoder)
        return encoder(text);
    std::vector<std::uint8_t> out;
    out.reserve(text.size());
    for (auto c : text) {
        if (c > 127)
            throw ConfigHttpHeaderError("System.Net.Http.HttpRequestException",
                                        u"Request headers must contain only ASCII characters.", 0x80131500,
                                        0);
        out.push_back(static_cast<std::uint8_t>(c));
    }
    return out;
}
std::u16string config_http_decode_response_header(Bytes bytes, ConfigHttpHeaderDecoder decoder) {
    if (decoder)
        return decoder(bytes);
    std::u16string out;
    out.reserve(bytes.size());
    for (auto b : bytes)
        out += char16_t(b);
    return out;
}
std::u16string config_http_decode_named_response_header(std::string_view name, Bytes bytes,
                                                        ConfigHttpHeaderDecoder decoder) {
    if (bytes.empty())
        return {};
    bool location = name.size() == 8;
    constexpr std::string_view target = "location";
    for (std::size_t i = 0; location && i < name.size(); ++i) {
        auto c = name[i];
        if (c >= 'A' && c <= 'Z')
            c += 32;
        location = c == target[i];
    }
    if (location)
        try {
            return utf16(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        } catch (const std::invalid_argument&) {
        }
    return config_http_decode_response_header(bytes, std::move(decoder));
}
std::optional<std::int64_t> config_http_content_length(const std::vector<std::u16string>& values) {
    if (values.empty())
        return {};
    auto value = std::u16string_view(values.front());
    while (!value.empty() && (value.front() == u' ' || value.front() == u'\t'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == u' ' || value.back() == u'\t'))
        value.remove_suffix(1);
    if (value.empty() || value.size() > 19)
        return {};
    std::uint64_t number = 0;
    for (auto c : value) {
        if (c < u'0' || c > u'9')
            return {};
        number = number * 10 + (c - u'0');
        if (number > 9223372036854775807ULL)
            return {};
    }
    return static_cast<std::int64_t>(number);
}
namespace {
bool header_token(char16_t c) {
    return c < 128 && token(static_cast<std::uint8_t>(c));
}
void ws(std::u16string_view value, std::size_t& at) {
    while (at < value.size() && (value[at] == u' ' || value[at] == u'\t'))
        ++at;
}
bool take_token(std::u16string_view value, std::size_t& at) {
    auto start = at;
    while (at < value.size() && header_token(value[at]))
        ++at;
    return at != start;
}
bool quoted_value(std::u16string_view value, std::size_t& at) {
    if (at == value.size() || value[at] != u'"')
        return false;
    ++at;
    while (at < value.size()) {
        if (at + 2 < value.size() && value[at] == u'\\' && value[at + 1] <= 127) {
            at += 2;
            continue;
        }
        auto c = value[at++];
        if (c == u'\r' || c == u'\n')
            return false;
        if (c == u'"')
            return true;
    }
    return false;
}
bool parameter(std::u16string_view value, std::size_t& at) {
    if (!take_token(value, at))
        return false;
    ws(value, at);
    if (at == value.size() || value[at] != u'=')
        return true;
    ++at;
    ws(value, at);
    if (!take_token(value, at) && !quoted_value(value, at))
        return false;
    ws(value, at);
    return true;
}
bool parse_transfer(std::u16string_view value, bool& chunked,
                    std::vector<std::u16string>* codings = nullptr) {
    std::size_t at = 0;
    bool found = false;
    while (true) {
        ws(value, at);
        while (at < value.size() && value[at] == u',') {
            ++at;
            ws(value, at);
        }
        if (at == value.size()) {
            chunked = found;
            return true;
        }
        auto start = at;
        if (!take_token(value, at))
            return false;
        auto name = value.substr(start, at - start);
        std::u16string coding;
        if (codings)
            coding = name;
        bool is_chunked = name.size() == 7;
        constexpr std::u16string_view target = u"chunked";
        for (std::size_t i = 0; is_chunked && i < name.size(); ++i) {
            auto c = name[i];
            if (c >= u'A' && c <= u'Z')
                c += 32;
            is_chunked = c == target[i];
        }
        ws(value, at);
        if (at < value.size() && value[at] == u';') {
            is_chunked = false;
            do {
                ++at;
                ws(value, at);
                const auto parameter_start = at;
                if (!parameter(value, at))
                    return false;
                if (codings) {
                    auto item = value.substr(parameter_start, at - parameter_start);
                    while (!item.empty() && (item.back() == u' ' || item.back() == u'\t'))
                        item.remove_suffix(1);
                    coding += u"; ";
                    coding += item;
                }
                ws(value, at);
            } while (at < value.size() && value[at] == u';');
        }
        found |= is_chunked;
        if (codings)
            codings->push_back(is_chunked ? u"chunked" : std::move(coding));
        if (at == value.size()) {
            chunked = found;
            return true;
        }
        if (value[at] != u',')
            return false;
        ++at;
    }
}
}
void config_http_apply_keep_alive(ConfigHttpKeepAliveState& state, std::u16string_view input) {
    std::vector<std::pair<std::u16string_view, std::u16string_view>> items;
    std::size_t at = 0;
    ws(input, at);
    while (at < input.size()) {
        auto start = at;
        if (!take_token(input, at))
            return;
        auto name = input.substr(start, at - start);
        ws(input, at);
        std::u16string_view value;
        if (at < input.size() && input[at] == u'=') {
            ++at;
            ws(input, at);
            start = at;
            if (!take_token(input, at) && !quoted_value(input, at))
                return;
            value = input.substr(start, at - start);
            ws(input, at);
        }
        items.emplace_back(name, value);
        if (at == input.size())
            break;
        if (input[at++] != u',')
            return;
        ws(input, at);
        if (at == input.size())
            return;
    }
    auto equal = [](std::u16string_view a, std::u16string_view b) {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            auto c = a[i];
            if (c >= u'A' && c <= u'Z')
                c += 32;
            if (c != b[i])
                return false;
        }
        return true;
    };
    for (auto [name, value] : items) {
        if (equal(name, u"timeout")) {
            std::uint32_t result = 0;
            bool valid = !value.empty();
            for (auto c : value) {
                if (c < u'0' || c > u'9' || result > (2147483647u - (c - u'0')) / 10u) {
                    valid = false;
                    break;
                }
                result = result * 10u + (c - u'0');
            }
            if (valid) {
                if (result <= 1)
                    state.close = true;
                else
                    state.timeout_seconds = static_cast<std::int32_t>(result - 1);
            }
        } else if (equal(name, u"max") && value == u"0")
            state.close = true;
    }
}
bool config_http_transfer_encoding_chunked(const std::vector<std::u16string>& values) {
    for (auto& value : values) {
        bool chunked = false;
        if (parse_transfer(value, chunked) && chunked)
            return true;
    }
    return false;
}
std::vector<std::u16string> config_http_disable_chunked_request_encoding(
    const std::vector<std::u16string>& values) {
    std::vector<std::u16string> result;
    bool removed_chunked = false;
    for (const auto& value : values) {
        std::vector<std::u16string> codings;
        bool chunked = false;
        if (!parse_transfer(value, chunked, &codings)) {
            result.push_back(value);
            continue;
        }
        for (auto& coding : codings) {
            if (!removed_chunked && coding == u"chunked") {
                removed_chunked = true;
                continue;
            }
            result.push_back(std::move(coding));
        }
    }
    return result;
}
[[noreturn]] void config_http_response_headers_ended(bool can_retry) {
    auto inner = std::make_shared<ConfigHttpHeaderError>("System.Net.Http.HttpIOException",
                                                         u"The response ended prematurely. (ResponseEnded)",
                                                         0x80131620, 10);
    ConfigHttpHeaderError error("System.Net.Http.HttpRequestException",
                                u"An error occurred while sending the request.", inner->hresult, 10, inner);
    error.retry_on_connection_failure = can_retry;
    throw error;
}
std::optional<ConfigHttpResponseHead> config_http_parse_response_head(Bytes input, std::size_t maximum,
                                                                      std::optional<std::size_t> configured) {
    auto reported = configured.value_or(maximum);
    std::vector<std::uint8_t> data(input.begin(), input.end());
    Bytes bytes(data);
    auto first = std::find(bytes.begin(), bytes.end(), 10);
    if (first == bytes.end()) {
        if (bytes.size() >= maximum)
            invalid(u"The HTTP response headers length exceeded the set limit of " +
                        utf16(std::to_string(reported)) + u" bytes.",
                    11);
        return {};
    }
    std::size_t line_end = first - bytes.begin(), cursor = line_end + 1;
    if (line_end && bytes[line_end - 1] == 13)
        --line_end;
    auto line = bytes.first(line_end);
    ConfigHttpResponseHead out;
    if (line.size() < 12 || line[8] != 32)
        line_error(line);
    if (!std::equal(line.begin(), line.begin() + 7, reinterpret_cast<const std::uint8_t*>("HTTP/1.")) ||
        !digit(line[7]))
        line_error(line);
    out.minor_version = line[7] - '0';
    if (!digit(line[9]) || !digit(line[10]) || !digit(line[11]))
        invalid(u"Received an invalid status code: '" + ascii(line.subspan(9, 3)) + u"'.");
    out.status = 100 * (line[9] - 48) + 10 * (line[10] - 48) + (line[11] - 48);
    if (line.size() > 12) {
        if (line[12] != 32)
            line_error(line);
        auto phrase = line.subspan(13);
        out.reason = config_http_decode_response_header(phrase);
        if (out.reason.find_first_of(u"\r\n") != std::u16string::npos) {
            auto inner = std::make_shared<ConfigHttpHeaderError>(
                "System.FormatException", u"The reason phrase must not contain new-line characters.",
                0x80131537);
            throw ConfigHttpHeaderError("System.Net.Http.HttpRequestException",
                                        u"Received status phrase could not be decoded with iso-8859-1: '" +
                                            ascii(phrase) + u"'.",
                                        inner->hresult, 9, inner);
        }
    }
    while (true) {
        auto start = cursor;
        auto sep = cursor;
        while (sep < data.size() && data[sep] != 58 && data[sep] != 10)
            ++sep;
        if (sep == data.size()) {
            if (data.size() >= maximum)
                invalid(u"The HTTP response headers length exceeded the set limit of " +
                            utf16(std::to_string(reported)) + u" bytes.",
                        11);
            return {};
        }
        if (data[sep] == 10) {
            if (sep == start || (sep == start + 1 && data[start] == 13)) {
                out.bytes_consumed = sep + 1;
                if (out.bytes_consumed > maximum)
                    invalid(u"The HTTP response headers length exceeded the set limit of " +
                                utf16(std::to_string(reported)) + u" bytes.",
                            11);
                return out;
            }
            invalid(u"Received an invalid header line: '" + ascii(bytes.subspan(start, sep - start)) + u"'.");
        }
        auto value_start = sep + 1;
        auto current = value_start;
        std::size_t value_end = 0, next = 0;
        while (true) {
            auto end = current;
            while (end < data.size() && data[end] != 10)
                ++end;
            if (end == data.size() || end + 1 == data.size()) {
                if (data.size() >= maximum)
                    invalid(u"The HTTP response headers length exceeded the set limit of " +
                                utf16(std::to_string(reported)) + u" bytes.",
                            11);
                return {};
            }
            value_end = end;
            if (end > current && data[end - 1] == 13)
                --value_end;
            next = end + 1;
            if (data[next] != 9 && data[next] != 32)
                break;
            data[value_end] = 32;
            data[end] = 32;
            current = next + 1;
        }
        auto name_end = sep;
        while (name_end > start && data[name_end - 1] == 32)
            --name_end;
        auto name = bytes.subspan(start, name_end - start);
        bool ignored = out.status >= 100 && out.status <= 199 && out.status != 101;
        if (!ignored && (name.empty() || !std::all_of(name.begin(), name.end(), token)))
            invalid(u"Received an invalid header name: '" + ascii(name) + u"'.");
        while (value_start < value_end && (data[value_start] == 9 || data[value_start] == 32))
            ++value_start;
        while (value_end > value_start && (data[value_end - 1] == 9 || data[value_end - 1] == 32))
            --value_end;
        if (!ignored)
            out.headers.push_back(
                {std::string(name.begin(), name.end()),
                 config_http_decode_named_response_header(
                     std::string_view(reinterpret_cast<const char*>(name.data()), name.size()),
                     bytes.subspan(value_start, value_end - value_start))});
        cursor = next;
    }
}
}
