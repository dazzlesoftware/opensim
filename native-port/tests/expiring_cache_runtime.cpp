// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/expiring_cache.hpp"
#include <thread>
#include <future>
#include <chrono>
#include <iostream>
using namespace opensim::framework;
using namespace std::chrono_literals;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    try {
        ExpiringCacheOS<int, int> cache(500);
        cache.add(1, 7, 1);
        cache.add(2, 8, 0);
        auto deadline = std::chrono::steady_clock::now() + 4s;
        while (cache.count() != 1 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(5ms);
        int value = 0;
        check(cache.count() == 1 && !cache.try_get_value(1, value) && cache.try_get_value(2, value) &&
                  value == 8,
              "real one-shot timer purges expired entry and retains immortal entry");
        cache.clear();
        std::vector<std::thread> threads;
        std::atomic<bool> good = true;
        for (int actor = 0; actor < 4; ++actor)
            threads.emplace_back([&, actor] {
                for (int i = 0; i < 2000; ++i) {
                    int key = actor * 10000 + i % 64;
                    cache.add_or_update(key, i, 1);
                    int item = 0;
                    if (!cache.try_get_value(key, item) || item != i)
                        good = false;
                    if (!cache.contains(key, 1000))
                        good = false;
                    if (i % 3 == 0)
                        cache.remove(key);
                }
            });
        for (auto& thread : threads)
            thread.join();
        check(good, "parallel independent writers/readers/touches");
        cache.clear();
        check(cache.count() == 0, "clear after concurrent writes");
        auto live = std::make_shared<int>(9);
        ExpiringCacheOS<int, std::shared_ptr<int>> refs(500);
        refs.add(1, live, 0);
        auto first = refs.values();
        auto second = refs.values();
        check(first == second && first->at(0) == live, "reference values and cached array identity");
        first->at(0) = nullptr;
        std::shared_ptr<int> found;
        check(refs.try_get_value(1, found) && found == live && refs.values()->at(0) == nullptr,
              "array mutation does not replace dictionary reference");
        refs.clear();
        check(first->size() == 1 && !first->at(0) && refs.values() != first,
              "clear invalidates current array while retaining old snapshots");
        std::promise<void> entered, release, finished;
        auto wait = release.get_future().share();
        auto ready = entered.get_future();
        auto done = finished.get_future();
        auto backend = native_expiring_cache_backend();
        auto timer = backend.timer;
        backend.timer = [&](auto callback, int due) {
            return timer(
                [&, callback] {
                    entered.set_value();
                    wait.wait();
                    try {
                        callback();
                        finished.set_value();
                    } catch (...) {
                        finished.set_exception(std::current_exception());
                    }
                },
                due);
        };
        ExpiringCacheOS<int, int> cancel(500, backend);
        cancel.add(1, 1);
        check(ready.wait_for(3s) == std::future_status::ready, "real cancellation callback entered");
        cancel.clear();
        release.set_value();
        check(done.wait_for(3s) == std::future_status::ready, "cancelled in-flight callback completes");
        done.get();
        check(cancel.count() == 0, "timer disposal returns before the blocked callback is released");
        std::cout
            << "PASS: real cache timer, concurrent operations, reference arrays and nonblocking timer disposal\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
