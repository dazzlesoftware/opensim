// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "object_details_color_probe.hpp"
#include <chrono>
#include <stdexcept>
#include <thread>

using namespace opensim;
using namespace framework;
using namespace scene;

void verify_object_details_text_color(RegionRuntime &runtime,
                                      const std::function<void(std::shared_ptr<AssetBase>)> &store) {
    auto host = SceneObject::primitive(runtime.descriptor().estate_owner, {229, 215, 30});
    host.text_argb = 0x80804020u;
    if (!runtime.world().add(host))
        throw std::runtime_error("Could not create the object-details text color fixture");
    const std::string_view source = R"LSL(default {
        state_entry() {
            list values = llGetObjectDetails(llGetKey(), [OBJECT_TEXT_COLOR, OBJECT_TEXT_ALPHA]);
            vector color = llList2Vector(values, 0);
            float alpha = llList2Float(values, 1);
            if (llGetListLength(values) == 2 &&
                color.x == 0.501960813999176025390625 &&
                color.y == 0.2509804069995880126953125 &&
                color.z == 0.12549020349979400634765625 &&
                alpha == 0.50196075439453125)
                llSetObjectDesc("Text color verified");
            else
                llSetObjectDesc("Text color rounding mismatch");
        }
    })LSL";
    auto asset = std::make_shared<AssetBase>(Uuid::random(), "Object-details color fixture", 10, "");
    asset->data = std::make_shared<std::vector<std::uint8_t>>(source.begin(), source.end());
    store(asset);
    auto item = std::make_shared<TaskInventoryItem>();
    item->item_id = Uuid::random();
    item->parent_id = item->parent_part_id = host.id;
    item->owner_id = item->creator_id = host.owner;
    item->asset_id = asset->full_id();
    item->type = item->inventory_type = 10;
    item->script_running = true;
    item->name = "Object-details color fixture";
    runtime.restore_task_inventory(host.id, {item});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (runtime.world().object(host.id)->description.empty() && std::chrono::steady_clock::now() < deadline) {
        runtime.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto description = runtime.world().object(host.id)->description;
    if (description != "Text color verified")
        throw std::runtime_error("Object-details text color regression: " + description);
    runtime.world().remove(host.id);
}
