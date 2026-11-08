#pragma once
#include "legacy_inventory_probe.hpp"
namespace opensim::tests {
struct DownloadedTaskInventory {
    std::string filename;
    std::string text;
};

DownloadedTaskInventory read_task_inventory_file(framework::Uuid object, std::uint32_t local,
                                                 std::int16_t serial, const LegacyInventoryProbeIo& io);
void check_empty_task_inventory(framework::Uuid object, std::uint32_t local, const LegacyInventoryProbeIo& io);
std::string check_task_inventory_download(
    framework::Uuid agent, framework::Uuid object, std::uint32_t local, std::int16_t serial,
    const LegacyInventoryProbeIo& io, const std::function<void(framework::Uuid)>& verify_asset = {},
    const std::function<void(framework::Uuid, framework::Uuid)>& verify_running = {}, int expected_items = -1);
}
