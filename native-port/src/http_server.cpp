/*
Copyright (c) Contributors, http://opensimulator.org/
See CONTRIBUTORS.TXT for a full list of copyright holders.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenSimulator Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "opensim/network/http_server.hpp"
#include "opensim/framework/filesystem.hpp"
#include "http_tls_certificate.hpp"
#include <httplib.h>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace opensim::network {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
bool load_http_pem_certificate(void* context, const HttpServerOptions& options) {
    auto* tls = static_cast<SSL_CTX*>(context);
    const auto read = [](const std::string& filename) -> std::string {
        std::ifstream file(framework::utf8_path(filename), std::ios::binary | std::ios::ate);
        const auto size = static_cast<std::streamoff>(file.tellg());
        if (size <= 0 || !std::in_range<int>(size))
            return {};
        std::string contents(static_cast<std::size_t>(size), '\0');
        file.seekg(0);
        if (!file.read(contents.data(), size))
            return {};
        return contents;
    };
    const auto certificate_pem = read(options.certificate);
    const auto key_pem = read(options.private_key);
    if (certificate_pem.empty() || key_pem.empty())
        return false;

    const std::unique_ptr<BIO, decltype(&BIO_free)> certificates(
        BIO_new_mem_buf(certificate_pem.data(), static_cast<int>(certificate_pem.size())), BIO_free);
    const std::unique_ptr<BIO, decltype(&BIO_free)> private_key(
        BIO_new_mem_buf(key_pem.data(), static_cast<int>(key_pem.size())), BIO_free);
    if (!certificates || !private_key)
        return false;

    ERR_clear_error();
    const std::unique_ptr<X509, decltype(&X509_free)> certificate(
        PEM_read_bio_X509_AUX(certificates.get(), nullptr, nullptr, nullptr), X509_free);
    if (!certificate || SSL_CTX_use_certificate(tls, certificate.get()) != 1)
        return false;

    for (;;) {
        const std::unique_ptr<X509, decltype(&X509_free)> intermediate(
            PEM_read_bio_X509(certificates.get(), nullptr, nullptr, nullptr), X509_free);
        if (!intermediate) {
            const auto error = ERR_peek_last_error();
            if (ERR_GET_LIB(error) != ERR_LIB_PEM || ERR_GET_REASON(error) != PEM_R_NO_START_LINE)
                return false;
            ERR_clear_error();
            break;
        }
        if (SSL_CTX_add1_chain_cert(tls, intermediate.get()) != 1)
            return false;
    }

    const std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(
        PEM_read_bio_PrivateKey(private_key.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    return key && SSL_CTX_use_PrivateKey(tls, key.get()) == 1 && SSL_CTX_check_private_key(tls) == 1;
}
#endif

struct HttpServer::Impl {
    HttpServerOptions options;
    std::unique_ptr<httplib::Server> server;
    std::thread listener;
    bool started = false;
    int bound_port = -1;
};
HttpServer::HttpServer(HttpServerOptions options, Handler handler) : impl_(std::make_unique<Impl>()) {
    if (options.port < 0 || options.port > 65535 || options.workers < 1 || options.workers > 256 ||
        options.queued_requests < 1 || options.timeout_seconds < 1 || options.maximum_body < 1)
        throw std::invalid_argument("Invalid HTTP server limits");
    impl_->options = options;
    if (!options.certificate.empty() || !options.private_key.empty()) {
        if (options.certificate.empty() || options.private_key.empty())
            throw std::invalid_argument("TLS requires both certificate and private key");
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        impl_->server = std::make_unique<httplib::SSLServer>(httplib::tls::ContextSetupCallback(
            [&](auto context) { return load_http_pem_certificate(context, options); }));
        if (!impl_->server->is_valid())
            throw std::runtime_error("Cannot load TLS certificate/private key");
#else
        throw std::runtime_error("TLS requested but OpenSSL support was not built");
#endif
    } else
        impl_->server = std::make_unique<httplib::Server>();
    auto& server = *impl_->server;
    // Do not inherit httplib's SO_REUSEPORT/default sharing policy: a second
    // simulator must fail rather than split requests with an existing listener.
    server.set_socket_options([](auto socket) {
        const int enabled = 1;
#ifdef _WIN32
        const auto result = setsockopt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                                       reinterpret_cast<const char*>(&enabled), sizeof(enabled));
        if (result != 0) {
            closesocket(socket);
            throw std::runtime_error("Cannot set exclusive listener binding");
        }
#else
        const auto result = setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
        if (result != 0) {
            close(socket);
            throw std::runtime_error("Cannot set listener socket options");
        }
#endif
    });
    server.new_task_queue = [options] {
        return new httplib::ThreadPool(options.workers, 0, options.queued_requests);
    };
    // Original OSHttpServer.HttpListener disables Nagle on accepted sockets.
    server.set_tcp_nodelay(true);
    server.set_read_timeout(options.timeout_seconds);
    server.set_write_timeout(options.timeout_seconds);
    server.set_payload_max_length(options.maximum_body);
    server.set_request_decompression(options.decompress_request_body);
    server.set_exception_handler(
        [](const httplib::Request&, httplib::Response& response, std::exception_ptr) {
            response.status = 500;
            response.set_content("Internal server error\n", "text/plain");
            response.set_header("Connection", "close");
        });
    auto dispatch = [handler = std::move(handler)](const httplib::Request& request,
                                                   httplib::Response& response) {
        HttpRequest incoming{request.method,
                             request.path,
                             request.body,
                             request.remote_addr,
                             request.get_header_value("Content-Type"),
                             {}};
        incoming.raw_target = request.target;
        incoming.remote_port = request.remote_port;
        incoming.http_version = request.version;
        for (const auto& [name, value] : request.headers) {
            auto key = name;
            for (auto& c : key)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c + 32);
            if (incoming.headers.contains(key))
                incoming.headers[key] += "," + value;
            else
                incoming.headers[key] = value;
        }
        auto reply = handler(incoming);
        response.status = reply.status;
        response.set_content(reply.body, reply.content_type);
        for (const auto& [key, value] : reply.headers)
            response.set_header(key, value);
        response.set_header("Connection", reply.keep_alive ? "keep-alive" : "close");
    };
    server.Get("/.*", dispatch);
    server.Post("/.*", dispatch);
    server.Put("/.*", dispatch);
    for (const auto& method : impl_->options.custom_methods)
        server.CustomRoute(method, "/.*", dispatch);
    server.Delete("/.*", dispatch);
    server.Patch("/.*", dispatch);
    server.Options("/.*", dispatch);
}
HttpServer::~HttpServer() {
    stop();
}
int HttpServer::start(const std::function<void(int)>& on_bound) {
    if (impl_->started)
        throw std::logic_error("HTTP server already started");
    auto& server = *impl_->server;
    impl_->bound_port =
        impl_->options.port == 0
            ? server.bind_to_any_port(impl_->options.host)
            : (server.bind_to_port(impl_->options.host, impl_->options.port) ? impl_->options.port : -1);
    if (impl_->bound_port < 0)
        throw std::runtime_error("Cannot bind HTTP listener");
    impl_->started = true;
    if (on_bound)
        on_bound(impl_->bound_port);
    impl_->listener = std::thread([this] { impl_->server->listen_after_bind(); });
    server.wait_until_ready();
    if (!server.is_running()) {
        stop();
        throw std::runtime_error("HTTP listener failed to start");
    }
    return impl_->bound_port;
}
void HttpServer::stop() {
    if (!impl_)
        return;
    impl_->server->stop();
    if (impl_->listener.joinable())
        impl_->listener.join();
}
bool HttpServer::is_running() const {
    return impl_->server->is_running();
}
bool HttpServer::tls_available() noexcept {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    return true;
#else
    return false;
#endif
}
bool HttpServer::gzip_available() noexcept {
#ifdef CPPHTTPLIB_ZLIB_SUPPORT
    return true;
#else
    return false;
#endif
}
}
