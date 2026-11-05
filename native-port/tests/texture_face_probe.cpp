#include "opensim/framework/texture_face.hpp"
#include <bit>
#include <cmath>
#include <iostream>
#include <sstream>
using namespace opensim::framework;
int main() {
    std::array<std::shared_ptr<TextureFace>, 4> faces;
    faces[0] = std::make_shared<TextureFace>();
    faces[1] = std::make_shared<TextureFace>(faces[0]);
    faces[2] = std::make_shared<TextureFace>(faces[1]);
    faces[3] = faces[2]->clone();
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        unsigned index, op, bits;
        in >> index >> op >> std::hex >> bits;
        auto& f = *faces.at(index);
        float v = std::bit_cast<float>(static_cast<std::uint32_t>(bits));
        switch (op) {
        case 0:
            f.repeat_u(v);
            break;
        case 1:
            f.repeat_v(v);
            break;
        case 2:
            f.offset_u(v);
            break;
        case 3:
            f.offset_v(v);
            break;
        case 4:
            f.rotation(v);
            break;
        case 5:
            f.glow(v);
            break;
        case 6:
            f.bump(static_cast<std::uint8_t>(bits));
            break;
        case 7:
            f.shiny(static_cast<std::uint8_t>(bits));
            break;
        case 8:
            f.fullbright(bits != 0);
            break;
        case 9:
            f.media_flags(bits != 0);
            break;
        case 10:
            f.mapping(static_cast<std::uint8_t>(bits));
            break;
        case 11:
            f.color({v, v, v, v});
            break;
        case 12:
            f.dirty_flags(bits);
            break;
        case 13:
            faces[3] = faces[index]->clone();
            break;
        case 14:
            f.texture_id(
                Uuid(bits ? "5748decc-f629-461c-9a36-a35a221fe21f" : "00000000-0000-0000-0000-000000000000"));
            break;
        case 15:
            f.material_id(
                Uuid(bits ? "5748decc-f629-461c-9a36-a35a221fe21f" : "00000000-0000-0000-0000-000000000000"));
            break;
        }
        for (auto& face : faces) {
            auto& x = *face;
            auto emit = [](float n) {
                auto b = std::bit_cast<std::uint32_t>(n);
                if (std::isnan(n))
                    b = 0x7fc00000;
                std::cout << std::hex << b << ',';
            };
            std::cout << std::hex << x.attributes() << ',' << x.dirty_flags() << ',';
            for (float n : x.color())
                emit(n);
            for (float n : {x.repeat_u(), x.repeat_v(), x.offset_u(), x.offset_v(), x.rotation(), x.glow()})
                emit(n);
            std::cout << unsigned(x.bump()) << ',' << unsigned(x.shiny()) << ',' << x.fullbright() << ','
                      << x.media_flags() << ',' << unsigned(x.mapping()) << ',' << x.texture_id().str() << ','
                      << x.material_id().str() << ';';
        }
        std::cout << '\n';
    }
}
