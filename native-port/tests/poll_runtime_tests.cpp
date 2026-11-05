#include "opensim/server/poll_runtime.hpp"
#include <condition_variable>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>
using namespace opensim;
using namespace server;
using namespace std::chrono_literals;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void eventually(F condition, const char* message) {
    const auto end = std::chrono::steady_clock::now() + 5s;
    while (!condition()) {
        if (std::chrono::steady_clock::now() > end)
            throw std::runtime_error(message);
        std::this_thread::sleep_for(1ms);
    }
}
struct Channel {
    std::atomic<int> sent = 0, disposed = 0, status = 200;
    std::atomic<bool> keep = true;
    std::atomic<int64_t> length = -1;
};
struct Body : PollBody {
    std::shared_ptr<Channel> c;
    explicit Body(std::shared_ptr<Channel> x) : c(std::move(x)) {
    }
    bool can_read() const override {
        return true;
    }
    void dispose() override {
        ++c->disposed;
    }
};
struct Context : PollContext {
    std::atomic<bool> can = true, sending = false;
    std::atomic<int> sending_checks = 0;
    bool can_send() const override {
        return can.load();
    }
    bool is_sending() const override {
        const_cast<Context*>(this)->sending_checks++;
        return sending.load();
    }
};
struct Incoming : PollIncomingRequest {
    std::shared_ptr<Body> b;
    std::shared_ptr<Context> c = std::make_shared<Context>();
    explicit Incoming(std::shared_ptr<Channel> x) : b(std::make_shared<Body>(std::move(x))) {
    }
    std::shared_ptr<PollBody> body() const override {
        return b;
    }
    std::shared_ptr<PollContext> context() const override {
        return c;
    }
};
struct Response : PollResponse {
    std::shared_ptr<Channel> c;
    explicit Response(std::shared_ptr<Channel> x) : c(std::move(x)) {
    }
    void status_code(int32_t x) override {
        c->status = x;
    }
    void content_length(int64_t x) override {
        c->length = x;
    }
    void utf8_encoding() override {
    }
    void keep_alive(bool x) override {
        c->keep = x;
    }
    void keep_alive_timeout(int32_t) override {
    }
    void protocol_version(PollName) override {
    }
    void status_description(PollName) override {
    }
    void add_header(const std::string&, PollName) override {
    }
    void redirect(PollName, int32_t x) override {
        c->status = x;
    }
    void buffer_start(int32_t) override {
    }
    void buffer_length(int32_t) override {
    }
    void buffer(PollBytes) override {
    }
    void send() override {
        ++c->sent;
    }
};
struct RequestBackend : PollRequestBackend {
    std::atomic<int> warnings = 0;
    PollResponsePtr make_response(const std::shared_ptr<PollIncomingRequest>& request) override {
        return std::make_shared<Response>(std::dynamic_pointer_cast<Incoming>(request)->b->c);
    }
    void warn(const std::string&, const std::exception&) override {
        ++warnings;
    }
};
struct Fixture {
    std::shared_ptr<Channel> channel = std::make_shared<Channel>();
    std::shared_ptr<Incoming> incoming = std::make_shared<Incoming>(channel);
    std::shared_ptr<services::CapabilityPollHandler> args =
        std::make_shared<services::CapabilityPollHandler>();
    std::shared_ptr<PollServiceHttpRequest> request;
    std::atomic<int> has = 0, no = 0, drops = 0;
    std::atomic<bool> ready = false;
    explicit Fixture(RequestBackend& backend) {
        args->timeout_ms = 60000;
        args->has_events = [this](auto, auto) {
            ++has;
            return ready.load();
        };
        args->get_events = [this](auto, auto) {
            auto data = std::make_shared<PollTable>();
            data->set("h", PollResponsePtr(std::make_shared<Response>(channel)));
            return data;
        };
        args->no_events = [this](auto, auto) {
            ++no;
            auto data = std::make_shared<PollTable>();
            data->set("h", PollResponsePtr(std::make_shared<Response>(channel)));
            return data;
        };
        args->drop = [this](auto, auto) { ++drops; };
        request = std::make_shared<PollServiceHttpRequest>(args, incoming, backend);
    }
};
}
int main() {
    try {
        std::atomic<int> errors = 0, watchdogs = 0;
        PollRuntimeBackend runtime([&](const std::exception&) { ++errors; }, [&] { ++watchdogs; });
        RequestBackend responses;
        // More pending requests than workers must leave workers free for ready work.
        std::vector<std::unique_ptr<Fixture>> pending;
        for (int i = 0; i < 64; ++i)
            pending.push_back(std::make_unique<Fixture>(responses));
        Fixture immediate(responses);
        immediate.ready = true;
        {
            PollServiceRequestManager manager(false, 2, 0, runtime);
            manager.start();
            for (auto& f : pending)
                manager.enqueue(f->request);
            eventually(
                [&] {
                    for (auto& f : pending)
                        if (f->has == 0)
                            return false;
                    return true;
                },
                "Pending requests did not reach retry queue");
            manager.enqueue(immediate.request);
            eventually([&] { return int32_t(immediate.args->requests_handled) == 1; },
                       "Pending polls exhausted workers");
            for (auto& f : pending)
                f->ready = true;
            eventually(
                [&] {
                    for (auto& f : pending)
                        if (int32_t(f->args->requests_handled) != 1)
                            return false;
                    return true;
                },
                "Retried events were not delivered");
            manager.stop();
        }
        for (auto& f : pending)
            check(f->channel->sent == 1 && f->channel->disposed == 1 && f->no == 0,
                  "Retry duplicated response or body disposal");
        check(watchdogs > 0, "Retry watcher did not run");
        Fixture dead(responses), busy(responses), timeout(responses);
        dead.incoming->c->can = false;
        busy.incoming->c->sending = true;
        busy.ready = true;
        timeout.args->timeout_ms = -1;
        {
            PollServiceRequestManager manager(true, 2, 123, runtime);
            manager.start();
            manager.enqueue(dead.request);
            manager.enqueue(busy.request);
            manager.enqueue(timeout.request);
            eventually(
                [&] {
                    return dead.drops == 1 && busy.incoming->c->sending_checks > 0 &&
                           int32_t(timeout.args->requests_handled) == 1;
                },
                "Drop/busy/timeout processing failed");
            check(busy.channel->sent == 0, "Busy context was sent");
            busy.incoming->c->sending = false;
            eventually([&] { return int32_t(busy.args->requests_handled) == 1; },
                       "Busy context was not retried");
            manager.stop();
        }
        check(dead.channel->sent == 0 && dead.channel->disposed == 0, "Dropped request was answered");
        check(timeout.no == 1 && timeout.has >= 1, "NoEvents callback not used");
        Fixture shutdown(responses);
        {
            PollServiceRequestManager manager(false, 1, 0, runtime);
            manager.start();
            manager.enqueue(shutdown.request);
            eventually([&] { return manager.retry_count() == 1; }, "Shutdown request was not requeued");
            manager.stop();
        }
        check(shutdown.channel->sent == 1 && shutdown.channel->status == 503 && !shutdown.channel->keep &&
                  shutdown.channel->length == 0 && int32_t(shutdown.args->requests_handled) == 0,
              "Shutdown response differs");
        Fixture self_stop(responses);
        self_stop.ready = true;
        {
            PollServiceRequestManager manager(false, 1, 0, runtime);
            std::promise<void> stopped;
            auto done = stopped.get_future();
            auto get = self_stop.args->get_events;
            self_stop.args->get_events = [&](auto r, auto id) {
                manager.stop();
                stopped.set_value();
                return get(r, id);
            };
            manager.start();
            manager.enqueue(self_stop.request);
            check(done.wait_for(5s) == std::future_status::ready, "Stop from worker deadlocked");
            eventually([&] { return int32_t(self_stop.args->requests_handled) == 1; },
                       "Stop from callback lost current response");
        }
        Fixture blocked(responses);
        blocked.ready = true;
        {
            PollServiceRequestManager manager(false, 1, 0, runtime);
            std::mutex m;
            std::condition_variable changed;
            bool entered = false, release = false, timed_out = false;
            auto get = blocked.args->get_events;
            blocked.args->get_events = [&](auto r, auto id) {
                std::unique_lock lock(m);
                entered = true;
                changed.notify_all();
                if (!changed.wait_for(lock, 5s, [&] { return release; }))
                    timed_out = true;
                return get(r, id);
            };
            manager.start();
            manager.enqueue(blocked.request);
            {
                std::unique_lock lock(m);
                check(changed.wait_for(lock, 5s, [&] { return entered; }), "Blocked callback did not start");
            }
            manager.stop();
            {
                std::lock_guard lock(m);
                check(!timed_out, "Stop waited for blocked callback");
                release = true;
                changed.notify_all();
            }
            eventually([&] { return int32_t(blocked.args->requests_handled) == 1; },
                       "Blocked callback did not finish after Stop");
        }
        // Exercise actual worker concurrency, idle retirement/restart, and job errors.
        std::mutex mutex;
        std::condition_variable cv;
        int entered = 0, finished = 0;
        bool release = false;
        auto engine = runtime.make_pool(
            [&](PollRequestPtr) {
                std::unique_lock lock(mutex);
                ++entered;
                cv.notify_all();
                cv.wait(lock, [&] { return release; });
                ++finished;
                cv.notify_all();
            },
            "probe", 20, 2);
        engine->enqueue({});
        engine->enqueue({});
        engine->enqueue({});
        {
            std::unique_lock lock(mutex);
            check(cv.wait_for(lock, 5s, [&] { return entered == 2; }), "Worker concurrency not reached");
            check(engine->count() == 1, "Extra work did not remain queued");
            release = true;
            cv.notify_all();
            check(cv.wait_for(lock, 5s, [&] { return finished == 3; }), "Worker jobs lost");
        }
        std::this_thread::sleep_for(80ms);
        engine->enqueue({});
        {
            std::unique_lock lock(mutex);
            check(cv.wait_for(lock, 5s, [&] { return finished == 4; }), "Idle pool did not restart");
        }
        engine->dispose();
        engine->enqueue({});
        check(engine->count() == 0, "Disposed engine accepted job");
        std::atomic<int> jobs = 0;
        auto throwing = runtime.make_pool(
            [&](PollRequestPtr) {
                if (++jobs == 1)
                    throw std::runtime_error("job");
            },
            "errors", 20, 1);
        throwing->enqueue({});
        throwing->enqueue({});
        eventually([&] { return jobs == 2 && errors == 1; }, "Worker did not recover from job exception");
        throwing->dispose();
        check(responses.warnings == 0, "Unexpected response warning");
        auto empty = runtime.make_pool({}, "empty", 20, 0);
        empty->enqueue({});
        check(empty->count() == 0, "Null worker was activated");
        empty->dispose();
        std::mutex cm;
        std::condition_variable cc;
        bool cancel_entered = false, cancel_release = false;
        int cancel_jobs = 0;
        auto cancelled = runtime.make_pool(
            [&](PollRequestPtr) {
                std::unique_lock lock(cm);
                ++cancel_jobs;
                cancel_entered = true;
                cc.notify_all();
                cc.wait(lock, [&] { return cancel_release; });
            },
            "cancel", 20, -1);
        cancelled->enqueue({});
        {
            std::unique_lock lock(cm);
            check(cc.wait_for(lock, 5s, [&] { return cancel_entered; }), "Cancel fixture did not enter job");
        }
        cancelled->enqueue({});
        cancelled->cancel();
        check(cancelled->count() == 0, "Cancel did not drain pending jobs");
        {
            std::lock_guard lock(cm);
            cancel_release = true;
            cc.notify_all();
        }
        std::this_thread::sleep_for(40ms);
        cancelled->enqueue({});
        std::this_thread::sleep_for(40ms);
        check(cancelled->count() == 1, "Cancellation token was incorrectly reset");
        cancelled->dispose();
        cancelled->join();
        check(cancel_jobs == 1, "Cancelled job was run");
        std::cout
            << "PASS: native threaded polling retry, starvation, delivery, drop, timeout, shutdown and worker lifecycle\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
