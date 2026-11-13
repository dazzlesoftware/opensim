// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once

#include <atomic>
#include <memory>
#include <utility>

namespace opensim::framework {

#if defined(__cpp_lib_atomic_shared_ptr) && !defined(OPENSIM_USE_SHARED_PTR_ATOMIC_FUNCTIONS)
template<class T>
using AtomicSharedPtr = std::atomic<std::shared_ptr<T>>;
#else
// Some C++20 standard libraries only provide the shared_ptr atomic free functions.
template<class T>
class AtomicSharedPtr {
public:
    AtomicSharedPtr() noexcept = default;
    explicit AtomicSharedPtr(std::shared_ptr<T> value) noexcept : value_(std::move(value)) {}
    AtomicSharedPtr(const AtomicSharedPtr&) = delete;
    AtomicSharedPtr& operator=(const AtomicSharedPtr&) = delete;

    void operator=(std::shared_ptr<T> value) noexcept {
        store(std::move(value));
    }
    std::shared_ptr<T> load(std::memory_order order = std::memory_order_seq_cst) const noexcept {
        return std::atomic_load_explicit(&value_, order);
    }
    void store(std::shared_ptr<T> value, std::memory_order order = std::memory_order_seq_cst) noexcept {
        std::atomic_store_explicit(&value_, std::move(value), order);
    }
    std::shared_ptr<T> exchange(std::shared_ptr<T> value,
                                std::memory_order order = std::memory_order_seq_cst) noexcept {
        return std::atomic_exchange_explicit(&value_, std::move(value), order);
    }
    bool compare_exchange_strong(std::shared_ptr<T>& expected, std::shared_ptr<T> desired) noexcept {
        return std::atomic_compare_exchange_strong(&value_, &expected, std::move(desired));
    }

private:
    std::shared_ptr<T> value_;
};
#endif

}
