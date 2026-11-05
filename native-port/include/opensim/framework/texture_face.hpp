// SDK compatibility model. See protocol/LIBOPENMETAVERSE-LICENSE.txt.
#pragma once
#include "opensim/framework/uuid.hpp"
#include <array>
#include <cstdint>
#include <memory>

namespace opensim::framework {
// Values are part of the viewer texture-entry contract.
enum TextureAttribute : std::uint32_t {
    TextureId = 1,
    TextureColor = 2,
    RepeatU = 4,
    RepeatV = 8,
    OffsetU = 16,
    OffsetV = 32,
    Rotation = 64,
    Material = 128,
    Media = 256,
    Glow = 512,
    MaterialId = 1024,
    AllTextureAttributes = 0xffffffffu
};
class TextureFace {
  public:
    explicit TextureFace(std::shared_ptr<TextureFace> parent = {});
    std::shared_ptr<TextureFace> clone() const;
    std::uint32_t dirty_flags() const {
        return dirty_;
    }
    void dirty_flags(std::uint32_t value) {
        dirty_ = value;
    }
    std::uint32_t attributes() const {
        return attributes_;
    }
    std::array<float, 4> color() const;
    void color(std::array<float, 4> value);
    float repeat_u() const;
    void repeat_u(float value);
    float repeat_v() const;
    void repeat_v(float value);
    float offset_u() const;
    void offset_u(float value);
    float offset_v() const;
    void offset_v(float value);
    float rotation() const;
    void rotation(float value);
    float glow() const;
    void glow(float value);
    Uuid texture_id() const;
    void texture_id(Uuid value);
    Uuid material_id() const;
    void material_id(Uuid value);
    std::uint8_t bump() const;
    void bump(std::uint8_t value);
    std::uint8_t shiny() const;
    void shiny(std::uint8_t value);
    bool fullbright() const;
    void fullbright(bool value);
    bool media_flags() const;
    void media_flags(bool value);
    std::uint8_t mapping() const;
    void mapping(std::uint8_t value);
    static std::int16_t quantize_offset(float value);
    static std::int16_t quantize_rotation(float value);
    static std::uint8_t quantize_glow(float value);

  private:
    friend class TextureEntry;
    const TextureFace& source(std::uint32_t attribute) const;
    void mark(std::uint32_t attribute) {
        attributes_ |= attribute;
        dirty_ |= attribute;
    }
    void material_bits(std::uint8_t keep, std::uint8_t value);
    void media_bits(std::uint8_t keep, std::uint8_t value);
    std::shared_ptr<TextureFace> parent_;
    Uuid texture_, material_id_;
    std::array<float, 4> color_{1, 1, 1, 1};
    float repeat_u_ = 1, repeat_v_ = 1;
    std::int16_t offset_u_ = 0, offset_v_ = 0, rotation_ = 0;
    std::uint8_t glow_ = 0, material_ = 0, media_ = 0;
    std::uint32_t attributes_, dirty_ = 0;
};
}
