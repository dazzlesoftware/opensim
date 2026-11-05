// Native XML HTTP boundary: preserves raw UTF-16 cookie/header values and typed redirects.
#pragma once
#include "opensim/framework/config_xml_transport.hpp"
#include <utility>
#include <chrono>
namespace opensim::framework {
struct ConfigHttpResponseHead;
struct ConfigXmlHttpPoolSettings {
    std::int32_t max_connections_per_server = 32;
    std::chrono::milliseconds idle_timeout{31000}, lifetime{180000}, connect_timeout{120000};
};
struct ConfigXmlHttpTlsSettings {
    bool verify_chain = true, verify_hostname = true;
    // Consumed SslProtocols.Tls|Tls11|Tls12|Tls13 from WebUtil.SetupHTTPClients.
    std::int32_t enabled_protocols = 16320;
};
struct ConfigXmlHttpAddress {
    std::string host, target;
    std::uint16_t port = 80;
    bool tls = false;
};
class ConfigUriObject;
struct ConfigXmlHttpRoute {
    std::string host;
    std::uint16_t port = 0;
    std::shared_ptr<ConfigXmlCredentials> credentials;
    std::shared_ptr<const ConfigUriObject> proxy_uri;
    bool default_credentials_used = false;
};
class ConfigXmlHttpProxyRoutePlan {
  public:
    virtual ~ConfigXmlHttpProxyRoutePlan() = default;
    virtual std::optional<ConfigXmlHttpRoute> current() = 0;
    // Advance only after a retryable pre-response failure. false exhausts the plan.
    virtual bool advance() = 0;
};
class ConfigXmlHttpProxyProvider {
  public:
    virtual ~ConfigXmlHttpProxyProvider() = default;
    virtual std::shared_ptr<ConfigXmlHttpProxyRoutePlan> routes(const std::shared_ptr<ConfigXmlUri>&) = 0;
    virtual std::shared_ptr<ConfigXmlCredentials> credentials() const = 0;
    virtual bool default_credentials_used() const = 0;
};
class ConfigXmlCookieContainer {
  public:
    virtual ~ConfigXmlCookieContainer() = default;
    virtual NiniWriterText header(const std::shared_ptr<ConfigXmlUri>&) = 0;
    virtual void set(const std::shared_ptr<ConfigXmlUri>&, NiniWriterText) = 0;
};
class ConfigXmlHttpError : public std::runtime_error {
  public:
    ConfigXmlHttpError(std::string type, std::string message, std::uint32_t hresult,
                       std::optional<int> status = {});
    std::string source_type;
    std::u16string message;
    std::uint32_t hresult;
    std::optional<int> status;
};
struct ConfigXmlNativeHttpServices {
    // Retained handler-scoped HTTP/1.1 pool. Negative idle/lifetime durations
    // mean infinite; connect_timeout also accepts -1 for infinite (bounded by the
    // active request deadline). Direct HTTP and HTTPS retain distinct pool keys and owned
    // TLS sessions. Unauthenticated plain HTTP proxy routes share connections
    // across destinations using the source proxy URI identity; HTTPS CONNECT
    // tunnels retain separate pools for each destination authority and proxy.
    std::optional<ConfigXmlHttpPoolSettings> connection_pool;
    std::optional<ConfigXmlHttpTlsSettings> tls_settings;
    // Required System.Uri request canonicalization and full typed RedirectHandler operation.
    std::function<ConfigXmlHttpAddress(const std::shared_ptr<ConfigXmlUri>&)> address;
    std::function<std::shared_ptr<ConfigXmlUri>(const std::shared_ptr<ConfigXmlUri>&, std::int32_t,
                                                const std::vector<NiniWriterText>&)>
        redirect;
    // Fresh source CookieContainer per handler; no process-global cookie jar.
    std::function<std::shared_ptr<ConfigXmlCookieContainer>()> cookies;
    // Optional HttpClient.DefaultProxy getter, captured once when a handler first
    // sends a supported request without an explicit proxy.
    std::function<std::shared_ptr<ConfigXmlProxy>()> default_proxy;
    // Required environment/system/explicit IWebProxy selection. nullopt is direct.
    std::function<std::optional<ConfigXmlHttpRoute>(const std::shared_ptr<ConfigXmlUri>&,
                                                    const std::shared_ptr<ConfigXmlProxy>&)>
        proxy;
    // Optional fresh route plan per redirect target. When supplied, this owns
    // selection instead of proxy; connection retries keep the current candidate.
    std::function<std::shared_ptr<ConfigXmlHttpProxyRoutePlan>(const std::shared_ptr<ConfigXmlUri>&,
                                                               const std::shared_ptr<ConfigXmlProxy>&)>
        proxy_plan;
    // Optional handler-scoped provider, initialized with default/explicit proxy at
    // first supported send. It supersedes proxy_plan and proxy when supplied.
    std::function<std::shared_ptr<ConfigXmlHttpProxyProvider>(const std::shared_ptr<ConfigXmlProxy>&,
                                                              bool explicit_proxy)>
        proxy_provider;
    // Non-null source ICredentials requires a complete adapter; this implementation
    // explicitly rejects credentials until that adapter is supplied by future work.
    // Required native failure translation: BCL exception/inner/socket details differ
    // by OS and failure phase; no generic message is substituted.
    std::function<std::exception_ptr(std::string phase, int native_error)> error;
    // Actual native socket code and physical endpoint for connect failures.
    std::function<std::exception_ptr(std::string host, int port, int native_error)> connect_error;
    std::function<std::exception_ptr(std::string host, int port, int resolver_error)> dns_error;
    std::function<std::exception_ptr(bool reading_headers, int native_error)> read_error;
    std::function<std::exception_ptr(int native_error)> write_error;
    // An explicit CA-file trust source. When absent on Windows, the native
    // Crypt32 current-user/system chain engine implements the consumed default
    // server-auth/NoCheck policy over the actual peer chain. Other platforms still
    // require their trust provider. This is not a claim that OpenSSL
    // default roots/verification reproduce each platform's SocketsHttpHandler trust.
    std::function<std::string(const ConfigXmlHttpAddress&)> tls_ca_file;
};
struct ConfigXmlHttpOperation {
    std::string method = "GET", body;
    std::vector<std::pair<std::string, NiniWriterText>> headers;
    std::int32_t timeout_milliseconds = 100000, redirect_limit = 50;
    // Opt-in response/cancellation surface for HttpClient.ResponseHeadersRead.
    // DownloadManager and Nwc retain their original defaults.
    bool ensure_success = true, allow_auto_redirect = true;
    bool source_synchronous_buffering = false;
    std::function<void(const ConfigHttpResponseHead&)> response_headers;
    std::function<bool()> body_interrupted;
    // Request cancellation ends with ResponseHeadersRead; body ownership has its
    // separate interruption callback above.
    std::function<bool()> request_interrupted;
    std::function<std::exception_ptr(std::string, int)> error;
};
using ConfigXmlHttpRequester = std::function<std::shared_ptr<ConfigXmlStream>(std::shared_ptr<ConfigXmlUri>,
                                                                              const ConfigXmlHttpOperation&)>;
// Request-scoped method/content/timeouts over a retained handler policy. Cookie
// creation and processing can be disabled for source WebUtil global handlers.
ConfigXmlHttpRequester config_xml_native_http_requester(ConfigXmlNativeHttpServices, bool use_cookies);
// Concrete socket/framing backend. Does not provide System.Uri/CookieContainer/
// proxy/certificate/error policy implementations; all are required above.
ConfigXmlDownloadBackend config_xml_native_http_backend(ConfigXmlNativeHttpServices);
}
