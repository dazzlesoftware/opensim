// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/primitive_parameter_rules.hpp"
#include <iostream>
#include <stdexcept>
#include <tuple>

using namespace opensim::script;

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main(int argc, char **) {
    try {
        PrimitiveParameterRuleBackend backend;
        std::vector<std::pair<int, std::optional<int>>> calls;
        backend.append = [&](int code, std::optional<int> face, LslList &result) {
            calls.emplace_back(code, face);
            result.items.emplace_back(std::int32_t{code});
        };
        if (argc > 1) {
            int avatar, count;
            while (std::cin >> avatar >> count) {
                backend.avatar = avatar != 0;
                LslList rules, result;
                while (count-- > 0) {
                    int value;
                    std::cin >> value;
                    rules.items.emplace_back(std::int32_t{value});
                }
                calls.clear();
                const auto tail = read_primitive_parameter_rules(backend, rules, result);
                for (const auto &[code, face] : calls)
                    std::cout << code << ":" << (face ? std::to_string(*face) : "-") << ";";
                std::cout << "|";
                for (const auto &value : tail.items)
                    std::cout << std::get<std::int32_t>(value) << ",";
                std::cout << "\n";
            }
            return 0;
        }
        LslList result{{std::string("earlier result")}};
        const auto tail =
            read_primitive_parameter_rules(backend, LslList{{std::int32_t{27}, std::int32_t{17}}}, result);
        check(tail.items.empty() && calls == std::vector<std::pair<int, std::optional<int>>>{{27, {}}} &&
                  result.items.size() == 2 && std::get<std::string>(result.items[0]) == "earlier result",
              "A missing face ends the read without discarding prior query results");
        calls.clear();
        const LslList short_target{{std::int32_t{34}, std::int32_t{2}, std::int32_t{27}}};
        result.items.clear();
        check(read_primitive_parameter_rules(backend, short_target, result).items.size() == 2 && calls.empty(),
              "A part link target returns its link and remaining single-property rule");
        backend.avatar = true;
        check(read_primitive_parameter_rules(backend, short_target, result).items.empty(),
              "Avatar link targets retain the original extra trailing-argument requirement");
        read_primitive_parameter_rules(backend, LslList{{std::int32_t{37}, std::int32_t{27}}}, result);
        check(calls == std::vector<std::pair<int, std::optional<int>>>{{27, {}}},
              "A prim-only rule on an avatar does not consume the next rule as a face");
        calls.clear();
        backend.avatar = false;
        read_primitive_parameter_rules(backend, LslList{{std::int32_t{17}, 2.9, std::int32_t{18}, std::string(" -1 ")}},
                                       result);
        check(calls == std::vector<std::pair<int, std::optional<int>>>{{17, 2}, {18, -1}},
              "Faces use strict source list integer conversion");
        try {
            read_primitive_parameter_rules(backend, LslList{{opensim::framework::Vector3d{}}}, result);
            throw std::runtime_error("A vector was accepted as a rule number");
        } catch (const std::invalid_argument &) {
        }
        std::cout << "PASS: primitive and avatar parameter rule consumption\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
