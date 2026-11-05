// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/osd_service_http.hpp"
#include "opensim/network/web_http_clients.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/config_xml_typed_http_services.hpp"
#include "opensim/framework/config_http_headers.hpp"
#include "opensim/framework/config_http_cookies_native.hpp"
#include "opensim/framework/nini_streams.hpp"
#include "opensim/framework/nini_xml_config.hpp"
#include "opensim/framework/invariant_number_parse.hpp"
#include "opensim/framework/text.hpp"
#include <mutex>
#include <array>
#include <algorithm>
#include <thread>
#include <condition_variable>
namespace opensim::network {
namespace {
using Text = std::u16string;
using Nullable = std::optional<Text>;
class Request final : public OsdServiceHttpRequest {
  public:
    framework::ConfigUriObjectPtr uri;
    bool closed = false;
    void close() override {
        closed = true;
    }
};
Text timeout_message(std::int32_t milliseconds) {
    auto seconds = std::to_string(milliseconds / 1000);
    auto fraction = milliseconds % 1000;
    if (fraction) {
        auto digits = std::to_string(fraction + 1000).substr(1);
        while (digits.back() == '0')
            digits.pop_back();
        seconds += '.' + digits;
    }
    return u"The request was canceled due to the configured HttpClient.Timeout of " +
           framework::utf16(seconds) + u" seconds elapsing.";
}
class LiveStream final : public OsdServiceStream, public framework::NiniByteStream {
    std::shared_ptr<framework::ConfigXmlStream> raw_;
    std::shared_ptr<std::atomic<bool>> interrupted_;
    std::mutex read_mutex_;
    std::atomic<bool> disposing_{false};
    std::optional<std::uint64_t> remaining_;
    std::atomic<std::uint64_t> remaining_snapshot_;
    std::atomic<bool> eof_{false};
    bool can_drain_;

  public:
    LiveStream(std::shared_ptr<framework::ConfigXmlStream> raw, std::shared_ptr<std::atomic<bool>> stop,
               std::optional<std::uint64_t> length, bool drain)
        : raw_(std::move(raw)), interrupted_(std::move(stop)), remaining_(length),
          remaining_snapshot_(length.value_or(0)), eof_(length && !*length), can_drain_(drain) {
    }
    ~LiveStream() {
        try {
            close();
        } catch (...) {
        }
    }
    bool can_read() override {
        return !interrupted_->load();
    }
    bool can_write() override {
        return false;
    }
    bool can_seek() override {
        return false;
    }
    std::int64_t position() override {
        throw std::logic_error("HTTP body is not seekable");
    }
    std::size_t read(std::span<std::uint8_t> bytes, std::size_t offset, std::size_t count) override {
        std::lock_guard lock(read_mutex_);
        if (!raw_ || (remaining_ && !*remaining_))
            return 0;
        std::size_t n;
        try {
            n = raw_->read(bytes.subspan(offset, count));
        } catch (const framework::ConfigHttpHeaderError&) {
            throw;
        } catch (const framework::ConfigUriComponentsError&) {
            throw;
        } catch (...) {
#ifdef _WIN32
            if (interrupted_->load())
                throw OsdServiceError(
                    "System.IO.IOException",
                    u"Unable to read data from the transport connection: An established connection was aborted by the software in your host machine..");
#elif defined(__linux__)
            if (interrupted_->load())
                throw OsdServiceError(
                    "System.IO.IOException",
                    u"Unable to read data from the transport connection: Software caused connection abort.");
#endif
            throw;
        }
        if (remaining_) {
            *remaining_ -= std::min<std::uint64_t>(*remaining_, n);
            remaining_snapshot_.store(*remaining_);
        }
        if (!n || (remaining_ && !*remaining_))
            eof_.store(true);
        return n;
    }
    void write(std::span<const std::uint8_t>) override {
        throw std::logic_error("HTTP response is read-only");
    }
    void flush() override {
    }
    void close() override {
        if (disposing_.exchange(true))
            return;
        // HttpContentReadStream.Dispose attempts a bounded drain. Closing it is not
        // an unconditional cancellation of an already running synchronous Read.
        bool drain = can_drain_ && !eof_.load() && (!remaining_ || remaining_snapshot_.load() <= 1048576);
        std::mutex timer_mutex;
        std::condition_variable_any done;
        std::jthread timer;
        if (drain)
            timer = std::jthread([&](std::stop_token stop) {
                std::unique_lock lock(timer_mutex);
                done.wait_for(lock, stop, std::chrono::seconds(2), [] { return false; });
                if (!stop.stop_requested())
                    interrupted_->store(true);
            });
        else
            interrupted_->store(true);
        {
            std::lock_guard lock(read_mutex_);
            if (raw_) {
                if (drain && !interrupted_->load() && (!remaining_ || *remaining_ <= 1048576)) {
                    std::array<std::uint8_t, 4096> buffer;
                    std::uint64_t spent = 0;
                    try {
                        while (!interrupted_->load() && spent < 1048576 && (!remaining_ || *remaining_)) {
                            auto n = raw_->read(buffer);
                            if (!n)
                                break;
                            spent += n;
                            if (remaining_)
                                *remaining_ -=
                                    std::min<std::uint64_t>(*remaining_, static_cast<std::uint64_t>(n));
                        }
                    } catch (...) {
                    }
                }
                interrupted_->store(true);
                raw_->close();
                raw_.reset();
            }
        }
        timer.request_stop();
        done.notify_all();
        if (timer.joinable())
            timer.join();
    }
};
class Reader final : public OsdServiceReader {
    framework::NiniByteTextReader reader_;

  public:
    explicit Reader(std::shared_ptr<LiveStream> stream) : reader_(std::move(stream)) {
    }
    Text read_to_end() override {
        Text text;
        std::array<char16_t, 4096> buffer;
        while (auto n = reader_.read_chars(buffer))
            text.append(buffer.data(), static_cast<std::size_t>(n));
        return text;
    }
    void close() override {
        reader_.close();
    }
};
class Cancellation final : public OsdServiceCancellation {
    std::mutex mutex_;
    std::condition_variable changed_;
    bool closed_ = false, fired_ = false;
    std::function<void()> callback_;
    std::thread thread_;

  public:
    explicit Cancellation(std::int32_t delay) {
        if (delay < -1)
            throw OsdServiceError(
                "System.ArgumentOutOfRangeException",
                u"Specified argument was out of the range of valid values. (Parameter 'millisecondsDelay')");
        if (delay != -1)
            thread_ = std::thread([this, delay] {
                std::function<void()> callback;
                {
                    std::unique_lock lock(mutex_);
                    if (changed_.wait_for(lock, std::chrono::milliseconds(delay), [this] { return closed_; }))
                        return;
                    fired_ = true;
                    callback = callback_;
                }
                if (callback)
                    callback();
            });
    }
    ~Cancellation() {
        close();
    }
    void on_cancel(std::function<void()> callback) override {
        bool fired;
        {
            std::lock_guard lock(mutex_);
            callback_ = callback;
            fired = fired_;
        }
        if (fired)
            callback();
    }
    void close() override {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        changed_.notify_all();
        if (thread_.joinable())
            thread_.join();
    }
};
class Response final : public OsdServiceResponse {
    framework::ConfigHttpResponseHead head_;
    std::shared_ptr<LiveStream> stream_;

  public:
    Response(framework::ConfigHttpResponseHead head, std::shared_ptr<LiveStream> stream)
        : head_(std::move(head)), stream_(std::move(stream)) {
    }
    void ensure_success() override {
        if (head_.status >= 200 && head_.status < 300)
            return;
        auto message = u"Response status code does not indicate success: " +
                       framework::utf16(std::to_string(head_.status));
        if (!std::all_of(head_.reason.begin(), head_.reason.end(), framework::unicode_whitespace))
            message += u" (" + head_.reason + u")";
        throw OsdServiceError("System.Net.Http.HttpRequestException", message + u".", head_.status);
    }
    std::shared_ptr<OsdServiceStream> stream() override {
        return stream_;
    }
    std::optional<std::int64_t> content_length() override {
        std::vector<Text> fields;
        for (auto& h : head_.headers) {
            auto key = h.name;
            std::transform(key.begin(), key.end(), key.begin(),
                           [](char c) { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; });
            if (key == "content-length")
                fields.push_back(h.value);
        }
        return framework::config_http_content_length(fields);
    }
    void close() override {
        stream_->close();
    }
};
OsdServiceException describe(std::exception_ptr error) {
    try {
        std::rethrow_exception(error);
    } catch (const OsdServiceError& e) {
        return {e.message, e.source_type == "System.Net.Http.HttpRequestException", e.status};
    } catch (const framework::ConfigXmlHttpError& e) {
        return {e.message, e.source_type == "System.Net.Http.HttpRequestException", e.status};
    } catch (const framework::ConfigUriComponentsError& e) {
        return {e.message, e.source_type == "System.Net.Http.HttpRequestException", {}};
    } catch (const framework::ConfigHttpHeaderError& e) {
        return {e.message, e.source_type == "System.Net.Http.HttpRequestException", {}};
    } catch (const framework::ConfigXmlStreamError& e) {
        return {e.message, false, {}};
    } catch (const framework::NiniStreamError& e) {
        return {e.message, false, {}};
    } catch (const framework::NiniXmlProviderError& e) {
        return {e.message, false, {}};
    } catch (const framework::InvariantNumberError& e) {
        return {e.message, false, {}};
    } catch (const std::exception& e) {
        return {framework::utf16_replacing(e.what()), false, {}};
    }
}
bool method_char(char16_t c) {
    return (c >= u'0' && c <= u'9') || (c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z') ||
           std::u16string_view(u"!#$%&'*+-.^_`|~").find(c) != std::u16string_view::npos;
}
}
struct OsdServiceNativeHttp::State {
    std::shared_ptr<framework::ConfigUriObjectBackend> uris = framework::native_config_uri_object_backend();
    framework::ConfigXmlUriObjects objects;
    framework::ConfigXmlHttpRequester send;
    std::function<std::exception_ptr(std::string, int)> error;
    std::shared_ptr<WebHttpClients> clients;
    explicit State(framework::ConfigXmlNativeHttpServices services) : error(services.error) {
        if (!services.connection_pool)
            services.connection_pool = framework::ConfigXmlHttpPoolSettings{};
        auto bound = framework::config_xml_bind_typed_http(std::move(services), objects, *uris, [] {
            return framework::native_invariant_config_http_cookies_backend();
        });
        send = framework::config_xml_native_http_requester(std::move(bound), false);
    }
    State(std::shared_ptr<WebHttpClients> shared, std::function<std::exception_ptr(std::string, int)> errors)
        : error(std::move(errors)), clients(std::move(shared)) {
        if (!clients || !error)
            throw std::invalid_argument(
                "OSD shared HTTP requires global clients and native failure translation");
    }
};
namespace {
class Client final : public OsdServiceHttpClient {
    framework::ConfigXmlHttpRequester send_;
    framework::ConfigXmlUriObjects objects_;
    std::function<std::exception_ptr(std::string, int)> error_;
    int timeout_;
    bool closed_ = false;
    std::shared_ptr<WebHttpClient> global_client_;

  public:
    Client(framework::ConfigXmlHttpRequester send, framework::ConfigXmlUriObjects objects,
           std::function<std::exception_ptr(std::string, int)> error, int timeout,
           std::shared_ptr<WebHttpClient> global = {})
        : send_(std::move(send)), objects_(std::move(objects)), error_(std::move(error)),
          timeout_(timeout > 0 ? timeout : 30000), global_client_(std::move(global)) {
    }
    std::shared_ptr<OsdServiceResponse> send(const std::shared_ptr<OsdServiceHttpRequest>& input) override {
        if (closed_) {
#ifdef _WIN32
            throw OsdServiceError(
                "System.ObjectDisposedException",
                u"Cannot access a disposed object.\r\nObject name: 'System.Net.Http.HttpClient'.");
#else
            throw OsdServiceError(
                "System.ObjectDisposedException",
                u"Cannot access a disposed object.\nObject name: 'System.Net.Http.HttpClient'.");
#endif
        }
        auto request = std::dynamic_pointer_cast<Request>(input);
        if (!request)
            throw std::invalid_argument("OSD client requires matching native request");
        if (!request->uri || !request->uri->is_absolute())
            throw OsdServiceError(
                "System.InvalidOperationException",
                u"An invalid request URI was provided. Either the request URI must be an absolute URI or BaseAddress must be set.");
        auto scheme = *request->uri->scheme();
        if (scheme != u"http" && scheme != u"https")
            throw OsdServiceError("System.NotSupportedException",
                                  u"The '" + scheme + u"' scheme is not supported.");
        auto interrupted = std::make_shared<std::atomic<bool>>(false);
        framework::ConfigHttpResponseHead head;
        framework::ConfigXmlHttpOperation operation;
        operation.method = framework::utf8(request->method.value());
        operation.body = request->body.value_or("");
        operation.timeout_milliseconds = timeout_;
        operation.redirect_limit = 10;
        operation.ensure_success = false;
        auto content_header = [](const std::string& name) {
            return name == "Content-Type" || name == "Content-Length";
        };
        for (auto& [name, value] : request->headers)
            if (!content_header(name))
                operation.headers.emplace_back(name, framework::nini_writer_text(value));
        for (auto& [name, value] : request->headers)
            if (content_header(name))
                operation.headers.emplace_back(name, framework::nini_writer_text(value));
        auto normalized = operation.method;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                       [](char c) { return c >= 'a' && c <= 'z' ? char(c - 32) : c; });
        if (!request->body && normalized != "GET" && normalized != "HEAD" && normalized != "DELETE" &&
            normalized != "OPTIONS" && normalized != "TRACE" && normalized != "CONNECT")
            operation.headers.emplace_back("Content-Length", framework::nini_writer_text(u"0"));
        operation.response_headers = [&](const auto& h) { head = h; };
        operation.body_interrupted = [interrupted] { return interrupted->load(); };
        auto start = std::chrono::steady_clock::now();
        operation.error = [start, timeout = timeout_, fallback = error_](std::string phase, int code) {
            if ((phase == "timeout" || phase == "headers") &&
                std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(timeout))
                return std::make_exception_ptr(OsdServiceError("System.Threading.Tasks.TaskCanceledException",
                                                               timeout_message(timeout)));
            return fallback(std::move(phase), code);
        };
        auto raw = global_client_ ? global_client_->send_headers(request->uri, operation)
                                  : send_(objects_.wrap(request->uri), operation);
        std::vector<Text> lengths, encodings;
        for (auto& h : head.headers) {
            auto key = h.name;
            std::transform(key.begin(), key.end(), key.begin(),
                           [](char c) { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; });
            if (key == "content-length")
                lengths.push_back(h.value);
            else if (key == "transfer-encoding")
                encodings.push_back(h.value);
        }
        bool chunked = framework::config_http_transfer_encoding_chunked(encodings);
        auto length = framework::config_http_content_length(lengths);
        bool empty = normalized == "HEAD" || head.status == 204 || head.status == 304;
        std::optional<std::uint64_t> remaining =
            empty ? std::optional<std::uint64_t>(0)
                  : (!chunked && length ? std::optional<std::uint64_t>(static_cast<std::uint64_t>(*length))
                                        : std::nullopt);
        auto stream = std::make_shared<LiveStream>(std::move(raw), std::move(interrupted), remaining,
                                                   !empty && (chunked || bool(length)));
        return std::make_shared<Response>(std::move(head), std::move(stream));
    }
    void close() override {
        closed_ = true;
        if (global_client_)
            global_client_->close();
    }
};
}
OsdServiceNativeHttp::OsdServiceNativeHttp(framework::ConfigXmlNativeHttpServices services)
    : state_(std::make_shared<State>(std::move(services))) {
}
OsdServiceNativeHttp::OsdServiceNativeHttp(std::shared_ptr<WebHttpClients> clients,
                                           std::function<std::exception_ptr(std::string, int)> errors)
    : state_(std::make_shared<State>(std::move(clients), std::move(errors))) {
}
void OsdServiceNativeHttp::bind(OsdServiceServices& services) const {
    auto state = state_;
    services.client = [state](int timeout) {
        auto shared = state->clients ? state->clients->get_new_global_client(timeout)
                                     : std::shared_ptr<WebHttpClient>{};
        return std::make_shared<Client>(state->send, state->objects, state->error, timeout,
                                        std::move(shared));
    };
    services.request = [state](const Nullable& method,
                               const Nullable& url) -> std::shared_ptr<OsdServiceHttpRequest> {
        if (!method)
            throw OsdServiceError("System.ArgumentNullException",
                                  u"Value cannot be null. (Parameter 'method')");
        if (method->empty() || std::all_of(method->begin(), method->end(), framework::unicode_whitespace))
            throw OsdServiceError(
                "System.ArgumentException",
                u"The value cannot be an empty string or composed entirely of whitespace. (Parameter 'method')");
        if (!std::all_of(method->begin(), method->end(), method_char))
            throw OsdServiceError("System.FormatException", u"The format of the HTTP method is invalid.");
        auto request = std::make_shared<Request>();
        request->method = method;
        request->url = url;
        if (url && !url->empty())
            request->uri = state->uris->create(std::make_shared<const Text>(*url),
                                               framework::ConfigUriObjectKind::RelativeOrAbsolute);
        return request;
    };
    services.reader = [](const auto& stream) -> std::shared_ptr<OsdServiceReader> {
        auto native = std::dynamic_pointer_cast<LiveStream>(stream);
        if (!native)
            throw std::invalid_argument("OSD reader requires native HTTP stream");
        return std::make_shared<Reader>(std::move(native));
    };
    services.cancellation = [](int delay) { return std::make_shared<Cancellation>(delay); };
    services.describe = describe;
}
}
