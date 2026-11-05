/*
Copyright (c) 2012, Alex Regueiro
All rights reserved.
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
following conditions are met:

Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*/
// C++ adaptation of OpenSim/Framework/CircularBuffer.cs.
#pragma once
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace opensim::framework {
// Fixed-size shared arrays preserve C# array aliasing, including GetBuffer()
// references that remain alive after Capacity changes. Unlike vector<bool>,
// this also gives bool buffers real contiguous bool elements.
template<class T> class BufferArray {
public:
    explicit BufferArray(std::size_t size) : values_(new T[size]{}), size_(size) { }
    T& at(std::size_t index) const {
        if (index >= size_) throw std::out_of_range("Buffer array index");
        return values_[index];
    }
    T& operator[](std::size_t index) const { return at(index); }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    T* begin() const noexcept { return values_.get(); }
    T* end() const noexcept { return values_.get() + size_; }
    std::span<T> span() const noexcept { return {values_.get(), size_}; }
private:
    std::shared_ptr<T[]> values_;
    std::size_t size_;
};
// Preserves the source's distinct single/bulk overflow, Skip, resize and CopyTo
// semantics. This is intentionally not a replacement with conventional FIFO
// semantics. Like the original, operations require external synchronization.
template<class T> class CircularBuffer {
public:
    explicit CircularBuffer(std::int32_t capacity, bool overflow = false)
        : allow_overflow(overflow), capacity_(capacity), buffer_(checked_size(capacity)) { }
    bool allow_overflow;
    [[nodiscard]] std::int32_t capacity() const noexcept { return capacity_; }
    void capacity(std::int32_t value) {
        if (value == capacity_) return;
        if (value < size_) throw std::out_of_range("Capacity is too small.");
        BufferArray<T> replacement(checked_size(value));
        if (size_ > 0) copy_to(replacement.span());
        buffer_ = std::move(replacement);
        capacity_ = value; // Source intentionally leaves head and tail untouched.
    }
    [[nodiscard]] std::int32_t size() const noexcept { return size_; }
    [[nodiscard]] std::int32_t count() const noexcept { return size_; }
    [[nodiscard]] bool is_read_only() const noexcept { return false; }
    [[nodiscard]] bool is_synchronized() const noexcept { return false; }
    std::mutex& sync_root() noexcept { return sync_root_; }
    [[nodiscard]] bool contains(const T& item) const {
        auto index = head_;
        for (std::int32_t i = 0; i < size_; i = add(i, 1), index = add(index, 1)) {
            if (index == capacity_) index = 0;
            const auto& stored = buffer_.at(static_cast<std::size_t>(index));
            if constexpr (std::is_floating_point_v<T>) {
                // EqualityComparer<float/double>.Default uses Equals, which
                // considers two NaNs equal even though operator== does not.
                if (std::isnan(stored) && std::isnan(item)) return true;
            }
            if (stored == item) return true;
        }
        return false;
    }
    void clear() noexcept { size_ = head_ = tail_ = 0; }
    std::int32_t put(std::span<const T> source) { return put(source, 0, length(source.size())); }
    std::int32_t put(std::span<const T> source, std::int32_t offset, std::int32_t count) {
        if (!allow_overflow && count > subtract(capacity_, size_))
            throw std::overflow_error("Buffer Overflow");
        auto source_index = offset;
        for (std::int32_t copied = 0; copied < count; copied = add(copied, 1)) {
            if (tail_ == capacity_) tail_ = 0;
            // Managed bounds exceptions must never become native memory corruption.
            if (source_index < 0 || static_cast<std::size_t>(source_index) >= source.size())
                throw std::out_of_range("Source array index");
            buffer_.at(static_cast<std::size_t>(tail_)) = source[static_cast<std::size_t>(source_index)];
            tail_ = add(tail_, 1);
            source_index = add(source_index, 1);
        }
        size_ = std::min(add(size_, count), capacity_);
        return count;
    }
    void put(const T& item) {
        if (!allow_overflow && size_ == capacity_) throw std::overflow_error("Buffer Overflow");
        buffer_.at(static_cast<std::size_t>(tail_)) = item;
        tail_ = add(tail_, 1);
        if (tail_ == capacity_) tail_ = 0;
        size_ = add(size_, 1);
    }
    void add_item(const T& item) { put(item); }
    // ICollection.Remove in the source ignores item and removes the head.
    bool remove(const T& /*item*/) {
        if (size_ == 0) return false;
        get();
        return true;
    }
    void skip(std::int32_t count) noexcept {
        head_ = add(head_, count);
        if (head_ >= capacity_) head_ = subtract(head_, capacity_);
    }
    std::vector<T> get(std::int32_t count) {
        BufferArray<T> result(checked_size(count));
        get(result.span());
        return std::vector<T>(result.begin(), result.end());
    }
    std::int32_t get(std::span<T> destination) { return get(destination, 0, length(destination.size())); }
    std::int32_t get(std::span<T> destination, std::int32_t offset, std::int32_t count) {
        const auto real_count = std::min(count, size_);
        auto destination_index = offset;
        for (std::int32_t copied = 0; copied < real_count; copied = add(copied, 1)) {
            if (head_ == capacity_) head_ = 0;
            if (destination_index < 0 || static_cast<std::size_t>(destination_index) >= destination.size())
                throw std::out_of_range("Destination array index");
            destination[static_cast<std::size_t>(destination_index)] = buffer_.at(static_cast<std::size_t>(head_));
            head_ = add(head_, 1);
            destination_index = add(destination_index, 1);
        }
        size_ = subtract(size_, real_count);
        return real_count;
    }
    T get() {
        if (size_ == 0) throw std::underflow_error("Buffer Empty");
        auto item = buffer_.at(static_cast<std::size_t>(head_));
        head_ = add(head_, 1);
        if (head_ == capacity_) head_ = 0;
        size_ = add(size_, -1);
        return item;
    }
    void copy_to(std::span<T> destination, std::int32_t array_index = 0) const {
        copy_to(0, destination, array_index, size_);
    }
    void copy_to(std::int32_t /*index*/, std::span<T> destination, std::int32_t array_index, std::int32_t count) const {
        if (count > size_) throw std::out_of_range("Count Too Large");
        auto buffer_index = head_;
        for (std::int32_t copied = 0; copied < count; copied = add(copied, 1)) {
            if (buffer_index == capacity_) buffer_index = 0;
            if (array_index < 0 || static_cast<std::size_t>(array_index) >= destination.size())
                throw std::out_of_range("Destination array index");
            destination[static_cast<std::size_t>(array_index)] = buffer_.at(static_cast<std::size_t>(buffer_index));
            buffer_index = add(buffer_index, 1);
            array_index = add(array_index, 1);
        }
    }
    [[nodiscard]] std::vector<T> to_array() const {
        BufferArray<T> result(checked_size(size_));
        copy_to(result.span());
        return std::vector<T>(result.begin(), result.end());
    }
    BufferArray<T> get_buffer() const noexcept { return buffer_; }
    // Enumeration has the same logical order as ToArray. It exposes references,
    // including changes to the underlying buffer made during enumeration.
    class Iterator {
    public:
        Iterator(const CircularBuffer* owner, std::int32_t position, std::int32_t index)
            : owner_(owner), position_(position), index_(index) { }
        const T& operator*() const {
            return owner_->buffer_.at(static_cast<std::size_t>(index_ == owner_->capacity_ ? 0 : index_));
        }
        Iterator& operator++() {
            if (index_ == owner_->capacity_) index_ = 0;
            index_ = add(index_, 1);
            position_ = add(position_, 1);
            return *this;
        }
        bool operator!=(const Iterator& /*end*/) const { return position_ < owner_->size_; }
    private:
        const CircularBuffer* owner_;
        std::int32_t position_, index_;
    };
    Iterator begin() const { return {this, 0, head_}; }
    Iterator end() const { return {this, size_, head_}; }
private:
    static std::int32_t add(std::int32_t a, std::int32_t b) noexcept {
        return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
    }
    static std::int32_t subtract(std::int32_t a, std::int32_t b) noexcept {
        return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
    }
    static std::size_t checked_size(std::int32_t value) {
        if (value < 0) throw std::invalid_argument("Negative array length");
        return static_cast<std::size_t>(value);
    }
    static std::int32_t length(std::size_t value) {
        if (value > 2147483647u) throw std::length_error("Array length exceeds Int32");
        return static_cast<std::int32_t>(value);
    }
    std::int32_t capacity_, size_ = 0, head_ = 0, tail_ = 0;
    BufferArray<T> buffer_;
    std::mutex sync_root_;
};
}
