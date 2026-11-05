// Consumed runtime v8.0.31 System.Net.Http.X509ResourceClient.
// .NET Foundation MIT license: third_party/dotnet_collation/LICENSE.
#include "opensim/framework/config_http_x509_resource_client.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/config_http_location.hpp"
#include "opensim/framework/config_http_headers.hpp"
#include "opensim/framework/config_http_cookies_native.hpp"
#include "opensim/framework/config_xml_typed_http_services.hpp"
#include "opensim/framework/config_xml_native_proxy.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace opensim::framework {
namespace {
using Clock = std::chrono::steady_clock;
struct BufferGate {
    std::atomic<bool> enabled = false, disposed = false;
    std::atomic<std::uint64_t> remaining = 0;
    bool chunked = false, framed = false;
    Clock::time_point deadline;
    bool interrupt() const {
        if (!enabled.load() || Clock::now() < deadline)
            return false;
        // Synchronous content serialization ignores its token while reading. Dispose
        // starts the source bounded drain; it does not abort a small framed read.
        return !framed || (!chunked && remaining.load() > 1048576) ||
               Clock::now() >= deadline + std::chrono::seconds(2);
    }
};
struct BufferCancellation {
    std::shared_ptr<BufferGate> gate;
    std::mutex mutex;
    std::condition_variable_any changed;
    std::jthread worker;
    BufferCancellation(std::shared_ptr<BufferGate> value, std::shared_ptr<ConfigXmlStream> stream)
        : gate(std::move(value)) {
        gate->enabled.store(true);
        worker = std::jthread([this, stream = std::move(stream)](std::stop_token token) {
            std::unique_lock lock(mutex);
            changed.wait_until(lock, token, gate->deadline, [] { return false; });
            if (token.stop_requested())
                return;
            gate->disposed.store(true);
            lock.unlock();
            stream->cancel_buffered_copy();
        });
    }
    void stop() {
        worker.request_stop();
        changed.notify_all();
        if (worker.joinable())
            worker.join();
        gate->enabled.store(false);
    }
    ~BufferCancellation() {
        stop();
    }
};
}
struct ConfigHttpX509ResourceClient::State {
    ConfigXmlNativeHttpServices services;
    std::shared_ptr<ConfigUriObjectBackend> uris;
    ConfigXmlUriObjects objects;
    ConfigXmlHttpRequester sender;
    std::once_flag started;
    std::int64_t maximum;
    explicit State(ConfigXmlNativeHttpServices policy, std::int64_t limit)
        : services(std::move(policy)), maximum(limit) {
        if (limit > 0 && limit <= INT_MAX)
            uris = native_config_uri_object_backend();
    }
    void start() {
        std::call_once(started, [this] {
            services.connection_pool =
                ConfigXmlHttpPoolSettings{INT_MAX, std::chrono::milliseconds(15000),
                                          std::chrono::milliseconds(-1), std::chrono::milliseconds(-1)};
            if (!services.proxy && !services.proxy_plan && !services.proxy_provider)
                services = config_xml_bind_native_proxy(std::move(services), objects);
            auto bound = config_xml_bind_typed_http(
                services, objects, *uris, [] { return native_invariant_config_http_cookies_backend(); });
            sender = config_xml_native_http_requester(std::move(bound), true);
        });
    }
};
ConfigHttpX509ResourceClient::ConfigHttpX509ResourceClient(ConfigXmlNativeHttpServices services,
                                                           std::int64_t maximum) {
    try {
        state_ = std::make_shared<State>(std::move(services), maximum);
    } catch (...) {
    }
}
std::optional<std::vector<std::uint8_t>>
ConfigHttpX509ResourceClient::download_asset(std::optional<std::u16string_view> text,
                                             std::int64_t ticks) const {
    if (!state_ || !state_->uris || ticks <= 0)
        return {};
    using Clock = std::chrono::steady_clock;
    const auto milliseconds = static_cast<std::int64_t>(static_cast<double>(ticks) / 10000.0);
    const auto start = Clock::now();
    std::optional<Clock::time_point> asset_deadline;
    if (milliseconds <= INT_MAX)
        asset_deadline = start + std::chrono::milliseconds(milliseconds);
    auto zero_cancel = std::make_shared<std::atomic<bool>>(false);
    std::jthread zero_timer;
    try {
        if (milliseconds == 0)
            zero_timer = std::jthread([zero_cancel] { zero_cancel->store(true); });
        if (!text)
            return {};
        auto& state = *state_;
        auto uri =
            state.uris->create(std::make_shared<const std::u16string>(*text), ConfigUriObjectKind::Absolute);
        if (*uri->scheme() != u"http")
            return {};
        state.start();
        ConfigHttpLocationAdapter locations(*state.uris);
        int redirects = 0;
        for (;;) {
            const auto request_deadline = Clock::now() + std::chrono::seconds(100);
            auto stop = [asset_deadline, request_deadline, zero_cancel, milliseconds] {
                return zero_cancel->load() || Clock::now() >= request_deadline ||
                       (milliseconds > 0 && asset_deadline && Clock::now() >= *asset_deadline);
            };
            auto gate = std::make_shared<BufferGate>();
            gate->deadline = asset_deadline ? std::min(request_deadline, *asset_deadline) : request_deadline;
            ConfigXmlHttpOperation operation;
            operation.ensure_success = false;
            operation.allow_auto_redirect = false;
            operation.source_synchronous_buffering = true;
            operation.request_interrupted = stop;
            operation.body_interrupted = [gate] { return gate->interrupt(); };
            if (milliseconds > 0 && asset_deadline) {
                auto remaining =
                    std::chrono::duration_cast<std::chrono::milliseconds>(*asset_deadline - Clock::now())
                        .count();
                operation.timeout_milliseconds =
                    static_cast<std::int32_t>(std::clamp<std::int64_t>(remaining, 1, 100000));
            }
            ConfigHttpResponseHead head;
            operation.response_headers = [&head](const auto& value) { head = value; };
            auto stream = state.sender(state.objects.wrap(uri), operation);
            struct Close {
                std::shared_ptr<ConfigXmlStream> stream;
                ~Close() {
                    try {
                        stream->close();
                    } catch (...) {
                    }
                }
            } close{stream};
            std::vector<std::u16string> lengths, encodings;
            std::vector<ConfigUriText> redirect_values;
            for (const auto& header : head.headers) {
                auto name = header.name;
                for (auto& c : name)
                    if (c >= 'A' && c <= 'Z')
                        c = static_cast<char>(c + 32);
                if (name == "content-length")
                    lengths.push_back(header.value);
                if (name == "transfer-encoding")
                    encodings.push_back(header.value);
                if (name == "location")
                    redirect_values.push_back(std::make_shared<const std::u16string>(header.value));
            }
            auto length = config_http_content_length(lengths);
            if (length && *length > state.maximum)
                return {};
            gate->chunked = config_http_transfer_encoding_chunked(encodings);
            gate->framed = gate->chunked || length.has_value();
            gate->remaining.store(static_cast<std::uint64_t>(length.value_or(0)));
            std::vector<std::uint8_t> result;
            std::array<std::uint8_t, 8192> buffer;
            BufferCancellation cancellation(gate, stream);
            while (auto count = stream->read(buffer)) {
                if (count < 0 || static_cast<std::size_t>(count) > buffer.size())
                    throw std::logic_error("Invalid native HTTP read count");
                if (static_cast<std::uint64_t>(count) >
                    static_cast<std::uint64_t>(state.maximum) - result.size())
                    return {};
                result.insert(result.end(), buffer.begin(), buffer.begin() + count);
                if (length)
                    gate->remaining.fetch_sub(
                        std::min<std::uint64_t>(gate->remaining.load(), static_cast<std::uint64_t>(count)));
            }
            cancellation.stop();
            if (gate->disposed.load())
                return {};
            // The source buffers the full response before checking even redirect status.
            const auto status = static_cast<unsigned>(head.status);
            const bool redirect = status - 300u <= 3u || status - 307u <= 1u;
            if (!redirect)
                return result;
            auto next = locations.redirect(uri, head.status, redirect_values);
            if (!next || *next->scheme() != u"http" || ++redirects > 10)
                return {};
            uri = std::move(next);
        }
    } catch (...) {
        return {};
    }
}
}
