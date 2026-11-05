#include "opensim/scene/object_update.hpp"
#include <iostream>
#include <iomanip>
#include <stdexcept>
using namespace opensim;
using namespace scene;
static SceneObject object(int seed, int mode) {
    auto o = SceneObject::primitive(framework::Uuid("11223344-5566-7788-99aa-bbccddeeff00"),
                                    {seed * .25f, -seed * .125f, 23});
    o.id = framework::Uuid("00112233-4455-6677-8899-aabbccddeeff");
    o.local_id = 720001 + seed;
    o.velocity = {seed * .03125f, 0, 1};
    o.acceleration = {1, -2, 0};
    o.angular_velocity = {.5f, 0, -.25f};
    o.rotation = {seed * .01f, -.2f, .3f, (mode & 4) ? -.4f : .4f};
    if (mode & 8)
        o.rotation = {0, 0, 0, 0};
    if (mode & 16)
        o.rotation = {.01f, .02f, .03f, .99995f};
    o.flags = 0x12347;
    if (mode & 32) {
        auto& s = o.shape;
        s.state = seed & 255;
        s.path_curve = 32;
        s.profile_curve = 5;
        s.path_begin = 123;
        s.path_end = 456;
        s.path_scale_x = 115;
        s.path_scale_y = 133;
        s.path_shear_x = 17;
        s.path_shear_y = 255;
        s.path_twist = -123;
        s.path_twist_begin = 127;
        s.path_radius_offset = -17;
        s.path_taper_x = -31;
        s.path_taper_y = 29;
        s.path_revolutions = 33;
        s.path_skew = -44;
        s.profile_begin = 1234;
        s.profile_end = 2345;
        s.profile_hollow = 3333;
    }
    if (mode & 64)
        o.texture_entry = {1, 0, 2, 255, 17};
    if (mode & 128)
        o.texture_entry.clear();
    if (mode & 256) {
        o.scale = {-0.f, 0, -0.f};
        o.position = {-0.f, 0, 0};
        o.velocity = {0, -0.f, 0};
        o.acceleration = {0, 0, -0.f};
        o.angular_velocity = {0, -0.f, 0};
    }
    return o;
}
static ObjectUpdateOptions options(const SceneObject& o, int seed, int mode) {
    ObjectUpdateOptions x;
    x.pseudo_crc = 0xaabb0000u + seed;
    x.material = mode & 32 ? 7 : 3;
    x.click_action = mode & 32 ? 2 : 0;
    if (mode & 2)
        x.generated_client_flags = 0xfff0fff3;
    x.recipient = mode & 1 ? o.owner : framework::Uuid{};
    x.create_selected = mode & 4;
    return x;
}
int main(int argc, char**) {
    try {
        if (argc > 1) {
            int seed, mode;
            while (std::cin >> seed >> mode) {
                auto o = object(seed, mode);
                auto p = object_update(o, 0x123456789abcdef0ull, seed * .03125f, options(o, seed, mode));
                for (auto b : network::lludp::encode_datagram(p))
                    std::cout << std::hex << std::setfill('0') << std::setw(2) << unsigned(b);
                std::cout << "\n";
            }
            return 0;
        }
        auto o = object(3, 0);
        auto p = object_update(o, 123, .5f);
        auto& b = p.block("ObjectData");
        if (std::get<std::vector<std::uint8_t>>(b.at("ObjectData")).size() != 60 ||
            std::get<std::uint64_t>(b.at("Material")) != 3 ||
            std::get<std::uint64_t>(b.at("PathCurve")) != 16 ||
            !std::get<std::vector<std::uint8_t>>(b.at("NameValue")).empty())
            throw std::runtime_error("prim fields");
        auto wire = network::lludp::encode_datagram(p);
        auto back = network::lludp::decode_datagram(wire);
        if (std::get<framework::Uuid>(back.block("ObjectData").at("FullID")) != o.id)
            throw std::runtime_error("wire identity");
        for (int mode = 0; mode < 2; mode++) {
            o = object(1, 0);
            if (mode == 0)
                o.is_attachment = true;
            if (mode == 1)
                o.shape.pcode = 255;
            bool threw = false;
            try {
                object_update(o, 0, 1);
            } catch (const std::invalid_argument&) {
                threw = true;
            }
            if (!threw)
                throw std::runtime_error("unsupported object profile");
        }
        o = object(1, 0);
        o.parent_id = 42;
        const auto child =
            network::lludp::decode_datagram(network::lludp::encode_datagram(object_update(o, 123, 1)));
        const auto& linked = child.block("ObjectData");
        if (std::get<std::uint64_t>(linked.at("ParentID")) != 42 ||
            !std::get<std::vector<std::uint8_t>>(linked.at("NameValue")).empty())
            throw std::runtime_error("linked child fields on the wire");
        o = object(1, 0);
        o.is_attachment = true;
        o.parent_id = 42;
        o.shape.state = 0x16;
        const auto attachment =
            network::lludp::decode_datagram(network::lludp::encode_datagram(object_update(o, 123, 1)));
        const auto& attached = attachment.block("ObjectData");
        const auto expected_name = "AttachItemID STRING RW SV " + o.id.str();
        if (std::get<std::uint64_t>(attached.at("ParentID")) != 42 ||
            std::get<std::uint64_t>(attached.at("State")) != 0x61 ||
            std::get<std::vector<std::uint8_t>>(attached.at("NameValue")) !=
                std::vector<std::uint8_t>(expected_name.c_str(),
                                          expected_name.c_str() + expected_name.size() + 1))
            throw std::runtime_error("attachment fields on the wire");
        const std::vector<std::uint8_t> projection{1, 0x40, 0, 28, 0, 0, 0};
        auto projection_params = projection;
        projection_params.resize(35, 1);
        o.extra_params = projection_params;
        o.extra_params[0] = 2;
        o.extra_params.insert(o.extra_params.end(), {0x20, 0, 16, 0, 0, 0});
        o.extra_params.resize(o.extra_params.size() + 16, 2);
        const auto original_params = o.extra_params;
        ObjectUpdateOptions light_options;
        light_options.disable_facelights = true;
        const auto wire_params = [&](const ObjectUpdateOptions& opts) {
            const auto packet = object_update(o, 123, 1, opts);
            return std::get<std::vector<std::uint8_t>>(packet.block("ObjectData").at("ExtraParams"));
        };
        if (wire_params(light_options) != projection_params || o.extra_params != original_params ||
            wire_params({}) != original_params)
            throw std::runtime_error(
                "facelight filter removes only the light record from the outgoing update");
        for (const auto point : {5, 6}) {
            o.shape.state = static_cast<std::uint8_t>(point);
            if (wire_params(light_options) != original_params)
                throw std::runtime_error("hand attachment retains its light");
        }
        o.is_attachment = false;
        o.parent_id = 0;
        if (wire_params(light_options) != original_params)
            throw std::runtime_error("world prim retains its light");
        o = object(0, 0);
        o.local_id = 0x12345678;
        o.position = {};
        o.velocity = {128, -128, 0};
        o.acceleration = {0, 1, -1};
        o.rotation = {.5f, -.5f, .5f, -.5f};
        o.angular_velocity = {128, -64, 32};
        const std::vector<std::uint8_t> expected{
            0x78, 0x56, 0x34, 0x12, 0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
            0,    0,    0,    0xff, 0xff, 0,    0,    0xff, 0x7f, 0xff, 0x7f, 0xfe, 0x81, 0xff, 0x7d,
            0xfe, 0xbf, 0xff, 0x3f, 0xfe, 0xbf, 0xff, 0x3f, 0xff, 0xff, 0xff, 0x3f, 0xfe, 0x9f};
        const auto terse =
            network::lludp::decode_datagram(network::lludp::encode_datagram(object_terse_update(o, 123)));
        const auto& part = terse.block("ObjectData");
        if (terse.spec->name != "ImprovedTerseObjectUpdate" || !terse.reliable || !terse.zerocoded ||
            std::get<std::uint64_t>(terse.block("RegionData").at("RegionHandle")) != 123 ||
            std::get<std::vector<std::uint8_t>>(part.at("Data")) != expected ||
            !std::get<std::vector<std::uint8_t>>(part.at("TextureEntry")).empty())
            throw std::runtime_error("terse prim wire layout and managed quantization");
        o.is_attachment = true;
        o.parent_id = 42;
        o.shape.state = 0x16;
        auto expected_attachment = expected;
        expected_attachment[4] = 0x61;
        const auto terse_attachment =
            network::lludp::decode_datagram(network::lludp::encode_datagram(object_terse_update(o, 123)));
        if (std::get<std::vector<std::uint8_t>>(terse_attachment.block("ObjectData").at("Data")) !=
            expected_attachment)
            throw std::runtime_error("terse attachment wire layout");
        std::cout << "PASS: prim fields, full and terse wire round trips and unsupported-profile rejection\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
