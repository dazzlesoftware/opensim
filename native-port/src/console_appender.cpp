// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/console_appender.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include <iostream>
namespace opensim::framework {
ConsoleAppender::ConsoleAppender(Write write, ExceptionText exception_text)
    : write_(std::move(write)), exception_text_(std::move(exception_text)) {
    if (!write_)
        write_ = [](const auto& text, bool newline) {
            std::cout << utf8(text);
            if (newline)
                std::cout << '\n';
            std::cout << std::flush;
        };
    if (!exception_text_)
        exception_text_ = [](std::exception_ptr failure) -> std::u16string {
            try {
                std::rethrow_exception(failure);
            } catch (const ConsoleAppenderFailure& error) {
                return error.source_text;
            } catch (const std::exception& error) {
                return utf16_replacing(error.what());
            } catch (...) {
                return u"Unknown native exception";
            }
        };
}
void ConsoleAppender::append(std::optional<ConsoleLogEvent> event, const Render& render) const {
    // RenderLoggingEvent is outside the original Append's catch block.
    auto message = render();
    try {
        if (auto target = console_.load()) {
            if (!event)
                throw ConsoleAppenderFailure(
                    u"System.NullReferenceException: Object reference not set to an instance of an object.");
            auto level = event->level == console_log_error  ? u"error"
                         : event->level == console_log_warn ? u"warn"
                                                            : u"normal";
            (*target)(message, level);
        } else
            write_(message, !invariant_ends_with_utf16(message, u"\n"));
    } catch (...) {
        auto text = std::u16string(u"Couldn't write out log message: ");
        text += exception_text_(std::current_exception());
        write_(text, true);
    }
}
void ConsoleAppender::append(std::int32_t level, const std::u16string& message) const {
    append(ConsoleLogEvent{level}, [&] { return message; });
}
}
