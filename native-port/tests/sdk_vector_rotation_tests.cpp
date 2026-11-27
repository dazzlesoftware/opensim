// SDK compatibility: protocol/LIBOPENMETAVERSE-LICENSE.txt.
#include "opensim/framework/sdk_quaternion.hpp"
#include <bit>
#include <iomanip>
#include <iostream>
#include <stdexcept>
using namespace opensim::framework;

int main(int argc, char **) {
    try {
        if (argc > 1) {
            std::uint32_t a, b, c, x, y, z, w;
            while (std::cin >> std::hex >> a >> b >> c >> x >> y >> z >> w) {
                const auto result =
                    sdk_rotate_vector({std::bit_cast<float>(a), std::bit_cast<float>(b), std::bit_cast<float>(c)},
                                      {std::bit_cast<float>(x), std::bit_cast<float>(y), std::bit_cast<float>(z),
                                       std::bit_cast<float>(w)});
                std::cout << std::hex << std::setfill('0') << std::setw(8) << std::bit_cast<std::uint32_t>(result.x)
                          << " " << std::setw(8) << std::bit_cast<std::uint32_t>(result.y) << " " << std::setw(8)
                          << std::bit_cast<std::uint32_t>(result.z) << "\n";
            }
            return 0;
        }
        const Vector3 input{.25f, 2.f, 3.f};
        if (sdk_rotate_vector(input, {0, 0, 0, 1}) != input || sdk_rotate_vector(input, {}) != input ||
            sdk_rotate_vector(input, {0, 0, 1, 0}) != Vector3{-.25f, -2.f, 3.f})
            throw std::runtime_error("SDK vector rotation identity, zero, or half-turn mismatch");
        std::cout << "PASS: SDK vector/quaternion matrix arithmetic\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
