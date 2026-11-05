// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/time_span.hpp"
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
using namespace opensim::framework;
namespace {
std::string probe(const std::string& line) {
    std::istringstream in(line);
    char op;
    std::string hex;
    in >> op >> hex;
    auto bits = std::stoull(hex, nullptr, 16);
    try {
        std::int64_t ticks;
        if (op == 'h')
            ticks = timespan_from_hours(std::bit_cast<double>(bits));
        else if (op == 't')
            ticks = std::bit_cast<std::int64_t>(bits);
        else
            throw std::runtime_error("Unknown TimeSpan fixture");
        return "OK|" + std::to_string(ticks) + "|" +
               std::to_string(std::bit_cast<std::uint64_t>(timespan_total_hours(ticks)));
    } catch (const TimeSpanError& e) {
        return std::string("EX|") +
               (e.kind() == TimeSpanError::Kind::ArgumentNaN ? "ArgumentException" : "OverflowException") +
               "|" + e.what() + "|" + std::to_string(e.hresult()) + "|";
    }
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--probe") {
            std::string line;
            while (std::getline(std::cin, line))
                std::cout << probe(line) << '\n';
            return 0;
        }
        if (timespan_from_hours(48) != 1728000000000ll || timespan_from_hours(-7.25) != -261000000000ll ||
            timespan_from_hours(1.e-8) != 360 || timespan_total_hours(36000000000ll) != 1.0)
            throw std::runtime_error("TimeSpan hours/tick conversion");
        if (timespan_from_hours(-0.0) != 0 || std::bit_cast<std::uint64_t>(timespan_total_hours(0)) != 0)
            throw std::runtime_error("TimeSpan signed zero normalization");
        auto boundary = 9223372036854775808.0 / 36000000000.0;
        if (timespan_from_hours(boundary) != INT64_MAX || timespan_from_hours(-boundary) != INT64_MIN)
            throw std::runtime_error("TimeSpan extreme tick boundary");
        try {
            timespan_from_hours(boundary + 1.0);
            throw std::runtime_error("Missing TimeSpan overflow");
        } catch (const TimeSpanError& e) {
            if (e.kind() != TimeSpanError::Kind::Overflow)
                throw;
        }
        if (!probe("h 7ff8000000000000").starts_with("EX|ArgumentException|"))
            throw std::runtime_error("NaN classification");
        if (!probe("h 7ff0000000000000").starts_with("EX|OverflowException|"))
            throw std::runtime_error("Infinity classification");
        std::cout << "PASS: TimeSpan hours/ticks/overflow/NaN boundaries\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
