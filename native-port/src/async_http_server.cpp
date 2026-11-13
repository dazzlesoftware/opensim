// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/async_http_server.hpp"
#include "opensim/framework/config_uri_host_name.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/filesystem.hpp"
#include "opensim/framework/ip_address.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/network/service_protocol.hpp"
#include "http_tls_certificate.hpp"
#include <httplib.h>
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#include <openssl/pkcs12.h>
#include <openssl/x509v3.h>
#endif
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <optional>
#include <regex>
#include <thread>
#include <unordered_map>
#include <utility>
namespace opensim::network {
using Clock = std::chrono::steady_clock;
namespace {
std::atomic<std::uint32_t> request_sequence = 0;
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
bool matches_certificate_dns(std::u16string_view host, std::u16string_view name) {
    const auto wildcard = name.find(u'*');
    const auto equal = framework::invariant_equals_ignore_case_utf16;
    if (wildcard == name.npos)
        return equal(host, name);
    if (wildcard + 1 == name.size())
        return true;
    if (name.size() - wildcard < 3 || name[wildcard + 1] != u'.' ||
        name.find(u'*', wildcard + 1) != name.npos)
        return false;
    const auto suffix = name.substr(wildcard + 1);
    if (host.size() <= suffix.size() || !equal(host.substr(host.size() - suffix.size()), suffix))
        return false;
    if (wildcard == 0)
        return host.find(u'.') >= host.size() - suffix.size();
    return equal(host.substr(0, wildcard), name.substr(0, wildcard));
}

std::u16string certificate_text(const ASN1_STRING* value) {
    unsigned char* encoded = nullptr;
    const int size = ASN1_STRING_to_UTF8(&encoded, value);
    const auto release = [](unsigned char* bytes) { OPENSSL_free(bytes); };
    const std::unique_ptr<unsigned char, decltype(release)> bytes(encoded, release);
    if (size <= 0)
        return {};
    return framework::utf16_replacing(
        {reinterpret_cast<const char*>(encoded), static_cast<std::size_t>(size)});
}

bool matches_certificate_host(X509* certificate, std::string_view hostname) {
    if (!certificate)
        return false;
    using Type = framework::ConfigUriHostNameType;
    const auto host = std::make_shared<const std::u16string>(framework::utf16_replacing(hostname));
    const auto type = framework::config_uri_check_host_name(host);
    if (type == Type::Unknown || type == Type::Basic)
        return false;
    // BaseHttpServer treats IPv4 hostnames as DNS names and reserves the IP SAN check for IPv6.
    const bool ipv6 = type == Type::IPv6;
    const std::unique_ptr<GENERAL_NAMES, decltype(&GENERAL_NAMES_free)> names(
        static_cast<GENERAL_NAMES*>(X509_get_ext_d2i(certificate, NID_subject_alt_name, nullptr, nullptr)),
        GENERAL_NAMES_free);
    for (int i = 0; names && i < sk_GENERAL_NAME_num(names.get()); ++i) {
        const auto* entry = sk_GENERAL_NAME_value(names.get(), i);
        if (!ipv6 && entry->type == GEN_DNS &&
            matches_certificate_dns(*host, certificate_text(entry->d.dNSName)))
            return true;
        if (ipv6 && entry->type == GEN_IPADD && ASN1_STRING_length(entry->d.iPAddress) == 16) {
            std::array<std::uint8_t, 16> address{};
            std::copy_n(ASN1_STRING_get0_data(entry->d.iPAddress), address.size(), address.begin());
#ifdef _WIN32
            // Windows AsnEncodedData.Format supplies eight four-digit IPv6 groups.
            constexpr std::string_view digits = "0123456789abcdef";
            std::string formatted;
            for (std::size_t byte = 0; byte < address.size(); ++byte) {
                if (byte != 0 && byte % 2 == 0)
                    formatted += ':';
                formatted += digits[address[byte] >> 4];
                formatted += digits[address[byte] & 15];
            }
#else
            const auto formatted = framework::IpAddress::from_bytes(address, true).str();
#endif
            if (framework::invariant_equals_ignore_case(hostname, formatted))
                return true;
        }
    }
    if (ipv6)
        return false;
    const auto* subject = X509_get_subject_name(certificate);
    if (!subject)
        return false;
    constexpr std::array simple_name_types{NID_commonName, NID_organizationalUnitName, NID_organizationName,
                                          NID_pkcs9_emailAddress};
    const ASN1_STRING* simple_name = nullptr;
    std::size_t priority = simple_name_types.size();
    for (int i = X509_NAME_entry_count(subject) - 1; i >= 0; --i) {
        const auto* entry = X509_NAME_get_entry(subject, i);
        const auto type_id = OBJ_obj2nid(X509_NAME_ENTRY_get_object(entry));
        const auto match = std::find(simple_name_types.begin(), simple_name_types.end(), type_id);
        const auto rank = static_cast<std::size_t>(match - simple_name_types.begin());
        if (!simple_name || rank < priority) {
            simple_name = X509_NAME_ENTRY_get_data(entry);
            priority = rank;
        }
    }
    return simple_name && matches_certificate_dns(*host, certificate_text(simple_name));
}

bool configure_pkcs12(SSL_CTX* context, const HttpServerOptions& options) {
    std::ifstream file(framework::utf8_path(options.pkcs12_bundle), std::ios::binary | std::ios::ate);
    const auto size = static_cast<std::streamoff>(file.tellg());
    if (size <= 0 || !std::in_range<int>(size))
        return false;
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size))
        return false;
    const unsigned char* next = bytes.data();
    std::unique_ptr<PKCS12, decltype(&PKCS12_free)> bundle(
        d2i_PKCS12(nullptr, &next, static_cast<long>(size)), PKCS12_free);
    if (!bundle)
        return false;

    EVP_PKEY* key = nullptr;
    X509* leaf = nullptr;
    STACK_OF(X509)* intermediates = nullptr;
    const bool parsed = PKCS12_parse(bundle.get(), options.pkcs12_password.c_str(),
                                    &key, &leaf, &intermediates) == 1;
    const std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> private_key(key, EVP_PKEY_free);
    const std::unique_ptr<X509, decltype(&X509_free)> certificate(leaf, X509_free);
    const auto free_chain = [](STACK_OF(X509)* chain) { sk_X509_pop_free(chain, X509_free); };
    const std::unique_ptr<STACK_OF(X509), decltype(free_chain)> chain(intermediates, free_chain);
    if (!parsed || !key || !leaf || SSL_CTX_use_certificate(context, leaf) != 1 ||
        SSL_CTX_use_PrivateKey(context, key) != 1 || SSL_CTX_check_private_key(context) != 1)
        return false;
    for (int i = 0; chain && i < sk_X509_num(chain.get()); ++i) {
        if (SSL_CTX_add1_chain_cert(context, sk_X509_value(chain.get(), i)) != 1)
            return false;
    }
    return true;
}
#endif
}
struct AsyncHttpServer::Impl : std::enable_shared_from_this<Impl> {
    enum class Phase { Reading, Awaiting, Writing, Idle, Closing, Closed };
    struct Chunk {
        std::shared_ptr<const std::vector<std::uint8_t>> bytes;
        std::size_t start = 0, length = 0;
        std::function<void()> complete;
    };
    struct Connection {
        socket_t socket = INVALID_SOCKET;
        std::recursive_mutex mutex;
        std::mutex socket_mutex;
        Phase phase = Phase::Reading;
        std::shared_ptr<httplib::Stream> stream;
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        httplib::tls::session_t session = nullptr;
        httplib::tls::ctx_t tls_context = nullptr;
#endif
        httplib::Request request;
        std::optional<HttpResponse> reply;
        std::optional<RawHttpResponse> raw_reply;
        std::shared_ptr<const std::vector<std::uint8_t>> raw_body;
        std::size_t raw_start = 0, raw_length = 0;
        std::optional<Chunk> chunk;
        std::function<void()> chunk_complete;
        std::vector<std::function<void()>> disconnect;
        std::vector<std::function<void()>> notifications;
        bool chunk_write = false;
        int idle_timeout = 30000;
        std::string output;
        std::size_t offset = 0, served = 0, generation = 0, drained = 0;
        Clock::time_point drain_deadline;
        bool sending = false, close_after_write = true;
        Clock::time_point idle_since = Clock::now(), write_started;
        void close() {
            std::lock_guard socket_lock(socket_mutex);
            for (auto& callback : disconnect)
                notifications.push_back(std::move(callback));
            disconnect.clear();
            chunk.reset();
            chunk_complete = {};
            phase = Phase::Closed;
            sending = false;
            if (socket == INVALID_SOCKET)
                return;
            stream.reset();
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            if (session) {
                ERR_clear_error();
                httplib::tls::shutdown(session, true);
                httplib::tls::free_session(session);
                session = nullptr;
            }
#endif
            httplib::detail::shutdown_socket(socket);
            httplib::detail::close_socket(socket);
            socket = INVALID_SOCKET;
            reply.reset();
            raw_reply.reset();
            raw_body.reset();
            raw_length = 0;
            output.clear();
        }
        void finish_close() {
            // Source EndSendResponse marks closing; its timeout manager later
            // flushes/disconnects. Do not shut down the socket in the same
            // reactor pass that queues the final successful write.
            sending = false;
            phase = Phase::Closing;
            drained = 0;
            drain_deadline = Clock::now() + std::chrono::seconds(1);
        }
        void finish_pending_close() {
            if (Clock::now() >= drain_deadline || drained >= 1024u * 1024u) {
                close();
                return;
            }
            if (httplib::detail::select_read(socket, 0, 0) <= 0)
                return;
            char bytes[65536];
            auto count = httplib::detail::read_socket(socket, bytes, sizeof(bytes), CPPHTTPLIB_RECV_FLAGS);
            if (count > 0) {
                drained += static_cast<std::size_t>(count);
                return;
            }
            if (count < 0) {
#ifdef _WIN32
                auto error = WSAGetLastError();
                if (error == WSAEWOULDBLOCK || error == WSAEINTR)
                    return;
#else
                if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
                    return;
#endif
            }
            close();
        }
        void interrupt() {
            std::lock_guard lock(socket_mutex);
            if (socket != INVALID_SOCKET)
                httplib::detail::shutdown_socket(socket);
        }
        bool peer_alive() {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            if (session) {
                // The library helper restores blocking mode and only recognizes
                // close_notify. Reactor sockets stay nonblocking, including on
                // abrupt EOF/reset and TLS errors from unrelated connections.
                ERR_clear_error();
                char byte;
                const auto result = SSL_peek(static_cast<SSL*>(session), &byte, 1);
                if (result > 0)
                    return true;
                const auto error = SSL_get_error(static_cast<SSL*>(session), result);
                return error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE;
            }
#endif
            return httplib::detail::is_socket_alive(socket);
        }
        ~Connection() {
            close();
        }
    };
    struct Channel final : HttpResponseChannel {
        std::weak_ptr<Impl> owner;
        std::shared_ptr<Connection> connection;
        std::size_t generation;
        bool submitted = false;
        Channel(std::shared_ptr<Impl> host, std::shared_ptr<Connection> c)
            : owner(host), connection(std::move(c)), generation(connection->generation) {
        }
        bool can_send() const override {
            auto host = owner.lock();
            if (!host || host->stopping)
                return false;
            std::lock_guard lock(connection->mutex);
            if (connection->phase == Phase::Closed || connection->phase == Phase::Closing ||
                connection->generation != generation)
                return false;
            if (connection->phase == Phase::Awaiting && !connection->peer_alive()) {
                connection->close();
                return false;
            }
            return true;
        }
        bool is_sending() const override {
            std::lock_guard lock(connection->mutex);
            return connection->generation == generation && connection->sending;
        }
        bool is_closed() const override {
            auto host = owner.lock();
            if (!host || host->stopping)
                return true;
            std::lock_guard lock(connection->mutex);
            return connection->phase == Phase::Closed || connection->phase == Phase::Closing;
        }
        bool complete(HttpResponse response) override {
            auto host = owner.lock();
            if (!host || host->stopping)
                return false;
            std::lock_guard lock(connection->mutex);
            if (connection->phase == Phase::Closed || connection->phase == Phase::Closing ||
                connection->generation != generation)
                return false;
            if (submitted)
                throw std::logic_error("HTTP response was already submitted");
            submitted = true;
            connection->reply = std::move(response);
            connection->sending = true;
            host->changed.notify_one();
            return true;
        }
        bool complete_raw(RawHttpResponse response) override {
            auto host = owner.lock();
            if (!host || host->stopping)
                return false;
            std::lock_guard lock(connection->mutex);
            if (connection->phase == Phase::Closed || connection->phase == Phase::Closing ||
                connection->generation != generation)
                return false;
            if (submitted)
                throw std::logic_error("HTTP response was already submitted");
            submitted = true;
            connection->raw_reply = std::move(response);
            connection->sending = true;
            host->changed.notify_one();
            return true;
        }
        int requests_remaining() const override {
            std::lock_guard lock(connection->mutex);
            return connection->served >= 20 ? 0 : static_cast<int>(20 - connection->served);
        }
        int idle_timeout_ms() const override {
            std::lock_guard lock(connection->mutex);
            return connection->idle_timeout;
        }
        void idle_timeout_ms(int value) override {
            std::lock_guard lock(connection->mutex);
            connection->idle_timeout = std::min(value, 120000);
        }
        bool send_chunk(std::shared_ptr<const std::vector<std::uint8_t>> bytes, std::size_t start,
                        std::size_t length, std::function<void()> complete) override {
            auto host = owner.lock();
            if (!host || host->stopping)
                return false;
            if (!bytes || start > bytes->size() || length > bytes->size() - start)
                throw std::out_of_range("HTTP response chunk range");
            std::lock_guard lock(connection->mutex);
            if (connection->phase == Phase::Closed || connection->phase == Phase::Closing ||
                connection->generation != generation)
                return false;
            if (connection->chunk || connection->phase == Phase::Writing || connection->reply ||
                connection->raw_reply)
                return false;
            submitted = true;
            connection->chunk = Chunk{std::move(bytes), start, length, std::move(complete)};
            connection->sending = true;
            host->changed.notify_one();
            return true;
        }
        void finish_response(bool keep_alive) override {
            std::lock_guard lock(connection->mutex);
            if (connection->phase == Phase::Closed || connection->phase == Phase::Closing ||
                connection->generation != generation)
                return;
            connection->sending = false;
            connection->request = {};
            if (!keep_alive || connection->served >= 20)
                connection->finish_close();
            else {
                connection->phase = Phase::Idle;
                connection->idle_since = Clock::now();
            }
        }
        void on_disconnect(std::function<void()> callback) override {
            bool closed;
            {
                std::lock_guard lock(connection->mutex);
                closed = connection->phase == Phase::Closed;
                if (!closed)
                    connection->disconnect.push_back(std::move(callback));
            }
            if (closed && callback)
                callback();
        }
        void cancel() override {
            std::lock_guard lock(connection->mutex);
            if (connection->generation == generation)
                connection->close();
        }
    };
    struct Transport {
        virtual ~Transport() = default;
        virtual bool parse(const std::shared_ptr<Connection>& c, bool& closed, bool& deferred) = 0;
        virtual bool serialize(httplib::Stream& stream, bool close, const httplib::Request& request,
                               httplib::Response& response) = 0;
    };
    template <class Base> struct Listener : Base, Transport {
        std::weak_ptr<Impl> owner;
        template <class... A>
        Listener(std::shared_ptr<Impl> host, A&&... args) : Base(std::forward<A>(args)...), owner(host) {
        }
        bool process_and_close_socket(socket_t socket) override {
            if (auto host = owner.lock()) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
                if constexpr (std::is_same_v<Base, httplib::SSLServer>)
                    host->accept(socket, this->tls_context());
                else
#endif
                    host->accept(socket);
                return true;
            }
            httplib::detail::shutdown_socket(socket);
            httplib::detail::close_socket(socket);
            return false;
        }
        bool parse(const std::shared_ptr<Connection>& c, bool& closed, bool& deferred) override {
            std::string remote, local;
            int remote_port = 0, local_port = 0;
            c->stream->get_remote_ip_and_port(remote, remote_port);
            c->stream->get_local_ip_and_port(local, local_port);
            return this->serve_guarded([&] {
                return this->process_request(
                    *c->stream, remote, remote_port, local, local_port, c->served >= 20, closed,
                    [&](httplib::Request& request) {
                        request.opensim_async_context = c;
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
                        request.ssl = c->session;
#endif
                    },
                    nullptr,
                    [&](httplib::Request&& request, httplib::Response&&) {
                        c->request = std::move(request);
                        c->phase = Phase::Awaiting;
                        deferred = true;
                    });
            });
        }
        bool serialize(httplib::Stream& stream, bool close, const httplib::Request& request,
                       httplib::Response& response) override {
            return this->opensim_write_deferred(stream, close, request, response);
        }
    };
    HttpServerOptions options;
    Handler handler;
    std::shared_ptr<httplib::Server> server;
    std::shared_ptr<Transport> transport;
    std::unique_ptr<httplib::ThreadPool> readers;
    std::thread listener, reactor;
    std::atomic<bool> stopping = false;
    bool started = false;
    std::mutex connections_mutex, wait_mutex;
    std::condition_variable changed;
    std::unordered_map<std::uint64_t, std::shared_ptr<Connection>> connections;
    std::uint64_t next_connection = 0;
    Impl(HttpServerOptions value, Handler callback)
        : options(std::move(value)), handler(std::move(callback)) {
    }
    void initialize() {
        if (options.port < 0 || options.port > 65535 || options.workers < 1 || options.workers > 256 ||
            options.queued_requests < 1 || options.timeout_seconds < 1 || options.maximum_body < 1)
            throw std::invalid_argument("Invalid asynchronous HTTP server limits");
        auto self = shared_from_this();
        if (!options.certificate.empty() || !options.private_key.empty() || !options.pkcs12_bundle.empty()) {
            if (options.pkcs12_bundle.empty() && (options.certificate.empty() || options.private_key.empty()))
                throw std::invalid_argument("TLS requires certificate and private key");
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            auto tls = options.pkcs12_bundle.empty()
                ? std::make_shared<Listener<httplib::SSLServer>>(self, httplib::tls::ContextSetupCallback(
                      [&](auto context) { return load_http_pem_certificate(context, options); }))
                : std::make_shared<Listener<httplib::SSLServer>>(self, httplib::tls::ContextSetupCallback(
                      [&](auto context) { return configure_pkcs12(static_cast<SSL_CTX*>(context), options); }));
            server = tls;
            transport = tls;
            if (!server->is_valid())
                throw std::runtime_error(options.pkcs12_bundle.empty() ? "Cannot load TLS certificate/private key"
                                                                      : "Cannot load TLS PKCS#12 certificate bundle");
#else
            throw std::runtime_error("TLS requested but OpenSSL support was not built");
#endif
        } else {
            auto plain = std::make_shared<Listener<httplib::Server>>(self);
            server = plain;
            transport = plain;
        }
        server->set_socket_options([](socket_t socket) {
            const int enabled = 1;
#ifdef _WIN32
            if (setsockopt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&enabled),
                           sizeof(enabled)) != 0) {
                httplib::detail::close_socket(socket);
                throw std::runtime_error("Cannot set exclusive HTTP listener");
            }
#else
            if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) != 0) {
                httplib::detail::close_socket(socket);
                throw std::runtime_error("Cannot set HTTP listener options");
            }
#endif
        });
        server->new_task_queue = [value = options] {
            return new httplib::ThreadPool(value.workers, 0, value.queued_requests);
        };
        readers = std::make_unique<httplib::ThreadPool>(options.workers, 0, options.queued_requests);
        // Original OSHttpServer.HttpListener disables Nagle on accepted sockets.
        server->set_tcp_nodelay(true);
        server->set_read_timeout(options.timeout_seconds);
        server->set_write_timeout(options.timeout_seconds);
        server->set_keep_alive_timeout(60);
        server->set_keep_alive_max_count(20);
        server->set_payload_max_length(options.maximum_body);
        server->set_request_decompression(options.decompress_request_body);
        auto dispatch = [weak = std::weak_ptr<Impl>(self)](const httplib::Request& request,
                                                           httplib::Response& response) {
            auto host = weak.lock();
            auto connection = std::static_pointer_cast<Connection>(request.opensim_async_context.lock());
            if (!host || !connection)
                throw std::logic_error("Missing asynchronous HTTP connection");
            HttpRequest incoming{request.method,
                                 request.path,
                                 request.body,
                                 request.remote_addr,
                                 request.get_header_value("Content-Type"),
                                 {}};
            incoming.raw_target = request.target;
            incoming.remote_port = request.remote_port;
            incoming.http_version = request.version;
            const auto previous = request_sequence.load(std::memory_order_relaxed);
            incoming.request_id = previous + 1;
            request_sequence.store(incoming.request_id, std::memory_order_relaxed);
            incoming.arrival_timestamp =
                std::chrono::duration<double>(request.start_time_.time_since_epoch()).count();
            for (const auto& [name, value] : request.headers) {
                auto key = name;
                for (auto& ch : key)
                    if (ch >= 'A' && ch <= 'Z')
                        ch = static_cast<char>(ch + 32);
                // OSHttpServer filters each forwarded address field before
                // joining duplicate headers for callers.
                if (key == "x-forwarded-for" && framework::utf16_replacing(value).size() < 7)
                    continue;
                auto [i, inserted] = incoming.headers.try_emplace(key, value);
                if (!inserted)
                    i->second += "," + value;
            }
            response.opensim_deferred = true;
            auto channel = std::make_shared<Channel>(host, connection);
            incoming.response_channel = channel;
            try {
                host->handler(incoming, channel);
            } catch (...) {
                if (!channel->submitted) {
                    HttpResponse error;
                    error.status = 500;
                    error.body = "Internal server error\n";
                    channel->complete(std::move(error));
                }
            }
        };
        server->Get("/.*", dispatch);
        server->Post("/.*", dispatch);
        server->Put("/.*", dispatch);
        server->Delete("/.*", dispatch);
        server->Patch("/.*", dispatch);
        server->Options("/.*", dispatch);
        for (const auto& method : options.custom_methods)
            server->CustomRoute(method, "/.*", dispatch);
        server->CustomMethodFallback(
            [](const httplib::Request& request) {
                static const std::regex script_path("/lslhttps?/[0-9A-Fa-f-]{36}(/.*)?");
                const auto target = canonical_service_request_target(request.target);
                return target && std::regex_match(target->path, script_path);
            },
            dispatch);
    }
    void accept(socket_t socket
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
                ,
                httplib::tls::ctx_t tls_context = nullptr
#endif
    ) {
        auto c = std::make_shared<Connection>();
        c->socket = socket;
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        c->tls_context = tls_context;
#endif
        {
            std::lock_guard lock(connections_mutex);
            if (stopping) {
                c->close();
                return;
            }
            connections.emplace(++next_connection, c);
        }
        if (!readers->enqueue([self = shared_from_this(), c] { self->read(c); })) {
            std::lock_guard lock(c->mutex);
            c->close();
        }
    }
    void read(const std::shared_ptr<Connection>& c) {
        std::lock_guard lock(c->mutex);
        try {
            if (stopping || c->phase == Phase::Closed) {
                c->close();
                return;
            }
            if (!c->stream) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
                if (c->tls_context) {
                    c->session = httplib::tls::create_session(c->tls_context, c->socket);
                    httplib::tls::TlsError error;
                    if (!c->session || !httplib::tls::accept_nonblocking(
                                           c->session, c->socket, options.timeout_seconds, 0, &error)) {
                        c->close();
                        return;
                    }
                    c->stream = std::make_shared<httplib::detail::SSLSocketStream>(
                        c->socket, c->session, options.timeout_seconds, 0, options.timeout_seconds, 0);
                } else
#endif
                    c->stream = std::make_shared<httplib::detail::SocketStream>(
                        c->socket, options.timeout_seconds, 0, options.timeout_seconds, 0);
                httplib::detail::set_nonblocking(c->socket, true);
#ifdef SO_NOSIGPIPE
                const int enabled = 1;
                setsockopt(c->socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
            }
            ++c->served;
            ++c->generation;
            bool closed = false, deferred = false;
            if (!transport->parse(c, closed, deferred)) {
                c->close();
                return;
            }
            // Expect/parse-error writes may have used synchronous stream helpers.
            if (c->socket != INVALID_SOCKET)
                httplib::detail::set_nonblocking(c->socket, true);
            if (!deferred) {
                if (closed)
                    c->close();
                else {
                    c->phase = Phase::Idle;
                    c->idle_since = Clock::now();
                }
            }
        } catch (...) {
            c->close();
        }
        changed.notify_one();
    }
    void prepare(const std::shared_ptr<Connection>& c) {
        if (c->chunk) {
            auto chunk = std::move(*c->chunk);
            c->chunk.reset();
            c->output.clear();
            c->raw_body = std::move(chunk.bytes);
            c->raw_start = chunk.start;
            c->raw_length = chunk.length;
            c->offset = 0;
            c->chunk_complete = std::move(chunk.complete);
            c->chunk_write = true;
            c->write_started = Clock::now();
            c->phase = Phase::Writing;
            return;
        }
        if (c->raw_reply) {
            auto raw = std::move(*c->raw_reply);
            c->raw_reply.reset();
            const auto size = raw.body ? raw.body->size() : 0;
            if (raw.start > size || raw.length > size - raw.start)
                throw std::out_of_range("Raw HTTP body range");
            c->output = std::move(raw.headers);
            c->raw_body = std::move(raw.body);
            c->raw_start = raw.start;
            c->raw_length = raw.length;
            c->close_after_write = !raw.keep_alive || c->served >= 20;
            c->offset = 0;
            c->write_started = Clock::now();
            c->phase = Phase::Writing;
            return;
        }
        auto response = std::move(*c->reply);
        c->reply.reset();
        httplib::Response outgoing;
        outgoing.version = "HTTP/1.1";
        outgoing.status = response.status;
        outgoing.set_content(std::move(response.body), response.content_type);
        for (const auto& [name, value] : response.headers)
            outgoing.set_header(name, value);
        outgoing.set_header("Connection", response.keep_alive ? "keep-alive" : "close");
        httplib::detail::BufferStream buffer;
        if (!transport->serialize(buffer, !response.keep_alive || c->served >= 20, c->request, outgoing)) {
            c->close();
            return;
        }
        c->close_after_write = httplib::detail::has_header_token(outgoing.headers, "Connection", "close");
        c->output = buffer.get_buffer();
        c->offset = 0;
        c->write_started = Clock::now();
        c->phase = Phase::Writing;
    }
    void write(const std::shared_ptr<Connection>& c) {
        if (Clock::now() - c->write_started > std::chrono::seconds(options.timeout_seconds)) {
            c->close();
            return;
        }
        const bool headers = c->offset < c->output.size();
        const auto count = std::min<std::size_t>(
            65536, headers ? c->output.size() - c->offset : c->raw_length - (c->offset - c->output.size()));
        const char* data = headers
                               ? c->output.data() + c->offset
                               : (count ? reinterpret_cast<const char*>(c->raw_body->data() + c->raw_start +
                                                                        c->offset - c->output.size())
                                        : nullptr);
        std::ptrdiff_t written = 0;
        if (count) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            if (c->session) {
                ERR_clear_error();
                httplib::tls::TlsError error;
                written = httplib::tls::write(c->session, data, count, error);
                if (written < 0 && (error.code == httplib::tls::ErrorCode::WantRead ||
                                    error.code == httplib::tls::ErrorCode::WantWrite))
                    return;
                if (written <= 0) {
                    c->close();
                    return;
                }
            } else
#endif
            {
                int flags = CPPHTTPLIB_SEND_FLAGS;
#ifdef MSG_NOSIGNAL
                flags |= MSG_NOSIGNAL;
#endif
                written = httplib::detail::send_socket(c->socket, data, count, flags);
                if (written < 0) {
#ifdef _WIN32
                    const auto error = WSAGetLastError();
                    if (error == WSAEWOULDBLOCK || error == WSAEINTR)
                        return;
#else
                    if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
                        return;
#endif
                    c->close();
                    return;
                }
                if (written == 0) {
                    c->close();
                    return;
                }
            }
            c->offset += static_cast<std::size_t>(written);
        }
        if (c->offset == c->output.size() + c->raw_length) {
            // Keep the completed output storage until response teardown.
            c->raw_length = 0;
            if (c->chunk_write) {
                c->chunk_write = false;
                c->phase = Phase::Awaiting;
                if (c->chunk_complete)
                    c->notifications.push_back(std::move(c->chunk_complete));
                return;
            }
            c->sending = false;
            c->request = {};
            if (c->close_after_write)
                c->finish_close();
            else {
                c->phase = Phase::Idle;
                c->idle_since = Clock::now();
            }
        }
    }
    void run() {
        while (!stopping) {
            std::vector<std::shared_ptr<Connection>> snapshot;
            std::vector<std::function<void()>> callbacks;
            {
                std::lock_guard lock(connections_mutex);
                for (const auto& [socket, c] : connections)
                    snapshot.push_back(c);
            }
            for (const auto& c : snapshot) {
                std::unique_lock lock(c->mutex, std::try_to_lock);
                if (!lock)
                    continue;
                try {
                    if (c->phase == Phase::Awaiting) {
                        if (c->reply || c->raw_reply || c->chunk)
                            prepare(c);
                        else if (!c->peer_alive())
                            c->close();
                    }
                    if (c->phase == Phase::Writing)
                        write(c);
                    if (c->phase == Phase::Closing)
                        c->finish_pending_close();
                    if (c->phase == Phase::Idle) {
                        if (Clock::now() - c->idle_since > std::chrono::milliseconds(c->idle_timeout))
                            c->close();
                        else if (c->stream->is_readable() ||
                                 httplib::detail::select_read(c->socket, 0, 0) > 0) {
                            c->phase = Phase::Reading;
                            if (!readers->enqueue([self = shared_from_this(), c] { self->read(c); }))
                                c->close();
                        }
                    }
                } catch (...) {
                    c->close();
                }
                for (auto& callback : c->notifications)
                    callbacks.push_back(std::move(callback));
                c->notifications.clear();
            }
            for (auto& callback : callbacks)
                try {
                    callback();
                } catch (...) {
                }
            {
                std::lock_guard lock(connections_mutex);
                for (auto i = connections.begin(); i != connections.end();) {
                    auto retained = i->second;
                    std::unique_lock item(retained->mutex, std::try_to_lock);
                    if (item && retained->phase == Phase::Closed)
                        i = connections.erase(i);
                    else
                        ++i;
                }
            }
            std::unique_lock lock(wait_mutex);
            changed.wait_for(lock, std::chrono::milliseconds(2));
        }
    }
    int start(const std::function<void(int)>& on_bound) {
        if (started || stopping)
            throw std::logic_error("Asynchronous HTTP server already started/stopped");
        const int port = options.port == 0
                             ? server->bind_to_any_port(options.host)
                             : (server->bind_to_port(options.host, options.port) ? options.port : -1);
        if (port < 0)
            throw std::runtime_error("Cannot bind asynchronous HTTP listener");
        started = true;
        try {
            if (on_bound)
                on_bound(port);
            reactor = std::thread([self = shared_from_this()] { self->run(); });
            listener = std::thread([self = shared_from_this()] { self->server->listen_after_bind(); });
            server->wait_until_ready();
            if (!server->is_running())
                throw std::runtime_error("Asynchronous HTTP listener failed to start");
        } catch (...) {
            stop();
            throw;
        }
        return port;
    }
    void stop() {
        if (stopping.exchange(true))
            return;
        server->stop();
        changed.notify_all();
        {
            std::lock_guard lock(connections_mutex);
            for (const auto& [id, c] : connections)
                c->interrupt();
        }
        if (listener.joinable())
            listener.join();
        if (reactor.joinable())
            reactor.join();
        readers->shutdown();
        std::vector<std::function<void()>> callbacks;
        {
            std::lock_guard lock(connections_mutex);
            for (auto& [socket, c] : connections) {
                std::lock_guard item(c->mutex);
                c->close();
                for (auto& callback : c->notifications)
                    callbacks.push_back(std::move(callback));
                c->notifications.clear();
            }
            connections.clear();
        }
        for (auto& callback : callbacks)
            try {
                callback();
            } catch (...) {
            }
    }
};
AsyncHttpServer::AsyncHttpServer(HttpServerOptions options, Handler handler)
    : impl_(std::make_shared<Impl>(std::move(options), std::move(handler))) {
    impl_->initialize();
}
AsyncHttpServer::~AsyncHttpServer() {
    stop();
}
int AsyncHttpServer::start(const std::function<void(int)>& on_bound) {
    return impl_->start(on_bound);
}
void AsyncHttpServer::stop() {
    if (impl_)
        impl_->stop();
}
bool AsyncHttpServer::is_running() const {
    return impl_->server->is_running() && !impl_->stopping;
}
bool AsyncHttpServer::certificate_matches_host(std::string_view hostname) const {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    const auto* tls = dynamic_cast<const httplib::SSLServer*>(impl_->server.get());
    return tls && matches_certificate_host(
        SSL_CTX_get0_certificate(static_cast<SSL_CTX*>(tls->tls_context())), hostname);
#else
    (void)hostname;
    return false;
#endif
}
}
