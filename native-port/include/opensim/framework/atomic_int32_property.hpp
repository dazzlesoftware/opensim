// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <atomic>
#include <bit>
#include <cstdint>
namespace opensim::framework {
// Separate source Int32 property reads/writes; deliberately not atomic RMW.
class AtomicInt32Property {
public:
    AtomicInt32Property(std::int32_t value=0):value_(value){}
    AtomicInt32Property(const AtomicInt32Property& other):value_(static_cast<std::int32_t>(other)){}
    operator std::int32_t()const noexcept{return value_.load(std::memory_order_relaxed);}
    AtomicInt32Property& operator=(std::int32_t value)noexcept{value_.store(value,std::memory_order_relaxed);return *this;}
    AtomicInt32Property& operator=(const AtomicInt32Property& other)noexcept{return *this=static_cast<std::int32_t>(other);}
    std::int32_t operator++(int)noexcept{const auto old=value_.load(std::memory_order_relaxed);value_.store(std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(old)+1u),std::memory_order_relaxed);return old;}
    std::int32_t operator++()noexcept{const auto old=(*this)++;return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(old)+1u);}
    std::int32_t operator--(int)noexcept{const auto old=value_.load(std::memory_order_relaxed);value_.store(std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(old)-1u),std::memory_order_relaxed);return old;}
    std::int32_t operator--()noexcept{const auto old=(*this)--;return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(old)-1u);}
private:
    std::atomic<std::int32_t> value_;
};
}
