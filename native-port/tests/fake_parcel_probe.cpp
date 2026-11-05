// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/fake_parcel.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace opensim::framework;
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            std::istringstream in(line);
            char op;
            std::string a, b, c, d;
            in >> op >> a;
            Uuid id;
            if (op == 'B') {
                in >> b >> c >> d;
                auto handle = std::stoull(a, nullptr, 16);
                auto x = static_cast<std::uint32_t>(std::stoull(b, nullptr, 16)),
                     y = static_cast<std::uint32_t>(std::stoull(c, nullptr, 16));
                id = d == "-" ? build_fake_parcel_id(handle, x, y)
                              : build_fake_parcel_id(handle, x, y,
                                                     static_cast<std::uint32_t>(std::stoull(d, nullptr, 16)));
            } else {
                std::array<std::uint8_t, 16> bytes;
                for (std::size_t i = 0; i < 16; ++i)
                    bytes[i] = static_cast<std::uint8_t>(std::stoi(a.substr(i * 2, 2), nullptr, 16));
                id = Uuid::from_bytes(bytes);
            }
            FakeParcelPosition p;
            bool valid = parse_fake_parcel_id(id, p);
            std::cout << id.str() << ":" << std::hex << std::setfill('0');
            for (auto byte : id.bytes())
                std::cout << std::setw(2) << static_cast<unsigned>(byte);
            std::cout << ":" << (valid ? 1 : 0) << ":" << std::setw(16) << p.region_handle << ":"
                      << std::setw(8) << p.x << ":" << std::setw(8) << p.y << ":" << std::setw(8) << p.z
                      << std::dec << "\n";
        } catch (const std::exception& e) {
            std::cout << "EX:" << e.what() << "\n";
        }
    }
}
