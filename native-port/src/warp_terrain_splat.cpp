// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/map_tile.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace opensim::scene {
namespace {

// The seeded System.Random sequence used by Warp3D's Perlin table.
class SplatRandom {
  public:
    SplatRandom() {
        int previous = 161803398 - 42;
        state_[55] = previous;
        int next = 1;
        for (int i = 1; i < 55; ++i) {
            state_[21 * i % 55] = next;
            next = previous - next;
            if (next < 0)
                next += maximum;
            previous = state_[21 * i % 55];
        }
        for (int pass = 0; pass < 4; ++pass) {
            for (int i = 1; i <= 55; ++i) {
                state_[i] -= state_[1 + (i + 30) % 55];
                if (state_[i] < 0)
                    state_[i] += maximum;
            }
        }
    }

    int next() {
        if (++left_ == 56)
            left_ = 1;
        if (++right_ == 56)
            right_ = 1;
        int sample = state_[left_] - state_[right_];
        if (sample == maximum)
            --sample;
        if (sample < 0)
            sample += maximum;
        state_[left_] = sample;
        return sample;
    }

  private:
    static constexpr int maximum = 2147483647;
    std::array<int, 56> state_{};
    int left_ = 0, right_ = 21;
};

float lerp(float a, float b, float fraction) {
    return a + (b - a) * fraction;
}

class SplatNoise {
  public:
    SplatNoise() {
        SplatRandom random;
        for (int i = 0; i < size; ++i) {
            permutation_[i] = i;
            random.next(); // One-dimensional gradient.
            const float x = float(random.next() % (2 * size) - size) / size;
            const float y = float(random.next() % (2 * size) - size) / size;
            const float scale = 1.f / std::sqrt(x * x + y * y);
            gradients_[i] = {x * scale, y * scale};
            for (int axis = 0; axis < 3; ++axis)
                random.next(); // Three-dimensional gradient.
        }
        for (int i = size - 1; i > 0; --i)
            std::swap(permutation_[i], permutation_[random.next() % size]);
        std::copy_n(permutation_.begin(), size, permutation_.begin() + size);
    }

    float sample(float x, float y) const {
        x += 4096.f;
        y += 4096.f;
        const auto cell_x = static_cast<std::int64_t>(x);
        const auto cell_y = static_cast<std::int64_t>(y);
        const float dx = x - static_cast<float>(cell_x);
        const float dy = y - static_cast<float>(cell_y);
        const auto x0 = cell_x & (size - 1);
        const auto x1 = (x0 + 1) & (size - 1);
        const auto y0 = cell_y & (size - 1);
        const auto y1 = (y0 + 1) & (size - 1);
        const auto gradient = [&](std::size_t ix, std::size_t iy, float rx, float ry) {
            const auto& g = gradients_[permutation_[permutation_[ix] + iy]];
            return rx * g[0] + ry * g[1];
        };
        const auto smooth = [](float t) { return t * t * (3.f - 2.f * t); };
        const float south = lerp(gradient(x0, y0, dx, dy), gradient(x1, y0, dx - 1, dy), smooth(dx));
        const float north = lerp(gradient(x0, y1, dx, dy - 1), gradient(x1, y1, dx - 1, dy - 1), smooth(dx));
        return lerp(south, north, smooth(dy));
    }

  private:
    static constexpr int size = 1024;
    std::array<int, 2 * size> permutation_{};
    std::array<std::array<float, 2>, size> gradients_{};
};

float splat_layer(float elevation, float x, float y, std::uint32_t global_x, std::uint32_t global_y,
                  const WarpTerrainSplat& options) {
    const auto bilinear = [&](const std::array<float, 4>& corners) {
        return lerp(lerp(corners[0], corners[2], x), lerp(corners[1], corners[3], x), y);
    };
    const float start = std::clamp(bilinear(options.start_heights), 0.f, 255.f);
    const float range = std::clamp(bilinear(options.height_ranges), 0.f, 255.f);
    if (range == 0 || std::isnan(start) || std::isnan(range) || std::isnan(elevation))
        return 0;

    static const SplatNoise noise;
    const float sx = global_x * .20319f;
    const float sy = global_y * .20319f;
    const float low = noise.sample(sx * .222222f, sy * .222222f) * 13.f;
    const float turbulence = noise.sample(sx * 2, sy * 2) / 2 + noise.sample(sx, sy);
    return std::clamp((elevation + (low + turbulence * 4.5f) - start) / range * 4.f, 0.f, 3.f);
}

} // namespace

MapBitmap render_warp_terrain_splat(std::span<const float> terrain, std::uint32_t terrain_width,
                                  std::uint32_t terrain_height, const WarpTerrainSplat& options,
                                  std::array<MapBitmap, 4> textures) {
    const auto pixel_count = std::uint64_t(options.width) * options.height;
    if (pixel_count == 0 || pixel_count > std::numeric_limits<std::size_t>::max() / 3 ||
        terrain_width == 0 || terrain_height == 0 || std::uint64_t(terrain_width) * terrain_height != terrain.size())
        throw std::invalid_argument("Invalid Warp3D terrain splat dimensions");

    constexpr std::array<MapColor, 4> defaults{{{164, 136, 117}, {65, 87, 47}, {157, 145, 131}, {125, 128, 130}}};
    const bool average = !options.textured || options.average_textures;
    auto colors = defaults;
    if (average) {
        // The managed solid-color path puts these constants into BGR scanlines.
        for (auto& color : colors)
            std::swap(color[0], color[2]);
    }
    if (options.textured) {
        for (std::size_t layer = 0; layer < textures.size(); ++layer) {
            if (textures[layer].pixels.empty())
                continue;
            MapBitmap patch{16, 16, std::vector<std::uint8_t>(16 * 16 * 3)};
            composite_map_bitmap(patch, std::move(textures[layer]), 0, 0, 16, 16);
            textures[layer] = std::move(patch);
            if (average) {
                std::array<unsigned, 3> sums{};
                for (std::size_t offset = 0; offset < textures[layer].pixels.size(); offset += 3)
                    for (std::size_t channel = 0; channel < 3; ++channel)
                        sums[channel] += textures[layer].pixels[offset + channel];
                for (std::size_t channel = 0; channel < 3; ++channel)
                    colors[layer][channel] = static_cast<std::uint8_t>(sums[channel] / (16 * 16));
            }
        }
    }

    MapBitmap result{options.width, options.height,
                     std::vector<std::uint8_t>(static_cast<std::size_t>(pixel_count) * 3)};
    // The managed renderer divides the integer dimensions before converting to float.
    const auto step_x = terrain_width / options.width;
    const auto step_y = terrain_height / options.height;
    const float inverse_width = options.width > 1 ? 1.f / (options.width - 1) : 0.f;
    const float inverse_height = options.height > 1 ? 1.f / (options.height - 1) : 0.f;
    for (std::uint32_t y = 0; y < options.height; ++y) {
        const auto ty = y * step_y;
        for (std::uint32_t x = 0; x < options.width; ++x) {
            const auto tx = x * step_x;
            const float layer = splat_layer(terrain[std::size_t(ty) * terrain_width + tx],
                                           x * inverse_width, y * inverse_height,
                                           options.region_x + tx, options.region_y + ty, options);
            const auto low = static_cast<std::size_t>(layer);
            const auto high = std::min<std::size_t>(low + 1, 3);
            const auto patch_offset = ((ty & 15u) * 16 + (tx & 15u)) * 3;
            const auto output = (std::size_t(y) * options.width + x) * 3;
            const auto component = [&](std::size_t index, std::size_t channel) {
                return average || textures[index].pixels.empty()
                    ? colors[index][channel]
                    : textures[index].pixels[patch_offset + channel];
            };
            for (std::size_t channel = 0; channel < 3; ++channel)
                result.pixels[output + channel] = static_cast<std::uint8_t>(
                    lerp(component(low, channel), component(high, channel), layer - float(low)));
        }
    }
    return result;
}

} // namespace opensim::scene
