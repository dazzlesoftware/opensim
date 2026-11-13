#include "opensim/scene/root_position.hpp"
#include <bit>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
using namespace opensim;
using namespace scene;
static float f(const std::string& s) {
    return std::bit_cast<float>(static_cast<std::uint32_t>(std::stoul(s, nullptr, 16)));
}
static std::string h(float v) {
    std::ostringstream s;
    s << std::hex << std::bit_cast<std::uint32_t>(v);
    return s.str();
}
static std::string v(framework::Vector3 p) {
    return h(p.x) + "," + h(p.y) + "," + h(p.z);
}
int main(int argc, char**) {
    try {
        if (argc > 1) {
            std::uint32_t width, height;
            std::string ah, x, y, z;
            int mode, appearance;
            while (std::cin >> width >> height >> ah >> x >> y >> z >> mode >> appearance) {
                int calls = 0;
                framework::Vector3 at;
                auto p = root_landing_position({f(x), f(y), f(z)}, width, height, appearance ? f(ah) : 0,
                                               [&](float x, float y) {
                                                   calls++;
                                                   at = {x, y, 0};
                                                   if (mode == 1)
                                                       return (21.f + x * .03125f) + y * .015625f;
                                                   if (mode == 2)
                                                       return -90.f;
                                                   if (mode == 3)
                                                       return 4000.f;
                                                   return 21.f;
                                               });
                std::cout << v(p) << "|" << calls << "|" << h(at.x) << "," << h(at.y) << "\n";
            }
            return 0;
        }
        int checks = 0;
        auto test = [&](bool b, const char* text) {
            if (!b)
                throw std::runtime_error(text);
            checks++;
        };
        int calls = 0;
        test(clamp_default_landing({-10, 900, 70000}, 256, 512) ==
                 framework::Vector3{5, 507, 65536},
             "default landing uses a five-meter border and caps altitude");
        test(clamp_default_landing({150, 150, -101}, 256, 256) ==
                 framework::Vector3{150, 150, -100},
             "default landing preserves valid horizontal coordinates and caps minimum altitude");
        framework::Vector3 teleport_sample;
        const auto emergency = local_teleport_position({-1, 256, 0}, 256, 256, {}, [&](float x, float y) {
            teleport_sample = {x, y, 0};
            return 20.f;
        });
        test(emergency == framework::Vector3{128, 128, 128} &&
                 teleport_sample == framework::Vector3{128, 128, 0},
             "out-of-bounds local teleport samples the emergency destination");
        const auto ground = [](float, float) { return 20.f; };
        test(local_teleport_position({12, 16, 0}, 256, 256, 2.f, ground).z == 20.f + (1.f + .1f) &&
                 local_teleport_position({12, 16, 0}, 256, 256, {}, ground).z == 20.f + (.8f + .1f) &&
                 local_teleport_position({12, 16, 0}, 256, 256, 0.f, ground).z == 20.f + .1f,
             "local teleport clearance uses the avatar appearance and the managed margin");
        test(local_teleport_position({12, 16, 70000}, 256, 256, {}, ground).z == 65536.f &&
                 local_teleport_position({12, 16, -500}, 256, 256, {}, [](float, float) {
                     return std::numeric_limits<float>::infinity();
                 }).z == -100.f,
             "local teleport clamps altitude and ignores nonfinite terrain");
        framework::Vector3 sampled;
        auto p = root_landing_position({-1, 256, 0}, 256, 256, 1.9f, [&](float x, float y) {
            sampled = {x, y, 0};
            calls++;
            return 21.f;
        });
        test(p.x == .5f && p.y == 255.5f && sampled.x == .5f && sampled.y == 255.5f,
             "source clamp before terrain sampling");
        test(p.z == (21.f + .01f) + .95f && calls == 1, "source height margin/halfheight and one sample");
        p = root_landing_position({1, 2, 100}, 256, 256, 0, [](float, float) { return 21.f; });
        test(p.z == 100, "raise-only placement");
        p = root_landing_position({1, 2, 0}, 256, 256, -1, [](float, float) { return 21.f; });
        test(p.z == (21.f + .01f) + .8f, "nonpositive height fallback");
        bool missing = false;
        try {
            root_landing_position({}, 256, 256, 1.9f, {});
        } catch (const std::invalid_argument&) {
            missing = true;
        }
        test(missing, "required backend");
        bool propagated = false;
        try {
            root_landing_position({}, 256, 256, 1.9f,
                                  [](float, float) -> float { throw std::runtime_error("ground failure"); });
        } catch (const std::runtime_error&) {
            propagated = true;
        }
        test(propagated, "ground backend exception propagation");
        std::cout << "PASS: " << checks << " root placement/backend checks\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
