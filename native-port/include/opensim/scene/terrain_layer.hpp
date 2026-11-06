// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/terrain.hpp"
#include <span>
namespace opensim::scene {
struct TerrainPatchCoordinate {
    std::uint32_t x, y;
};
struct TerrainLayerPacket {
    std::uint8_t type;
    std::vector<std::uint8_t> data;
};
// OpenSim's land DCT/quantization and LLClientView batching. Data is the
// LayerData variable field; the host supplies LLUDP headers/reliability.
std::vector<TerrainLayerPacket> terrain_layer_packets(const Terrain& terrain,
                                                      std::span<const TerrainPatchCoordinate> patches);
}
