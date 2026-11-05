/*
Copyright (c) Contributors, http://opensimulator.org/
See CONTRIBUTORS.TXT for a full list of copyright holders.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenSimulator Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "opensim/services/inventory_login.hpp"
namespace opensim::services {
namespace {
network::Value array() {
    network::Value result;
    result.kind = network::Value::Kind::Array;
    return result;
}
network::Value folder_value(const InventoryFolder &folder) {
    return network::Value::object(
        {{"name", folder.name},
         {"parent_id", folder.parent.str()},
         {"version", network::Value(static_cast<std::int32_t>(static_cast<std::uint16_t>(folder.version)))},
         {"type_default", network::Value(static_cast<std::int32_t>(static_cast<std::int16_t>(folder.type)))},
         {"folder_id", folder.id.str()}});
}
}
network::Value inventory_login_fields(const std::optional<std::vector<InventoryFolder>> &skeleton,
                                      const LibraryService *library,
                                      const std::optional<std::vector<InventoryItem>> &gestures) {
    auto result = network::Value::object({{"inventory-skeleton", array()},
                                          {"inventory-root", array()},
                                          {"inventory-skel-lib", array()},
                                          {"inventory-lib-root", network::Value{}},
                                          {"inventory-lib-owner", array()},
                                          {"gestures", array()}});
    if (skeleton) {
        framework::Uuid root;
        for (const auto &folder : *skeleton) {
            if (folder.parent.is_zero() && folder.name == "My Inventory")
                root = folder.id;
            result.map["inventory-skeleton"].array.push_back(folder_value(folder));
        }
        result.map["inventory-root"].array.push_back(network::Value::object({{"folder_id", root.str()}}));
    }
    if (library && library->root()) {
        result.map["inventory-lib-root"] = array();
        result.map["inventory-lib-root"].array.push_back(
            network::Value::object({{"folder_id", "00000112-000f-0000-0000-000100bba000"}}));
        result.map["inventory-lib-owner"].array.push_back(
            network::Value::object({{"agent_id", "11111111-1111-0000-0000-000100bba000"}}));
        for (const auto &folder : library->ordered_folders())
            result.map["inventory-skel-lib"].array.push_back(folder_value(*folder));
    }
    if (gestures)
        for (const auto &item : *gestures)
            result.map["gestures"].array.push_back(
                network::Value::object({{"item_id", item.id.str()}, {"asset_id", item.asset_id.str()}}));
    return result;
}
}
