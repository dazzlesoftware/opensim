// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/web_http_clients.hpp"
#include "opensim/framework/config_xml_native_proxy.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/config_xml_typed_http_services.hpp"
#include "opensim/framework/config_http_cookies_native.hpp"
#include "opensim/framework/config_http_connect_error.hpp"
#include "opensim/framework/config_http_dns_error.hpp"
#include "opensim/framework/config_http_read_error.hpp"
#include "opensim/framework/config_http_write_error.hpp"
#include "opensim/framework/text.hpp"
#include <mutex>
namespace opensim::network {
namespace {
#ifdef _WIN32
constexpr const char *newline = "\r\n";
#else
constexpr const char *newline = "\n";
#endif
[[noreturn]] void error(std::string type, std::string message, std::uint32_t code) {
    throw framework::ConfigXmlHttpError(std::move(type), std::move(message), code);
}
void require_positive(std::int32_t value) {
    if (value > 0)
        return;
    auto text = std::to_string(value);
    std::string message =
        "value ('" + text + "') must be a non-negative and non-zero value. (Parameter 'value')";
    message += newline;
    error("System.ArgumentOutOfRangeException", message + "Actual value was " + text + ".", 0x80131502);
}
}
bool WebHttpHandlerSettings::accepts_certificate_errors(std::int32_t errors) const noexcept {
    auto flags = static_cast<std::uint32_t>(errors);
    if (no_verify_cert_chain)
        flags &= ~4u;
    if (no_verify_cert_hostname)
        flags &= ~2u;
    return flags == 0;
}
WebHttpHandler::WebHttpHandler(WebHttpHandlerSettings settings, WebHttpSender sender,
                               std::function<std::exception_ptr(std::string, int)> errors)
    : settings_(std::move(settings)), sender_(std::move(sender)), error_(std::move(errors)) {
    if (!sender_)
        throw std::invalid_argument("Shared HTTP handler requires its real transport sender");
}
std::shared_ptr<framework::ConfigXmlStream>
WebHttpHandler::send_headers(framework::ConfigUriObjectPtr uri, framework::ConfigXmlHttpOperation operation,
                             bool ensure_success) const {
    operation.allow_auto_redirect = settings_.allow_auto_redirect;
    operation.redirect_limit = settings_.max_automatic_redirections;
    // HttpClient.Send(ResponseHeadersRead) returns non-success responses; callers
    // such as ServiceOSDRequest invoke EnsureSuccessStatusCode themselves.
    operation.ensure_success = ensure_success;
    return sender_(std::move(uri), std::move(operation));
}
WebHttpClient::WebHttpClient(std::shared_ptr<WebHttpHandler> handler, std::int32_t timeout,
                             std::int32_t maximum)
    : handler_(std::move(handler)), timeout_(timeout > 0 ? timeout : 30000), maximum_buffer_(maximum) {
    if (!handler_)
        error("System.ArgumentNullException", "Value cannot be null. (Parameter 'handler')", 0x80004003);
    require_positive(maximum);
}
std::shared_ptr<framework::ConfigXmlStream>
WebHttpClient::send_headers(framework::ConfigUriObjectPtr uri, framework::ConfigXmlHttpOperation operation,
                            bool ensure_success) const {
    if (disposed_->load())
        error("System.ObjectDisposedException",
              std::string("Cannot access a disposed object.") + newline +
                  "Object name: 'System.Net.Http.HttpClient'.",
              0x80131622);
    if (!uri || !uri->is_absolute())
        error(
            "System.InvalidOperationException",
            "An invalid request URI was provided. Either the request URI must be an absolute URI or BaseAddress must be set.",
            0x80131509);
    auto scheme = *uri->scheme();
    if (scheme != u"http" && scheme != u"https")
        error("System.NotSupportedException",
              "The '" + framework::utf8(scheme) + "' scheme is not supported.", 0x80131515);
    operation.timeout_milliseconds = timeout_;
    auto method = operation.method;
    for (auto &c : method)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 32);
    bool has_length = false;
    for (auto &header : operation.headers) {
        auto name = header.first;
        for (auto &c : name)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c + 32);
        has_length |= name == "content-length";
    }
    if (operation.body.empty() && !has_length && method != "GET" && method != "HEAD" && method != "DELETE" &&
        method != "OPTIONS" && method != "TRACE" && method != "CONNECT")
        operation.headers.emplace_back("Content-Length", std::make_shared<const std::u16string>(u"0"));
    auto cancel = disposed_;
    auto prior = operation.request_interrupted;
    operation.request_interrupted = [cancel, prior] { return cancel->load() || (prior && prior()); };
    auto stop = operation.request_interrupted;
    auto fallback = operation.error ? operation.error : handler_->error_factory();
    auto start = std::chrono::steady_clock::now();
    auto timeout = timeout_;
    operation.error = [stop, fallback, start, timeout](std::string phase, int code) -> std::exception_ptr {
        if (phase != "body" && stop())
            return std::make_exception_ptr(framework::ConfigXmlHttpError(
                "System.Threading.Tasks.TaskCanceledException",
                phase == "queue-cancel" ? "A task was canceled." : "The operation was canceled.",
                0x8013153b));
        if (phase == "timeout" || (phase == "headers" && std::chrono::steady_clock::now() - start >=
                                                             std::chrono::milliseconds(timeout))) {
            auto seconds = std::to_string(timeout / 1000);
            auto fraction = timeout % 1000;
            if (fraction) {
                auto digits = std::to_string(fraction + 1000).substr(1);
                while (digits.back() == '0')
                    digits.pop_back();
                seconds += '.' + digits;
            }
            return std::make_exception_ptr(framework::ConfigXmlHttpError(
                "System.Threading.Tasks.TaskCanceledException",
                "The request was canceled due to the configured HttpClient.Timeout of " + seconds +
                    " seconds elapsing.",
                0x8013153b));
        }
        if (!fallback)
            throw std::invalid_argument("Shared HTTP requires native failure translation");
        return fallback(std::move(phase), code);
    };
    return handler_->send_headers(std::move(uri), std::move(operation), ensure_success);
}
WebHttpClients::WebHttpClients(HandlerFactory factory) : factory_(std::move(factory)) {
    if (!factory_)
        throw std::invalid_argument("Shared HTTP clients require a handler factory");
}
void WebHttpClients::setup(bool no_chain, bool no_name, std::shared_ptr<framework::ConfigXmlProxy> proxy,
                           std::int32_t maximum) {
    require_positive(maximum);
    WebHttpHandlerSettings settings;
    settings.allow_auto_redirect = false;
    settings.pool.max_connections_per_server = maximum;
    settings.no_verify_cert_chain = no_chain;
    settings.no_verify_cert_hostname = no_name;
    settings.proxy = std::move(proxy);
    settings.use_proxy = bool(settings.proxy);
    auto no_redirect = factory_(settings);
    if (!no_redirect)
        throw std::invalid_argument("Shared HTTP handler factory returned no handler");
    shared_no_redirect_handler.store(std::move(no_redirect));
    settings.allow_auto_redirect = true;
    settings.max_automatic_redirections = 10;
    auto redirect = factory_(settings);
    if (!redirect)
        throw std::invalid_argument("Shared HTTP handler factory returned no handler");
    shared_handler.store(std::move(redirect));
}
std::shared_ptr<WebHttpClient> WebHttpClients::get_new_global_client(std::int32_t timeout,
                                                                     std::int32_t maximum) const {
    return std::make_shared<WebHttpClient>(shared_handler.load(), timeout, maximum);
}
std::shared_ptr<WebHttpClient> WebHttpClients::get_no_redirect_client(std::int32_t timeout,
                                                                      std::int32_t maximum) const {
    return std::make_shared<WebHttpClient>(shared_no_redirect_handler.load(), timeout, maximum);
}
namespace {
struct NativeHandlerState {
    framework::ConfigXmlNativeHttpServices services;
    framework::ConfigXmlUriObjects objects;
    std::once_flag started;
    framework::ConfigXmlHttpRequester sender;
    NativeHandlerState(framework::ConfigXmlNativeHttpServices policy, const WebHttpHandlerSettings &settings)
        : services(std::move(policy)) {
        services.connection_pool = settings.pool;
        services.tls_settings = framework::ConfigXmlHttpTlsSettings{!settings.no_verify_cert_chain,
                                                                    !settings.no_verify_cert_hostname,
                                                                    settings.enabled_ssl_protocols};
        if (settings.use_proxy)
            services.default_proxy = [proxy = settings.proxy] { return proxy; };
        else {
            services.default_proxy = {};
            services.proxy_plan = {};
            services.proxy_provider = {};
            services.proxy = [](const auto &, const auto &) -> std::optional<framework::ConfigXmlHttpRoute> {
                return {};
            };
        }
    }
    std::shared_ptr<framework::ConfigXmlStream> send(framework::ConfigUriObjectPtr uri,
                                                     const framework::ConfigXmlHttpOperation &operation) {
        std::call_once(started, [this] {
            auto uris = framework::native_config_uri_object_backend();
            if (!services.proxy && !services.proxy_plan && !services.proxy_provider)
                services = framework::config_xml_bind_native_proxy(std::move(services), objects);
            auto bound = framework::config_xml_bind_typed_http(services, objects, *uris, [] {
                return framework::native_invariant_config_http_cookies_backend();
            });
            sender = framework::config_xml_native_http_requester(std::move(bound), false);
        });
        return sender(objects.wrap(std::move(uri)), operation);
    }
};
}
WebHttpClients::HandlerFactory
native_web_http_handler_factory(framework::ConfigXmlNativeHttpServices policy) {
    return [policy = std::move(policy)](const WebHttpHandlerSettings &settings) {
        auto state = std::make_shared<NativeHandlerState>(policy, settings);
        return std::make_shared<WebHttpHandler>(
            settings,
            [state](framework::ConfigUriObjectPtr uri, framework::ConfigXmlHttpOperation operation) {
                return state->send(std::move(uri), operation);
            },
            policy.error);
    };
}
WebHttpClients::HandlerFactory native_web_http_handler_factory() {
    framework::ConfigXmlNativeHttpServices policy;
    policy.connect_error = framework::config_http_connect_error;
    policy.dns_error = framework::config_http_dns_error;
    policy.read_error = framework::config_http_read_error;
    policy.write_error = framework::config_http_write_error;
    policy.error = [](std::string phase, int) {
        const auto message = phase == "tls"
                                 ? "The SSL connection could not be established, see inner exception."
                                 : "An error occurred while sending the request.";
        return std::make_exception_ptr(
            framework::ConfigXmlHttpError("System.Net.Http.HttpRequestException", message, 0x80131500u));
    };
    return native_web_http_handler_factory(std::move(policy));
}
}
