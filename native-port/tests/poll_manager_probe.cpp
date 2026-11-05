#include "opensim/server/poll_manager.hpp"
#include <bit>
#include <iostream>
#include <sstream>
using namespace opensim;
using namespace server;
namespace {
struct State {
    std::string trace, fail;
    bool can = true, sending = false, has = false, watching = false;
    int watch_sleeps = 0;
    int32_t now = 0, started = 0;
    std::size_t pending = 0;
    PollServiceRequestManager* manager = nullptr;
    void event(const std::string& s) {
        trace += s + ";";
        if (s == fail)
            throw std::runtime_error("injected");
    }
};
struct Body : PollBody {
    State& s;
    explicit Body(State& x) : s(x) {
    }
    bool can_read() const override {
        s.event("read");
        return true;
    }
    void dispose() override {
        s.event("dispose");
    }
};
struct Context : PollContext {
    State& s;
    explicit Context(State& x) : s(x) {
    }
    bool can_send() const override {
        s.event("can");
        return s.can;
    }
    bool is_sending() const override {
        s.event("sending");
        return s.sending;
    }
};
struct Incoming : PollIncomingRequest {
    State& s;
    std::shared_ptr<Body> b;
    std::shared_ptr<Context> c;
    explicit Incoming(State& x) : s(x), b(std::make_shared<Body>(x)), c(std::make_shared<Context>(x)) {
    }
    std::shared_ptr<PollBody> body() const override {
        s.event("body");
        return b;
    }
    std::shared_ptr<PollContext> context() const override {
        s.event("context");
        return c;
    }
};
struct Response : PollResponse {
    State& s;
    explicit Response(State& x) : s(x) {
    }
    void status_code(int32_t x) override {
        s.event("status:" + std::to_string(x));
    }
    void content_length(int64_t x) override {
        s.event("length:" + std::to_string(x));
    }
    void utf8_encoding() override {
        s.event("utf8");
    }
    void keep_alive(bool x) override {
        s.event(x ? "keep:1" : "keep:0");
    }
    void keep_alive_timeout(int32_t) override {
    }
    void protocol_version(PollName) override {
    }
    void status_description(PollName) override {
    }
    void add_header(const std::string&, PollName) override {
    }
    void redirect(PollName, int32_t) override {
    }
    void buffer_start(int32_t) override {
    }
    void buffer_length(int32_t) override {
    }
    void buffer(PollBytes) override {
    }
    void send() override {
        s.event("send");
    }
};
struct RequestBackend : PollRequestBackend {
    State& s;
    explicit RequestBackend(State& x) : s(x) {
    }
    int32_t tick_count() const override {
        return s.started;
    }
    framework::Uuid random_id() const override {
        return {};
    }
    PollResponsePtr make_response(const std::shared_ptr<PollIncomingRequest>&) override {
        s.event("factory");
        return std::make_shared<Response>(s);
    }
    void warn(const std::string&, const std::exception&) override {
        s.event("warn");
    }
};
struct Pool : PollJobEngine {
    State& s;
    explicit Pool(State& x) : s(x) {
    }
    void enqueue(PollRequestPtr) override {
        s.event("enqueue");
    }
    std::size_t count() const override {
        s.event("count");
        return s.pending;
    }
    void dispose() override {
        s.event("pool_dispose");
    }
};
struct Watcher : PollWatcher {
    void join() override {
    }
};
struct Backend : PollManagerBackend {
    State& s;
    std::function<void()> watch;
    explicit Backend(State& x) : s(x) {
    }
    int32_t tick_count() const override {
        s.event("tick");
        return s.now;
    }
    void sleep(int32_t n) override {
        s.event("sleep:" + std::to_string(n));
        if (s.watching && ++s.watch_sleeps == 2) {
            s.watching = false;
            s.manager->stop();
        }
    }
    void watchdog_update() override {
        s.event("watchdog");
    }
    void error(const std::exception&) override {
        s.event("error");
    }
    std::shared_ptr<PollJobEngine> make_pool(Worker, const std::string& name, int32_t hold,
                                             int32_t count) override {
        s.event("pool:" + name + ":" + std::to_string(hold) + ":" + std::to_string(count));
        return std::make_shared<Pool>(s);
    }
    std::shared_ptr<PollWatcher> start_watcher(std::function<void()> w) override {
        s.event("watcher");
        watch = std::move(w);
        return std::make_shared<Watcher>();
    }
};
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::vector<std::string> a;
        std::stringstream in(line);
        std::string v;
        while (std::getline(in, v, '|'))
            a.push_back(v);
        State s;
        s.can = a[0] == "1";
        s.sending = a[1] == "1";
        s.has = a[2] == "1";
        s.started = std::stoi(a[3]);
        s.now = std::stoi(a[4]);
        s.fail = a[6];
        s.pending = std::stoul(a[8]);
        Backend backend(s);
        RequestBackend rb(s);
        auto args = std::make_shared<services::CapabilityPollHandler>();
        args->timeout_ms = std::stoi(a[5]);
        auto data = [&](std::string event) {
            s.event(event);
            auto table = std::make_shared<PollTable>();
            table->set("h", PollResponsePtr(std::make_shared<Response>(s)));
            return table;
        };
        args->has_events = [&](auto, auto) {
            s.event("has");
            return s.has;
        };
        args->get_events = [&](auto, auto) { return data("get"); };
        args->no_events = [&](auto, auto) { return data("no"); };
        args->drop = [&](auto, auto) { s.event("drop"); };
        if (a[10] == "nullhas")
            args->has_events = {};
        if (a[10] == "nullget")
            args->get_events = {};
        if (a[10] == "nullno")
            args->no_events = {};
        if (a[10] == "nulldrop")
            args->drop = {};
        auto request = std::make_shared<PollServiceHttpRequest>(args, std::make_shared<Incoming>(s), rb);
        PollServiceRequestManager manager(false, std::stoul(a[7]), 123, backend);
        s.manager = &manager;
        std::stringstream ops(a[9]);
        bool error = false;
        while (std::getline(ops, v, ',')) {
            try {
                if (v == "start")
                    manager.start();
                if (v == "stop")
                    manager.stop();
                if (v == "enqueue")
                    manager.enqueue(request);
                if (v == "job")
                    manager.pool_worker_job(request);
                if (v == "null")
                    manager.pool_worker_job({});
                if (v == "watch") {
                    s.watching = true;
                    s.watch_sleeps = 0;
                    if (!backend.watch)
                        throw std::runtime_error("null watcher");
                    backend.watch();
                }
            } catch (...) {
                error = true;
                s.event("caught");
            }
        }
        std::cout << s.trace << "handled:" << int32_t(args->requests_handled)
                  << ";running:" << manager.running() << ";exception:" << error << '\n';
        s.fail.clear();
    }
}
