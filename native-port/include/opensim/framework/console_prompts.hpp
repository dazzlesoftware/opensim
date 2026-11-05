// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <stdexcept>
#include <memory>
#include <cstdint>
#include <algorithm>
namespace opensim::framework {
class ConsolePromptNullError : public std::runtime_error {
public:
    ConsolePromptNullError() : std::runtime_error("Null console prompt input or list") {}
};
class ConsolePromptListModifiedError : public std::runtime_error {
public:
    ConsolePromptListModifiedError() : std::runtime_error("Collection was modified") {}
};
// Shared List<T> identity and enumerator invalidation for callbacks that change
// validation lists while a prompt or a diagnostic is being processed.
template <class T> class ConsolePromptList {
    struct Storage {
        std::vector<T> values;
        std::uint32_t version = 0;
    };
    std::shared_ptr<Storage> storage_ = std::make_shared<Storage>();

public:
    ConsolePromptList() = default;
    ConsolePromptList(std::vector<T> values) {
        storage_->values = std::move(values);
    }
    void add(T value) const {
        storage_->values.push_back(std::move(value));
        ++storage_->version;
    }
    void clear() const {
        storage_->values.clear();
        ++storage_->version;
    }
    void set(std::size_t index, T value) const {
        storage_->values.at(index) = std::move(value);
        ++storage_->version;
    }
    void remove_at(std::size_t index) const {
        if (index >= storage_->values.size())
            throw std::out_of_range("Prompt list index");
        storage_->values.erase(storage_->values.begin() + index);
        ++storage_->version;
    }
    bool contains(const T& value) const {
        return std::find(storage_->values.begin(), storage_->values.end(), value) != storage_->values.end();
    }
    template <class F> void each(F visitor) const {
        auto version = storage_->version;
        std::size_t index = 0;
        while (true) {
            if (storage_->version != version)
                throw ConsolePromptListModifiedError();
            if (index >= storage_->values.size())
                return;
            auto value = storage_->values[index++];
            visitor(value);
        }
    }
};
// ConsoleBase prompt overloads. Diagnostics deliberately go to the local console,
// as the original System.Console.WriteLine calls do, rather than virtual Output.
class ConsolePrompts {
public:
    using Text = std::optional<std::u16string>;
    using Excluded = std::optional<ConsolePromptList<char16_t>>;
    using Options = std::optional<ConsolePromptList<Text>>;
    using Input = std::function<Text(const std::u16string&, bool, bool)>;
    using Diagnostic = std::function<void(const std::u16string&)>;
    explicit ConsolePrompts(Input, Diagnostic = {});
    Text prompt(Text) const;
    Text prompt_default(Text, Text) const;
    Text prompt_excluding(Text, Excluded) const;
    Text prompt_default_excluding(Text, Text, Excluded, bool echo = true) const;
    Text prompt_options(Text, Text, Options) const;

private:
    Input input_;
    Diagnostic diagnostic_;
};
}
