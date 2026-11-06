// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/map_tile.hpp"
#include "opensim/framework/text.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace opensim::scene {
namespace {

std::vector<unsigned char> map_label_font() {
#ifdef _WIN32
    const auto* windows = std::getenv("WINDIR");
    const std::filesystem::path candidates[]{
        std::filesystem::path(windows ? windows : "C:/Windows") / "Fonts/arial.ttf",
    };
#elif defined(__APPLE__)
    const std::filesystem::path candidates[]{
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/Library/Fonts/Arial.ttf",
    };
#else
    const std::filesystem::path candidates[]{
        "/usr/share/fonts/truetype/msttcorefonts/Arial.ttf",
        "/usr/share/fonts/truetype/msttcorefonts/arial.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };
#endif
    for (const auto& path : candidates) {
        std::ifstream font(path, std::ios::binary);
        if (font)
            return {std::istreambuf_iterator<char>(font), {}};
    }
    throw std::runtime_error("Could not find a font for world map labels");
}

struct LabelFont {
    FT_Library library = nullptr;
    FT_Face face = nullptr;

    ~LabelFont() {
        if (face)
            FT_Done_Face(face);
        if (library)
            FT_Done_FreeType(library);
    }
};

} // namespace

void draw_map_label(MapBitmap& canvas, std::string_view text, std::int64_t x, std::int64_t y,
                    bool bottom_aligned) {
    if (text.empty())
        return;
    static const auto bytes = map_label_font();
    LabelFont font;
    if (FT_Init_FreeType(&font.library) ||
        FT_New_Memory_Face(font.library, bytes.data(), static_cast<FT_Long>(bytes.size()), 0, &font.face) ||
        FT_Set_Char_Size(font.face, 0, 32 * 64, 96, 96))
        throw std::runtime_error("Could not initialize the world map label font");

    const auto unicode = framework::utf16_replacing(text);
    const auto& metrics = font.face->size->metrics;
    const auto top = bottom_aligned ? y - metrics.height / 64 : y;
    const auto baseline = top + metrics.ascender / 64;
    FT_UInt previous = 0;
    FT_Pos pen = 0;
    for (std::size_t index = 0; index < unicode.size(); ++index) {
        std::uint32_t codepoint = unicode[index];
        if (codepoint >= 0xd800 && codepoint <= 0xdbff && index + 1 < unicode.size()) {
            const auto low = unicode[index + 1];
            if (low >= 0xdc00 && low <= 0xdfff) {
                codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                ++index;
            }
        }
        const auto glyph_index = FT_Get_Char_Index(font.face, codepoint);
        if (previous && glyph_index && FT_HAS_KERNING(font.face)) {
            FT_Vector kerning{};
            if (!FT_Get_Kerning(font.face, previous, glyph_index, FT_KERNING_DEFAULT, &kerning))
                pen += kerning.x;
        }
        if (FT_Load_Glyph(font.face, glyph_index, FT_LOAD_RENDER))
            continue;
        const auto* glyph = font.face->glyph;
        const auto& bitmap = glyph->bitmap;
        const auto left = x + pen / 64 + glyph->bitmap_left;
        const auto glyph_top = baseline - glyph->bitmap_top;
        if (bitmap.pixel_mode == FT_PIXEL_MODE_GRAY) {
            for (unsigned row = 0; row < bitmap.rows; ++row) {
                const auto target_y = glyph_top + row;
                if (target_y < 0 || target_y >= canvas.height)
                    continue;
                const auto* coverage = bitmap.buffer + std::ptrdiff_t(row) * bitmap.pitch;
                for (unsigned column = 0; column < bitmap.width; ++column) {
                    const auto target_x = left + column;
                    if (target_x < 0 || target_x >= canvas.width)
                        continue;
                    const auto offset = (std::size_t(target_y) * canvas.width + std::size_t(target_x)) * 3;
                    const unsigned alpha = coverage[column];
                    for (std::size_t channel = 0; channel < 3; ++channel) {
                        auto& pixel = canvas.pixels[offset + channel];
                        pixel = static_cast<std::uint8_t>((255 * alpha + pixel * (255 - alpha) + 127) / 255);
                    }
                }
            }
        }
        pen += glyph->advance.x;
        previous = glyph_index;
    }
}

} // namespace opensim::scene
