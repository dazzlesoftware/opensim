// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/atomic_shared_ptr.hpp"
#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
namespace opensim::framework {
struct ConsoleLogEvent {
    std::optional<std::int32_t> level;
};
// Values from the project's installed log4net Level, not ordinal severities.
inline constexpr std::int32_t console_log_error = 70000, console_log_warn = 60000, console_log_info = 40000,
                              console_log_debug = 30000;
class ConsoleAppenderFailure : public std::runtime_error {
  public:
    std::u16string source_text;
    explicit ConsoleAppenderFailure(std::u16string text)
        : std::runtime_error("Console appender callback failed"), source_text(std::move(text)) {}
};
class ConsoleAppender {
  public:
    using Output = std::function<void(const std::u16string&, const std::u16string&)>;
    using Render = std::function<std::u16string()>;
    using Write = std::function<void(const std::u16string&, bool newline)>;
    using ExceptionText = std::function<std::u16string(std::exception_ptr)>;
    explicit ConsoleAppender(Write = {}, ExceptionText = {});
    void console(std::shared_ptr<Output> value) {
        console_.store(std::move(value));
    }
    std::shared_ptr<Output> console() const {
        return console_.load();
    }
    void append(std::optional<ConsoleLogEvent>, const Render&) const;
    void append(std::int32_t level, const std::u16string& message) const;

  private:
    opensim::framework::AtomicSharedPtr<Output> console_;
    Write write_;
    ExceptionText exception_text_;
};
}
