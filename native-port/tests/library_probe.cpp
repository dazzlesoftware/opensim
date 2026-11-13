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
#include "opensim/services/library_service.hpp"
#include "opensim/services/inventory_login.hpp"
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <sstream>
using namespace opensim::services;
using opensim::framework::Uuid;
std::string json_value(const opensim::network::Value& value) {
    using V = opensim::network::Value;
    auto quote = [](const std::string& text) {
        auto encoded = opensim::network::json_strings({{"", text}});
        return encoded.substr(4, encoded.size() - 5);
    };
    if (value.kind == V::Kind::String)
        return quote(value.text);
    if (value.kind == V::Kind::Integer)
        return std::to_string(value.integer);
    std::string result;
    if (value.kind == V::Kind::Array) {
        result = "[";
        for (const auto& item : value.array) {
            if (result.size() > 1)
                result += ',';
            result += json_value(item);
        }
        return result + "]";
    }
    if (value.kind == V::Kind::Map) {
        result = "{";
        for (const auto& [key, item] : value.map) {
            if (result.size() > 1)
                result += ',';
            result += quote(key) + ":" + json_value(item);
        }
        return result + "}";
    }
    return "null";
}
int tree_test() {
    auto root = std::make_shared<InventoryFolderTree>();
    root->id = Uuid("00000000-0000-0000-0000-000000000001");
    root->owner = Uuid::random();
    const Uuid a("00000000-0000-0000-0000-000000000002"), b("00000000-0000-0000-0000-000000000003"),
        i("00000000-0000-0000-0000-000000000004");
    auto child = root->create_child(a, "A", 65535);
    auto nested = child->create_child(b, "B", 6);
    auto item = std::make_shared<InventoryItem>();
    item->id = i;
    item->name = "Object";
    item->parent = b;
    item->asset_id = a;
    nested->add_item(item);
    std::vector<bool> checks;
    checks.push_back(child->owner == root->owner && child->parent == root->id && child->type == -1 &&
                     child->version == 0);
    checks.push_back(!root->create_child(a, "Duplicate", 1));
    checks.push_back(root->find_item(i) == item && root->find_asset(a) == item &&
                     root->find_folder(b) == nested);
    checks.push_back(!root->find_type(6) && child->find_type(6) == nested);
    checks.push_back(root->find_folder_path("\xe2\x80\x83 A/B \xc2\xa0") == nested);
    checks.push_back(!root->find_folder_path("A//B") && !root->find_folder_path("/A"));
    checks.push_back(root->find_folder_path("A/") == child && root->find_folder_path("/") == root);
    checks.push_back(root->find_item_path("A/B/Object") == item && !root->find_item_path(" A/B/Object"));
    checks.push_back(root->total_count() == 1);
    auto snapshot = nested->items();
    item->name = "Changed";
    checks.push_back(snapshot.front()->name == "Changed");
    checks.push_back(root->delete_item(i) && !root->delete_item(i) && root->total_count() == 0 &&
                     snapshot.front() == item);
    nested->add_item(item);
    checks.push_back(root->remove_child(a) == child && !root->contains_child(a) && root->total_count() == 0 &&
                     child->parent == root->id);
    root->add_child(child);
    checks.push_back(root->total_count() == 1);
    root->purge();
    checks.push_back(root->children().empty() && nested->items().empty() && root->total_count() == 0);
    bool passed = true;
    std::cout << "T";
    for (bool value : checks) {
        std::cout << '|' << value;
        passed &= value;
    }
    std::cout << '\n';
    return passed ? 0 : 1;
}
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    try {
        if (argc == 2 && std::string(argv[1]) == "--tree")
            return tree_test();
        if (argc == 4 && std::string(argv[1]) == "--login") {
            const auto seed = std::stoi(argv[2]);
            auto id = [](int number) {
                std::ostringstream text;
                text << "00000000-0000-0000-0000-" << std::hex << std::setfill('0') << std::setw(12)
                     << number;
                return Uuid(text.str());
            };
            std::optional<std::vector<InventoryFolder>> folders;
            if (seed != 0)
                folders = std::vector<InventoryFolder>{};
            if (seed >= 2)
                *folders = {{"My Inventory", 8, 65535, id(2), id(1), {}},
                            {"My Inventory", -1, 1, id(3), id(1), {}},
                            {"Nested & folder", 6, 7, id(4), id(1), id(3)}};
            std::optional<std::vector<InventoryItem>> gestures;
            if (seed == 2) {
                InventoryItem item;
                item.id = id(5);
                item.asset_id = id(6);
                gestures = std::vector{item};
            }
            std::unique_ptr<LibraryService> library;
            if (seed == 2)
                library = std::make_unique<LibraryService>(argv[3]);
            std::cout << json_value(inventory_login_fields(folders, library.get(), gestures)) << '\n';
            return 0;
        }
        if (argc < 2)
            return 2;
        LibraryService library(argv[1], "OpenSim Library");
        std::vector<std::string> rows;
        for (const auto& folder : library.ordered_folders()) {
            std::ostringstream row;
            row << "F|" << folder->id.str() << '|' << folder->parent.str() << '|' << folder->owner.str()
                << '|' << folder->name << '|' << folder->type << '|' << folder->version;
            rows.push_back(row.str());
            for (const auto& item : folder->items()) {
                std::ostringstream value;
                value << "I|" << item->id.str() << '|' << item->parent.str() << '|' << item->owner.str()
                      << '|' << item->creator_id << '|' << item->asset_id.str() << '|' << item->name << '|'
                      << item->description << '|' << item->inventory_type << '|' << item->asset_type << '|'
                      << static_cast<std::uint32_t>(item->base_permissions) << '|'
                      << static_cast<std::uint32_t>(item->current_permissions) << '|'
                      << static_cast<std::uint32_t>(item->next_permissions) << '|'
                      << static_cast<std::uint32_t>(item->everyone_permissions) << '|'
                      << static_cast<std::uint32_t>(item->group_permissions) << '|'
                      << static_cast<std::uint32_t>(item->flags);
                rows.push_back(value.str());
            }
        }
        std::sort(rows.begin(), rows.end());
        for (const auto& row : rows)
            std::cout << row << '\n';
        std::vector<Uuid> ids;
        for (const auto& folder : library.ordered_folders())
            for (const auto& value : folder->items())
                ids.push_back(value->id);
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        for (const auto& id : ids) {
            const auto value = library.item(id);
            std::cout << "G|" << id.str() << '|' << value->name << '|' << value->parent.str() << '\n';
        }
        if (!ids.empty()) {
            auto values = library.multiple_items({Uuid{}, ids.front(), Uuid{}, ids.front()});
            std::cout << "Q|" << values->size() << '|' << ((*values)[0] == (*values)[1]) << '\n';
        }
        const auto folders = library.ordered_folders();
        std::cout << "O";
        for (const auto& folder : folders)
            std::cout << '|' << folder->id.str();
        std::cout << '\n';
        LibraryService second("missing-file.xml", "Ignored");
        std::cout << "S|" << (second.root() == library.root()) << '\n';
        auto values = library.multiple_items({Uuid{}});
        std::cout << "M|" << values.has_value() << '\n';
    } catch (const std::exception& error) {
        std::cout << "ERROR:" << error.what() << '\n';
        return 1;
    }
}
