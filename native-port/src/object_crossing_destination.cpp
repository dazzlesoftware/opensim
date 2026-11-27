// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/object_crossing_destination.hpp"
#include "opensim/framework/sdk_float.hpp"
#include <bit>
#include <cmath>
#include <limits>

namespace opensim::scene {
namespace {
std::int32_t integer(float value) {
    // Bundled .NET 8 x64 conversion, without an undefined out-of-range C++ cast.
    if (!std::isfinite(value) || value >= 2147483648.f || value < -2147483648.f)
        return std::numeric_limits<std::int32_t>::min();
    return static_cast<std::int32_t>(value);
}

std::int32_t coordinate(float local, std::int32_t origin) {
    auto value = std::bit_cast<std::uint32_t>(integer(local)) + std::bit_cast<std::uint32_t>(origin);
    value += local >= 0.f ? 1u : 0xffffffffu;
    return std::bit_cast<std::int32_t>(value);
}

float local_position(float local, std::int32_t origin, std::int32_t destination, std::int32_t size) {
    const auto difference =
        std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(destination) - std::bit_cast<std::uint32_t>(origin));
    const float value = framework::sdk_float_difference(local, static_cast<float>(difference));
    const float maximum = static_cast<float>(size == 0 ? 256 : size) - .2f;
    // Bundled Utils.Clamp tests the upper bound first; NaN returns that bound.
    if (!(value < maximum))
        return maximum;
    return value > .2f ? value : .2f;
}
} // namespace

std::optional<services::GridRegion> object_crossing_destination(bool scene_available, std::int32_t source_x,
                                                                std::int32_t source_y, framework::Vector3 target,
                                                                const ObjectDestinationLookup &lookup,
                                                                framework::Vector3 &arrival) {
    arrival = target;
    if (!scene_available)
        return std::nullopt;
    auto destination = lookup(coordinate(target.x, source_x), coordinate(target.y, source_y));
    if (!destination)
        return std::nullopt;
    arrival.x = local_position(target.x, source_x, destination->x, destination->size_x);
    arrival.y = local_position(target.y, source_y, destination->y, destination->size_y);
    return destination;
}
} // namespace opensim::scene
