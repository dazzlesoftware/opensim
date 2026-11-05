// Copyright (c) Contributors, http://opensimulator.org/; BSD native-port/LICENSE.txt.
#include "opensim/framework/dotnet_regex_case.hpp"
#include <array>
#include <iomanip>
#include <iostream>
using namespace opensim::framework;
const std::array<std::u16string_view, 14> names = {u"",           u"en-US", u"tr",         u"tr-TR", u"az",
                                                   u"az-Latn-AZ", u"TR",    u"trick",      u"tr_TR", u"az-",
                                                   u"t",          u"a",     u"tr-unknown", u"Az"};
void hex(std::span<const char16_t> text) {
    for (auto c : text)
        std::cout << std::hex << std::setfill('0') << std::setw(4) << unsigned(c);
    std::cout << std::dec;
}
int main() {
    for (std::size_t n = 0; n < names.size(); ++n) {
        int reads = 0;
        auto behavior = dotnet_regex_case_behavior([&]() {
            ++reads;
            return std::u16string(names[n]);
        });
        std::cout << "C:" << n << ":" << static_cast<int>(behavior) << ':' << reads << '\n';
    }
    constexpr std::array<std::size_t, 4> cultures = {0, 1, 3, 5};
    for (auto n : cultures)
        for (int initial = 0; initial < 4; ++initial)
            for (unsigned code = 0; code < 65536; ++code) {
                int reads = 0;
                auto behavior = static_cast<DotnetRegexCaseBehavior>(initial);
                auto result = dotnet_regex_case_equivalences(
                    static_cast<char16_t>(code),
                    [&]() {
                        ++reads;
                        return std::u16string(names[n]);
                    },
                    behavior);
                std::cout << n << ':' << initial << ':' << code << ':' << static_cast<int>(behavior) << ':'
                          << reads << ':';
                if (result.empty())
                    std::cout << '-';
                else
                    hex(result);
                std::cout << '\n';
            }
}
