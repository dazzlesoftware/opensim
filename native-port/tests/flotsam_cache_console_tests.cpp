// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/flotsam_cache_console.hpp"
#include "opensim/framework/text.hpp"
#include <deque>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
using namespace opensim;
using namespace services;
namespace {
std::string hex(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
std::optional<std::string> decode(const std::string& s) {
    if (s == "~")
        return {};
    std::string out;
    for (std::size_t i = 0; i < s.size(); i += 2)
        out += static_cast<char>(std::stoi(s.substr(i, 2), nullptr, 16));
    return out;
}
std::string show(const std::optional<std::string>& s) {
    return s ? hex(*s) : "~";
}
std::string exception(const std::exception& error) {
    auto domain = dynamic_cast<const FlotsamConsoleError*>(&error);
    std::ostringstream code;
    code << std::hex << std::uppercase << std::setw(8) << std::setfill('0')
         << (domain ? domain->hresult() : 0x80131500u);
    return std::string("EX:") + (domain ? domain->source_type() : "System.Exception") + ":" + code.str() +
           ":" + (domain ? show(domain->parameter) : "~") + ":" +
           (domain && domain->actual_value ? std::to_string(*domain->actual_value) : "~") + ":" +
           hex(error.what());
}
struct Probe;
struct Output : FlotsamConsoleOutput {
    Probe& p;
    explicit Output(Probe& p) : p(p) {
    }
    void output(const std::string&) override;
};
struct Timer : FlotsamCleanupTimer {
    Probe& p;
    explicit Timer(Probe& p) : p(p) {
    }
    void subscribe(std::function<void()>) override {
    }
    void start() override;
    void stop() override;
    void close() override {
    }
};
struct Probe {
    int point = 0, nth = 1, seen = 0, mut_point = 0, mut_nth = 1, mut_seen = 0, mutation = 0, lower_at = 0,
        lower_seen = 0;
    std::optional<std::string> lower_current, lower_later;
    std::shared_ptr<FlotsamCacheConsole::Arguments> current_args;
    int memory = 7, negative = 3, files = 11, touch = 13, file_mode = 0, dispatch = 0;
    bool null_console = false, active = false;
    std::int64_t now = 638712864000000000ll;
    std::vector<std::string> trace;
    std::deque<std::function<void()>> jobs;
    std::shared_ptr<FlotsamInstanceConfig> config;
    std::shared_ptr<FlotsamCacheLifecycle> life;
    std::shared_ptr<FlotsamCacheConsole> controller;
    void log(const std::string& s) {
        trace.push_back(s);
    }
    void hook(int p) {
        if (p == mut_point && ++mut_seen == mut_nth) {
            switch (mutation) {
            case 1:
                config->file = !config->file;
                config->memory = !config->memory;
                config->negative = !config->negative;
                break;
            case 2:
                life->cleanup_running = false;
                break;
            case 3:
                life->timer_running = false;
                break;
            case 4:
                life->timer_running = true;
                break;
            case 5:
                config->directory = "cache.bad";
                break;
            case 6:
                life->timer.reset();
                break;
            }
        }
        if (p == point && ++seen == nth)
            throw std::runtime_error("fault");
    }
    void lower_hook() {
        if (!lower_at)
            return;
        log("LOWER:" + std::to_string(++lower_seen));
        if (lower_seen == lower_at && current_args && current_args->size() > 3) {
            (*current_args)[2] = lower_current;
            (*current_args)[3] = lower_later;
        }
    }
    void flags(int value) {
        config->memory = (value & 1) != 0;
        config->file = (value & 2) != 0;
        config->negative = (value & 4) != 0;
    }
    std::vector<std::optional<std::string>> paths() {
        switch (file_mode) {
        case 1:
            return {"cache.bad/RegionStatus_uuid.fac"};
        case 2:
            return {"cache_under/RegionStatus_uuid.fac"};
        case 3:
            return {};
        case 4:
            return {std::nullopt};
        case 5:
            return {"é漢/RegionStatus_uuid.fac"};
        default:
            return {"cache/RegionStatus_uuid.fac", "cache/no-marker", "cache/a.fac"};
        }
    }
    void reset(int value, int timer, int cleanup) {
        jobs.clear();
        current_args.reset();
        lower_at = lower_seen = 0;
        point = seen = mut_point = mut_seen = mutation = 0;
        nth = mut_nth = 1;
        memory = 7;
        negative = 3;
        files = 11;
        touch = 13;
        file_mode = dispatch = 0;
        null_console = active = false;
        now = 638712864000000000ll;
        config = std::make_shared<FlotsamInstanceConfig>();
        flags(value);
        life = std::make_shared<FlotsamCacheLifecycle>();
        life->timer = std::make_shared<Timer>(*this);
        life->timer_running = timer != 0;
        life->cleanup_running = cleanup != 0;
        active = timer != 0;
        auto b = std::make_shared<FlotsamConsoleBackend>();
        b->console = [this] {
            log("CON");
            hook(1);
            return null_console ? std::shared_ptr<FlotsamConsoleOutput>{} : std::make_shared<Output>(*this);
        };
        b->post = [this](auto callback, const auto& name) {
            log("POST:" + name + ":0");
            hook(3);
            if (dispatch)
                callback();
            else
                jobs.push_back(std::move(callback));
        };
        b->memory_count = [this] {
            log("MEM");
            hook(4);
            return memory;
        };
        b->file_count = [this](auto& path) {
            log("COUNT:" + show(path));
            hook(5);
            return files;
        };
        b->hit_report = [this] {
            log("REPORT");
            hook(6);
            return std::vector<std::string>{"hit one", "hit two"};
        };
        b->status_files = [this](auto& path, auto& pattern) {
            log("FILES:" + show(path) + ":" + pattern);
            hook(7);
            return paths();
        };
        b->last_write = [this](auto& path) {
            log("WRITE:" + show(path));
            hook(8);
            return framework::AssetDateTime(638658908550000000ll, 2);
        };
        b->format_date = [this](auto date, auto& format) {
            log("FORMAT:" + std::to_string(date.ticks()) + ":" + format);
            hook(9);
            return std::string("10/30/2024 01:14:15");
        };
        b->clear_memory = [this] {
            log("CM");
            hook(10);
            memory = 0;
        };
        b->negative_count = [this] {
            log("NEG");
            hook(11);
            return negative;
        };
        b->clear_negative = [this] {
            log("CN");
            hook(12);
            negative = 0;
        };
        b->clear_files = [this] {
            log("CF");
            hook(13);
            files = 0;
            file_mode = 3;
        };
        b->sleep = [this](int ms) {
            log("SLEEP:" + std::to_string(ms));
            hook(14);
        };
        b->touch = [this](bool get) {
            log(std::string("TOUCH:") + (get ? "1" : "0"));
            hook(15);
            return touch;
        };
        b->clean = [this](auto date) {
            log("CLEAN:" + std::to_string(date.ticks()) + ":" + std::to_string(date.kind()));
            hook(16);
        };
        b->now = [this] {
            log("NOW");
            hook(17);
            return framework::AssetDateTime(now, 2);
        };
        b->try_parse = [this](auto& value, auto& date) {
            log("PARSE:" + show(value));
            hook(18);
            if (value == "2024-01-01") {
                date = framework::AssetDateTime(638396640000000000ll, 0);
                return true;
            }
            if (value == "2025-01-01") {
                date = framework::AssetDateTime(638712864000000000ll, 0);
                return true;
            }
            if (value == "2030-01-01") {
                date = framework::AssetDateTime(640290528000000000ll, 0);
                return true;
            }
            if (value == "12/31/2024 23:59:59") {
                date = framework::AssetDateTime(638712863990000000ll, 0);
                return true;
            }
            return false;
        };
        b->lower = [this](std::string value) {
            lower_hook();
            for (auto& c : value)
                if (c >= 'A' && c <= 'Z')
                    c += ('a' - 'A');
            return value;
        };
        b->equals_now = [](const auto& value) {
            if (!value)
                return false;
            auto s = *value;
            for (auto& c : s)
                if (c >= 'A' && c <= 'Z')
                    c += ('a' - 'A');
            return s == "now";
        };
        b->load_defaults = [this] {
            log("LOAD");
            hook(19);
            ++memory;
        };
        b->delete_defaults = [this] {
            log("DELETE");
            hook(20);
            memory = 0;
        };
        controller = std::make_shared<FlotsamCacheConsole>(config, life, b);
    }
    std::string run(const std::string& line) {
        trace.clear();
        std::istringstream in(line);
        std::string op, result;
        int n = 0;
        in >> op >> n;
        try {
            if (op == "reset") {
                int timer, cleanup;
                in >> timer >> cleanup;
                reset(n, timer, cleanup);
                result = "reset";
            } else if (op == "fail") {
                point = n;
                in >> nth;
                seen = 0;
                result = "fail";
            } else if (op == "mutate") {
                mut_point = n;
                in >> mut_nth >> mutation;
                mut_seen = 0;
                result = "mutate";
            } else if (op == "lower") {
                std::string current, later;
                in >> current >> later;
                lower_at = n;
                lower_seen = 0;
                lower_current = decode(current);
                lower_later = decode(later);
                result = "lower";
            } else if (op == "flags") {
                flags(n);
                result = "flags";
            } else if (op == "files") {
                file_mode = n;
                result = "files";
            } else if (op == "dispatch") {
                dispatch = n;
                result = "dispatch";
            } else if (op == "console") {
                null_console = n != 0;
                result = "console";
            } else if (op == "timer") {
                if (n)
                    life->timer = std::make_shared<Timer>(*this);
                else
                    life->timer.reset();
                result = "timer";
            } else if (op == "life") {
                int cleanup;
                in >> cleanup;
                life->timer_running = n != 0;
                life->cleanup_running = cleanup != 0;
                result = "life";
            } else if (op == "call") {
                std::shared_ptr<FlotsamCacheConsole::Arguments> args;
                if (n >= 0) {
                    args = std::make_shared<FlotsamCacheConsole::Arguments>();
                    for (int i = 0; i < n; ++i) {
                        std::string value;
                        in >> value;
                        args->push_back(decode(value));
                    }
                }
                current_args = args;
                controller->handle(args);
                result = "call";
            } else if (op == "drain") {
                int count = 0;
                while (!jobs.empty() && (n == 0 || count < n)) {
                    auto job = std::move(jobs.front());
                    jobs.pop_front();
                    ++count;
                    job();
                }
                result = "drain";
            } else
                throw std::runtime_error("Unknown operation");
        } catch (const std::exception& error) {
            result = exception(error);
        }
        std::string events;
        for (auto& s : trace) {
            if (!events.empty())
                events += ';';
            events += s;
        }
        return result + "|" + hex(events) + "|" + (config->memory ? "1" : "0") + (config->file ? "1" : "0") +
               (config->negative ? "1" : "0") + "," + (life->timer_running ? "1" : "0") +
               (life->cleanup_running ? "1" : "0") + "," + (active ? "1" : "0") + "," +
               (life->timer ? "1" : "0") + "," + std::to_string(memory) + "," + std::to_string(negative) +
               "," + std::to_string(files) + "," + std::to_string(file_mode) + "," +
               std::to_string(jobs.size()) + "," + show(config->directory);
    }
};
void Output::output(const std::string& s) {
    p.log("O:" + s);
    p.hook(2);
}
void Timer::start() {
    p.log("START");
    p.hook(22);
    p.active = true;
}
void Timer::stop() {
    p.log("STOP");
    p.hook(21);
    p.active = false;
}
}
int main(int argc, char** argv) {
    try {
        Probe p;
        p.reset(7, 1, 0);
        if (argc > 1 && std::string(argv[1]) == "--probe") {
            std::string line;
            while (std::getline(std::cin, line))
                std::cout << p.run(line) << '\n';
            return 0;
        }
        p.run("call 2 666361636865 617373657473");
        if (!p.life->cleanup_running || p.jobs.size() != 1)
            throw std::runtime_error("Assets command gate/post");
        p.run("drain 0");
        if (p.life->cleanup_running || !p.life->timer_running || !p.active)
            throw std::runtime_error("Assets timer resume");
        p.reset(0, 0, 0);
        p.run("call 3 666361636865 657870697265 6e6f77");
        if (!p.life->cleanup_running)
            throw std::runtime_error("Original disabled file expire retains cleanup gate");
        p.reset(7, 1, 0);
        p.run("call 2 666361636865 737461747573");
        std::weak_ptr<FlotsamInstanceConfig> retained_config = p.config;
        std::weak_ptr<FlotsamCacheLifecycle> retained_life = p.life;
        p.controller.reset();
        p.config.reset();
        p.life.reset();
        if (retained_config.expired() || retained_life.expired())
            throw std::runtime_error("Queued console task lost owner dependencies");
        auto job = std::move(p.jobs.front());
        p.jobs.pop_front();
        job();
        job = {};
        if (!retained_config.expired() || !retained_life.expired())
            throw std::runtime_error("Completed console task retained owner dependencies");
        std::cout << "PASS: Flotsam console deferred tasks and source timer/gate semantics\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
