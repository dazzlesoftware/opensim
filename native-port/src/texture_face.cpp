// SDK compatibility model. See protocol/LIBOPENMETAVERSE-LICENSE.txt.
#include "opensim/framework/texture_face.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace opensim::framework {
namespace {
// The bundled .NET runtime converts invalid float-to-int values to INT_MIN,
// then narrows unchecked. Avoid undefined C++ casts at that boundary.
std::int32_t integer(float value) {
    if (!std::isfinite(value) || value >= 2147483648.f || value < -2147483648.f)
        return std::numeric_limits<std::int32_t>::min();
    return static_cast<std::int32_t>(value);
}
}
TextureFace::TextureFace(std::shared_ptr<TextureFace> parent)
    : parent_(std::move(parent)), attributes_(parent_ ? 0 : AllTextureAttributes) {}
const TextureFace &TextureFace::source(std::uint32_t flag) const {
    // SDK inheritance reads the immediate parent's raw storage, not its getter.
    return (attributes_ & flag) ? *this : *parent_;
}
std::shared_ptr<TextureFace> TextureFace::clone() const {
    auto result = std::make_shared<TextureFace>(*this);
    if (parent_)
        result->parent_ = parent_->clone();
    return result;
}
std::int16_t TextureFace::quantize_offset(float value) {
    value = (!(value < 1.f) ? 1.f : (!(value > -1.f) ? -1.f : value)) * 32767.f;
    value = value >= 0 ? value + 0.5f : value - 0.5f;
    return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(integer(value)));
}
std::int16_t TextureFace::quantize_rotation(float value) {
    value = static_cast<float>(std::remainder(static_cast<double>(value), 6.2831854820251465));
    value *= 16384.f / 3.1415927410125732421875f;
    value = value >= 0 ? value + 0.5f : value - 0.5f;
    return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(integer(value)));
}
std::uint8_t TextureFace::quantize_glow(float value) {
    return static_cast<std::uint8_t>(integer(value * 255.f));
}
std::array<float, 4> TextureFace::color() const { return source(TextureColor).color_; }
void TextureFace::color(std::array<float, 4> value) {
    if (color() != value) {
        color_ = value;
        mark(TextureColor);
    }
}
float TextureFace::repeat_u() const { return source(RepeatU).repeat_u_; }
void TextureFace::repeat_u(float value) {
    if (repeat_u() != value) {
        repeat_u_ = value;
        mark(RepeatU);
    }
}
float TextureFace::repeat_v() const { return source(RepeatV).repeat_v_; }
void TextureFace::repeat_v(float value) {
    if (repeat_v() != value) {
        repeat_v_ = value;
        mark(RepeatV);
    }
}
float TextureFace::offset_u() const { return source(OffsetU).offset_u_ * (1.f / 32767.f); }
void TextureFace::offset_u(float value) {
    auto v = quantize_offset(value);
    if (source(OffsetU).offset_u_ != v) {
        offset_u_ = v;
        mark(OffsetU);
    }
}
float TextureFace::offset_v() const { return source(OffsetV).offset_v_ * (1.f / 32767.f); }
void TextureFace::offset_v(float value) {
    auto v = quantize_offset(value);
    if (source(OffsetV).offset_v_ != v) {
        offset_v_ = v;
        mark(OffsetV);
    }
}
float TextureFace::rotation() const { return source(Rotation).rotation_ * 0.0001917476f; }
void TextureFace::rotation(float value) {
    auto v = quantize_rotation(value);
    if (source(Rotation).rotation_ != v) {
        rotation_ = v;
        mark(Rotation);
    }
}
float TextureFace::glow() const { return source(Glow).glow_ * (1.f / 255.f); }
void TextureFace::glow(float value) {
    auto v = quantize_glow(value);
    if (source(Glow).glow_ != v) {
        glow_ = v;
        mark(Glow);
    }
}
Uuid TextureFace::texture_id() const { return source(TextureId).texture_; }
void TextureFace::texture_id(Uuid value) {
    if (texture_id() != value) {
        texture_ = value;
        mark(TextureId);
    }
}
Uuid TextureFace::material_id() const { return source(MaterialId).material_id_; }
void TextureFace::material_id(Uuid value) {
    auto previous = (attributes_ & MaterialId) ? material_id_ : parent_->material_id();
    if (previous != value) {
        material_id_ = value;
        mark(MaterialId);
    }
}
void TextureFace::material_bits(std::uint8_t keep, std::uint8_t value) {
    auto previous = material_;
    if (!(attributes_ & Material))
        material_ = parent_->material_;
    auto next = static_cast<std::uint8_t>((material_ & keep) | value);
    if (previous != next) {
        material_ = next;
        mark(Material);
    }
}
void TextureFace::media_bits(std::uint8_t keep, std::uint8_t value) {
    auto previous = media_;
    if (!(attributes_ & Media))
        media_ = parent_->media_;
    auto next = static_cast<std::uint8_t>((media_ & keep) | value);
    if (previous != next) {
        media_ = next;
        mark(Media);
    }
}
std::uint8_t TextureFace::bump() const { return source(Material).material_ & 31; }
void TextureFace::bump(std::uint8_t value) { material_bits(0xe0, value); }
std::uint8_t TextureFace::shiny() const { return source(Material).material_ & 192; }
void TextureFace::shiny(std::uint8_t value) { material_bits(0x3f, value); }
bool TextureFace::fullbright() const { return (source(Material).material_ & 32) != 0; }
void TextureFace::fullbright(bool value) { material_bits(0xdf, value ? 32 : 0); }
bool TextureFace::media_flags() const { return (source(Media).media_ & 1) != 0; }
void TextureFace::media_flags(bool value) { media_bits(0xfe, value ? 1 : 0); }
std::uint8_t TextureFace::mapping() const { return source(Media).media_ & 6; }
void TextureFace::mapping(std::uint8_t value) { media_bits(0xf9, value); }
}
