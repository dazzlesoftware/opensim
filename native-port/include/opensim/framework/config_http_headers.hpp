// Consumed .NET8 HttpConnection HTTP/1 header parsing and default encoding.
#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
namespace opensim::framework {
std::vector<std::u16string> config_http_disable_chunked_request_encoding(
    const std::vector<std::u16string>& values);
class ConfigHttpHeaderError : public std::runtime_error {
public:
    ConfigHttpHeaderError(std::string, std::u16string, std::uint32_t, int = -1,
                          std::shared_ptr<const ConfigHttpHeaderError> = {});
    std::string source_type;
    std::u16string message;
    std::uint32_t hresult;
    int request_error;
    std::shared_ptr<const ConfigHttpHeaderError> inner;
    bool retry_on_connection_failure = false;
};
// Explicit Encoding callbacks. The caller owns selector invocation and known-value
// HeaderDescriptor fast paths; this is not a complete HeaderEncodingSelector API.
// Null means original handler defaults:
// strict ASCII on request; ISO-8859-1 (one byte per UTF16 code unit) on response.
using ConfigHttpHeaderEncoder = std::function<std::vector<std::uint8_t>(std::u16string_view)>;
using ConfigHttpHeaderDecoder = std::function<std::u16string(std::span<const std::uint8_t>)>;
std::vector<std::uint8_t> config_http_encode_request_header(std::u16string_view,
                                                            ConfigHttpHeaderEncoder = {});
std::u16string config_http_decode_response_header(std::span<const std::uint8_t>,
                                                  ConfigHttpHeaderDecoder = {});
// HeaderDescriptor applies strict UTF8 to Location before selector/Latin1 fallback.
std::u16string config_http_decode_named_response_header(std::string_view, std::span<const std::uint8_t>,
                                                        ConfigHttpHeaderDecoder = {});
// Consumed HttpHeaders typed getters; raw duplicate order is significant.
std::optional<std::int64_t> config_http_content_length(const std::vector<std::u16string>&);
bool config_http_transfer_encoding_chunked(const std::vector<std::u16string>&);
// HttpRequestHeaders.Host typed getter over raw header values, followed by
// HttpConnectionPoolManager.ParseHostNameFromHeader (the raw TLS pool key).
std::optional<std::u16string> config_http_request_host(const std::vector<std::u16string>&);
std::u16string config_http_tls_host_key(std::u16string_view);
// SslStream TargetHostNameHelper with the selected default ICU IdnMapping.
std::u16string config_http_normalize_tls_host(std::optional<std::u16string_view>);
// Default ICU IdnMapping.GetAscii without TargetHostNameHelper's ASCII fallback
// or trailing-dot removal (the Unix certificate verifier's second IDN step).
std::u16string config_http_certificate_idn_ascii(std::u16string_view);
struct ConfigHttpTlsTargetNames {
    std::u16string normalized;
    std::string certificate_host, sni_host;
    bool ignore_sni_error = false;
};
ConfigHttpTlsTargetNames config_http_tls_target_names(std::u16string_view);
#ifdef _WIN32
// Read-only Crypt32 SSL name policy. Chain trust/revocation remain separate.
bool config_http_windows_certificate_name(std::span<const std::uint8_t>, std::u16string_view);
#endif
struct ConfigHttpKeepAliveState {
    bool close = false;
    std::int32_t timeout_seconds = 0;
};
// HttpConnection.ProcessKeepAliveHeader retains its timeout across responses.
void config_http_apply_keep_alive(ConfigHttpKeepAliveState&, std::u16string_view);
[[noreturn]] void config_http_response_headers_ended(bool can_retry = false);
struct ConfigHttpRawHeader {
    std::string name;
    std::u16string value;
};
struct ConfigHttpResponseHead {
    int status = 0, minor_version = 1;
    std::u16string reason;
    std::vector<ConfigHttpRawHeader> headers;
    std::size_t bytes_consumed = 0;
};
// Header framing only: retains ordered duplicate raw values. The caller owns
// HttpHeaders typed semantics, informational responses and entity framing.
// nullopt requests more bytes. Input is copied because source unfolds in place.
std::optional<ConfigHttpResponseHead>
config_http_parse_response_head(std::span<const std::uint8_t>, std::size_t maximum_bytes = 65536,
                                std::optional<std::size_t> configured_maximum_bytes = {});
}
