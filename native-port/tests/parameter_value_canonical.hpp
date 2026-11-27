// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/script/lsl_runtime.hpp"
#include <bit>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace opensim::test {
inline std::string parameter_value_canonical(const script::LslList &values) {
    std::ostringstream output;
    const auto real = [&](double number) {
        output << std::hex << std::setfill('0') << std::setw(16) << std::bit_cast<std::uint64_t>(number) << std::dec;
    };
    for (const auto &value : values.items) {
        std::visit(
            [&](const auto &item) {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, std::int32_t>)
                    output << "i" << item;
                else if constexpr (std::is_same_v<T, double>) {
                    output << "f";
                    real(item);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    output << "s";
                    for (const auto byte : item)
                        output << std::hex << std::setfill('0') << std::setw(2)
                               << static_cast<unsigned>(static_cast<std::uint8_t>(byte)) << std::dec;
                } else if constexpr (std::is_same_v<T, framework::Vector3d>) {
                    output << "v";
                    real(item.x);
                    real(item.y);
                    real(item.z);
                } else if constexpr (std::is_same_v<T, script::LslRotation>) {
                    output << "r";
                    real(item.x);
                    real(item.y);
                    real(item.z);
                    real(item.w);
                } else
                    throw std::invalid_argument("Unexpected nested parameter result list");
            },
            value);
        output << ";";
    }
    return output.str();
}
} // namespace opensim::test
