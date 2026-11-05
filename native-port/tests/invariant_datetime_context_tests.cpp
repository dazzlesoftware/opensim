// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/invariant_datetime_context.hpp"
#include "../src/invariant_datetime_context_platform.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace opensim::framework;
namespace {
InvariantTimeZoneTransition transition(std::istream& in) {
    InvariantTimeZoneTransition t;
    in >> t.time_of_day_ticks >> t.month >> t.week >> t.day >> t.day_of_week >> t.fixed_date;
    return t;
}
std::vector<std::uint8_t> bytes(const std::string& s) {
    std::vector<std::uint8_t> b;
    for (std::size_t i = 0; i < s.size(); i += 2)
        b.push_back(static_cast<std::uint8_t>(std::stoul(s.substr(i, 2), nullptr, 16)));
    return b;
}
void print_model(const invariant_datetime_platform::ZoneModel& m) {
    std::cout << "ZONE " << m.base_offset << ' ' << m.rules.size() << '\n';
    for (auto& r : m.rules) {
        std::cout << r.start.serialized_data() << ' ' << r.end.serialized_data() << ' '
                  << r.daylight_delta_ticks << ' ' << r.base_offset_delta_ticks << ' '
                  << r.no_daylight_transitions;
        for (auto t : {r.daylight_start, r.daylight_end})
            std::cout << ' ' << t.time_of_day_ticks << ' ' << t.month << ' ' << t.week << ' ' << t.day << ' '
                      << t.day_of_week << ' ' << t.fixed_date;
        std::cout << '\n';
    }
    std::cout << "END\n" << std::flush;
}
void probe() {
    std::shared_ptr<InvariantTimeZoneRules> zone, current_zone;
    std::optional<InvariantDateTimeParseContext> native_context;
    for (std::string line; std::getline(std::cin, line);) {
        try {
            std::istringstream row(line);
            std::string command;
            row >> command;
            if (command == "CONVERT") {
#ifdef _WIN32
                std::string hex;
                row >> hex;
                auto b = bytes(hex);
                std::u16string input;
                for (std::size_t i = 0; i + 1 < b.size(); i += 2)
                    input.push_back(static_cast<char16_t>(b[i] | unsigned(b[i + 1]) << 8));
                auto mapped = invariant_datetime_platform::windows_system_iana_to_windows(input);
                if (!mapped)
                    std::cout << "~";
                else
                    for (auto c : *mapped)
                        for (auto v : {unsigned(c) & 255, unsigned(c) >> 8})
                            std::cout << "0123456789abcdef"[v >> 4] << "0123456789abcdef"[v & 15];
                std::cout << '\n' << std::flush;
#else
                throw std::runtime_error("System ICU Windows mapping requires Windows execution");
#endif
            } else if (command == "TZIF" || command == "TZIF_LOCAL") {
                std::string hex, id = "Audit";
                row >> hex;
                row >> id;
                auto clock = []() { return invariant_datetime_platform::native_utc_now().ticks(); };
                auto model = command == "TZIF_LOCAL"
                                 ? invariant_datetime_platform::unix_tzif_local_data(bytes(hex), id, clock)
                                 : invariant_datetime_platform::unix_tzif_zone(bytes(hex), id, false, clock);
                zone = std::make_shared<InvariantTimeZoneRules>(model.base_offset, model.rules);
                print_model(model);
            } else if (command == "NATIVE") {
                int mode = 0;
                row >> mode;
                native_context = NativeInvariantDateTimeContext(mode != 0).callbacks();
                std::cout << "READY\n" << std::flush;
            } else if (command == "TOLOCAL" || command == "FROMFILE" || command == "TOFILE") {
                NativeInvariantDateTimeContext owner;
                std::string value;
                row >> value;
                try {
                    if (command == "FROMFILE")
                        std::cout << owner.from_file_time_utc(std::stoll(value)).serialized_data();
                    else if (command == "TOLOCAL")
                        std::cout << owner.to_local_time(AssetDateTime::from_serialized(std::stoull(value)))
                                         .serialized_data();
                    else
                        std::cout << owner.to_file_time_utc(
                            AssetDateTime::from_serialized(std::stoull(value)));
                } catch (const NativeDateTimeError& e) {
                    std::cout << "ERR|" << e.hresult() << '|' << e.parameter.value_or("~") << '|' << e.what();
                }
                std::cout << '\n' << std::flush;
            } else if (command == "CUTOFF") {
                std::cout << native_context->two_digit_year_max() << '\n' << std::flush;
            } else if (command == "UTCNOW") {
                std::cout << invariant_datetime_platform::native_utc_now().serialized_data() << '\n'
                          << std::flush;
            } else if (command == "NOW") {
                std::cout << native_context->now().serialized_data() << '\n' << std::flush;
            } else if (command == "LEAP") {
                int y, m, d, h, minute;
                row >> y >> m >> d >> h >> minute;
                std::cout << native_context->valid_leap_second(y, m, d, h, minute) << '\n' << std::flush;
            } else if (command == "WZONE") {
#ifdef _WIN32
                std::string name;
                std::getline(row, name);
                if (!name.empty())
                    name.erase(0, 1);
                auto chars = utf16_replacing(name);
                auto m = name == "LOCAL" ? invariant_datetime_platform::windows_local_zone()
                                         : invariant_datetime_platform::windows_registry_zone(
                                               std::wstring(chars.begin(), chars.end()));
                zone = std::make_shared<InvariantTimeZoneRules>(m.base_offset, std::move(m.rules));
                std::cout << "READY\n" << std::flush;
#else
                throw std::runtime_error("Windows native loader requires Windows execution");
#endif
            } else if ((command == "ZONE" || command == "CURRENT_ZONE")) {
                std::int64_t base;
                int count;
                row >> base >> count;
                std::vector<InvariantTimeZoneRule> rules;
                for (int i = 0; i < count; ++i) {
                    std::getline(std::cin, line);
                    std::istringstream r(line);
                    InvariantTimeZoneRule rule;
                    std::uint64_t start, end;
                    r >> start >> end >> rule.daylight_delta_ticks >> rule.base_offset_delta_ticks >>
                        rule.no_daylight_transitions;
                    rule.start = AssetDateTime::from_serialized(start);
                    rule.end = AssetDateTime::from_serialized(end);
                    rule.daylight_start = transition(r);
                    rule.daylight_end = transition(r);
                    if (!r)
                        throw std::runtime_error("Truncated rule");
                    rules.push_back(rule);
                }
                auto next = std::make_shared<InvariantTimeZoneRules>(base, std::move(rules));
                if (command == "CURRENT_ZONE")
                    current_zone = next;
                else
                    zone = next;
                std::cout << "READY\n" << std::flush;
            } else if (command == "NOWOFFSET") {
                std::int64_t ticks;
                row >> ticks;
                auto o = current_zone->current_year_offset(
                    ticks, [zone]() -> std::shared_ptr<const InvariantTimeZoneRules> { return zone; });
                std::cout << o.ticks << '|' << o.ambiguous_daylight << '\n' << std::flush;
            } else if (command == "U") {
                std::int64_t ticks;
                row >> ticks;
                auto o = native_context ? native_context->utc_offset(ticks) : zone->utc_offset(ticks);
                std::cout << o.ticks << '|' << o.ambiguous_daylight << '\n' << std::flush;
            } else if (command == "L") {
                std::uint64_t raw;
                row >> raw;
                std::cout << (native_context
                                  ? native_context->local_offset_no_throw(AssetDateTime::from_serialized(raw))
                                  : zone->local_offset_no_throw(AssetDateTime::from_serialized(raw)))
                          << '\n'
                          << std::flush;
            } else
                throw std::runtime_error("Unknown query");
        } catch (const std::exception& e) {
            std::cout << "EX:" << e.what() << '\n' << std::flush;
        }
    }
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--probe") {
            probe();
            return 0;
        }
        InvariantTimeZoneRules utc(0, {});
        if (utc.utc_offset(0).ticks || utc.utc_offset(3155378975999999999ll).ambiguous_daylight)
            throw std::runtime_error("UTC boundaries");
        std::cout << "PASS: native timezone rule smoke\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
