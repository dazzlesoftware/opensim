#include "config_http_native_pool.hpp"
#include <algorithm>
#include <limits>
#include <bit>
namespace opensim::framework {
struct ConfigHttpNativePool::Bucket {
    struct Idle {
        httplib::ClientImpl::StreamHandle handle;
        Clock::time_point created, returned;
        ConfigHttpKeepAliveState keep_alive;
        std::shared_ptr<ConfigHttpSourceReceiveBuffer> receive;
    };
    std::vector<Idle> idle;
    std::deque<std::shared_ptr<char>> waiters;
    std::size_t associated = 0;
    std::optional<std::u16string> tunnel_user_agent;
};
struct ConfigHttpNativePool::State {
    ConfigXmlHttpPoolSettings settings;
    std::mutex mutex;
    std::condition_variable_any changed;
    bool started = false, closed = false;
    std::size_t pending_revision = 0;
    std::map<std::pair<std::string, std::uint16_t>, std::shared_ptr<Bucket>> buckets;
    explicit State(ConfigXmlHttpPoolSettings s) : settings(s) {
    }
    bool expired(Clock::time_point start, std::chrono::milliseconds duration, Clock::time_point now) const {
        return duration.count() >= 0 && now - start > duration;
    }
    bool keep_alive_expired(const Bucket::Idle& idle, Clock::time_point now) const {
        if (!idle.keep_alive.timeout_seconds)
            return false;
        // Source unchecked Int32 multiplication precedes comparison to Int64 ticks.
        auto milliseconds =
            std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(idle.keep_alive.timeout_seconds) * 1000u);
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - idle.returned).count() >=
               milliseconds;
    }
};
ConfigHttpNativePool::ConfigHttpNativePool(ConfigXmlHttpPoolSettings settings)
    : state_(std::make_shared<State>(settings)) {
    if (settings.max_connections_per_server <= 0)
        throw std::invalid_argument("HTTP pool requires a positive connection limit");
    if (settings.idle_timeout.count() < -1 || settings.lifetime.count() < -1 ||
        settings.connect_timeout.count() < -1 || settings.connect_timeout.count() == 0)
        throw std::invalid_argument(
            "HTTP pool requires nonnegative/infinite lifetimes and a positive/infinite connect timeout");
    scavenger_ = std::jthread([state = state_](std::stop_token stop) {
        std::unique_lock lock(state->mutex);
        state->changed.wait(lock, stop, [&] { return state->started; });
        auto idle = state->settings.idle_timeout;
        auto interval = idle.count() < 0 ? std::chrono::milliseconds(30000)
                                         : std::max(std::chrono::milliseconds(1000), idle / 4);
        while (!stop.stop_requested()) {
            auto until = Clock::now() + interval;
            auto revision = state->pending_revision;
            for (const auto& [key, bucket] : state->buckets)
                for (const auto& connection : bucket->idle)
                    if (connection.receive)
                        if (auto deadline = connection.receive->deadline())
                            until = std::min(until, *deadline);
            state->changed.wait_until(lock, stop, until, [&] { return revision != state->pending_revision; });
            if (stop.stop_requested())
                break;
            auto now = Clock::now();
            for (auto it = state->buckets.begin(); it != state->buckets.end();) {
                auto& bucket = *it->second;
                std::erase_if(bucket.idle, [&](auto& connection) {
                    if (!state->expired(connection.created, state->settings.lifetime, now) &&
                        !state->expired(connection.returned, idle, now) &&
                        !state->keep_alive_expired(connection, now) &&
                        !(connection.receive && connection.receive->expired(now)) &&
                        connection.handle.opensim_idle_connection_usable())
                        return false;
                    --bucket.associated;
                    return true;
                });
                if (!bucket.associated && bucket.waiters.empty())
                    it = state->buckets.erase(it);
                else
                    ++it;
            }
            state->changed.notify_all();
        }
    });
}
ConfigHttpNativePool::~ConfigHttpNativePool() {
    std::unique_lock lock(state_->mutex);
    state_->closed = true;
    for (auto& [key, bucket] : state_->buckets) {
        bucket->associated -= bucket->idle.size();
        bucket->idle.clear();
    }
    lock.unlock();
    scavenger_.request_stop();
    state_->changed.notify_all();
}
ConfigHttpNativePool::Lease::Lease(std::shared_ptr<State> state, std::shared_ptr<Bucket> bucket)
    : state_(std::move(state)), bucket_(std::move(bucket)), created_(Clock::now()) {
}
ConfigHttpNativePool::Lease::~Lease() {
    finish({}, false);
}
std::optional<std::u16string> ConfigHttpNativePool::Lease::tunnel_user_agent() const {
    std::lock_guard lock(state_->mutex);
    return bucket_->tunnel_user_agent;
}
void ConfigHttpNativePool::Lease::adopt(httplib::Client& client) {
    if (idle_.is_valid()) {
        if (!client.opensim_adopt_stream(std::move(idle_)))
            throw std::logic_error("HTTP pool could not adopt its idle socket");
        // PrepareForReuse rejects both EOF and unsolicited response bytes. A stale
        // socket is closed before the next request writes any bytes.
        if ((receive_ && receive_->expired(Clock::now())) || !client.opensim_idle_socket_usable()) {
            client.stop();
            receive_.reset();
        }
        idle_ = {};
    }
}
void ConfigHttpNativePool::Lease::connected(const httplib::ClientImpl::StreamHandle& handle) {
    if (!handle.opensim_reused_connection) {
        created_ = handle.opensim_connection_created;
        keep_alive_ = {};
    }
}
std::shared_ptr<ConfigHttpSourceReceiveBuffer> ConfigHttpNativePool::Lease::source_receive_buffer() {
    if (!receive_)
        receive_ = std::make_shared<ConfigHttpSourceReceiveBuffer>();
    return receive_;
}
bool ConfigHttpNativePool::Lease::response_headers(const ConfigHttpResponseHead& head) {
    for (auto& header : head.headers)
        if (httplib::detail::case_ignore::equal(header.name, "Keep-Alive"))
            config_http_apply_keep_alive(keep_alive_, header.value);
    return !keep_alive_.close;
}
void ConfigHttpNativePool::Lease::finish(httplib::ClientImpl::StreamHandle&& handle, bool reusable) noexcept {
    if (returned_)
        return;
    returned_ = true;
    std::unique_lock lock(state_->mutex);
    bool keep = reusable && handle.is_valid() && !state_->closed &&
                !state_->expired(created_, state_->settings.lifetime, Clock::now()) &&
                state_->settings.lifetime.count() != 0;
    if (keep) {
        try {
            bucket_->idle.push_back({std::move(handle), created_, Clock::now(), keep_alive_, receive_});
            if (receive_ && receive_->deadline())
                ++state_->pending_revision;
        } catch (...) {
            keep = false;
        }
    }
    if (!keep) {
        // Close the physical transport before releasing its capacity to a waiter.
        // Socket/TLS disposal must not hold the handler's pool lock.
        lock.unlock();
        handle = {};
        idle_ = {};
        lock.lock();
        --bucket_->associated;
    }
    lock.unlock();
    state_->changed.notify_all();
}
std::shared_ptr<ConfigHttpNativePool::Lease>
ConfigHttpNativePool::acquire(const std::string& host, std::uint16_t port, Clock::time_point deadline,
                              std::function<bool()> interrupted,
                              std::optional<std::u16string> tunnel_user_agent) {
    auto state = state_;
    std::unique_lock lock(state->mutex);
    if (!state->started) {
        state->started = true;
        state->changed.notify_all();
    }
    std::shared_ptr<Bucket> bucket;
    if (state->settings.max_connections_per_server == std::numeric_limits<std::int32_t>::max() &&
        (state->settings.idle_timeout.count() == 0 || state->settings.lifetime.count() == 0))
        bucket = std::make_shared<Bucket>();
    else {
        auto& item = state->buckets[{host, port}];
        if (!item)
            item = std::make_shared<Bucket>();
        bucket = item;
    }
    if (tunnel_user_agent)
        bucket->tunnel_user_agent = std::move(tunnel_user_agent);
    auto ticket = std::make_shared<char>();
    bucket->waiters.push_back(ticket);
    try {
        for (;;) {
            auto now = Clock::now();
            if (now >= deadline || (interrupted && interrupted())) {
                std::erase(bucket->waiters, ticket);
                lock.unlock();
                state->changed.notify_all();
                return {};
            }
            if (bucket->waiters.front() == ticket) {
                // LIFO reuse matches SocketsHttpHandler's available HTTP/1 stack.
                while (!bucket->idle.empty()) {
                    auto& idle = bucket->idle.back();
                    if (!state->expired(idle.created, state->settings.lifetime, now) &&
                        !state->keep_alive_expired(idle, now) &&
                        !(idle.receive && idle.receive->expired(now)))
                        break;
                    auto stale = std::move(idle);
                    bucket->idle.pop_back();
                    --bucket->associated;
                    lock.unlock();
                    stale.handle = {};
                    lock.lock();
                    now = Clock::now();
                }
                if (!bucket->idle.empty() ||
                    bucket->associated <
                        static_cast<std::size_t>(state->settings.max_connections_per_server)) {
                    auto lease = std::shared_ptr<Lease>(new Lease(state, bucket));
                    if (!bucket->idle.empty()) {
                        auto idle = std::move(bucket->idle.back());
                        bucket->idle.pop_back();
                        lease->idle_ = std::move(idle.handle);
                        lease->created_ = idle.created;
                        lease->keep_alive_ = idle.keep_alive;
                        lease->receive_ = std::move(idle.receive);
                    } else
                        ++bucket->associated;
                    lease->returned_ = false;
                    bucket->waiters.pop_front();
                    lock.unlock();
                    state->changed.notify_all();
                    return lease;
                }
            }
            state->changed.wait_until(
                lock,
                interrupted ? std::min(deadline, Clock::now() + std::chrono::milliseconds(10)) : deadline);
        }
    } catch (...) {
        std::erase(bucket->waiters, ticket);
        lock.unlock();
        state->changed.notify_all();
        throw;
    }
}
}
