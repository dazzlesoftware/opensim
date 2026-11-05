// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/expiring_cache.hpp"
#include <iostream>
#include <sstream>
using namespace opensim::framework;
using Key = std::optional<std::string>;
using Cache = ExpiringCacheOS<Key, std::int32_t>;
namespace {
double now;
int flags;
std::string trace;
void Fail(int mask, const char* message) {
    if (flags & mask)
        throw std::runtime_error(message);
}
std::string H(std::string_view value) {
    std::string out;
    for (unsigned char c : value) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
struct Timer : ExpiringCacheTimer {
    explicit Timer(std::int32_t due) {
        trace += "timer:" + std::to_string(due) + ";";
        Fail(2, "timer");
    }
    void change(std::int32_t due) override {
        trace += "change:" + std::to_string(due) + ";";
        Fail(4, "change");
    }
    void dispose() override {
        trace += "dispose-timer;";
        Fail(8, "dispose-timer");
    }
};
std::string Values(const std::shared_ptr<std::vector<std::int32_t>>& a) {
    if (!a)
        return "N";
    std::string out;
    for (auto value : *a) {
        if (!out.empty())
            out += ',';
        out += std::to_string(value);
    }
    return out;
}
Key KeyOf(std::string s) {
    return s == "_" ? Key{} : Key(s);
}
double Number(const std::string& s) {
    return s == "nan"    ? std::numeric_limits<double>::quiet_NaN()
           : s == "inf"  ? std::numeric_limits<double>::infinity()
           : s == "-inf" ? -std::numeric_limits<double>::infinity()
                         : std::stod(s);
}
}
namespace opensim::framework {
template <> struct ExpiringCacheProbe<Key, std::int32_t, std::hash<Key>> {
    static std::string state(const Cache* cache) {
        if (!cache)
            return "N";
        auto s = cache->state_;
        std::string expires, values;
        for (const auto& item : s->expires) {
            if (!expires.empty())
                expires += ',';
            expires += item.first.value_or("") + "=" + std::to_string(item.second);
        }
        for (const auto& item : s->values) {
            if (!values.empty())
                values += ',';
            values += item.first.value_or("") + "=" + std::to_string(item.second);
        }
        return std::to_string(cache->count()) + ":" + expires + ":" + values + ":" +
               (s->timer.load() ? "1" : "0") + ":" + (s->lock.load() ? "1" : "0");
    }
    static void purge(Cache& cache) {
        Cache::purge_state(cache.state_);
    }
    static void release(Cache& cache) {
        Cache::required(cache.state_->lock.load()).exit(ExpiringCacheLock::Mode::Write);
    }
};
}
using Probe = ExpiringCacheProbe<Key, std::int32_t, std::hash<Key>>;
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::int32_t interval;
        in >> interval >> flags;
        now = 0;
        trace.clear();
        std::unique_ptr<Cache> cache;
        std::shared_ptr<std::vector<std::int32_t>> saved;
        std::string output;
        try {
            ExpiringCacheBackend backend{[] {
                                             trace += "clock;";
                                             Fail(1, "clock");
                                             return now;
                                         },
                                         [](auto, std::int32_t due) { return std::make_shared<Timer>(due); }};
            cache = std::make_unique<Cache>(interval, backend);
            output = "NEW:" + Probe::state(cache.get());
        } catch (const std::exception& e) {
            output = "EX:" + H(e.what());
        }
        for (std::string token; in >> token;) {
            std::vector<std::string> p;
            std::istringstream parts(token.substr(1));
            for (std::string part; std::getline(parts, part, ':');)
                p.push_back(part);
            std::string result = "OK";
            try {
                if (token[0] == 't')
                    now = Number(p[0]);
                else if (token[0] == 'f')
                    flags = std::stoi(p[0]);
                else {
                    if (!cache)
                        throw std::runtime_error("Object reference not set to an instance of an object.");
                    auto key = KeyOf(p.empty() ? "" : p[0]);
                    switch (token[0]) {
                    case 'a':
                        cache->add(key, std::stoi(p[1]), std::stoi(p[2]));
                        break;
                    case 'd':
                        cache->add(key, std::stoi(p[1]));
                        break;
                    case 'i':
                        cache->add_or_update(key, std::stoi(p[1]), std::stoi(p[2]));
                        break;
                    case 'j':
                        cache->add_or_update(key, std::stoi(p[1]), Number(p[2]));
                        break;
                    case 'r':
                        result = cache->remove(key) ? "1" : "0";
                        break;
                    case 'c':
                        cache->clear();
                        break;
                    case 'k':
                        result = cache->contains_key(key) ? "1" : "0";
                        break;
                    case 'h':
                        result = cache->contains(key, std::stoi(p[1])) ? "1" : "0";
                        break;
                    case 'g': {
                        std::int32_t value = 0;
                        auto found = cache->try_get_value(key, value);
                        result = std::string(found ? "1" : "0") + ":" + std::to_string(value);
                        break;
                    }
                    case 'l': {
                        std::int32_t value = 0;
                        auto found = cache->try_get_value(key, std::stoi(p[1]), value);
                        result = std::string(found ? "1" : "0") + ":" + std::to_string(value);
                        break;
                    }
                    case 'q': {
                        bool existed = false;
                        auto& value = cache->try_get_or_default_value(key, existed);
                        result = std::string(existed ? "1" : "0") + ":" + std::to_string(value);
                        break;
                    }
                    case 's': {
                        bool existed = false;
                        auto& value = cache->try_get_or_default_value(key, std::stoi(p[1]), existed);
                        result = std::string(existed ? "1" : "0") + ":" + std::to_string(value);
                        break;
                    }
                    case 'v': {
                        auto values = cache->values();
                        result = std::string(values == saved ? "1" : "0") + ":" + Values(values);
                        saved = values;
                        break;
                    }
                    case 'w':
                        if (!saved)
                            throw std::runtime_error("Object reference not set to an instance of an object.");
                        if (saved->empty())
                            throw std::out_of_range("Index was outside the bounds of the array.");
                        (*saved)[0] = 99;
                        break;
                    case 'b':
                        result = Values(saved);
                        break;
                    case 'p':
                        Probe::purge(*cache);
                        break;
                    case 'u':
                        Probe::release(*cache);
                        break;
                    case 'z':
                        cache->dispose();
                        break;
                    case 'n':
                        result = std::to_string(cache->count());
                        break;
                    }
                }
            } catch (const std::exception& e) {
                result = "EX:" + H(e.what());
            }
            output += "/" + result + ":" + Probe::state(cache.get());
        }
        std::cout << output << "|" << H(trace) << "\n";
    }
}
