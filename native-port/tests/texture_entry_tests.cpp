#include "opensim/framework/texture_entry.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace opensim::framework;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::out_of_range&) {
        return;
    }
    throw std::runtime_error("Malformed texture input accepted");
}
int main() {
    try {
        TextureEntry entry(TextureEntry::white_texture);
        entry.default_texture->offset_u(.5f);
        entry.get_face(44)->texture_id(Uuid{});
        entry.get_face(8)->glow(.5f);
        entry.get_face(20)->material_id(TextureEntry::white_texture);
        auto bytes = entry.bytes();
        auto framed = bytes;
        framed.insert(framed.begin(), {1, 2, 3});
        framed.insert(framed.end(), {4, 5, 6});
        TextureEntry decoded(framed, 3, bytes.size());
        check(decoded.bytes() == TextureEntry(bytes).bytes(), "Texture frame offset");
        check(decoded.dirty_flags(45, false) == 0, "Decoded textures start clean");
        rejects([&] { entry.get_face(45); });
        rejects([&] { entry.get_face(std::numeric_limits<unsigned>::max()); });
        rejects([&] { entry.dirty_flags(std::numeric_limits<int>::min(), false); });
        rejects([&] { entry.dirty_flags(std::numeric_limits<int>::min(), std::uint32_t{1}); });
        rejects([&] { TextureEntry bad(bytes, bytes.size() + 1, 16); });
        rejects([&] { TextureEntry bad(bytes, 0, std::numeric_limits<std::size_t>::max()); });
        // All truncated prefixes and a deterministic byte-mutation corpus must
        // terminate cleanly, regardless of whether a legacy optional tail parses.
        std::size_t cases = 0;
        auto exercise = [&](const std::vector<std::uint8_t>& input) {
            try {
                TextureEntry model(input);
                (void)model.bytes();
                (void)model.bakes_bytes();
            } catch (const std::out_of_range&) {
            }
            ++cases;
        };
        for (std::size_t n = 0; n < bytes.size(); ++n)
            exercise({bytes.begin(), bytes.begin() + n});
        for (std::size_t n = 0; n < bytes.size(); ++n)
            for (std::uint8_t value : {std::uint8_t{0}, std::uint8_t{128}, std::uint8_t{255}}) {
                auto changed = bytes;
                changed[n] = value;
                exercise(changed);
            }
        for (std::size_t n = 16; n < 128; ++n)
            exercise(std::vector<std::uint8_t>(n, 128));
        TextureEntry empty(std::span<const std::uint8_t>{});
        check(!empty.default_texture && empty.bytes().empty(), "Empty texture entry");
        std::cout << "PASS: texture framing, bounds, clean decoding and " << cases << " malformed inputs\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
