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
#include "opensim/framework/text.hpp"
#include "opensim/framework/ip_address.hpp"
#include <chrono>
#include <array>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
namespace opensim::framework {
ConfigXmlHttpError::ConfigXmlHttpError(std::string type, std::string text, std::uint32_t h,
                                       std::optional<int> s)
    : runtime_error(text), source_type(std::move(type)), message(utf16(text)), hresult(h), status(s) {
}
namespace {
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
              bool head_only, std::shared_ptr<ConfigHttpNativePool::Lease> pool_lease, bool can_reuse,
              std::function<bool()> stop)
        : proxy_owner(std::move(proxy)), lease(std::move(pool_lease)), handle(std::move(h)),
          services(std::move(s)), source(std::move(input)), reusable(can_reuse),
          interrupted(std::move(stop)) {
        auto framing = source->head;
        if (head_only)
            framing.status = 204;
        std::vector<std::u16string> encodings;
        for (auto& header : framing.headers)
            if (httplib::detail::case_ignore::equal(header.name, "Transfer-Encoding"))
                encodings.push_back(header.value);
        chunked = config_http_transfer_encoding_chunked(encodings);
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
    std::atomic<std::shared_ptr<BodyState>> state;
    Body(httplib::ClientImpl::StreamHandle h, ConfigXmlNativeHttpServices s,
         std::shared_ptr<HeadBuffer> source, std::shared_ptr<ConfigXmlHttpProxyProvider> proxy,
         bool head_only = false, std::shared_ptr<ConfigHttpNativePool::Lease> lease = {},
         bool reusable = false, std::function<bool()> stop = {})
        : state(std::make_shared<BodyState>(std::move(h), std::move(s), std::move(source), std::move(proxy),
                                            head_only, std::move(lease), reusable, std::move(stop))) {
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
        if (handler_->credentials_)
            unsupported("XML HTTP native ICredentials adapter is not yet implemented");
        auto& services = handler_->services;
        auto& request_error = operation.error ? operation.error : services.error;
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(operation.timeout_milliseconds);
        auto selected_proxy = handler_->effective_proxy();
        std::shared_ptr<ConfigXmlHttpProxyRoutePlan> proxy_plan;
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
            if (handler_->proxy_provider_ && route && route->proxy_uri &&
                (*route->proxy_uri->scheme() != u"http" ||
                 (*uri->scheme() != u"http" && *uri->scheme() != u"https")))
                throw ConfigHttpProxyProviderError(
                    "This proxy/target scheme requires its verified CONNECT/TLS/SOCKS transport provider");
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
            auto tls_host = parsed_host ? config_http_tls_host_key(*parsed_host) : utf16(address.host);
            auto fallback_host = httplib::detail::make_default_host_header_value(address.host, address.port,
                                                                                 address.tls, AF_UNSPEC);
            request.opensim_prepare_request_headers = [raw = operation.headers, jar = handler_->jar, uri,
                                                       host_values, parsed_host,
                                                       fallback_host](auto& headers) {
                // Values remain UTF16 until the connected request writer needs their ASCII
                // representation. In particular, TLS failures precede encoding failures.
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
                if (cookie) {
                    auto bytes = config_http_encode_request_header(*cookie);
                    if (!bytes.empty())
                        encoded.emplace("Cookie", std::string(bytes.begin(), bytes.end()));
                }
                headers = std::move(encoded);
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
            // Plain HTTP proxy pools are keyed by the proxy URI, independently of the
            // destination origin. Authenticated routes need their separate
            // identity/transport policies before they can participate in this pool.
            auto pool_proxy_uri = route ? route->proxy_uri : ConfigUriObjectPtr{};
            if (pool_proxy_uri)
                if (auto components = pool_proxy_uri->http_components())
                    pool_proxy_uri = components->source_object();
            const bool pooled_proxy = route && (*uri->scheme() == u"http" || *uri->scheme() == u"https") &&
                                      !route->credentials && !route->default_credentials_used &&
                                      pool_proxy_uri && config_generic_uri_owns(pool_proxy_uri) &&
                                      *pool_proxy_uri->scheme() == u"http";
            if (handler_->pool && (!route || pooled_proxy)) {
                std::string key = (address.tls ? "https:" : "http:") + address.host;
                auto port = address.port;
                if (pooled_proxy) {
                    auto identity = tls_key_bytes(*config_generic_uri_http_pool_identity(pool_proxy_uri));
                    if (address.tls) {
                        key = "tunnel:";
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
            auto host = address.host;
            if (host.find(':') != std::string::npos && host.front() != '[')
                host = '[' + host + ']';
            httplib::Client client(std::string(address.tls ? "https://" : "http://") + host + ":" +
                                   std::to_string(address.port));
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
            if (route)
                client.set_proxy(route->host, route->port);
            if (route && address.tls && route->proxy_uri) {
                if (!route->proxy_uri || *route->proxy_uri->scheme() != u"http" || route->credentials ||
                    route->default_credentials_used)
                    unsupported("HTTPS proxy tunneling requires an unauthenticated HTTP proxy route");
                auto authority = host + ":" + std::to_string(address.port);
                std::string connect = "CONNECT " + authority + " HTTP/1.1\r\nHost: " + authority + "\r\n";
                auto proxy_uri = route->proxy_uri;
                client.opensim_set_proxy_tunnel([connect = std::move(connect), lease, tunnel_agent,
                                                 reader = request.opensim_response_head_reader, proxy_uri,
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
                    std::string wire = connect;
                    if (agent) {
                        auto bytes = config_http_encode_request_header(*agent);
                        wire += "User-Agent: " + std::string(bytes.begin(), bytes.end()) + "\r\n";
                    }
                    wire += "\r\n";
                    stream.set_max_timeout(static_cast<time_t>(remaining));
                    stream.set_read_interrupt(interrupted);
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
                    if (!reader(read, head)) {
                        error = httplib::Error::Read;
                        return false;
                    }
                    // The pinned runtime's resource includes the trailing double quote.
                    if (head.status != 200)
                        throw ConfigXmlHttpError(
                            "System.Net.Http.HttpRequestException",
                            "The proxy tunnel request to proxy '" + utf8(*proxy_uri->to_string()) +
                                "' failed with status code '" + std::to_string(head.status) + "'.\"",
                            0x80131500);
                    response = httplib::Response{};
                    return true;
                });
            }
            if (address.tls) {
#ifdef CPPHTTPLIB_SSL_ENABLED
                auto tls_name = std::make_shared<std::string>(address.host);
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
                if (verify_chain
#if defined(_WIN32) || defined(__linux__)
                    && services.tls_ca_file
#endif
                ) {
                    if (!services.tls_ca_file)
                        unsupported("XML HTTPS requires explicit native certificate trust provider");
                    auto ca = services.tls_ca_file(address);
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
                        address.tls && (response.error == httplib::Error::SSLConnection ||
                                        response.error == httplib::Error::SSLServerVerification ||
                                        response.error == httplib::Error::SSLServerHostnameVerification ||
                                        response.error == httplib::Error::SSLLoadingCerts);
                    auto error =
                        request_error(tls_failure ? "tls" : "headers", static_cast<int>(response.error));
                    if (!error)
                        throw std::invalid_argument("XML HTTP error provider returned no exception");
                    std::rethrow_exception(error);
                }
            } catch (...) {
                auto error = std::current_exception();
                if (proxy_plan && config_xml_http_proxy_retryable(error) && proxy_plan->advance()) {
                    connection_retries = 0;
                    continue;
                }
                throw;
            }
            if (lease)
                lease->connected(response);
            if (handler_->jar)
                for (auto& header : source_head->head.headers)
                    if (httplib::detail::case_ignore::equal(header.name, "Set-Cookie"))
                        handler_->jar->set(uri, std::make_shared<const std::u16string>(header.value));
            auto status = response.response->status;
            std::vector<NiniWriterText> locations;
            for (auto& header : source_head->head.headers)
                if (httplib::detail::case_ignore::equal(header.name, "Location"))
                    locations.push_back(std::make_shared<const std::u16string>(header.value));
            auto create_body = [&]() {
                auto body_services = services;
                if (operation.error)
                    body_services.error = operation.error;
                bool reusable =
                    (!lease || lease->response_headers(source_head->head)) && status != 101 &&
                    !httplib::detail::has_header_token(request.headers, "Connection", "close") &&
                    !httplib::detail::has_header_token(response.response->headers, "Connection", "close") &&
                    (source_head->head.minor_version != 0 ||
                     httplib::detail::has_header_token(response.response->headers, "Connection",
                                                       "keep-alive"));
                return std::make_shared<Body>(std::move(response), std::move(body_services), source_head,
                                              handler_->proxy_provider_,
                                              httplib::detail::case_ignore::equal(operation.method, "HEAD"),
                                              std::move(lease), reusable, operation.body_interrupted);
            };
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
