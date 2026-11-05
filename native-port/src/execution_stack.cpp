#include "opensim/framework/execution_stack.hpp"
#include <cstdint>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif
namespace opensim::framework {
bool sufficient_execution_stack() noexcept {
    char marker;
    const auto current = reinterpret_cast<std::uintptr_t>(&marker);
    std::uintptr_t low = 0, high = 0;
#if defined(_WIN32)
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(&marker, &info, sizeof(info)) != sizeof(info))
        return false;
    low = reinterpret_cast<std::uintptr_t>(info.AllocationBase);
    ULONG_PTR committed_low = 0, stack_high = 0;
    GetCurrentThreadStackLimits(&committed_low, &stack_high);
    high = stack_high;
#elif defined(__APPLE__)
    high = reinterpret_cast<std::uintptr_t>(pthread_get_stackaddr_np(pthread_self()));
    const auto size = pthread_get_stacksize_np(pthread_self());
    if (size > high)
        return false;
    low = high - size;
#elif defined(__linux__)
    pthread_attr_t attributes;
    if (pthread_getattr_np(pthread_self(), &attributes) != 0)
        return false;
    void* base = nullptr;
    std::size_t size = 0;
    const auto result = pthread_attr_getstack(&attributes, &base, &size);
    pthread_attr_destroy(&attributes);
    if (result != 0)
        return false;
    low = reinterpret_cast<std::uintptr_t>(base);
    high = low + size;
#else
#error Unsupported execution stack platform
#endif
    constexpr std::uintptr_t reserve = sizeof(void*) == 8 ? 128 * 1024 : 64 * 1024;
    return low < current && current <= high && current - low > reserve;
}
}
