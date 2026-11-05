// SDK compatibility model. See protocol/LIBOPENMETAVERSE-LICENSE.txt.
#include "opensim/framework/texture_entry.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>
namespace opensim::framework {
TextureEntry::TextureEntry(Uuid texture) : default_texture(std::make_shared<TextureFace>()) {
    default_texture->texture_id(texture);
}
TextureEntry::TextureEntry(const TextureFace& face) : TextureEntry(Uuid{}) {
    auto& d = *default_texture;
    d.bump(face.bump());
    d.fullbright(face.fullbright());
    d.media_flags(face.media_flags());
    d.offset_u(face.offset_u());
    d.offset_v(face.offset_v());
    d.repeat_u(face.repeat_u());
    d.repeat_v(face.repeat_v());
    d.color(face.color());
    d.rotation(face.rotation());
    d.glow(face.glow());
    d.shiny(face.shiny());
    d.mapping(face.mapping());
    d.texture_id(face.texture_id());
    d.material_id(face.material_id());
}
TextureEntry::TextureEntry(std::span<const std::uint8_t> data) : TextureEntry(data, 0, data.size()) {
}
TextureEntry::TextureEntry(std::span<const std::uint8_t> data, std::size_t position, std::size_t length) {
    if (length < 16)
        return;
    if (position > data.size() || length > data.size() - position)
        throw std::out_of_range("Texture entry slice");
    default_texture = std::make_shared<TextureFace>();
    auto cursor = position;
    const auto end = position + length;
    auto take = [&](std::size_t count) {
        // SDK reads some fields beyond its declared length; the underlying array
        // remains the hard bound. Never reproduce its unchecked memory reads.
        if (count > data.size() - cursor)
            throw std::out_of_range("Truncated texture entry");
        auto bytes = data.subspan(cursor, count);
        cursor += count;
        return bytes;
    };
    std::uint64_t bits = 0;
    std::size_t size = 0;
    auto read_mask = [&]() {
        if (cursor == data.size())
            return false;
        auto b = take(1)[0];
        if (!b)
            return false;
        bits = b & 127;
        size = 7;
        while (b & 128) {
            b = take(1)[0];
            bits = (bits << 7) | (b & 127);
            size += 7;
        }
        return true;
    };
    auto read = [&]<class T>() -> T {
        if constexpr (std::is_same_v<T, Uuid>)
            return Uuid::from_bytes(take(16));
        else if constexpr (std::is_same_v<T, std::array<float, 4>>) {
            std::array<float, 4> result;
            auto bytes = take(4);
            for (unsigned i = 0; i < 4; ++i)
                result[i] = static_cast<float>(255 - bytes[i]) * (1.f / 255.f);
            return result;
        } else {
            std::array<std::uint8_t, sizeof(T)> bytes;
            auto input = take(sizeof(T));
            std::copy(input.begin(), input.end(), bytes.begin());
            if constexpr (std::endian::native == std::endian::big)
                std::reverse(bytes.begin(), bytes.end());
            return std::bit_cast<T>(bytes);
        }
    };
    auto block = [&](auto member, std::uint32_t attribute, bool clamp = true) {
        using T = std::remove_reference_t<decltype(default_texture.get()->*member)>;
        default_texture.get()->*member = read.template operator()<T>();
        while ((attribute != Media || cursor < end) &&
               (attribute != MaterialId || (cursor <= end && end - cursor >= 16)) && read_mask()) {
            if (clamp)
                size = std::min<std::size_t>(size, 45);
            auto value = read.template operator()<T>();
            for (std::size_t i = 0; i < size; ++i) {
                // C# ulong shifts mask the shift count to six bits.
                if (bits & (std::uint64_t{1} << (i & 63))) {
                    auto face = get_face(static_cast<unsigned>(i));
                    face.get()->*member = value;
                    face->attributes_ |= attribute;
                }
            }
        }
    };
    block(&TextureFace::texture_, TextureId);
    block(&TextureFace::color_, TextureColor);
    block(&TextureFace::repeat_u_, RepeatU);
    block(&TextureFace::repeat_v_, RepeatV, false);
    block(&TextureFace::offset_u_, OffsetU);
    block(&TextureFace::offset_v_, OffsetV, false);
    block(&TextureFace::rotation_, Rotation);
    block(&TextureFace::material_, Material);
    block(&TextureFace::media_, Media);
    block(&TextureFace::glow_, Glow);
    if (cursor <= end && end - cursor >= 16)
        block(&TextureFace::material_id_, MaterialId);
}
std::shared_ptr<TextureFace> TextureEntry::get_face(unsigned index) {
    if (index >= max_faces)
        throw std::out_of_range("Texture face outside MAX_FACES");
    if (!faces[index])
        faces[index] = std::make_shared<TextureFace>(default_texture);
    return faces[index];
}
std::uint32_t TextureEntry::dirty_flags(int length, bool clear) {
    if (length == std::numeric_limits<int>::min())
        throw std::out_of_range("Texture face length overflow");
    auto flags = default_texture ? default_texture->dirty_flags() : 0;
    if (clear && default_texture)
        default_texture->dirty_flags(0);
    for (int i = std::min(length, 45) - 1; i >= 0; --i)
        if (faces[i]) {
            flags |= faces[i]->dirty_flags();
            if (clear)
                faces[i]->dirty_flags(0);
        }
    return flags;
}
void TextureEntry::dirty_flags(int length, std::uint32_t flags) {
    if (length == std::numeric_limits<int>::min())
        throw std::out_of_range("Texture face length overflow");
    if (default_texture)
        default_texture->dirty_flags(flags);
    for (int i = std::min(length, 45) - 1; i >= 0; --i)
        if (faces[i])
            faces[i]->dirty_flags(flags);
}
std::vector<std::uint8_t> TextureEntry::bakes_bytes(int maxfaces) const {
    return serialize(maxfaces, true);
}
std::vector<std::uint8_t> TextureEntry::bytes(int maxfaces) const {
    return serialize(maxfaces, false);
}
std::vector<std::uint8_t> TextureEntry::serialize(int maxfaces, bool bakes) const {
    std::vector<std::uint8_t> out;
    if (!default_texture)
        return out;
    constexpr std::array<int, 11> all{44, 43, 42, 41, 40, 20, 19, 11, 10, 9, 8};
    std::vector<int> indices;
    if (bakes) {
        auto selected = std::span(all).subspan(maxfaces > 21 ? 0 : 5);
        indices.assign(selected.begin(), selected.end());
    } else
        for (int i = (maxfaces == std::numeric_limits<int>::min() ? 45 : std::clamp(maxfaces, 0, 45)) - 1;
             i >= 0; --i)
            indices.push_back(i);
    auto mask = [&](std::uint64_t bits) {
        int shift = 0;
        while ((bits >> (shift + 7)) != 0)
            shift += 7;
        for (; shift > 0; shift -= 7)
            out.push_back(static_cast<std::uint8_t>((bits >> shift) | 0x80));
        out.push_back(static_cast<std::uint8_t>(bits & 127));
    };
    auto write = [&]<class T>(T value) {
        if constexpr (std::is_same_v<T, Uuid>) {
            auto b = value.bytes();
            out.insert(out.end(), b.begin(), b.end());
        } else if constexpr (std::is_same_v<T, std::array<float, 4>>) {
            for (float n : value) {
                auto b = n <= 0 ? 0 : (n >= 1 ? 255 : (!std::isfinite(n) ? 0 : static_cast<int>(n * 255.f)));
                out.push_back(static_cast<std::uint8_t>(255 - b));
            }
        } else {
            auto bytes = std::bit_cast<std::array<std::uint8_t, sizeof(T)>>(value);
            if constexpr (std::endian::native == std::endian::big)
                std::reverse(bytes.begin(), bytes.end());
            out.insert(out.end(), bytes.begin(), bytes.end());
        }
    };
    auto block = [&](auto getter, std::uint32_t attribute) {
        auto value = getter(*default_texture);
        write(value);
        std::uint64_t done = 0;
        for (int index : indices)
            if (faces[index]) {
                auto bit = std::uint64_t{1} << index;
                if (!bakes && ((done & bit) || !(faces[index]->attributes_ & attribute)))
                    continue;
                auto face = getter(*faces[index]);
                if (face != value) {
                    if (!bakes)
                        for (int other = index - 1; other >= 0; --other) {
                            auto next = std::uint64_t{1} << other;
                            if (!(done & next) && faces[other] && (faces[other]->attributes_ & attribute) &&
                                getter(*faces[other]) == face) {
                                done |= next;
                                bit |= next;
                            }
                        }
                    mask(bit);
                    write(face);
                }
            }
        out.push_back(0);
    };
    block([&](const TextureFace& f) { return bakes ? f.texture_id() : f.texture_; }, TextureId);
    block([&](const TextureFace& f) { return bakes ? f.color() : f.color_; }, TextureColor);
    block([&](const TextureFace& f) { return bakes ? f.repeat_u() : f.repeat_u_; }, RepeatU);
    block([&](const TextureFace& f) { return bakes ? f.repeat_v() : f.repeat_v_; }, RepeatV);
    // These SDK blocks intentionally use raw storage, including inherited faces.
    block([](const TextureFace& f) { return f.offset_u_; }, OffsetU);
    block([](const TextureFace& f) { return f.offset_v_; }, OffsetV);
    block([](const TextureFace& f) { return f.rotation_; }, Rotation);
    block([](const TextureFace& f) { return f.material_; }, Material);
    block([](const TextureFace& f) { return f.media_; }, Media);
    block([](const TextureFace& f) { return f.glow_; }, Glow);
    block([](const TextureFace& f) { return f.material_id_; }, MaterialId);
    return out;
}
}
