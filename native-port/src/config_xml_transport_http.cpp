#include "opensim/framework/atomic_shared_ptr.hpp"
#include "opensim/framework/config_uri_components.hpp"
#include "opensim/framework/config_generic_uri_object.hpp"
#include "opensim/framework/config_xml_http_multi_proxy.hpp"
#include "opensim/framework/config_xml_transport_http.hpp"
#include <httplib.h>
#include "config_http_native_pool.hpp"
#include "opensim/framework/config_http_headers.hpp"
#include "opensim/framework/config_http_windows_certificate.hpp"
#include "opensim/framework/config_http_linux_certificate_name.hpp"
#include "opensim/framework/config_http_linux_certificate_persistence.hpp"
#include "opensim/framework/config_http_headers_body.hpp"
#include "opensim/framework/config_http_environment_proxy_native.hpp"
#include "opensim/framework/config_http_credential_cache.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/ip_address.hpp"
#include "opensim/framework/host_resolver.hpp"
#include "opensim/framework/identity.hpp"
#include "opensim/framework/sha256.hpp"
#include "opensim/framework/stream_reader_text.hpp"
#include <chrono>
#include <array>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#ifdef _WIN32
#ifndef SECURITY_WIN32
#define SECURITY_WIN32
#endif
#include <security.h>
#include <wincrypt.h>
#elif defined(OPENSIM_HTTP_GSSAPI)
#ifdef __APPLE__
#include <GSS/GSS.h>
#else
#include <gssapi/gssapi.h>
#include <gssapi/gssapi_ext.h>
#endif
#endif
#if defined(_WIN32) || defined(OPENSIM_HTTP_GSSAPI)
#define OPENSIM_HTTP_INTEGRATED_PROXY
#endif
namespace opensim::framework {
ConfigXmlHttpError::ConfigXmlHttpError(std::string type, std::string text, std::uint32_t h,
                                       std::optional<int> s)
    : runtime_error(text), source_type(std::move(type)), message(utf16(text)), hresult(h), status(s) {
}
namespace {
[[noreturn]] void socks_error() {
    throw ConfigXmlHttpError("System.Net.Http.HttpRequestException",
                             "An error occurred while establishing a connection to the proxy tunnel.",
                             0x80131500);
}

void socks_write(httplib::Stream& stream, std::span<const std::uint8_t> data) {
    while (!data.empty()) {
        const auto count = stream.write(reinterpret_cast<const char*>(data.data()), data.size());
        if (count <= 0)
            socks_error();
        data = data.subspan(static_cast<std::size_t>(count));
    }
}

void socks_read(httplib::Stream& stream, std::span<std::uint8_t> data) {
    while (!data.empty()) {
        const auto count = stream.read(reinterpret_cast<char*>(data.data()), data.size());
        if (count <= 0)
            socks_error();
        data = data.subspan(static_cast<std::size_t>(count));
    }
}

void negotiate_socks5_authentication(httplib::Stream& stream, const ConfigHttpNetworkCredential* credential) {
    constexpr std::uint8_t no_authentication = 0;
    constexpr std::uint8_t username_and_password = 2;
    const std::array<std::uint8_t, 4> greeting{
        5, static_cast<std::uint8_t>(credential ? 2 : 1), no_authentication, username_and_password};
    socks_write(stream, std::span(greeting).first(credential ? 4 : 3));

    std::array<std::uint8_t, 2> method{};
    socks_read(stream, method);
    if (method[0] != 5)
        socks_error();
    if (method[1] == no_authentication)
        return;
    if (method[1] != username_and_password || !credential)
        socks_error();

    const auto username = utf8(*credential->user_name);
    const auto password = utf8(*credential->password);
    if (username.size() > 255 || password.size() > 255)
        socks_error();
    std::vector<std::uint8_t> authentication{1, static_cast<std::uint8_t>(username.size())};
    authentication.insert(authentication.end(), username.begin(), username.end());
    authentication.push_back(static_cast<std::uint8_t>(password.size()));
    authentication.insert(authentication.end(), password.begin(), password.end());
    socks_write(stream, authentication);

    std::array<std::uint8_t, 2> response{};
    socks_read(stream, response);
    if (response != std::array<std::uint8_t, 2>{1, 0})
        socks_error();
}

void connect_socks5(httplib::Stream& stream, const ConfigXmlHttpAddress& target,
                    const ConfigHttpNetworkCredential* credential) {
    negotiate_socks5_authentication(stream, credential);
    const auto ip = IpAddress::parse(target.host);
    std::vector<std::uint8_t> request{5, 1, 0};
    if (ip) {
        request.push_back(ip->v6() ? 4 : 1);
        const auto bytes = ip->bytes();
        request.insert(request.end(), bytes.begin(), bytes.begin() + (ip->v6() ? 16 : 4));
    } else {
        if (target.host.size() > 255)
            socks_error();
        request.insert(request.end(), {3, static_cast<std::uint8_t>(target.host.size())});
        request.insert(request.end(), target.host.begin(), target.host.end());
    }
    request.push_back(static_cast<std::uint8_t>(target.port >> 8));
    request.push_back(static_cast<std::uint8_t>(target.port));
    socks_write(stream, request);
    std::array<std::uint8_t, 5> response{};
    socks_read(stream, response);
    if (response[0] != 5 || response[1] != 0)
        socks_error();
    std::array<std::uint8_t, 257> discard{};
    switch (response[3]) {
    case 1: // IPv4
        socks_read(stream, std::span(discard).first(5));
        break;
    case 4: // IPv6
        socks_read(stream, std::span(discard).first(17));
        break;
    case 3: // Domain name
        socks_read(stream, std::span(discard).first(response[4] + 2u));
        break;
    default:
        socks_error();
    }
}

void connect_socks4(httplib::Stream& stream, const ConfigXmlHttpAddress& target,
                    const ConfigHttpNetworkCredential* credential, bool resolve_locally) {
    const auto high_port = static_cast<std::uint8_t>(target.port >> 8);
    const auto low_port = static_cast<std::uint8_t>(target.port);
    auto ip = IpAddress::parse(target.host);
    if (!ip && resolve_locally) {
        const auto addresses = HostResolver::system_lookup(target.host);
        if (addresses) {
            const auto ipv4 = std::ranges::find_if(*addresses, [](const auto& address) { return !address->v6(); });
            if (ipv4 != addresses->end())
                ip = **ipv4;
        }
        if (!ip)
            socks_error();
    }
    std::vector<std::uint8_t> request{4, 1, high_port, low_port};
    if (ip) {
        const auto bytes = ip->bytes();
        if (ip->v6()) {
            if (bytes[10] != 255 || bytes[11] != 255 ||
                !std::all_of(bytes.begin(), bytes.begin() + 10, [](auto byte) { return byte == 0; }))
                socks_error();
            request.insert(request.end(), bytes.begin() + 12, bytes.end());
        } else {
            request.insert(request.end(), bytes.begin(), bytes.begin() + 4);
        }
    } else {
        if (target.host.size() > 255)
            socks_error();
        request.insert(request.end(), {0, 0, 0, 255});
    }
    if (credential) {
        const auto username = utf8(*credential->user_name);
        if (username.size() > 255)
            socks_error();
        request.insert(request.end(), username.begin(), username.end());
    }
    request.push_back(0);
    if (!ip) {
        request.insert(request.end(), target.host.begin(), target.host.end());
        request.push_back(0);
    }
    socks_write(stream, request);
    std::array<std::uint8_t, 8> response{};
    socks_read(stream, response);
    if (response[1] != 90)
        socks_error();
}

void open_socks_tunnel(httplib::Stream& stream, std::u16string_view scheme, const ConfigXmlHttpAddress& target,
                       const ConfigHttpNetworkCredential* credential) {
    if (scheme == u"socks5") {
        connect_socks5(stream, target, credential);
    } else {
        connect_socks4(stream, target, credential, scheme == u"socks4");
    }
}

std::string tls_key_bytes(std::u16string_view value) {
    std::string result;
    for (auto c : value) {
        result.push_back(static_cast<char>(c & 255));
        result.push_back(static_cast<char>(c >> 8));
    }
    return result;
}
void tls_key_field(std::string& key, std::string_view value) {
    auto size = static_cast<std::uint64_t>(value.size());
    for (int i = 0; i < 8; ++i)
        key.push_back(static_cast<char>(size >> (i * 8)));
    key.append(value);
}

bool has_proxy_challenge(std::string_view value, std::string_view scheme) {
    bool quoted = false;
    bool challenge_start = true;
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char character = value[index];
        if (quoted && character == '\\') {
            ++index;
            continue;
        }
        if (character == '"') {
            quoted = !quoted;
            challenge_start = false;
        } else if (!quoted && character == ',') {
            challenge_start = true;
        } else if (challenge_start && character != ' ' && character != '\t') {
            auto rest = value.substr(index);
            if (std::ranges::equal(rest.substr(0, scheme.size()), scheme, [](char actual, char expected) {
                    return httplib::detail::case_ignore::to_lower(actual) ==
                           httplib::detail::case_ignore::to_lower(expected);
                })) {
                rest.remove_prefix(scheme.size());
                if (rest.empty())
                    return true;
                if (rest.front() == ' ' || rest.front() == '\t') {
                    const auto next = rest.find_first_not_of(" \t");
                    if (next == rest.npos || rest[next] != '=')
                        return true;
                }
                if (rest.front() == ',')
                    return true;
            }
            challenge_start = false;
        }
    }
    return false;
}

std::string digest_extended_username(std::string_view name) {
    constexpr std::string_view punctuation = "!#$&+-.^_`|~";
    constexpr std::string_view hex = "0123456789ABCDEF";
    std::string encoded = "utf-8''";
    for (unsigned char byte : name) {
        if ((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
            (byte >= '0' && byte <= '9') || punctuation.find(byte) != punctuation.npos) {
            encoded += static_cast<char>(byte);
        } else {
            encoded += '%';
            encoded += hex[byte >> 4];
            encoded += hex[byte & 15];
        }
    }
    return encoded;
}

std::string http_content_charset(std::string_view content_type) {
    std::string charset;
    bool media_type = true;
    bool valid = true;
    const auto is_token = [](std::string_view value) {
        return !value.empty() && std::ranges::all_of(value, httplib::detail::fields::is_token_char);
    };
    httplib::detail::split_unquoted(content_type.data(), content_type.data() + content_type.size(), ';',
        [&](const char* first, const char* last) {
            auto value = httplib::detail::trim_copy(std::string(first, last));
            if (media_type) {
                media_type = false;
                const auto slash = value.find('/');
                valid = slash != value.npos && is_token(std::string_view(value).substr(0, slash)) &&
                        is_token(std::string_view(value).substr(slash + 1));
                return;
            }
            const auto equal = value.find('=');
            if (equal == value.npos)
                return;
            const auto name = httplib::detail::trim_copy(value.substr(0, equal));
            if (charset.empty() && httplib::detail::case_ignore::equal(name, "charset")) {
                auto parameter = httplib::detail::trim_copy(value.substr(equal + 1));
                if (parameter.size() > 2 && parameter.front() == '"' && parameter.back() == '"')
                    parameter = parameter.substr(1, parameter.size() - 2);
                charset = httplib::detail::case_ignore::to_lower(parameter);
            }
        });
    return valid ? charset : std::string{};
}

std::string digest_content_as_utf8(std::string_view body, std::string_view content_type) {
    if (body.empty())
        return {};
    const auto bytes = [](std::string_view text) {
        return std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
    };
    const auto charset = http_content_charset(content_type);
    using Encoding = StreamReaderEncoding;
    if (charset.empty()) {
        // HttpContent's automatic detection omits UTF-32 big-endian.
        if (body.starts_with(std::string_view("\x00\x00\xfe\xff", 4)))
            return utf8(decode_unicode_text(bytes(body), Encoding::Utf8));
        return utf8(decode_stream_reader_text(bytes(body)).text);
    }
    const auto is_charset = [&](std::initializer_list<std::string_view> names) {
        return std::ranges::find(names, charset) != names.end();
    };
    const bool ascii = is_charset({"ansi_x3.4-1968", "ansi_x3.4-1986", "ascii", "cp367", "csascii",
                                   "ibm367", "iso-ir-6", "iso646-us", "iso_646.irv:1991", "us", "us-ascii"});
    const bool latin1 = is_charset({"cp819", "csisolatin1", "ibm819", "iso-8859-1", "iso-ir-100",
                                    "iso8859-1", "iso_8859-1", "iso_8859-1:1987", "l1", "latin1"});
    if (ascii || latin1) {
        std::u16string text;
        text.reserve(body.size());
        for (unsigned char byte : body)
            text += static_cast<char16_t>(ascii && byte > 127 ? '?' : byte);
        return utf8(text);
    }
    Encoding encoding;
    std::string_view preamble;
    if (is_charset({"utf-8", "unicode-1-1-utf-8", "unicode-2-0-utf-8",
                    "x-unicode-1-1-utf-8", "x-unicode-2-0-utf-8"})) {
        encoding = Encoding::Utf8;
        preamble = "\xef\xbb\xbf";
    } else if (is_charset({"iso-10646-ucs-2", "ucs-2", "unicode", "utf-16", "utf-16le"})) {
        encoding = Encoding::Utf16LE;
        preamble = "\xff\xfe";
    } else if (charset == "utf-16be" || charset == "unicodefffe") {
        encoding = Encoding::Utf16BE;
        preamble = "\xfe\xff";
    } else if (charset == "utf-32" || charset == "utf-32le") {
        encoding = Encoding::Utf32LE;
        preamble = std::string_view("\xff\xfe\x00\x00", 4);
    } else if (charset == "utf-32be") {
        encoding = Encoding::Utf32BE;
        preamble = std::string_view("\x00\x00\xfe\xff", 4);
    } else {
        throw ConfigXmlHttpError("System.InvalidOperationException",
                                 "The character set provided in ContentType is invalid. Cannot read content as string using an invalid character set.",
                                 0x80131509);
    }
    if (body.starts_with(preamble))
        body.remove_prefix(preamble.size());
    return utf8(decode_unicode_text(bytes(body), encoding));
}

std::optional<std::string> digest_authentication_token(const ConfigHttpNetworkCredential& credential,
                                               const httplib::Response& response,
                                               std::string_view method, std::string_view target,
                                               std::string_view body, std::string_view content_type,
                                               bool stale_only, bool is_proxy) {
    std::map<std::string, std::string> parameters;
    if (!httplib::detail::parse_www_authenticate(response, parameters, is_proxy))
        return {};
    const auto get = [&](std::string_view key) -> const std::string* {
        const auto entry = parameters.find(std::string(key));
        return entry == parameters.end() ? nullptr : &entry->second;
    };
    const auto stale = get("stale");
    if (stale_only && (!stale || *stale != "true"))
        return {};
    const auto specified_algorithm = get("algorithm");
    const auto algorithm = specified_algorithm ? httplib::detail::case_ignore::to_lower(*specified_algorithm) : "md5";
    const bool sha256 = algorithm == "sha-256" || algorithm == "sha-256-sess";
    if (!sha256 && algorithm != "md5" && algorithm != "md5-sess")
        return {};
    const auto hash = [sha256](std::string_view bytes) {
        return sha256 ? sha256_hex(bytes) : md5(bytes);
    };
    const auto& realm = parameters.at("realm");
    const auto& nonce = parameters.at("nonce");
    const auto username = utf8(*credential.user_name);
    auto cnonce = random_uuid();
    std::erase(cnonce, '-');
    cnonce.resize(16);
    constexpr std::string_view nonce_count = "00000001";
    const auto offered_qop = get("qop");
    const auto auth_int = offered_qop ? offered_qop->find("auth-int") : std::string::npos;
    const bool use_auth_int = auth_int != std::string::npos && offered_qop->find("auth") == auth_int &&
                              offered_qop->find("auth", auth_int + 8) == std::string::npos;
    const std::string qop = use_auth_int ? "auth-int" : "auth";
    auto a1 = username + ":" + realm + ":" + utf8(*credential.password);
    if (algorithm.ends_with("-sess"))
        a1 = hash(a1) + ":" + nonce + ":" + cnonce;
    auto a2 = std::string(method) + ":" + std::string(target);
    if (use_auth_int)
        a2 += ":" + hash(digest_content_as_utf8(body, content_type));
    auto digest = hash(a1) + ":" + nonce + ":";
    if (offered_qop)
        digest += std::string(nonce_count) + ":" + cnonce + ":" + qop + ":";
    digest += hash(a2);

    std::string token = "Digest ";
    const auto quoted = [&](std::string_view key, const std::string& value) {
        token += key;
        token += "=\"" + httplib::detail::escape_quoted_pairs(value) + "\", ";
    };
    const auto userhash = get("userhash");
    if (userhash && *userhash == "true") {
        quoted("username", hash(username + ":" + realm));
        token += "userhash=true, ";
    } else if (std::ranges::any_of(username, [](unsigned char byte) { return byte > 127; })) {
        token += "username*=" + digest_extended_username(username) + ", ";
    } else {
        quoted("username", username);
    }
    quoted("realm", realm);
    quoted("nonce", nonce);
    quoted("uri", std::string(target));
    quoted("response", hash(digest));
    if (const auto opaque = get("opaque"))
        quoted("opaque", *opaque);
    if (specified_algorithm)
        token += "algorithm=" + *specified_algorithm + ", ";
    if (offered_qop) {
        token += "qop=" + qop + ", nc=" + std::string(nonce_count) + ", ";
        quoted("cnonce", cnonce);
    }
    token.resize(token.size() - 2);
    return token;
}

std::shared_ptr<ConfigHttpNetworkCredential> credential_for_challenge(
    const std::shared_ptr<ConfigXmlCredentials>& credentials, const ConfigUriObjectPtr& proxy_uri,
    const std::shared_ptr<ConfigXmlUri>& origin_uri, std::string_view scheme) {
    if (origin_uri)
        if (const auto provider = std::dynamic_pointer_cast<ConfigHttpOriginCredentials>(credentials))
            return provider->get_credential(origin_uri, scheme);
    if (proxy_uri)
        if (const auto cache = std::dynamic_pointer_cast<ConfigHttpCredentialCache>(credentials))
            return cache->get_credential(proxy_uri, utf16(scheme));
    if (const auto environment = std::dynamic_pointer_cast<ConfigHttpEnvironmentProxyCredentials>(credentials))
        return std::dynamic_pointer_cast<ConfigHttpNetworkCredential>(
            environment->get_credential(proxy_uri, nini_writer_text(utf16(scheme))));
    return std::dynamic_pointer_cast<ConfigHttpNetworkCredential>(credentials);
}

std::optional<std::string> challenge_authorization(
    std::shared_ptr<ConfigXmlCredentials> credentials, const ConfigUriObjectPtr& proxy_uri,
    const httplib::Response& response, std::string_view method,
    std::string_view target, std::string_view body, std::string_view content_type,
    const std::optional<std::string>& previous, unsigned retries,
    const std::shared_ptr<ConfigXmlUri>& origin_uri = {},
    std::shared_ptr<ConfigHttpNetworkCredential>* digest_credential = nullptr) {
    if (!credentials || retries >= 2 || (previous && !previous->starts_with("Digest ")))
        return {};
    const auto [first, last] = response.headers.equal_range(proxy_uri ? "Proxy-Authenticate" : "WWW-Authenticate");
    const auto offered = [&](std::string_view scheme) {
        return std::ranges::any_of(std::ranges::subrange(first, last), [&](const auto& header) {
            return has_proxy_challenge(header.second, scheme);
        });
    };
    if (previous && digest_credential)
        return *digest_credential
            ? digest_authentication_token(**digest_credential, response, method, target, body, content_type,
                                          true, bool(proxy_uri))
            : std::nullopt;
    for (const auto scheme : {"Negotiate", "NTLM"})
        if (offered(scheme) && credential_for_challenge(credentials, proxy_uri, origin_uri, scheme))
            return {};
    auto credential = offered("Digest")
        ? credential_for_challenge(credentials, proxy_uri, origin_uri, "Digest") : nullptr;
    if (credential) {
        if (digest_credential)
            *digest_credential = credential;
        return digest_authentication_token(*credential, response, method, target, body, content_type,
                                           previous.has_value(), bool(proxy_uri));
    }
    if (previous || !offered("Basic"))
        return {};
    credential = credential_for_challenge(credentials, proxy_uri, origin_uri, "Basic");
    if (!credential)
        return {};
    auto username = *credential->user_name;
    if (!credential->domain->empty())
        username = *credential->domain + u"\\" + username;
    return "Basic " + httplib::detail::base64_encode(utf8(username + u":" + *credential->password));
}

struct RetryProxyAuthentication {
    std::string authorization;
};

#ifdef OPENSIM_HTTP_INTEGRATED_PROXY
std::string origin_channel_binding(void* tls_session) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    const auto session = static_cast<SSL*>(tls_session);
    if (!session)
        return {};
    const std::unique_ptr<X509, decltype(&X509_free)> certificate(SSL_get1_peer_certificate(session), X509_free);
    if (!certificate)
        return {};
    int digest_id = NID_undef;
    if (X509_get_signature_info(certificate.get(), &digest_id, nullptr, nullptr, nullptr) != 1)
        return {};
    if (digest_id == NID_md5 || digest_id == NID_sha1)
        digest_id = NID_sha256;
    const auto* digest = EVP_get_digestbynid(digest_id);
    std::array<unsigned char, EVP_MAX_MD_SIZE> hash;
    unsigned int size = 0;
    if (!digest || X509_digest(certificate.get(), digest, hash.data(), &size) != 1)
        return {};
    return "tls-server-end-point:" + std::string(reinterpret_cast<const char*>(hash.data()), size);
#else
    (void)tls_session;
    return {};
#endif
}
#endif

#ifdef _WIN32
class HttpSecurityContext {
    CredHandle credentials_{};
    CtxtHandle context_{};
    bool acquired_ = false;
    bool started_ = false;
    bool complete_ = false;
    bool valid_step_ = false;
    bool integrity_;
    std::wstring target_;
    std::vector<unsigned char> binding_;

  public:
    HttpSecurityContext(const HttpSecurityContext&) = delete;
    HttpSecurityContext& operator=(const HttpSecurityContext&) = delete;

    HttpSecurityContext(std::string_view package, std::string_view host, const ConfigHttpNetworkCredential& credential, bool integrity)
        : integrity_(integrity) {
        const auto target = utf16("HTTP/" + std::string(host));
        target_.assign(target.begin(), target.end());
        std::wstring package_name(package.begin(), package.end());
        const auto copy = [](const ConfigUriText& value) { return std::wstring(value->begin(), value->end()); };
        auto username = copy(credential.user_name);
        auto password = copy(credential.password);
        auto domain = copy(credential.domain);
        SEC_WINNT_AUTH_IDENTITY_W identity{
            reinterpret_cast<unsigned short*>(username.data()), static_cast<unsigned long>(username.size()),
            reinterpret_cast<unsigned short*>(domain.data()), static_cast<unsigned long>(domain.size()),
            reinterpret_cast<unsigned short*>(password.data()), static_cast<unsigned long>(password.size()),
            SEC_WINNT_AUTH_IDENTITY_UNICODE};
        acquired_ = AcquireCredentialsHandleW(nullptr, package_name.data(), SECPKG_CRED_OUTBOUND, nullptr,
            credential.is_default ? nullptr : &identity, nullptr, nullptr, &credentials_, nullptr) == SEC_E_OK;
    }

    ~HttpSecurityContext() {
        if (started_)
            DeleteSecurityContext(&context_);
        if (acquired_)
            FreeCredentialsHandle(&credentials_);
    }

    std::optional<std::string> next_token(std::string_view challenge) {
        valid_step_ = false;
        if (!acquired_ || complete_ || (started_ && challenge.empty()))
            return {};
        std::vector<unsigned char> input;
        if (!challenge.empty()) {
            DWORD length = 0;
            constexpr DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT;
            if (!CryptStringToBinaryA(challenge.data(), static_cast<DWORD>(challenge.size()), flags,
                                      nullptr, &length, nullptr, nullptr) || !length)
                return {};
            input.resize(length);
            if (!CryptStringToBinaryA(challenge.data(), static_cast<DWORD>(challenge.size()), flags,
                                      input.data(), &length, nullptr, nullptr))
                return {};
            input.resize(length);
        }
        std::array<SecBuffer, 2> input_buffers{};
        unsigned long input_count = 0;
        if (!input.empty())
            input_buffers[input_count++] = {static_cast<unsigned long>(input.size()), SECBUFFER_TOKEN, input.data()};
        if (!binding_.empty())
            input_buffers[input_count++] = {static_cast<unsigned long>(binding_.size()),
                                            SECBUFFER_CHANNEL_BINDINGS, binding_.data()};
        SecBufferDesc input_desc{SECBUFFER_VERSION, input_count, input_buffers.data()};
        SecBuffer output_token{0, SECBUFFER_TOKEN, nullptr};
        SecBufferDesc output_desc{SECBUFFER_VERSION, 1, &output_token};
        unsigned long attributes = 0;
        const auto status = InitializeSecurityContextW(&credentials_, started_ ? &context_ : nullptr,
            target_.data(), ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_CONNECTION | (integrity_ ? ISC_REQ_INTEGRITY : 0),
            0, SECURITY_NATIVE_DREP,
            input_count ? &input_desc : nullptr, 0, &context_, &output_desc, &attributes, nullptr);
        const auto release = httplib::detail::scope_exit([&] {
            if (output_token.pvBuffer)
                FreeContextBuffer(output_token.pvBuffer);
        });
        if (status != SEC_E_OK && status != SEC_I_CONTINUE_NEEDED &&
            status != SEC_I_COMPLETE_NEEDED && status != SEC_I_COMPLETE_AND_CONTINUE)
            return {};
        started_ = true;
        complete_ = status == SEC_E_OK || status == SEC_I_COMPLETE_NEEDED;
        if (complete_ && integrity_ && !(attributes & ISC_RET_INTEGRITY))
            return {};
        if ((status == SEC_I_COMPLETE_NEEDED || status == SEC_I_COMPLETE_AND_CONTINUE) &&
            CompleteAuthToken(&context_, &output_desc) != SEC_E_OK)
            return {};
        valid_step_ = true;
        if (!output_token.pvBuffer || !output_token.cbBuffer)
            return {};
        return httplib::detail::base64_encode(
            std::string(static_cast<const char*>(output_token.pvBuffer), output_token.cbBuffer));
    }

    bool accept_final(std::string_view challenge) {
        if (complete_)
            return true;
        next_token(challenge);
        return valid_step_;
    }

    void channel_binding(const std::string& data) {
        binding_.clear();
        if (data.empty())
            return;
        SEC_CHANNEL_BINDINGS header{};
        header.cbApplicationDataLength = static_cast<unsigned long>(data.size());
        header.dwApplicationDataOffset = sizeof(header);
        binding_.resize(sizeof(header));
        std::memcpy(binding_.data(), &header, sizeof(header));
        binding_.insert(binding_.end(), data.begin(), data.end());
    }
};
#elif defined(OPENSIM_HTTP_GSSAPI)
std::optional<std::string> decode_proxy_token(std::string_view encoded) {
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (encoded.size() % 4 != 0)
        return {};
    std::string decoded;
    decoded.reserve(encoded.size() / 4 * 3);
    for (std::size_t index = 0; index < encoded.size(); index += 4) {
        std::uint32_t bits = 0;
        unsigned padding = 0;
        for (std::size_t offset = 0; offset < 4; ++offset) {
            const char character = encoded[index + offset];
            const auto digit = alphabet.find(character);
            bits <<= 6;
            if (digit != alphabet.npos && !padding) {
                bits |= static_cast<std::uint32_t>(digit);
            } else if (character == '=' && offset >= 2 && index + 4 == encoded.size()) {
                ++padding;
            } else {
                return {};
            }
        }
        for (unsigned byte = 0; byte < 3 - padding; ++byte)
            decoded.push_back(static_cast<char>(bits >> (16 - byte * 8)));
    }
    return decoded;
}

class HttpSecurityContext {
    gss_name_t target_ = GSS_C_NO_NAME;
    gss_cred_id_t credentials_ = GSS_C_NO_CREDENTIAL;
    gss_ctx_id_t context_ = GSS_C_NO_CONTEXT;
    gss_OID mechanism_;
    bool acquired_ = false;
    bool complete_ = false;
    bool valid_step_ = false;
    bool integrity_;
    std::string binding_;

  public:
    HttpSecurityContext(const HttpSecurityContext&) = delete;
    HttpSecurityContext& operator=(const HttpSecurityContext&) = delete;

    HttpSecurityContext(std::string_view package, std::string_view host, const ConfigHttpNetworkCredential& credential, bool integrity)
        : integrity_(integrity) {
        static unsigned char ntlm_oid[] = {0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x02, 0x0a};
        static unsigned char spnego_oid[] = {0x2b, 0x06, 0x01, 0x05, 0x05, 0x02};
        static gss_OID_desc ntlm{sizeof(ntlm_oid), ntlm_oid};
        static gss_OID_desc spnego{sizeof(spnego_oid), spnego_oid};
        mechanism_ = package == "NTLM" ? &ntlm : &spnego;

        OM_uint32 minor = 0;
        auto target = "HTTP@" + std::string(host);
        gss_buffer_desc target_buffer{target.size(), target.data()};
        auto status = gss_import_name(&minor, &target_buffer, GSS_C_NT_HOSTBASED_SERVICE, &target_);
        if (GSS_ERROR(status))
            return;
        gss_OID_set_desc mechanisms{1, mechanism_};
        if (credential.is_default) {
            status = gss_acquire_cred(&minor, GSS_C_NO_NAME, 0, &mechanisms, GSS_C_INITIATE,
                                      &credentials_, nullptr, nullptr);
            acquired_ = !GSS_ERROR(status);
            return;
        }

        auto username = utf8(*credential.user_name);
        auto domain = utf8(*credential.domain);
        const auto separator = username.find('\\');
        if (domain.empty() && separator != username.npos && separator != 0 &&
            username.find('\\', separator + 1) == username.npos) {
            domain = username.substr(0, separator);
            username.erase(0, separator + 1);
        }
        username = trim_utf8(username);
        domain = trim_utf8(domain);
        if (username.find('@') == username.npos && !domain.empty())
            username += "@" + domain;
        auto password = utf8(*credential.password);
        gss_name_t name = GSS_C_NO_NAME;
        gss_buffer_desc user_buffer{username.size(), username.data()};
        status = gss_import_name(&minor, &user_buffer, GSS_C_NT_USER_NAME, &name);
        if (GSS_ERROR(status))
            return;
        const auto release_name = httplib::detail::scope_exit([&] { gss_release_name(&minor, &name); });
        gss_buffer_desc password_buffer{password.size(), password.data()};
        status = gss_acquire_cred_with_password(&minor, name, &password_buffer, 0, &mechanisms,
                                                GSS_C_INITIATE, &credentials_, nullptr, nullptr);
        acquired_ = !GSS_ERROR(status);
    }

    ~HttpSecurityContext() {
        OM_uint32 minor = 0;
        if (context_ != GSS_C_NO_CONTEXT)
            gss_delete_sec_context(&minor, &context_, GSS_C_NO_BUFFER);
        if (credentials_ != GSS_C_NO_CREDENTIAL)
            gss_release_cred(&minor, &credentials_);
        if (target_ != GSS_C_NO_NAME)
            gss_release_name(&minor, &target_);
    }

    std::optional<std::string> next_token(std::string_view challenge) {
        valid_step_ = false;
        if (!acquired_ || complete_ || (context_ != GSS_C_NO_CONTEXT && challenge.empty()))
            return {};
        auto input = decode_proxy_token(challenge);
        if (!input)
            return {};
        gss_buffer_desc input_buffer{input->size(), input->data()};
        gss_buffer_desc output = GSS_C_EMPTY_BUFFER;
        OM_uint32 minor = 0;
#ifdef __APPLE__
        constexpr OM_uint32 flags = GSS_C_INTEG_FLAG;
#else
        const OM_uint32 flags = integrity_ ? GSS_C_INTEG_FLAG : 0;
#endif
        OM_uint32 returned_flags = 0;
        gss_channel_bindings_struct bindings{};
        bindings.application_data = {binding_.size(), binding_.data()};
        const auto status = gss_init_sec_context(&minor, credentials_, &context_, target_, mechanism_, flags, 0,
            binding_.empty() ? GSS_C_NO_CHANNEL_BINDINGS : &bindings,
            input->empty() ? GSS_C_NO_BUFFER : &input_buffer, nullptr, &output,
            &returned_flags, nullptr);
        const auto release = httplib::detail::scope_exit([&] { gss_release_buffer(&minor, &output); });
        if (GSS_ERROR(status))
            return {};
        complete_ = !(status & GSS_S_CONTINUE_NEEDED);
        if (complete_ && (flags & GSS_C_INTEG_FLAG) && !(returned_flags & GSS_C_INTEG_FLAG))
            return {};
        valid_step_ = true;
        if (!output.length)
            return {};
        return httplib::detail::base64_encode(std::string(static_cast<const char*>(output.value), output.length));
    }

    bool accept_final(std::string_view challenge) {
        if (complete_)
            return true;
        next_token(challenge);
        return valid_step_;
    }

    void channel_binding(std::string data) {
        binding_ = std::move(data);
    }
};
#endif

#ifdef OPENSIM_HTTP_INTEGRATED_PROXY

std::optional<std::string> integrated_authentication_challenge(
    const httplib::Response& response, const std::string& scheme, bool is_proxy = true) {
    const auto [first, last] = response.headers.equal_range(is_proxy ? "Proxy-Authenticate" : "WWW-Authenticate");
    for (auto entry = first; entry != last; ++entry) {
        std::optional<std::string> challenge;
        const auto& value = entry->second;
        httplib::detail::split_unquoted(value.data(), value.data() + value.size(), ',',
            [&](const char* start, const char* end) {
                if (challenge)
                    return;
                const auto header = httplib::detail::trim_copy(std::string(start, end));
                const auto separator = header.find_first_of(" \t");
                if (httplib::detail::case_ignore::equal(header.substr(0, separator), scheme))
                    challenge = separator == header.npos
                        ? std::string{} : httplib::detail::trim_copy(header.substr(separator));
            });
        if (challenge)
            return challenge;
    }
    return {};
}

bool drain_proxy_challenge(httplib::Stream& stream, const httplib::Response& response) {
    if (httplib::detail::has_header_token(response.headers, "Connection", "close") ||
        (response.version == "HTTP/1.0" &&
         !httplib::detail::has_header_token(response.headers, "Connection", "keep-alive")))
        return false;
    httplib::detail::BodyReader reader;
    reader.stream = &stream;
    reader.payload_max_length = 65536;
    reader.chunked = httplib::detail::has_header_token(response.headers, "Transfer-Encoding", "chunked");
    reader.has_content_length = response.has_header("Content-Length");
    if (!reader.chunked && !reader.has_content_length)
        return false;
    if (reader.has_content_length)
        reader.content_length = response.get_header_value_u64("Content-Length");
    if (!reader.chunked && reader.content_length > reader.payload_max_length)
        return false;
    std::array<char, 4096> buffer;
    while (reader.read(buffer.data(), buffer.size()) > 0) {}
    if (reader.has_error())
        return false;
    httplib::Headers trailers;
    return !reader.chunked || reader.chunked_decoder->parse_trailers_into(trailers, response.headers);
}

struct ProxyIntegratedExchange {
    std::unique_ptr<HttpSecurityContext> context;
    std::string scheme;
    std::optional<std::string> first_authorization;
    std::optional<ConfigXmlHttpRoute> route;
};

std::optional<std::string> begin_integrated_exchange(
    ProxyIntegratedExchange& exchange, const ConfigXmlHttpRoute& route, const httplib::Response& response,
    bool is_proxy = true, const std::shared_ptr<ConfigXmlUri>& origin_uri = {}) {
    if (!route.credentials)
        return {};
    for (const auto scheme : {"Negotiate", "NTLM"}) {
        auto challenge = integrated_authentication_challenge(response, scheme, is_proxy);
        if (!challenge)
            continue;
        const auto credential = credential_for_challenge(route.credentials, route.proxy_uri, origin_uri, scheme);
        if (!credential)
            continue;
        // Follow aliases without reverse-resolving IP literals.
        const auto host = IpAddress::parse(route.host)
            ? route.host : HostResolver::system_canonical_name(route.host).value_or(route.host);
        exchange.scheme = scheme;
        exchange.context = std::make_unique<HttpSecurityContext>(exchange.scheme, host, *credential, !is_proxy);
        return challenge;
    }
    // The managed connection-authentication pass selects from all four
    // schemes before the outer request-authentication pass handles Digest/Basic.
    if (origin_uri && std::dynamic_pointer_cast<ConfigHttpOriginCredentials>(route.credentials)) {
        const auto [first, last] = response.headers.equal_range("WWW-Authenticate");
        for (const auto scheme : {"Digest", "Basic"}) {
            const bool offered = std::ranges::any_of(std::ranges::subrange(first, last), [&](const auto& header) {
                return has_proxy_challenge(header.second, scheme);
            });
            if (offered && credential_for_challenge(route.credentials, {}, origin_uri, scheme))
                break;
        }
    }
    return {};
}

struct ReconnectIntegratedProxy {};

void validate_integrated_authentication(ProxyIntegratedExchange& exchange, const httplib::Response& response,
                                   bool is_proxy = true) {
    if (!exchange.context || response.status == (is_proxy ? 407 : 401))
        return;
    const auto token = integrated_authentication_challenge(response, exchange.scheme, is_proxy);
    if (token && !exchange.context->accept_final(*token))
        throw ConfigXmlHttpError("System.Net.Http.HttpRequestException",
                                 "Authentication validation failed with error - InvalidToken.", 0x80131500, 401);
}
#endif

struct Handler final : ConfigXmlHttpHandler {
    ConfigXmlNativeHttpServices services;
    std::shared_ptr<ConfigXmlCookieContainer> jar;
    std::shared_ptr<ConfigXmlCredentials> credentials_;
    std::shared_ptr<ConfigXmlProxy> proxy_;
    std::once_flag proxy_started_;
    std::shared_ptr<ConfigXmlProxy> effective_proxy_;
    std::shared_ptr<ConfigXmlHttpProxyProvider> proxy_provider_;
    std::shared_ptr<ConfigHttpNativePool> pool;
    std::shared_ptr<ConfigXmlProxy> effective_proxy() {
        std::call_once(proxy_started_, [this] {
            effective_proxy_ =
                proxy_ ? proxy_ : (services.default_proxy ? services.default_proxy() : nullptr);
            if (services.proxy_provider) {
                proxy_provider_ = services.proxy_provider(effective_proxy_, bool(proxy_));
                if (!proxy_provider_)
                    throw std::invalid_argument("XML HTTP proxy provider factory returned no provider");
            }
        });
        return effective_proxy_;
    }
    explicit Handler(ConfigXmlNativeHttpServices s, bool cookies = true) : services(std::move(s)) {
        if (services.connection_pool)
            pool = std::make_shared<ConfigHttpNativePool>(*services.connection_pool);
        if (cookies) {
            jar = services.cookies();
            if (!jar)
                throw std::invalid_argument("XML HTTP requires CookieContainer provider result");
        }
    }
    void credentials(std::shared_ptr<ConfigXmlCredentials> c) override {
        credentials_ = std::move(c);
    }
    void proxy(std::shared_ptr<ConfigXmlProxy> p) override {
        proxy_ = std::move(p);
    }
};
struct HeadBuffer {
    ConfigHttpResponseHead head;
    std::vector<std::uint8_t> unread;
    std::shared_ptr<ConfigHttpSourceReceiveBuffer> receive;
    std::optional<std::chrono::steady_clock::time_point> pending_deadline;
};
#ifdef OPENSIM_HTTP_INTEGRATED_PROXY
bool drain_authentication_challenge(httplib::ClientImpl::StreamHandle& response, HeadBuffer& head,
                                   bool head_only) {
    auto framing = head.head;
    if (head_only)
        framing.status = 204;
    ConfigHttpXmlBodyReader reader(framing, [&](std::span<std::uint8_t> buffer) -> std::int32_t {
        if (!head.unread.empty()) {
            const auto count = std::min(buffer.size(), head.unread.size());
            std::copy_n(head.unread.begin(), count, buffer.begin());
            head.unread.erase(head.unread.begin(), head.unread.begin() + count);
            return static_cast<std::int32_t>(count);
        }
        return static_cast<std::int32_t>(response.read_raw(reinterpret_cast<char*>(buffer.data()), buffer.size()));
    });
    if (!reader.can_drain_for_reuse(head.unread.size() + response.opensim_buffered_size(), 65536))
        return false;
    std::array<std::uint8_t, 4096> buffer;
    while (reader.read(buffer) > 0) {}
    return reader.completed_for_reuse() && head.unread.empty() && !response.opensim_has_buffered_data();
}
#endif
struct BodyState {
    using Clock = std::chrono::steady_clock;
    std::shared_ptr<ConfigXmlHttpProxyProvider> proxy_owner;
    std::shared_ptr<ConfigHttpNativePool::Lease> lease;
    httplib::ClientImpl::StreamHandle handle;
    ConfigXmlNativeHttpServices services;
    std::shared_ptr<HeadBuffer> source;
    std::unique_ptr<ConfigHttpXmlBodyReader> reader;
    bool reusable, chunked = false, draining = false;
    std::function<bool()> interrupted;
    std::optional<Clock::time_point> drain_deadline;
    std::size_t drained_wire_bytes = 0;
    std::mutex io_mutex, progress_mutex;
    std::mutex pending_mutex;
    bool pending_read_drainable = false;
    std::condition_variable progress;
    bool async_boundary = false;
    BodyState(httplib::ClientImpl::StreamHandle h, ConfigXmlNativeHttpServices s,
              std::shared_ptr<HeadBuffer> input, std::shared_ptr<ConfigXmlHttpProxyProvider> proxy,
              bool head_only, bool raw_tunnel, std::shared_ptr<ConfigHttpNativePool::Lease> pool_lease, bool can_reuse,
              std::function<bool()> stop)
        : proxy_owner(std::move(proxy)), lease(std::move(pool_lease)), handle(std::move(h)),
          services(std::move(s)), source(std::move(input)), reusable(can_reuse),
          interrupted(std::move(stop)) {
        auto framing = source->head;
        if (head_only)
            framing.status = 204;
        else if (raw_tunnel)
            framing.status = 101; // Same opaque stream framing as an HTTP upgrade.
        std::vector<std::u16string> encodings;
        for (auto& header : framing.headers)
            if (httplib::detail::case_ignore::equal(header.name, "Transfer-Encoding"))
                encodings.push_back(header.value);
        chunked = !raw_tunnel && config_http_transfer_encoding_chunked(encodings);
        handle.set_read_interrupt([this] {
            return (interrupted && interrupted()) || (drain_deadline && Clock::now() >= *drain_deadline);
        });
        reader =
            std::make_unique<ConfigHttpXmlBodyReader>(framing, [this](auto bytes) { return raw(bytes); });
        completed();
    }
    void signal_async_boundary() {
        std::lock_guard lock(progress_mutex);
        async_boundary = true;
        progress.notify_all();
    }
    std::int32_t socket_read(std::span<std::uint8_t> bytes) {
        {
            std::lock_guard lock(pending_mutex);
            pending_read_drainable = source->receive && reader && reusable &&
                                     reader->can_drain_for_reuse(
                                         source->unread.size() + handle.opensim_buffered_size(), 1048576) &&
                                     !handle.opensim_read_ready();
        }
        struct PendingDone {
            BodyState* owner;
            ~PendingDone() {
                std::lock_guard lock(owner->pending_mutex);
                owner->pending_read_drainable = false;
            }
        } done{this};
        auto n = handle.read_raw(reinterpret_cast<char*>(bytes.data()), bytes.size());
        if (n < 0) {
            if (handle.opensim_native_read_error && services.read_error) {
                auto error = services.read_error(false, handle.opensim_native_read_error);
                if (!error)
                    throw std::invalid_argument("XML HTTP read error provider returned no exception");
                std::rethrow_exception(error);
            }
            auto error = services.error("body", static_cast<int>(httplib::Error::Read));
            if (!error)
                throw std::invalid_argument("XML HTTP error provider returned no exception");
            std::rethrow_exception(error);
        }
        return static_cast<std::int32_t>(n);
    }
    void cancel_buffered_copy() {
        std::lock_guard lock(pending_mutex);
        if (pending_read_drainable)
            source->receive->begin_drain();
    }
    std::int32_t raw(std::span<std::uint8_t> bytes) {
        if (source->unread.empty() && draining) {
            if (chunked && drained_wire_bytes >= 1048576)
                throw std::runtime_error("HTTP response drain limit reached");
            if (!drain_deadline)
                drain_deadline = Clock::now() + std::chrono::seconds(2);
            if (!handle.opensim_read_ready())
                signal_async_boundary();
            std::array<std::uint8_t, 4096> buffer;
            auto n = socket_read(buffer);
            if (!n)
                return 0;
            drained_wire_bytes += n;
            source->unread.assign(buffer.begin(), buffer.begin() + n);
        }
        if (!source->unread.empty()) {
            auto n = std::min(bytes.size(), source->unread.size());
            std::copy_n(source->unread.begin(), n, bytes.begin());
            source->unread.erase(source->unread.begin(), source->unread.begin() + n);
            return static_cast<std::int32_t>(n);
        }
        return socket_read(bytes);
    }
    void completed() {
        if (lease && reader->completed_for_reuse()) {
            auto owner = std::move(lease);
            handle.set_read_interrupt({});
            bool can_reuse = reusable && source->unread.empty() && !handle.opensim_has_buffered_data();
            owner->finish(std::move(handle), can_reuse);
            handle = {};
        }
    }
    void discard() {
        if (lease)
            lease->finish(std::move(handle), false);
        lease.reset();
        handle = {};
        proxy_owner.reset();
    }
    std::int32_t read_unlocked(std::span<std::uint8_t> bytes) {
        try {
            auto n = reader->read(bytes);
            completed();
            return n;
        } catch (...) {
            discard();
            throw;
        }
    }
    std::int32_t read(std::span<std::uint8_t> bytes) {
        std::lock_guard lock(io_mutex);
        return read_unlocked(bytes);
    }
    void drain() {
        std::lock_guard lock(io_mutex);
        draining = true;
        drained_wire_bytes = source->unread.size();
        try {
            std::array<std::uint8_t, 4096> buffer;
            while (lease && read_unlocked(buffer)) {
            };
        } catch (...) {
        }
        discard();
        signal_async_boundary();
    }
    void dispose(const std::shared_ptr<BodyState>& self) {
        {
            std::lock_guard lock(io_mutex);
            if (!lease || !reusable ||
                !reader->can_drain_for_reuse(source->unread.size() + handle.opensim_buffered_size(),
                                             1048576)) {
                discard();
                return;
            }
        }
        try {
            std::thread([self] { self->drain(); }).detach();
        } catch (...) {
            std::lock_guard lock(io_mutex);
            discard();
            return;
        }
        // DrainOnDisposeAsync runs synchronously through buffered entity parsing,
        // then returns at the first pending socket read. Keep that ordering here.
        std::unique_lock lock(progress_mutex);
        progress.wait(lock, [&] { return async_boundary; });
    }
};
struct Body final : ConfigXmlStream {
    opensim::framework::AtomicSharedPtr<BodyState> state;
    Body(httplib::ClientImpl::StreamHandle h, ConfigXmlNativeHttpServices s,
         std::shared_ptr<HeadBuffer> source, std::shared_ptr<ConfigXmlHttpProxyProvider> proxy,
         bool head_only, bool raw_tunnel, std::shared_ptr<ConfigHttpNativePool::Lease> lease = {},
         bool reusable = false, std::function<bool()> stop = {})
        : state(std::make_shared<BodyState>(std::move(h), std::move(s), std::move(source), std::move(proxy),
                                            head_only, raw_tunnel, std::move(lease), reusable, std::move(stop))) {
    }
    ~Body() {
        close();
    }
    std::int32_t read(std::span<std::uint8_t> bytes) override {
        auto reading = state.load();
        if (!reading)
            return 0;
        return reading->read(bytes);
    }
    void close() override {
        auto closing = state.exchange({});
        if (closing)
            closing->dispose(closing);
    }
    void cancel_buffered_copy() override {
        auto cancelling = state.load();
        if (cancelling)
            cancelling->cancel_buffered_copy();
        close();
    }
};
[[noreturn]] void unsupported(const char* text) {
    throw std::runtime_error(text);
}
void configure_tls_client(httplib::Client& client, const ConfigXmlNativeHttpServices& services,
                          const ConfigXmlHttpAddress& transport_address, const std::u16string& tls_host,
                          const std::function<std::exception_ptr(std::string, int)>& request_error) {
#ifdef CPPHTTPLIB_SSL_ENABLED
    auto tls_name = std::make_shared<std::string>(transport_address.host);
    auto tls_normalized = std::make_shared<std::u16string>();
    client.opensim_set_tls_server_names([tls_host, tls_name, tls_normalized, request_error] {
        try {
            auto names = config_http_tls_target_names(tls_host);
            *tls_name = names.certificate_host;
            *tls_normalized = std::move(names.normalized);
            return httplib::OpensimTlsServerNames{std::move(names.certificate_host),
                                                  std::move(names.sni_host),
                                                  names.ignore_sni_error};
        } catch (...) {
            auto error = request_error("tls", 0);
            if (!error)
                throw std::invalid_argument("TLS target error provider returned no exception");
            std::rethrow_exception(error);
        }
    });
    bool verify_chain = !services.tls_settings || services.tls_settings->verify_chain;
    // macOS uses cpp-httplib's Keychain-backed defaults when no CA file is supplied.
#if !defined(_WIN32) && !defined(__linux__) && !defined(__APPLE__)
    if (verify_chain && !services.tls_ca_file)
        unsupported("XML HTTPS requires explicit native certificate trust provider");
#endif
    if (verify_chain && services.tls_ca_file) {
        auto ca = services.tls_ca_file(transport_address);
        if (ca.empty())
            unsupported("XML HTTPS native trust provider returned no CA file");
        client.set_ca_cert_path(ca);
    }
    if (services.tls_settings) {
        auto settings = *services.tls_settings;
        if (settings.enabled_protocols != 16320)
            unsupported(
                "Native WebUtil TLS requires its verified TLS1.0-through-TLS1.3 protocol policy");
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        auto context = static_cast<SSL_CTX*>(client.tls_context());
        if (!context || SSL_CTX_set_min_proto_version(context, TLS1_VERSION) != 1 ||
            SSL_CTX_set_max_proto_version(context, TLS1_3_VERSION) != 1)
            unsupported("Native WebUtil TLS protocol policy could not be applied");
#else
        unsupported("Native WebUtil TLS protocol policy requires the OpenSSL provider");
#endif
        client.enable_server_certificate_verification(settings.verify_chain);
        client.enable_server_hostname_verification(settings.verify_hostname);
        if (!settings.verify_chain) {
            // Disabling chain verification must not also disable the independent
            // hostname check, and never accepts a missing peer certificate.
            client.set_session_verifier([verify_name = settings.verify_hostname,
                                         name = tls_name](httplib::tls::session_t session) {
                auto cert = httplib::tls::get_peer_cert(session);
                if (!cert)
                    return httplib::SSLVerifierResponse::CertificateRejected;
                auto accepted =
                    !verify_name || httplib::tls::verify_hostname(cert, name->c_str());
                httplib::tls::free_cert(cert);
                return accepted ? httplib::SSLVerifierResponse::CertificateAccepted
                                : httplib::SSLVerifierResponse::CertificateRejected;
            });
        }
    }
#ifdef _WIN32
    // SslStream delegates Windows name matching to Crypt32. Its policy accepts
    // legacy DNS/CN identities as well as IP SANs; OpenSSL's IP-only check is
    // insufficient for these source semantics. Trust remains the CA provider.
    bool verify_name = !services.tls_settings || services.tls_settings->verify_hostname;
    client.enable_server_hostname_verification(false);
    bool platform_trust = !services.tls_ca_file;
    if (platform_trust)
        client.enable_server_certificate_verification(false);
    client.set_session_verifier([verify_chain, verify_name, platform_trust,
                                 name = tls_normalized](httplib::tls::session_t session) {
        auto cert = httplib::tls::get_peer_cert(session);
        if (!cert)
            return httplib::SSLVerifierResponse::CertificateRejected;
        struct CertGuard {
            httplib::tls::cert_t value;
            ~CertGuard() {
                httplib::tls::free_cert(value);
            }
        } guard{cert};
        std::vector<unsigned char> der;
        if (platform_trust) {
            if (!httplib::tls::get_cert_der(cert, der))
                return httplib::SSLVerifierResponse::CertificateRejected;
            struct PeerGuard {
                std::vector<httplib::tls::cert_t> certs;
                ~PeerGuard() {
                    for (auto value : certs)
                        httplib::tls::free_cert(value);
                }
            } peers;
            httplib::tls::get_peer_certs(session, peers.certs);
            std::vector<std::vector<std::uint8_t>> extra;
            for (auto peer : peers.certs) {
                std::vector<unsigned char> item;
                if (!httplib::tls::get_cert_der(peer, item))
                    return httplib::SSLVerifierResponse::CertificateRejected;
                extra.push_back(std::move(item));
            }
            auto errors = config_http_windows_certificate_errors(der, extra, *name);
            if (!verify_chain)
                errors &= ~4;
            if (!verify_name)
                errors &= ~2;
            return errors == 0 ? httplib::SSLVerifierResponse::CertificateAccepted
                               : httplib::SSLVerifierResponse::CertificateRejected;
        }
        bool accepted = !verify_name || (httplib::tls::get_cert_der(cert, der) &&
                                         config_http_windows_certificate_name(der, *name));
        if (!accepted)
            return httplib::SSLVerifierResponse::CertificateRejected;
        return verify_chain ? httplib::SSLVerifierResponse::NoDecisionMade
                            : httplib::SSLVerifierResponse::CertificateAccepted;
    });
#elif defined(__linux__)
    bool verify_name = !services.tls_settings || services.tls_settings->verify_hostname;
    const bool platform_trust = !services.tls_ca_file;
    client.enable_server_hostname_verification(false);
    if (platform_trust)
        client.enable_server_certificate_verification(false);
    client.set_session_verifier([verify_chain, verify_name, platform_trust, name = tls_normalized,
                                 request_error](httplib::tls::session_t session) {
        auto cert = httplib::tls::get_peer_cert(session);
        if (!cert)
            return httplib::SSLVerifierResponse::CertificateRejected;
        struct CertGuard {
            httplib::tls::cert_t value;
            ~CertGuard() {
                httplib::tls::free_cert(value);
            }
        } guard{cert};
        // WebUtil masks a returned name-mismatch flag, not exceptions raised
        // while constructing that flag (notably the secondary GetAscii call).
        try {
            CertificateDer der;
            if (!httplib::tls::get_cert_der(cert, der))
                return httplib::SSLVerifierResponse::CertificateRejected;
            if (platform_trust) {
                struct PeerGuard {
                    std::vector<httplib::tls::cert_t> certs;
                    ~PeerGuard() {
                        for (auto value : certs)
                            httplib::tls::free_cert(value);
                    }
                } peers;
                httplib::tls::get_peer_certs(session, peers.certs);
                std::vector<CertificateDer> extra;
                for (auto peer : peers.certs) {
                    CertificateDer item;
                    if (!httplib::tls::get_cert_der(peer, item))
                        return httplib::SSLVerifierResponse::CertificateRejected;
                    extra.push_back(std::move(item));
                }
                // Issuer downloads use their own default proxy and connection pool.
                static const ConfigHttpX509ResourceClient resources([] {
                    ConfigXmlNativeHttpServices policy;
                    policy.error = [](std::string, int) {
                        return std::make_exception_ptr(
                            std::runtime_error("Certificate resource download failed"));
                    };
                    return policy;
                }());
                auto errors = config_http_linux_certificate_errors_with_downloads(
                                  der, extra, *name, {}, resources)
                                  .errors;
                if (!verify_chain)
                    errors &= ~4;
                if (!verify_name)
                    errors &= ~2;
                return errors == 0 ? httplib::SSLVerifierResponse::CertificateAccepted
                                   : httplib::SSLVerifierResponse::CertificateRejected;
            }
            if (!config_http_linux_certificate_name(der, *name) && verify_name)
                return httplib::SSLVerifierResponse::CertificateRejected;
        } catch (...) {
            auto error = request_error("tls", 0);
            if (!error)
                throw std::invalid_argument(
                    "TLS certificate name error provider returned no exception");
            std::rethrow_exception(error);
        }
        return verify_chain ? httplib::SSLVerifierResponse::NoDecisionMade
                            : httplib::SSLVerifierResponse::CertificateAccepted;
    });
#endif
#else
    unsupported("XML HTTPS requires a TLS-enabled native transport build");
#endif
}

class Client final : public ConfigXmlHttpClient {
    std::shared_ptr<Handler> handler_;

  public:
    explicit Client(std::shared_ptr<Handler> h) : handler_(std::move(h)) {
    }
    std::shared_ptr<ConfigXmlStream> get_stream(std::shared_ptr<ConfigXmlUri> uri) override {
        return request_stream(std::move(uri), {});
    }
    std::shared_ptr<ConfigXmlStream> request_stream(std::shared_ptr<ConfigXmlUri> uri,
                                                    ConfigXmlHttpOperation operation) {
        auto scheme = uri->scheme();
        if (*scheme != u"http" && *scheme != u"https" && *scheme != u"ws" && *scheme != u"wss")
            throw ConfigUriComponentsError("System.NotSupportedException",
                                           u"The '" + *scheme + u"' scheme is not supported.", 0x80131515);
        if (handler_->credentials_ &&
            !std::dynamic_pointer_cast<ConfigHttpNetworkCredential>(handler_->credentials_) &&
            !std::dynamic_pointer_cast<ConfigHttpOriginCredentials>(handler_->credentials_))
            unsupported("XML HTTP origin credentials require a native network credential adapter");
        auto& services = handler_->services;
        auto& request_error = operation.error ? operation.error : services.error;
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(operation.timeout_milliseconds);
        auto selected_proxy = handler_->effective_proxy();
        std::shared_ptr<ConfigXmlHttpProxyRoutePlan> proxy_plan;
        std::optional<std::string> proxy_authorization;
        std::optional<std::string> origin_authorization;
        unsigned origin_auth_retries = 0;
        std::shared_ptr<ConfigHttpNetworkCredential> origin_digest;
        std::shared_ptr<ConfigHttpNetworkCredential> proxy_digest;
        auto origin_credentials = handler_->credentials_;
        std::optional<ConfigXmlHttpRoute> authorization_route;
        unsigned proxy_auth_retries = 0;
#ifdef OPENSIM_HTTP_INTEGRATED_PROXY
        ProxyIntegratedExchange integrated_proxy;
#endif
        unsigned connection_retries = 0;
        for (unsigned redirects = 0;;) {
            auto address = services.address(uri);
            if ((handler_->proxy_provider_ || services.proxy_plan) && !proxy_plan) {
                proxy_plan = handler_->proxy_provider_ ? handler_->proxy_provider_->routes(uri)
                                                       : services.proxy_plan(uri, selected_proxy);
                if (!proxy_plan)
                    throw std::invalid_argument("XML HTTP proxy plan provider returned no plan");
            }
            auto route = proxy_plan ? proxy_plan->current() : services.proxy(uri, selected_proxy);
            const auto proxy_scheme = route && route->proxy_uri ? *route->proxy_uri->scheme() : std::u16string{};
#ifdef OPENSIM_HTTP_INTEGRATED_PROXY
            if (integrated_proxy.route &&
                (!route || !route->proxy_uri || route->host != integrated_proxy.route->host ||
                 route->port != integrated_proxy.route->port ||
                 proxy_scheme != *integrated_proxy.route->proxy_uri->scheme()))
                integrated_proxy = {};
#endif
            if (proxy_authorization &&
                (!route || !route->proxy_uri || route->host != authorization_route->host ||
                 route->port != authorization_route->port || proxy_scheme != *authorization_route->proxy_uri->scheme())) {
                proxy_authorization.reset();
                proxy_auth_retries = 0;
                proxy_digest.reset();
            }
            const bool socks = proxy_scheme == u"socks4" || proxy_scheme == u"socks4a" || proxy_scheme == u"socks5";
            const bool tls_proxy = proxy_scheme == u"https";
            const bool tls_forward_proxy = tls_proxy && !address.tls;
            const bool forward_proxy = !address.tls && (tls_proxy || proxy_scheme == u"http");
            if (handler_->proxy_provider_ && route && route->proxy_uri &&
                ((!socks && !tls_proxy && proxy_scheme != u"http") ||
                 (*uri->scheme() != u"http" && *uri->scheme() != u"https")))
                throw ConfigHttpProxyProviderError(
                    "This proxy/target scheme requires its verified CONNECT/TLS/SOCKS transport provider");
            const auto transport_address = tls_forward_proxy
                ? ConfigXmlHttpAddress{route->host, {}, route->port, true}
                : address;
            auto source_head = std::make_shared<HeadBuffer>();
            httplib::Request request;
            request.method = operation.method;
            request.path = address.target;
            request.body = operation.body;
            for (const auto& [name, value] : operation.headers) {
                std::string placeholder;
                if (value)
                    for (auto c : *value)
                        placeholder += c <= 127 ? static_cast<char>(c) : '?';
                request.headers.emplace(name, std::move(placeholder));
            }
            request.opensim_no_default_accept_headers = true;
            request.opensim_stream_raw_content = true;
            request.opensim_request_ascii_headers = true;
            request.opensim_response_head_reader = [source_head, can_retry = request.body.empty()](
                                                       const auto& read, httplib::Response& response) {
                std::vector<std::uint8_t> raw;
                std::size_t spent = 0;
                bool refill = true;
                if (source_head->pending_deadline) {
                    std::vector<std::uint8_t> first(source_head->receive->capacity()), second(first.size());
                    auto count = read(reinterpret_cast<char*>(first.data()), first.size());
                    if (count < 0)
                        return false;
                    if (!count)
                        config_http_response_headers_ended(can_retry);
                    first.resize(count);
                    source_head->pending_deadline.reset();
                    count = read(reinterpret_cast<char*>(second.data()), second.size());
                    if (count < 0)
                        return false;
                    if (!count)
                        config_http_response_headers_ended(false);
                    second.resize(count);
                    raw = source_head->receive->overlapping_fills(first, second);
                    refill = false;
                }
                while (true) {
                    if (refill) {
                        if (spent == 65536)
                            config_http_parse_response_head(raw, 0, 65536);
                        std::array<std::uint8_t, 4096> buffer;
                        auto n = read(reinterpret_cast<char*>(buffer.data()), buffer.size());
                        if (n < 0)
                            return false;
                        if (!n)
                            config_http_response_headers_ended(can_retry && raw.empty() && spent == 0);
                        raw.insert(raw.end(), buffer.begin(), buffer.begin() + n);
                    }
                    auto head = config_http_parse_response_head(raw, 65536 - spent, 65536);
                    if (!head) {
                        refill = true;
                        continue;
                    }
                    auto consumed = head->bytes_consumed;
                    if (source_head->receive)
                        source_head->receive->observe(raw);
                    if (head->status >= 100 && head->status <= 199 && head->status != 101) {
                        spent += consumed;
                        raw.erase(raw.begin(), raw.begin() + consumed);
                        refill = raw.empty();
                        continue;
                    }
                    source_head->unread.assign(raw.begin() + consumed, raw.end());
                    source_head->head = *head;
                    source_head->head.bytes_consumed += spent;
                    response.status = head->status;
                    response.version = "HTTP/1." + std::to_string(head->minor_version);
                    response.reason = utf8(head->reason);
                    std::vector<std::u16string> lengths, encodings;
                    for (auto& h : head->headers) {
                        response.headers.emplace(h.name, utf8(h.value));
                        if (httplib::detail::case_ignore::equal(h.name, "Content-Length"))
                            lengths.push_back(h.value);
                        if (httplib::detail::case_ignore::equal(h.name, "Transfer-Encoding"))
                            encodings.push_back(h.value);
                    }
                    // HttpHeaders typed getters ignore malformed length/coding values and use
                    // the first Content-Length; native framing receives their exact result.
                    response.headers.erase("Content-Length");
                    response.headers.erase("Transfer-Encoding");
                    if (head->status != 101) {
                        if (config_http_transfer_encoding_chunked(encodings))
                            response.headers.emplace("Transfer-Encoding", "chunked");
                        else if (auto length = config_http_content_length(lengths))
                            response.headers.emplace("Content-Length", std::to_string(*length));
                    }
                    return true;
                }
            };
            std::vector<std::u16string> host_values;
            for (const auto& [name, value] : operation.headers)
                if (httplib::detail::case_ignore::equal(name, "Host"))
                    host_values.push_back(value ? *value : std::u16string{});
            auto parsed_host = config_http_request_host(host_values);
            const bool application_connect = request.method == "CONNECT";
            if (application_connect && parsed_host)
                request.opensim_stream_authority_target = utf8(*parsed_host);
            auto tls_host = parsed_host ? config_http_tls_host_key(*parsed_host) : utf16(address.host);
            auto fallback_host = httplib::detail::make_default_host_header_value(address.host, address.port,
                                                                                 address.tls, AF_UNSPEC);
            if (tls_forward_proxy) {
                request.path = "http://" + fallback_host + address.target;
                tls_host = utf16(transport_address.host);
                if (host_values.empty())
                    request.headers.emplace("Host", fallback_host);
            }
            request.opensim_prepare_request_headers = [raw = operation.headers, jar = handler_->jar, uri,
                                                       host_values, parsed_host,
                                                       fallback_host,
                                                       authorization = forward_proxy ? proxy_authorization : std::nullopt,
                                                       origin_authorization, application_connect,
                                                       validate_content = operation.validate_request_content](auto& headers) {
                // Values remain UTF16 until the connected request writer needs their ASCII
                // representation. In particular, TLS failures precede encoding failures.
                if (application_connect && !parsed_host)
                    throw ConfigXmlHttpError("System.Net.Http.HttpRequestException",
                                             "CONNECT request must contain Host header.", 0x80131500);
                NiniWriterText cookie;
                if (jar) {
                    cookie = jar->header(uri);
                    if (!cookie)
                        throw std::invalid_argument("XML CookieContainer provider returned null header");
                }
                httplib::Headers encoded;
                std::vector<bool> used(raw.size(), false);
                bool host_written = false;
                for (const auto& [name, value] : headers) {
                    if (!host_values.empty() && httplib::detail::case_ignore::equal(name, "Host")) {
                        if (host_written)
                            continue;
                        host_written = true;
                        auto combined = parsed_host ? *parsed_host : host_values.front();
                        for (std::size_t i = 1; i < host_values.size(); ++i)
                            combined += u", " + host_values[i];
                        auto bytes = config_http_encode_request_header(combined);
                        encoded.emplace("Host", std::string(bytes.begin(), bytes.end()));
                        continue;
                    }
                    bool found = false;
                    for (std::size_t i = 0; i < raw.size(); ++i)
                        if (!used[i] && httplib::detail::case_ignore::equal(name, raw[i].first)) {
                            used[i] = true;
                            auto bytes = config_http_encode_request_header(raw[i].second ? *raw[i].second
                                                                                         : std::u16string{});
                            encoded.emplace(name, std::string(bytes.begin(), bytes.end()));
                            found = true;
                            break;
                        }
                    if (!found)
                        encoded.emplace(name, value);
                }
                if (!host_values.empty() && !parsed_host)
                    encoded.emplace_front("Host", fallback_host);
                if (origin_authorization) {
                    encoded.erase("Authorization");
                    encoded.emplace("Authorization", *origin_authorization);
                }
                if (cookie) {
                    auto bytes = config_http_encode_request_header(*cookie);
                    if (!bytes.empty())
                        encoded.emplace("Cookie", std::string(bytes.begin(), bytes.end()));
                }
                if (authorization) {
                    encoded.erase("Proxy-Authorization");
                    encoded.emplace("Proxy-Authorization", *authorization);
                }
                headers = std::move(encoded);
                if (validate_content)
                    validate_content();
            };
            std::shared_ptr<ConfigHttpNativePool::Lease> lease;
            httplib::ClientImpl::StreamHandle response;
            request.opensim_stream_header_interrupted = operation.request_interrupted;
            std::optional<std::u16string> tunnel_agent;
            if (route && address.tls)
                for (const auto& [name, value] : operation.headers)
                    if (httplib::detail::case_ignore::equal(name, "User-Agent")) {
                        auto text = value ? *value : std::u16string{};
                        if (!tunnel_agent)
                            tunnel_agent = text;
                        else
                            *tunnel_agent += u" " + text;
                    }
            // Forwarding pools are shared across origins. Tunnels are scoped to their
            // destination and proxy URI within this handler.
            auto pool_proxy_uri = route ? route->proxy_uri : ConfigUriObjectPtr{};
            if (pool_proxy_uri)
                if (auto components = pool_proxy_uri->http_components())
                    pool_proxy_uri = components->source_object();
            const bool http_proxy_pool = route && (proxy_scheme == u"http" || tls_proxy);
            const bool pooled_proxy = (*uri->scheme() == u"http" || *uri->scheme() == u"https") &&
                                      pool_proxy_uri && config_generic_uri_owns(pool_proxy_uri) &&
                                      (socks || http_proxy_pool);
            if (handler_->pool && (!route || pooled_proxy)) {
                std::string key = (address.tls ? "https:" : "http:") + address.host;
                auto port = address.port;
                if (pooled_proxy) {
                    const auto identity = tls_key_bytes(*config_generic_uri_http_pool_identity(pool_proxy_uri));
                    if (address.tls || socks) {
                        key = "tunnel:";
                        if (socks)
                            tls_key_field(key, address.tls ? "https" : "http");
                        tls_key_field(key, address.host);
                        tls_key_field(key, std::to_string(address.port));
                        tls_key_field(key, identity);
                    } else
                        key = "proxy:" + identity;
                    port = 0;
                }
                if (address.tls) {
                    if (!pooled_proxy) {
                        key = "https:";
                        tls_key_field(key, address.host);
                    }
                    tls_key_field(key, tls_key_bytes(tls_host));
                }
                lease = handler_->pool->acquire(std::move(key), port, deadline, operation.request_interrupted,
                                                tunnel_agent);
                if (!lease) {
                    auto error = request_error(
                        operation.request_interrupted && operation.request_interrupted() ? "queue-cancel"
                                                                                         : "timeout",
                        0);
                    if (!error)
                        throw std::invalid_argument("HTTP pool timeout provider returned no exception");
                    std::rethrow_exception(error);
                }
            }
            auto remaining =
                std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now())
                    .count();
            if (remaining <= 0) {
                auto error = request_error("timeout", 0);
                if (!error)
                    throw std::invalid_argument("XML HTTP timeout provider returned no exception");
                std::rethrow_exception(error);
            }
            request.opensim_stream_header_timeout_msec = static_cast<time_t>(remaining);
            auto host = transport_address.host;
            if (host.find(':') != std::string::npos && host.front() != '[')
                host = '[' + host + ']';
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            std::optional<httplib::Client> proxy_client;
#endif
            httplib::Client client(std::string(transport_address.tls ? "https://" : "http://") + host + ":" +
                                   std::to_string(transport_address.port));
            client.set_keep_alive(true);
            client.set_decompress(false);
            client.set_follow_location(false);
            client.set_path_encode(false);
            auto connect_timeout = std::chrono::milliseconds(remaining);
            if (handler_->services.connection_pool &&
                handler_->services.connection_pool->connect_timeout.count() >= 0)
                connect_timeout =
                    std::min(connect_timeout, handler_->services.connection_pool->connect_timeout);
            client.set_connection_timeout(connect_timeout);
            client.set_write_timeout(std::chrono::milliseconds(remaining));
            client.set_read_timeout(std::chrono::milliseconds(remaining));
            if (route && !tls_forward_proxy)
                client.set_proxy(route->host, route->port);
            if (socks) {
                auto credentials = route->credentials;
                if (const auto environment = std::dynamic_pointer_cast<ConfigHttpEnvironmentProxyCredentials>(credentials))
                    credentials = environment->get_credential(route->proxy_uri, route->proxy_uri->scheme());
                const auto credential = std::dynamic_pointer_cast<ConfigHttpNetworkCredential>(credentials);
                if (credentials && !credential)
                    unsupported("SOCKS proxy routes require a native network credential adapter");
                client.opensim_set_proxy_tunnel(
                    [proxy_scheme, address, credential, deadline, interrupted = operation.request_interrupted, request_error](
                        httplib::Stream& stream, httplib::Response&, httplib::Error&) {
                        const auto remaining =
                            std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
                        stream.set_max_timeout(static_cast<time_t>(std::max(remaining.count(), decltype(remaining.count()){1})));
                        stream.set_read_interrupt(interrupted);
                        try {
                            open_socks_tunnel(stream, proxy_scheme, address, credential.get());
                        } catch (...) {
                            if (interrupted && interrupted())
                                std::rethrow_exception(request_error("cancel", 0));
                            if (remaining.count() <= 0 || std::chrono::steady_clock::now() >= deadline ||
                                stream.get_error() == httplib::Error::Timeout)
                                std::rethrow_exception(request_error("timeout", 0));
                            throw;
                        }
                        return true;
                    });
            } else if (route && address.tls && route->proxy_uri) {
                if (!tls_proxy && proxy_scheme != u"http")
                    unsupported("HTTPS proxy tunneling requires an HTTP proxy route");
                if (tls_proxy) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
                    const ConfigXmlHttpAddress proxy_address{route->host, {}, route->port, true};
                    auto proxy_host = route->host;
                    if (proxy_host.find(':') != std::string::npos && proxy_host.front() != '[')
                        proxy_host = '[' + proxy_host + ']';
                    proxy_client.emplace("https://" + proxy_host + ":" + std::to_string(route->port));
                    proxy_client->set_connection_timeout(connect_timeout);
                    configure_tls_client(*proxy_client, services, proxy_address, utf16(route->host), request_error);
                    client.opensim_set_tls_proxy(*proxy_client);
#else
                    unsupported("HTTPS proxy tunneling requires the OpenSSL provider");
#endif
                }
                auto authority = host + ":" + std::to_string(address.port);
                std::string connect = "CONNECT " + authority + " HTTP/1.1\r\nHost: " + authority + "\r\n";
                auto proxy_uri = route->proxy_uri;
                const auto proxy_components = proxy_uri->http_components();
                const auto digest_target = proxy_components
                    ? utf8(*proxy_components->absolute_path() + *proxy_components->query())
                    : "/";
                client.opensim_set_proxy_tunnel([connect = std::move(connect), lease, tunnel_agent,
                                                 reader = request.opensim_response_head_reader, proxy_uri,
                                                 &route, &proxy_authorization, &proxy_digest, proxy_auth_retries, digest_target,
#ifdef OPENSIM_HTTP_INTEGRATED_PROXY
                                                 &integrated_proxy,
#endif
                                                 interrupted = operation.request_interrupted, request_error,
                                                 &services, remaining](httplib::Stream& stream,
                                                                       httplib::Response& response,
                                                                       httplib::Error& error) {
                    auto raise = [](std::exception_ptr error) {
                        if (!error)
                            throw std::invalid_argument("Proxy tunnel error provider returned no exception");
                        std::rethrow_exception(error);
                    };
                    auto agent = lease ? lease->tunnel_user_agent() : tunnel_agent;
                    std::string wire_headers = connect;
                    if (agent) {
                        auto bytes = config_http_encode_request_header(*agent);
                        wire_headers += "User-Agent: " + std::string(bytes.begin(), bytes.end()) + "\r\n";
                    }
                    stream.set_max_timeout(static_cast<time_t>(remaining));
                    stream.set_read_interrupt(interrupted);
                    const auto write = [&](const std::optional<std::string>& authorization) {
                        auto wire = wire_headers;
                        if (authorization)
                            wire += "Proxy-Authorization: " + *authorization + "\r\n";
                        wire += "\r\n";
                        std::size_t written = 0;
                        while (written < wire.size()) {
                            auto n = stream.write(wire.data() + written, wire.size() - written);
                            if (n <= 0) {
                                if (auto code = stream.opensim_write_socket_error(); code && services.write_error)
                                    raise(services.write_error(code));
                                error = httplib::Error::Write;
                                return false;
                            }
                            written += static_cast<std::size_t>(n);
                        }
                        return true;
                    };
                    auto read = [&](char* buffer, std::size_t size) -> ssize_t {
                        // Stop at line boundaries without borrowing bytes from the upcoming
                        // TLS stream. SocketStream caching is disabled by the CONNECT hook.
                        std::size_t count = 0;
                        while (count < size) {
                            auto n = stream.read(buffer + count, 1);
                            if (n <= 0) {
                                if (n < 0) {
                                    if (interrupted && interrupted())
                                        raise(request_error("cancel", 0));
                                    if (stream.get_error() == httplib::Error::Timeout)
                                        raise(request_error("timeout", 0));
                                    if (auto code = stream.opensim_read_socket_error();
                                        code && services.read_error)
                                        raise(services.read_error(true, code));
                                }
                                return count ? static_cast<ssize_t>(count) : n;
                            }
                            if (buffer[count++] == '\n')
                                break;
                        }
                        return static_cast<ssize_t>(count);
                    };
                    httplib::Response head;
                    auto first_authorization = proxy_authorization;
#ifdef OPENSIM_HTTP_INTEGRATED_PROXY
                    if (integrated_proxy.first_authorization)
                        first_authorization = std::move(integrated_proxy.first_authorization);
                    integrated_proxy.first_authorization.reset();
#endif
                    if (!write(first_authorization))
                        return false;
                    if (!reader(read, head)) {
                        error = httplib::Error::Read;
                        return false;
                    }
#ifdef OPENSIM_HTTP_INTEGRATED_PROXY
                    if (head.status == 407 && !proxy_authorization && route->credentials) {
                        auto challenge = integrated_proxy.context
                            ? integrated_authentication_challenge(head, integrated_proxy.scheme)
                            : begin_integrated_exchange(integrated_proxy, *route, head);
                        const auto& scheme = integrated_proxy.scheme;
                        for (unsigned step = 0; integrated_proxy.context && step < 4 &&
                             challenge && head.status == 407; ++step) {
                            const auto token = integrated_proxy.context->next_token(*challenge);
                            if (!token)
                                break;
                            auto authorization = scheme + " " + *token;
                            if (!first_authorization && step == 0 &&
                                httplib::detail::has_header_token(head.headers, "Connection", "close")) {
                                integrated_proxy.first_authorization = std::move(authorization);
                                integrated_proxy.route = route;
                                throw ReconnectIntegratedProxy{};
                            }
                            if (!drain_proxy_challenge(stream, head))
                                break;
                            head = httplib::Response{};
                            if (!write(authorization))
                                return false;
                            if (!reader(read, head)) {
                                error = httplib::Error::Read;
                                return false;
                            }
                            challenge = integrated_authentication_challenge(head, scheme);
                        }
                        validate_integrated_authentication(integrated_proxy, head);
                    }
#endif
                    if (head.status == 407
#ifdef OPENSIM_HTTP_INTEGRATED_PROXY
                        && !integrated_proxy.context
#endif
                    )
                        if (auto authorization = challenge_authorization(
                                route->credentials, route->proxy_uri, head, "CONNECT", digest_target, {}, {},
                                proxy_authorization, proxy_auth_retries, {}, &proxy_digest))
                            throw RetryProxyAuthentication{std::move(*authorization)};
                    // The pinned runtime's resource includes the trailing double quote.
                    if (head.status != 200)
                        throw ConfigXmlHttpError(
                            "System.Net.Http.HttpRequestException",
                            "The proxy tunnel request to proxy '" + utf8(*proxy_uri->to_string()) +
                                "' failed with status code '" + std::to_string(head.status) + "'.\"",
                            0x80131500);
#ifdef OPENSIM_HTTP_INTEGRATED_PROXY
                    integrated_proxy = {};
#endif
                    response = httplib::Response{};
                    return true;
                });
            }
            if (transport_address.tls)
                configure_tls_client(client, services, transport_address, tls_host, request_error);
            if (lease) {
                lease->adopt(client);
                if (operation.source_synchronous_buffering) {
                    source_head->receive = lease->source_receive_buffer();
                    source_head->pending_deadline = source_head->receive->take_pending();
                    auto prior = request.opensim_stream_header_interrupted;
                    request.opensim_stream_header_interrupted = [prior, source_head] {
                        return (prior && prior()) ||
                               (source_head->pending_deadline &&
                                std::chrono::steady_clock::now() >= *source_head->pending_deadline);
                    };
                }
            }
#ifdef OPENSIM_HTTP_INTEGRATED_PROXY
            bool integrated_forward_attempted = false;
            bool integrated_origin_attempted = false;
            const bool origin_integrated = origin_credentials && !origin_authorization;
            const bool proxy_integrated = forward_proxy && route->credentials && !proxy_authorization;
            const auto authentication_request = proxy_integrated || origin_integrated
                ? std::optional<httplib::Request>(request) : std::nullopt;
#endif
            try {
                try {
                    response = client.open_stream(request);
                } catch (const ConfigHttpHeaderError& e) {
                    if (e.retry_on_connection_failure && connection_retries < 3) {
                        ++connection_retries;
                        continue;
                    }
                    throw;
                }
                connection_retries = 0;
#ifdef OPENSIM_HTTP_INTEGRATED_PROXY
                std::optional<std::string> connection_proxy_authorization;
                const auto authenticate = [&](auto&& send_exchange, bool authenticate_proxy,
                                              const httplib::Request& exchange_request) -> void {
                    const int challenge_status = authenticate_proxy ? 407 : 401;
                    const auto fail_authentication_io = [&](const char* phase, int code) {
                        auto failure = request_error(phase, code);
                        if (!failure)
                            throw std::invalid_argument("HTTP authentication error provider returned no exception");
                        std::rethrow_exception(failure);
                    };
                    ProxyIntegratedExchange exchange;
                    const auto credentials_route = authenticate_proxy ? *route : ConfigXmlHttpRoute{
                        address.host, address.port, origin_credentials, {}};
                    auto challenge = begin_integrated_exchange(
                        exchange, credentials_route, *response.response, authenticate_proxy,
                        authenticate_proxy ? nullptr : uri);
                    if (!exchange.context)
                        return;
                    if (authenticate_proxy)
                        integrated_forward_attempted = true;
                    else
                        integrated_origin_attempted = true;
                    for (unsigned step = 0; challenge && step < 4; ++step) {
                        const bool reconnect =
                            httplib::detail::has_header_token(response.response->headers, "Connection", "close");
                        if (reconnect && step != 0)
                            break;
                        const bool tls_origin = !authenticate_proxy && address.tls;
                        const bool defer_token = tls_origin && reconnect;
                        std::string authorization;
                        if (!defer_token) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
                            if (tls_origin)
                                exchange.context->channel_binding(origin_channel_binding(response.opensim_tls_session()));
#endif
                            const auto token = exchange.context->next_token(*challenge);
                            if (!token)
                                break;
                            authorization = exchange.scheme + " " + *token;
                        }
                        if (!authenticate_proxy && handler_->jar)
                            for (const auto& header : source_head->head.headers)
                                if (httplib::detail::case_ignore::equal(header.name, "Set-Cookie"))
                                    handler_->jar->set(uri, std::make_shared<const std::u16string>(header.value));
                        const auto time_left =
                            std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
                        if (time_left.count() <= 0)
                            fail_authentication_io("timeout", 0);
                        if (lease)
                            lease->connected(response);
                        if (reconnect && !defer_token) {
                            response = {};
                        } else if (!reconnect) {
                            response.set_read_timeouts(time_left.count() / 1000, (time_left.count() % 1000) * 1000,
                                                       time_left.count());
                            const bool head_only = httplib::detail::case_ignore::equal(operation.method, "HEAD");
                            if (!drain_authentication_challenge(response, *source_head, head_only) ||
                                !response.opensim_idle_connection_usable() ||
                                !client.opensim_adopt_stream(std::move(response))) {
                                if (operation.request_interrupted && operation.request_interrupted())
                                    fail_authentication_io("cancel", 0);
                                if (std::chrono::steady_clock::now() >= deadline)
                                    fail_authentication_io("timeout", 0);
                                fail_authentication_io("headers", static_cast<int>(httplib::Error::Read));
                            }
                        }
                        auto retry = exchange_request;
                        retry.opensim_require_reused_connection = !reconnect;
                        retry.opensim_stream_header_timeout_msec = time_left.count();
                        if (authenticate_proxy)
                            connection_proxy_authorization = authorization;
                        else if (reconnect)
                            connection_proxy_authorization.reset();
                        retry.opensim_prepare_request_headers =
                            [prepare = retry.opensim_prepare_request_headers, value = std::move(authorization),
                             authenticate_proxy, connection_proxy_authorization](httplib::Headers& headers) {
                                prepare(headers);
                                if (!authenticate_proxy && connection_proxy_authorization) {
                                    headers.erase("Proxy-Authorization");
                                    headers.emplace("Proxy-Authorization", *connection_proxy_authorization);
                                }
                                const auto* name = authenticate_proxy ? "Proxy-Authorization" : "Authorization";
                                headers.erase(name);
                                if (!value.empty())
                                    headers.emplace(name, value);
                            };
                        bool token_rejected = false;
                        std::optional<HeadBuffer> original_head;
                        if (defer_token) {
                            original_head = *source_head;
                            retry.opensim_prepare_connected_headers = [&](httplib::Headers& headers, void* session) {
                                exchange.context->channel_binding(origin_channel_binding(session));
                                const auto token = exchange.context->next_token(*challenge);
                                if (!token) {
                                    token_rejected = true;
                                    return false;
                                }
                                headers.emplace("Authorization", exchange.scheme + " " + *token);
                                return true;
                            };
                        }
                        source_head->head = {};
                        source_head->unread.clear();
                        source_head->pending_deadline.reset();
                        auto retried_response = client.open_stream(retry);
                        if (token_rejected) {
                            *source_head = std::move(*original_head);
                            break;
                        }
                        response = std::move(retried_response);
                        // The origin token belongs to this request while the replacement
                        // connection negotiates a fresh proxy security context.
                        if (!authenticate_proxy && proxy_integrated && response.is_valid() &&
                            response.response->status == 407)
                            send_exchange(send_exchange, true, retry);
                        if (!response.is_valid() || response.response->status != challenge_status)
                            break;
                        challenge = integrated_authentication_challenge(*response.response, exchange.scheme, authenticate_proxy);
                    }
                    if (response.is_valid())
                        validate_integrated_authentication(exchange, *response.response, authenticate_proxy);
                };
                if (authentication_request && response.is_valid()) {
                    if (proxy_integrated && response.response->status == 407)
                        authenticate(authenticate, true, *authentication_request);
                    if (origin_integrated && response.is_valid() && response.response->status == 401 &&
                        (!forward_proxy || httplib::detail::has_header_token(
                            response.response->headers, "Proxy-Support", "Session-Based-Authentication")))
                        authenticate(authenticate, false, *authentication_request);
                }
#endif
                response.set_read_timeouts(-1, 0, 0);
                if (!response.is_valid()) {
                    if (operation.request_interrupted && operation.request_interrupted()) {
                        auto error = request_error("cancel", 0);
                        if (!error)
                            throw std::invalid_argument("HTTP cancellation provider returned no exception");
                        std::rethrow_exception(error);
                    }
                    if (response.opensim_header_wait_timed_out) {
                        auto error = request_error("timeout", 0);
                        if (!error)
                            throw std::invalid_argument("HTTP timeout provider returned no exception");
                        std::rethrow_exception(error);
                    }
                    if (response.opensim_native_write_error && services.write_error) {
                        auto error = services.write_error(response.opensim_native_write_error);
                        if (!error)
                            throw std::invalid_argument(
                                "XML HTTP write error provider returned no exception");
                        std::rethrow_exception(error);
                    }
                    if (response.opensim_native_read_error && services.read_error) {
                        auto error = services.read_error(true, response.opensim_native_read_error);
                        if (!error)
                            throw std::invalid_argument("XML HTTP read error provider returned no exception");
                        std::rethrow_exception(error);
                    }
                    if (response.opensim_native_dns_error && services.dns_error) {
                        auto error = services.dns_error(route ? route->host : address.host,
                                                        route ? route->port : address.port,
                                                        response.opensim_native_dns_error);
                        if (!error)
                            throw std::invalid_argument("XML HTTP DNS error provider returned no exception");
                        std::rethrow_exception(error);
                    }
                    if (response.opensim_native_connection_error && services.connect_error) {
                        auto error = services.connect_error(route ? route->host : address.host,
                                                            route ? route->port : address.port,
                                                            response.opensim_native_connection_error);
                        if (!error)
                            throw std::invalid_argument(
                                "XML HTTP connect error provider returned no exception");
                        std::rethrow_exception(error);
                    }
                    auto tls_failure =
                        transport_address.tls && (response.error == httplib::Error::SSLConnection ||
                                        response.error == httplib::Error::SSLServerVerification ||
                                        response.error == httplib::Error::SSLServerHostnameVerification ||
                                        response.error == httplib::Error::SSLLoadingCerts);
                    auto error =
                        request_error(tls_failure ? "tls" : "headers", static_cast<int>(response.error));
                    if (!error)
                        throw std::invalid_argument("XML HTTP error provider returned no exception");
                    std::rethrow_exception(error);
                }
#ifdef OPENSIM_HTTP_INTEGRATED_PROXY
            } catch (const ReconnectIntegratedProxy&) {
                continue;
#endif
            } catch (const RetryProxyAuthentication& retry) {
                proxy_authorization = retry.authorization;
                authorization_route = route;
                ++proxy_auth_retries;
                continue;
            } catch (...) {
#ifdef OPENSIM_HTTP_INTEGRATED_PROXY
                integrated_proxy = {};
#endif
                auto error = std::current_exception();
                if (proxy_plan && config_xml_http_proxy_retryable(error) && proxy_plan->advance()) {
                    connection_retries = 0;
                    proxy_authorization.reset();
                    proxy_auth_retries = 0;
                    proxy_digest.reset();
                    continue;
                }
                throw;
            }
#ifdef OPENSIM_HTTP_INTEGRATED_PROXY
            integrated_proxy = {};
#endif
            if (lease)
                lease->connected(response);
            auto status = response.response->status;
            std::vector<NiniWriterText> locations;
            for (auto& header : source_head->head.headers)
                if (httplib::detail::case_ignore::equal(header.name, "Location"))
                    locations.push_back(std::make_shared<const std::u16string>(header.value));
            auto create_body = [&]() {
                auto body_services = services;
                if (operation.error)
                    body_services.error = operation.error;
                const bool raw_tunnel = application_connect && status == 200;
                bool reusable =
                    (!lease || lease->response_headers(source_head->head)) && status != 101 && !raw_tunnel &&
                    !httplib::detail::has_header_token(request.headers, "Connection", "close") &&
                    !httplib::detail::has_header_token(response.response->headers, "Connection", "close") &&
                    (source_head->head.minor_version != 0 ||
                     httplib::detail::has_header_token(response.response->headers, "Connection",
                                                       "keep-alive"));
#ifdef OPENSIM_HTTP_INTEGRATED_PROXY
                if ((integrated_forward_attempted && (status == 407 || !handler_->proxy_provider_)) ||
                    (integrated_origin_attempted && status == 401))
                    reusable = false;
#endif
                return std::make_shared<Body>(std::move(response), std::move(body_services), source_head,
                                              handler_->proxy_provider_,
                                              httplib::detail::case_ignore::equal(operation.method, "HEAD"),
                                              raw_tunnel, std::move(lease), reusable, operation.body_interrupted);
            };
            if (forward_proxy && status == 407
#ifdef OPENSIM_HTTP_INTEGRATED_PROXY
                && !integrated_forward_attempted
#endif
            ) {
                if (auto authorization = challenge_authorization(
                        route->credentials, route->proxy_uri, *response.response, operation.method, address.target, operation.body,
                        request.get_header_value("Content-Type"),
                        proxy_authorization, proxy_auth_retries, {}, &proxy_digest)) {
                    proxy_authorization = std::move(authorization);
                    authorization_route = route;
                    ++proxy_auth_retries;
                    create_body()->close();
                    continue;
                }
            }
            if (handler_->jar)
                for (auto& header : source_head->head.headers)
                    if (httplib::detail::case_ignore::equal(header.name, "Set-Cookie"))
                        handler_->jar->set(uri, std::make_shared<const std::u16string>(header.value));
            if (status == 401
#ifdef OPENSIM_HTTP_INTEGRATED_PROXY
                && !integrated_origin_attempted
#endif
            ) {
                if (auto authorization = challenge_authorization(
                        origin_credentials, {}, *response.response, operation.method, address.target,
                        operation.body, request.get_header_value("Content-Type"), origin_authorization,
                        origin_auth_retries, uri, &origin_digest)) {
                    origin_authorization = std::move(authorization);
                    ++origin_auth_retries;
                    create_body()->close();
                    continue;
                }
            }
            // RedirectHandler computes the typed target before incrementing/checking its
            // configured limit. Its final Location can still throw or be rejected.
            auto next = operation.allow_auto_redirect ? services.redirect(uri, status, locations)
                                                      : std::shared_ptr<ConfigXmlUri>{};
            if (next && ++redirects <= static_cast<unsigned>(operation.redirect_limit)) {
                create_body()->close();
                std::erase_if(operation.headers, [](const auto& header) {
                    return httplib::detail::case_ignore::equal(header.first, "Authorization");
                });
                const auto method_is = [&](const char* method) {
                    return httplib::detail::case_ignore::equal(operation.method, method);
                };
                if (((status == 300 || status == 301 || status == 302) && method_is("POST")) ||
                    (status == 303 && !method_is("GET") && !method_is("HEAD"))) {
                    operation.method = "GET";
                    operation.body.clear();
                    std::erase_if(operation.headers, [](const auto& h) {
                        return httplib::detail::case_ignore::equal(h.first, "Content-Length") ||
                               httplib::detail::case_ignore::equal(h.first, "Content-Type");
                    });
                }
                uri = std::move(next);
                proxy_plan.reset();
                origin_authorization.reset();
                origin_auth_retries = 0;
                origin_digest.reset();
                const auto scoped = std::dynamic_pointer_cast<ConfigHttpOriginCredentials>(origin_credentials);
                if (!scoped || !scoped->allows_redirect_lookup())
                    origin_credentials.reset();
                proxy_authorization.reset();
                proxy_auth_retries = 0;
                proxy_digest.reset();
                continue;
            }
            if (operation.response_headers)
                operation.response_headers(source_head->head);
            if (operation.ensure_success && (status < 200 || status >= 300)) {
                auto reason = source_head->head.reason;
                std::string message =
                    "Response status code does not indicate success: " + std::to_string(status);
                bool blank = true;
                for (auto c : reason)
                    if (!unicode_whitespace(c))
                        blank = false;
                if (!blank)
                    message += " (" + utf8(reason) + ")";
                message += '.';
                create_body()->close();
                throw ConfigXmlHttpError("System.Net.Http.HttpRequestException", std::move(message),
                                         0x80131500u, status);
            }
            return create_body();
        }
    }
    void close() override {
        handler_.reset();
    }
};
}
ConfigXmlDownloadBackend config_xml_native_http_backend(ConfigXmlNativeHttpServices services) {
    if (!services.address || !services.redirect || !services.cookies ||
        (!services.proxy && !services.proxy_plan && !services.proxy_provider) || !services.error)
        throw std::invalid_argument(
            "Native XML HTTP requires URI, redirect, cookies, proxy and typed-error providers");
    ConfigXmlDownloadBackend b;
    b.new_handler = [services]() { return std::make_shared<Handler>(services); };
    b.new_client = [](std::shared_ptr<ConfigXmlHttpHandler> h) {
        auto value = std::dynamic_pointer_cast<Handler>(h);
        if (!value)
            throw std::invalid_argument("Native XML HTTP requires matching handler");
        return std::make_shared<Client>(std::move(value));
    };
    return b;
}
ConfigXmlHttpRequester config_xml_native_http_requester(ConfigXmlNativeHttpServices services,
                                                        bool use_cookies) {
    if (!services.address || !services.redirect || (use_cookies && !services.cookies) ||
        (!services.proxy && !services.proxy_plan && !services.proxy_provider) || !services.error)
        throw std::invalid_argument(
            "Native HTTP requester requires URI, redirect, proxy and typed-error policies");
    auto handler = std::make_shared<Handler>(std::move(services), use_cookies);
    auto client = std::make_shared<Client>(std::move(handler));
    return [client](std::shared_ptr<ConfigXmlUri> uri, const ConfigXmlHttpOperation& operation) {
        if (operation.timeout_milliseconds <= 0 || operation.redirect_limit < 0)
            throw std::invalid_argument(
                "Native HTTP operation requires positive timeout and non-negative redirect limit");
        return client->request_stream(std::move(uri), operation);
    };
}

}
