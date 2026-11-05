// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/status_date_format.hpp"
#include "opensim/services/flotsam_cache_console.hpp"
#include <array>
#include <chrono>
#include <stdexcept>
namespace opensim::framework {
std::string format_invariant_status_date(AssetDateTime value) {
    constexpr std::int64_t day_ticks = 864000000000ll, unix_epoch_days = 719162;
    const auto days = value.ticks() / day_ticks;
    const auto time = value.ticks() % day_ticks;
    const auto date =
        std::chrono::year_month_day(std::chrono::sys_days(std::chrono::days(days - unix_epoch_days)));
    unsigned year = static_cast<unsigned>(int(date.year())), month = unsigned(date.month()),
             day = unsigned(date.day());
    unsigned hour = static_cast<unsigned>(time / 36000000000ll) % 12;
    if (!hour)
        hour = 12;
    unsigned minute = static_cast<unsigned>(time / 600000000ll % 60),
             second = static_cast<unsigned>(time / 10000000ll % 60);
    std::array<char, 19> output{};
    output[2] = output[5] = '/';
    output[10] = ' ';
    output[13] = output[16] = ':';
    auto two = [&](std::size_t at, unsigned number) {
        output[at] = static_cast<char>('0' + number / 10);
        output[at + 1] = static_cast<char>('0' + number % 10);
    };
    two(0, month);
    two(3, day);
    output[6] = static_cast<char>('0' + year / 1000);
    output[7] = static_cast<char>('0' + year / 100 % 10);
    output[8] = static_cast<char>('0' + year / 10 % 10);
    output[9] = static_cast<char>('0' + year % 10);
    two(11, hour);
    two(14, minute);
    two(17, second);
    return {output.data(), output.size()};
}
std::string format_status_date(AssetDateTime value, const std::string& pattern,
                               const StatusDateFormatBackend& backend) {
    if (!backend.is_invariant_culture || !backend.current_culture_format)
        throw std::invalid_argument("Status date formatting requires culture and general-format providers");
    if (pattern == invariant_status_date_pattern && backend.is_invariant_culture())
        return format_invariant_status_date(value);
    return backend.current_culture_format(value, pattern);
}
}
namespace opensim::services {
void bind_flotsam_status_date_format(FlotsamConsoleBackend& backend,
                                     std::shared_ptr<framework::StatusDateFormatBackend> provider) {
    if (!provider || !provider->is_invariant_culture || !provider->current_culture_format)
        throw std::invalid_argument("Status date formatting requires culture and general-format providers");
    backend.format_date = [provider = std::move(provider)](framework::AssetDateTime value,
                                                           const std::string& pattern) {
        return framework::format_status_date(value, pattern, *provider);
    };
}
}
