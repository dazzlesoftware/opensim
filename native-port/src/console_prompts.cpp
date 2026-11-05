// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/console_prompts.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
namespace opensim::framework {
namespace {
const std::u16string &require(const ConsolePrompts::Text &value) {
    if (!value)
        throw ConsolePromptNullError();
    return *value;
}
std::u16string label(const ConsolePrompts::Text &text, const ConsolePrompts::Text &fallback, bool brackets) {
    auto result = text.value_or(u"");
    if (brackets) {
        result += u" [";
        if (fallback)
            result += *fallback;
        result += u"]";
    }
    result += u": ";
    return result;
}
std::u16string rejection(char16_t character) {
    std::u16string result = u"The character \"";
    result += character;
    result += u"\" is not permitted.";
    return result;
}
}
ConsolePrompts::ConsolePrompts(Input input, Diagnostic diagnostic)
    : input_(std::move(input)), diagnostic_(std::move(diagnostic)) {
    if (!input_)
        throw std::invalid_argument("Console prompt input unavailable");
    if (!diagnostic_)
        diagnostic_ = [](const auto &value) { std::cout << utf8(value) << '\n' << std::flush; };
}
ConsolePrompts::Text ConsolePrompts::prompt(Text text) const {
    return input_(label(text, {}, false), false, true);
}
ConsolePrompts::Text ConsolePrompts::prompt_default(Text text, Text fallback) const {
    auto value = input_(label(text, fallback, true), false, true);
    if (require(value).empty())
        value = fallback;
    return value;
}
ConsolePrompts::Text ConsolePrompts::prompt_excluding(Text text, Excluded excluded) const {
    while (true) {
        auto value = prompt(text);
        if (!excluded)
            throw ConsolePromptNullError();
        bool valid = true;
        excluded->each([&](auto c) {
            if (require(value).find(c) != std::u16string::npos) {
                diagnostic_(rejection(c));
                valid = false;
            }
        });
        if (valid)
            return value;
    }
}
ConsolePrompts::Text ConsolePrompts::prompt_default_excluding(Text text, Text fallback, Excluded excluded,
                                                              bool echo) const {
    while (true) {
        auto value = input_(label(text, fallback, fallback.has_value()), false, echo);
        bool valid = true;
        if (require(value).empty() && fallback)
            value = fallback;
        else if (excluded)
            excluded->each([&](auto c) {
                if (require(value).find(c) != std::u16string::npos) {
                    diagnostic_(rejection(c));
                    valid = false;
                }
            });
        if (valid)
            return value;
    }
}
ConsolePrompts::Text ConsolePrompts::prompt_options(Text text, Text fallback, Options options) const {
    if (!options)
        throw ConsolePromptNullError();
    std::u16string description;
    options->each([&](const auto &option) {
        description += u" ";
        if (option)
            description += *option;
    });
    auto value = prompt_default(text, fallback);
    while (!options->contains(value)) {
        std::u16string message = u"Valid options are";
        message += description;
        diagnostic_(message);
        value = prompt_default(text, fallback);
    }
    return value;
}
}
