// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/background_task_queue.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using opensim::framework::BackgroundTaskQueue;
using namespace std::chrono_literals;

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

struct Gate {
    std::promise<void> promise;
    std::shared_future<void> ready = promise.get_future().share();
    std::atomic<bool> released = false;

    void release() {
        if (!released.exchange(true))
            promise.set_value();
    }

    ~Gate() {
        release();
    }
};

void ordering_and_errors() {
    std::atomic<int> errors = 0;
    BackgroundTaskQueue queue([&](std::exception_ptr error) {
        require(bool(error), "missing task failure");
        ++errors;
        throw std::runtime_error("error reporter failed");
    });
    Gate gate;
    std::vector<int> order;
    std::promise<void> done;
    auto completed = done.get_future();
    require(queue.submit([&] { gate.ready.wait(); }), "first submit rejected");
    queue.submit([&] {
        order.push_back(1);
        throw std::runtime_error("task failed");
    });
    queue.submit([&] {
        order.push_back(2);
        done.set_value();
    });
    gate.release();
    require(completed.wait_for(3s) == std::future_status::ready, "worker failed to recover");
    queue.close();
    const auto stats = queue.statistics();
    require(order == std::vector<int>{1, 2} && errors == 1, "FIFO/error handling failed");
    require(stats.completed == 3 && stats.failed == 1 && stats.finished, "completion statistics failed");
}

void shutdown_discards_waiting_work() {
    BackgroundTaskQueue queue;
    std::future<void> closing;
    Gate gate; // Release before the async future or queue can wait during unwinding.
    std::promise<void> entered;
    auto started = entered.get_future();
    queue.submit([&] {
        entered.set_value();
        gate.ready.wait();
    });
    require(started.wait_for(3s) == std::future_status::ready, "worker did not start");
    auto capture = std::make_shared<int>(42);
    std::weak_ptr<int> discarded = capture;
    queue.submit([capture] {});
    capture.reset();
    closing = std::async(std::launch::async, [&] { queue.close(); });
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!queue.statistics().closed && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    require(queue.statistics().closed, "shutdown did not begin");
    require(!queue.submit([] {}), "shutdown accepted new work");
    require(closing.wait_for(0s) != std::future_status::ready, "shutdown did not wait for active task");
    gate.release();
    require(closing.wait_for(3s) == std::future_status::ready, "shutdown failed to finish");
    closing.get();
    require(discarded.expired() && queue.statistics().discarded == 1, "queued captures were retained");
}

void worker_can_release_last_owner() {
    auto queue = std::make_shared<BackgroundTaskQueue>();
    std::weak_ptr<BackgroundTaskQueue> weak = queue;
    Gate gate;
    std::promise<void> done;
    auto completed = done.get_future();
    queue->submit([owner = queue, &gate, &done]() mutable {
        gate.ready.wait();
        owner->close();
        owner.reset();
        done.set_value();
    });
    queue.reset();
    gate.release();
    require(completed.wait_for(3s) == std::future_status::ready, "self-close or destruction deadlocked");
    require(weak.expired(), "worker retained queue owner");
}
} // namespace

int main() {
    try {
        ordering_and_errors();
        shutdown_discards_waiting_work();
        worker_can_release_last_owner();
        std::cout << "Background admission queue lifecycle passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
