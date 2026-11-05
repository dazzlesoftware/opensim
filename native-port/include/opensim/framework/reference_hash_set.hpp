// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>
namespace opensim::framework {
// The default reference equality used by the project's IClientAPI implementations.
// Entry slots, free-slot reuse and enumerators follow .NET8 HashSet<T>.
template <class T> class ReferenceHashSet {
  public:
    using value_type = std::shared_ptr<T>;

  private:
    struct State {
        std::vector<std::optional<value_type>> slots;
        std::vector<std::size_t> free;
        std::unordered_map<const T *, std::size_t> index;
        std::uint32_t version = 0;
    };
    std::shared_ptr<State> state_ = std::make_shared<State>();
    static void modified() {
        throw std::runtime_error("Collection was modified; enumeration operation may not execute.");
    }

  public:
    ReferenceHashSet() = default;
    ReferenceHashSet(const ReferenceHashSet &) = delete;
    ReferenceHashSet &operator=(const ReferenceHashSet &) = delete;
    class Iterator;
    class Enumerator {
        friend class Iterator;
        friend class ReferenceHashSet;
        std::shared_ptr<State> state_;
        std::uint32_t version_;
        std::size_t index_ = 0;
        value_type current_;
        explicit Enumerator(std::shared_ptr<State> state)
            : state_(std::move(state)), version_(state_->version) {}

      public:
        bool move_next() {
            if (version_ != state_->version)
                modified();
            while (index_ < state_->slots.size()) {
                const auto &slot = state_->slots[index_++];
                if (slot) {
                    current_ = *slot;
                    return true;
                }
            }
            index_ = state_->slots.size() + 1;
            current_.reset();
            return false;
        }
        const value_type &current() const { return current_; }
        value_type boxed_current() const {
            if (index_ == 0 || index_ == state_->slots.size() + 1)
                throw std::runtime_error("Enumeration has either not started or has already finished.");
            return current_;
        }
        void reset() {
            if (version_ != state_->version)
                modified();
            index_ = 0;
            current_.reset();
        }
        void dispose() const {}
    };
    class Iterator {
        std::optional<Enumerator> enumerator_;
        bool end_ = true;

      public:
        Iterator() = default;
        explicit Iterator(Enumerator enumerator) : enumerator_(std::move(enumerator)) {
            end_ = !enumerator_->move_next();
        }
        const value_type &operator*() const { return enumerator_->current(); }
        const value_type *operator->() const { return &**this; }
        Iterator &operator++() {
            end_ = !enumerator_->move_next();
            return *this;
        }
        bool operator==(const Iterator &other) const {
            if (end_ || other.end_)
                return end_ == other.end_;
            return enumerator_->state_ == other.enumerator_->state_ &&
                   enumerator_->index_ == other.enumerator_->index_;
        }
    };
    Enumerator get_enumerator() const { return Enumerator(state_); }
    Iterator begin() const { return Iterator(get_enumerator()); }
    Iterator end() const { return Iterator(); }
    std::size_t size() const { return state_->index.size(); }
    bool empty() const { return size() == 0; }
    bool contains(const value_type &value) const { return state_->index.contains(value.get()); }
    bool insert(value_type value) {
        if (contains(value))
            return false;
        const auto slot = state_->free.empty() ? state_->slots.size() : state_->free.back();
        auto key = value.get();
        state_->index.emplace(key, slot);
        try {
            if (state_->free.empty())
                state_->slots.emplace_back(std::move(value));
            else {
                state_->slots[slot] = std::move(value);
                state_->free.pop_back();
            }
        } catch (...) {
            state_->index.erase(key);
            throw;
        }
        ++state_->version;
        return true;
    }
    bool erase(const value_type &value) {
        auto found = state_->index.find(value.get());
        if (found == state_->index.end())
            return false;
        state_->free.push_back(found->second);
        state_->slots[found->second].reset();
        state_->index.erase(found);
        return true;
    }
    void clear() {
        state_->slots.clear();
        state_->free.clear();
        state_->index.clear();
    }
};
}
