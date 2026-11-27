#include "opensim/scene/entity_transfer_state.hpp"
#include <atomic>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace opensim;

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace

int main() {
    try {
        const auto agent = framework::Uuid::random();
        scene::EntityTransferStateMachine machine([] { return "Concurrency"; }, [] { return false; });
        std::atomic<int> entries = 0;
        std::vector<std::thread> threads;
        for (int i = 0; i < 32; ++i)
            threads.emplace_back([&] {
                if (machine.set_in_transit(agent))
                    ++entries;
            });
        for (auto &thread : threads)
            thread.join();
        require(entries == 1, "Concurrent reservation admitted the same agent more than once");
        threads.clear();
        std::atomic<int> completed = 0;
        for (int i = 0; i < 32; ++i)
            threads.emplace_back([&] {
                const auto id = framework::Uuid::random();
                if (!machine.set_in_transit(id) ||
                    !machine.update_in_transit(id, scene::AgentTransferState::Transferring))
                    return;
                machine.set_agent_arrived_at_destination(id);
                if (machine.agent_transfer_state(id) == scene::AgentTransferState::ReceivedAtDestination &&
                    machine.wait_for_agent_arrived_at_destination(id) && machine.reset_from_transit(id))
                    ++completed;
            });
        for (auto &thread : threads)
            thread.join();
        require(completed == 32, "Independent concurrent transfers interfered with each other");
        std::promise<void> entered, release;
        auto entered_future = entered.get_future();
        auto released = release.get_future().share();
        scene::EntityTransferStateMachine waiting([] { return "Concurrency"; }, [] { return true; },
                                                  [&](int delay) {
                                                      require(delay == 100, "Wrong wait interval");
                                                      entered.set_value();
                                                      released.wait();
                                                  });
        waiting.set_in_transit(agent);
        waiting.update_in_transit(agent, scene::AgentTransferState::Transferring);
        std::future<bool> result;

        struct Release {
            std::promise<void> &promise;
            bool done = false;

            ~Release() {
                if (!done)
                    promise.set_value();
            }
        } guard{release};

        result = std::async(std::launch::async, [&] { return waiting.wait_for_agent_arrived_at_destination(agent); });
        require(entered_future.wait_for(std::chrono::seconds(3)) == std::future_status::ready,
                "Wait never released the state mutex for sleeping");
        waiting.set_agent_arrived_at_destination(agent);
        release.set_value();
        guard.done = true;
        require(result.wait_for(std::chrono::seconds(3)) == std::future_status::ready && result.get(),
                "Destination notification did not wake the waiting transfer");
        std::cout << "PASS: concurrent reservation, isolated transfers and arrival during unlocked sleep\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
