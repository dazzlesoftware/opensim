// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/invariant_datetime_parse.hpp"
#include <iostream>
#include <sstream>
#include <tuple>
#include <vector>
#include <stdexcept>
using namespace opensim::framework;
namespace opensim::framework::invariant_datetime_test {
std::vector<std::tuple<std::u16string, int, int>> token_profile(bool);
}
namespace {
std::string encode(std::u16string_view value) {
    std::string out;
    for (auto c : value)
        for (unsigned byte : {unsigned(c) & 255, unsigned(c) >> 8}) {
            out += "0123456789abcdef"[byte >> 4];
            out += "0123456789abcdef"[byte & 15];
        }
    return out.empty() ? "." : out;
}
std::u16string decode(const std::string& text) {
    if (text == ".")
        return {};
    if (text.size() % 4)
        throw std::runtime_error("Invalid UTF16 fixture");
    std::u16string out;
    for (std::size_t i = 0; i < text.size(); i += 4)
        out.push_back(static_cast<char16_t>(std::stoul(text.substr(i, 2), nullptr, 16) |
                                            (std::stoul(text.substr(i + 2, 2), nullptr, 16) << 8)));
    return out;
}
std::string request(const std::string& line) {
    std::cout << "CTX " << line << '\n' << std::flush;
    std::string value;
    if (!std::getline(std::cin, value))
        throw std::runtime_error("Missing source context reply");
    return value;
}
void probe() {
    for (std::string line; std::getline(std::cin, line);) {
        try {
            std::istringstream row(line);
            std::string input, now;
            int cutoff, mode;
            row >> input >> now >> cutoff >> mode;
            InvariantDateTimeParseContext ctx;
            ctx.globalization_invariant_mode = mode != 0;
            auto captured = AssetDateTime::from_serialized(std::stoull(now, nullptr, 16));
            ctx.now = [captured] { return captured; };
            ctx.two_digit_year_max = [cutoff] { return cutoff; };
            ctx.local_offset_no_throw = [](auto date) {
                std::ostringstream query;
                query << "OFFSET " << std::hex << date.serialized_data();
                return std::stoll(request(query.str()));
            };
            ctx.utc_offset = [](auto ticks) {
                std::istringstream reply(request("UTC " + std::to_string(ticks)));
                InvariantDateTimeParseContext::UtcOffset out;
                reply >> out.ticks >> out.ambiguous_daylight;
                return out;
            };
            ctx.valid_leap_second = [](int y, int m, int d, int h, int minute) {
                return request("LEAP " + std::to_string(y) + " " + std::to_string(m) + " " +
                               std::to_string(d) + " " + std::to_string(h) + " " + std::to_string(minute)) ==
                       "1";
            };
            AssetDateTime out;
            std::u16string units;
            std::optional<std::u16string_view> text;
            if (input != "~") {
                units = decode(input);
                text = units;
            }
            bool parsed = try_parse_invariant_datetime_cookie_expiry(text, out, ctx);
            std::cout << "R:" << (parsed ? 1 : 0) << '|' << out.ticks() << '|' << out.kind() << '|'
                      << out.serialized_data() << '\n'
                      << std::flush;
        } catch (const std::exception& e) {
            std::cout << "EX:" << e.what() << '\n' << std::flush;
        }
    }
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--probe") {
            probe();
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--tokens") {
            int i = 0;
            for (const auto& [text, type, value] : invariant_datetime_test::token_profile(false))
                std::cout << "T|" << i++ << '|' << (text.empty() ? "~" : encode(text)) << '|' << type << '|'
                          << value << '\n';
            return 0;
        }
        InvariantDateTimeParseContext ctx;
        ctx.now = [] { return AssetDateTime(638712864000000000ll, 2); };
        ctx.two_digit_year_max = [] { return 2049; };
        ctx.local_offset_no_throw = [](auto) { return 0ll; };
        ctx.utc_offset = [](auto) { return InvariantDateTimeParseContext::UtcOffset{}; };
        ctx.valid_leap_second = [](auto, auto, auto, auto, auto) { return false; };
        AssetDateTime value;
        if (!try_parse_invariant_datetime_cookie_expiry(u"2024\u5e741\u67082\u65e5", value, ctx) ||
            value.ticks() != 638397504000000000ll || value.kind() != 0)
            throw std::runtime_error("CJK suffix parsing");
        if (!try_parse_invariant_datetime_cookie_expiry(u"2024-01-02T03:04:05.00000005", value, ctx) ||
            value.ticks() != 638397614450000001ll)
            throw std::runtime_error("Source fractional accumulation rounding");
        if (!try_parse_invariant_datetime_cookie_expiry(u"2024-01-02T03:04:05+02:00", value, ctx) ||
            value.ticks() != 638397542450000000ll || value.kind() != 1)
            throw std::runtime_error("Cookie expiry keeps explicit zone as UTC");
        if (!try_parse_invariant_datetime(u"2024-01-02T03:04:05+02:00", value, ctx) ||
            value.ticks() != 638397542450000000ll || value.kind() != 2)
            throw std::runtime_error("Existing None parser retains local result kind");
        if (try_parse_invariant_datetime_cookie_expiry(u"Monday, 02 Jan 2024", value, ctx) ||
            value.ticks() != 0)
            throw std::runtime_error("Rejected weekday must reset output");
        ctx.valid_leap_second = [](auto, auto, auto, auto, auto) { return true; };
        if (!try_parse_invariant_datetime_cookie_expiry(u"2016-12-31 23:59:60", value, ctx) ||
            value.ticks() != 636188255999990000ll)
            throw std::runtime_error("Source supported leap-second normalization");
        std::cout << "PASS: invariant DateTime grammar smoke checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
