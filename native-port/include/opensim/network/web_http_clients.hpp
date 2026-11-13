// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/atomic_shared_ptr.hpp"
#include "opensim/framework/config_uri_object.hpp"
#include "opensim/framework/config_xml_transport_http.hpp"
#include <atomic>
namespace opensim::network {
struct WebHttpHandlerSettings {
    bool allow_auto_redirect = true, automatic_decompression = false, pre_authenticate = false,
         use_cookies = false;
    std::int32_t max_automatic_redirections = 50;
    framework::ConfigXmlHttpPoolSettings pool;
    bool no_verify_cert_chain = false, no_verify_cert_hostname = false;
    std::int32_t enabled_ssl_protocols = 16320, certificate_revocation_mode = 0;
    bool use_proxy = false;
    std::shared_ptr<framework::ConfigXmlProxy> proxy;
    bool accepts_certificate_errors(std::int32_t errors) const noexcept;
};
using WebHttpSender = std::function<std::shared_ptr<framework::ConfigXmlStream>(
    framework::ConfigUriObjectPtr, framework::ConfigXmlHttpOperation)>;
class WebHttpHandler {
    WebHttpHandlerSettings settings_;
    WebHttpSender sender_;
    std::function<std::exception_ptr(std::string, int)> error_;

public:
    WebHttpHandler(WebHttpHandlerSettings, WebHttpSender,
                   std::function<std::exception_ptr(std::string, int)> errors = {});
    const WebHttpHandlerSettings& settings() const noexcept {
        return settings_;
    }
    auto error_factory() const {
        return error_;
    }
    std::shared_ptr<framework::ConfigXmlStream> send_headers(framework::ConfigUriObjectPtr,
                                                             framework::ConfigXmlHttpOperation,
                                                             bool ensure_success = false) const;
};
class WebHttpClient {
    std::shared_ptr<WebHttpHandler> handler_;
    std::int32_t timeout_, maximum_buffer_;
    std::shared_ptr<std::atomic<bool>> disposed_ = std::make_shared<std::atomic<bool>>(false);

public:
    WebHttpClient(std::shared_ptr<WebHttpHandler>, std::int32_t timeout, std::int32_t maximum_buffer);
    std::shared_ptr<WebHttpHandler> handler() const noexcept {
        return handler_;
    }
    std::int32_t timeout_milliseconds() const noexcept {
        return timeout_;
    }
    std::int32_t maximum_response_buffer_size() const noexcept {
        return maximum_buffer_;
    }
    bool expect_continue() const noexcept {
        return false;
    }
    std::shared_ptr<framework::ConfigXmlStream> send_headers(framework::ConfigUriObjectPtr,
                                                             framework::ConfigXmlHttpOperation,
                                                             bool ensure_success = false) const;
    // HttpClient(handler,false): disposing a wrapper retains the shared handler.
    void close() noexcept {
        disposed_->store(true);
    }
};
class WebHttpClients {
public:
    using HandlerFactory = std::function<std::shared_ptr<WebHttpHandler>(const WebHttpHandlerSettings&)>;
    opensim::framework::AtomicSharedPtr<WebHttpHandler> shared_handler, shared_no_redirect_handler;
    explicit WebHttpClients(HandlerFactory);
    void setup(bool no_verify_cert_chain, bool no_verify_cert_hostname, std::shared_ptr<framework::ConfigXmlProxy>,
               std::int32_t maximum_connections);
    std::shared_ptr<WebHttpClient> get_new_global_client(std::int32_t timeout,
                                                         std::int32_t maximum_buffer = 250 * 1024 * 1024) const;
    std::shared_ptr<WebHttpClient> get_no_redirect_client(std::int32_t timeout,
                                                          std::int32_t maximum_buffer = 250 * 1024 * 1024) const;

private:
    HandlerFactory factory_;
};
// Real typed URI/socket/pool backend. Platform trust and failure translation
// remain explicit host policies; a null Setup proxy disables default proxies.
WebHttpClients::HandlerFactory native_web_http_handler_factory(framework::ConfigXmlNativeHttpServices);
WebHttpClients::HandlerFactory native_web_http_handler_factory();
}
