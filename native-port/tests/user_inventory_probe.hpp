#pragma once
#include "legacy_inventory_probe.hpp"
namespace opensim::tests {
void run_user_inventory_probe(framework::Uuid agent, framework::Uuid object, framework::Uuid running_item,
                              const std::filesystem::path& metadata, const LegacyInventoryProbeIo&);
void run_inventory_copy_probe(framework::Uuid agent, framework::Uuid object, framework::Uuid running_item,
                              const std::filesystem::path& metadata, const LegacyInventoryProbeIo&);
void run_inventory_new_probe(framework::Uuid agent, framework::Uuid object, framework::Uuid running_item,
                             const std::filesystem::path& metadata, const LegacyInventoryProbeIo&);
void run_inventory_link_probe(framework::Uuid agent, framework::Uuid object, framework::Uuid running_item,
                              const std::filesystem::path& metadata, const LegacyInventoryProbeIo&);
void run_generic_asset_probe(framework::Uuid agent, const std::filesystem::path& metadata,
                             const LegacyInventoryProbeIo&);
}
