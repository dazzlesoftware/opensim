// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/execution_stack.hpp"
#include <cstddef>
#include <iostream>
#include <thread>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define STACK_NOINLINE __declspec(noinline)
#else
#include <pthread.h>
#define STACK_NOINLINE __attribute__((noinline))
#endif
namespace {
struct Result {
    bool initial = false, stopped = false;
    std::size_t depth = 0;
};
STACK_NOINLINE bool consume(std::size_t &depth) {
    volatile unsigned char frame[4096];
    for (std::size_t i = 0; i < sizeof(frame); ++i)
        frame[i] = 42;
    if (!opensim::framework::sufficient_execution_stack())
        return true;
    if (++depth > 4096)
        return false;
    const bool result = consume(depth);
    return result && frame[depth % sizeof(frame)] == 42;
}
void probe(Result &result, bool recurse) {
    result.initial = opensim::framework::sufficient_execution_stack();
    if (recurse && result.initial)
        result.stopped = consume(result.depth);
}
struct Work {
    Result *result;
    bool recurse;
};
#if defined(_WIN32)
DWORD WINAPI worker(void *value) {
    auto &work = *static_cast<Work *>(value);
    probe(*work.result, work.recurse);
    return 0;
}
#else
void *worker(void *value) {
    auto &work = *static_cast<Work *>(value);
    probe(*work.result, work.recurse);
    return nullptr;
}
#endif
bool run(std::size_t size, Result &result, bool recurse) {
    Work work{&result, recurse};
#if defined(_WIN32)
    HANDLE thread = CreateThread(nullptr, size, worker, &work, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (!thread)
        return false;
    const bool joined = WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0;
    CloseHandle(thread);
    return joined;
#else
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0)
        return false;
    if (pthread_attr_setstacksize(&attributes, size) != 0) {
        pthread_attr_destroy(&attributes);
        return false;
    }
    pthread_t thread;
    const int error = pthread_create(&thread, &attributes, worker, &work);
    pthread_attr_destroy(&attributes);
    return error == 0 && pthread_join(thread, nullptr) == 0;
#endif
}
}
int main() {
    Result small, large;
    // The minimum stack is below both supported reserve sizes. No recursion.
    if (!run(32 * 1024, small, false) || small.initial) {
        std::cerr << "small stack was accepted\n";
        return 1;
    }
    if (!run(1024 * 1024, large, true) || !large.initial || !large.stopped || large.depth == 0) {
        std::cerr << "large stack guard did not stop recursion\n";
        return 1;
    }
    bool thread_ok = false;
    std::thread ordinary([&] { thread_ok = opensim::framework::sufficient_execution_stack(); });
    ordinary.join();
    if (!thread_ok) {
        std::cerr << "ordinary worker stack rejected\n";
        return 1;
    }
    std::cout << "PASS: small stack rejected, large stack recursion stopped at " << large.depth
              << ", ordinary worker accepted\n";
}
