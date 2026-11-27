#include "opensim/framework/dotnet_identity_hash.hpp"
#include <mutex>

namespace opensim::framework {
std::uint32_t dotnet_identity_hash(std::uint32_t &state, std::uint32_t thread_id) noexcept {
    std::uint32_t result;
    do {
        state = state * (thread_id * 4u + 5u) + 1u;
        result = state >> 6;
    } while (!result);
    return result;
}

namespace {
struct ThreadState {
    std::uint32_t state, id;
};

ThreadState allocate_thread() noexcept {
    static std::mutex mutex;
    static std::uint32_t seed = 123456789u, next_id = 0;
    std::lock_guard lock(mutex);
    seed = seed * 1566083941u + 1u;
    return {seed, ++next_id};
}
} // namespace

std::uint32_t dotnet_identity_hash() noexcept {
    thread_local auto thread = allocate_thread();
    return dotnet_identity_hash(thread.state, thread.id);
}
} // namespace opensim::framework
