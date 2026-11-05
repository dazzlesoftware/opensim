// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/avatar_wearable.hpp"
#include "opensim/network/rpc.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace opensim;
using namespace framework;
Uuid id(int n) {
    std::ostringstream out;
    out << "00000000-0000-0000-0000-" << std::hex << std::setw(12) << std::setfill('0') << n;
    return Uuid(out.str());
}
std::string snapshot(const AvatarWearable& wearable) {
    std::string result = std::to_string(wearable.count()) + "|";
    const auto packed = wearable.pack();
    if (packed.kind != network::Value::Kind::Array || packed.array.size() != wearable.count())
        throw std::runtime_error("Invalid packed wearable");
    for (std::size_t i = 0; i < wearable.count(); ++i) {
        const auto item = wearable.at(static_cast<int>(i));
        const auto& row = packed.array[i];
        const bool typed = row.kind == network::Value::Kind::Map && row.map.size() == 2 &&
                           row.get("item").kind == network::Value::Kind::Uuid &&
                           row.get("asset").kind == network::Value::Kind::Uuid;
        result += item.item.str() + ":" + item.asset.str() + ":" + row.get("item").text + ":" +
                  row.get("asset").text + ":" + (typed ? "1" : "0") + ",";
    }
    result += '|';
    for (int i = 0; i < 10; ++i)
        result += wearable.asset(id(i)).str() + ",";
    for (int index : {-1, 5, 99}) {
        const auto value = wearable.at(index);
        result += '|' + value.item.str() + ":" + value.asset.str();
    }
    return result;
}
int main() {
    try {
        AvatarWearable wearable;
        std::string line;
        while (std::getline(std::cin, line)) {
            std::istringstream input(line);
            std::string command, prefix;
            input >> command;
            int item = 0, asset = 0;
            try {
                if (command == "add") {
                    input >> item >> asset;
                    wearable.add(id(item), id(asset));
                } else if (command == "wear") {
                    input >> item >> asset;
                    wearable.wear(id(item), id(asset));
                } else if (command == "wear-item") {
                    input >> item >> asset;
                    wearable.wear(WearableItem{id(item), id(asset)});
                } else if (command == "ctor") {
                    input >> item >> asset;
                    wearable = AvatarWearable(id(item), id(asset));
                } else if (command == "remove") {
                    input >> item;
                    wearable.remove_item(id(item));
                } else if (command == "asset") {
                    input >> asset;
                    wearable.remove_asset(id(asset));
                } else if (command == "clear")
                    wearable.clear();
                else if (command == "unpack-null")
                    wearable.unpack(static_cast<const network::Value*>(nullptr));
                else if (command == "unpack" || command == "construct") {
                    const auto data = network::parse_llsd_xml(line.substr(line.find(' ') + 1));
                    if (command == "construct")
                        wearable = AvatarWearable(data);
                    else
                        wearable.unpack(data);
                } else if (command == "defaults") {
                    auto first = AvatarWearable::defaults(), second = AvatarWearable::defaults();
                    auto alias = first;
                    first[0]->clear();
                    prefix = "DEFAULTS:" + std::to_string(first.size()) + ":" +
                             std::to_string(first[0] != first[1] && first[0] != second[0] &&
                                            second[0]->count() == 1 && alias[0]->count() == 0) +
                             ":";
                    for (const auto& value : second)
                        prefix += snapshot(*value) + "/";
                } else if (command == "constants") {
                    prefix = "CONSTANTS:";
                    for (int value :
                         {AvatarWearable::Body, AvatarWearable::Skin, AvatarWearable::Hair,
                          AvatarWearable::Eyes, AvatarWearable::Shirt, AvatarWearable::Pants,
                          AvatarWearable::Shoes, AvatarWearable::Socks, AvatarWearable::Jacket,
                          AvatarWearable::Gloves, AvatarWearable::Undershirt, AvatarWearable::Underpants,
                          AvatarWearable::Skirt, AvatarWearable::MaxBasicWearables, AvatarWearable::Alpha,
                          AvatarWearable::Tattoo, AvatarWearable::LegacyVersionMaxWearables,
                          AvatarWearable::Physics, AvatarWearable::Universal})
                        prefix += std::to_string(value) + ",";
                    prefix += std::to_string(AvatarWearable::max_wearables_pv7) + "," +
                              std::to_string(AvatarWearable::max_wearables) + "|";
                    for (const auto& value :
                         {AvatarWearable::default_body_item, AvatarWearable::default_body_asset,
                          AvatarWearable::default_hair_item, AvatarWearable::default_hair_asset,
                          AvatarWearable::default_skin_item, AvatarWearable::default_skin_asset,
                          AvatarWearable::default_eyes_item, AvatarWearable::default_eyes_asset,
                          AvatarWearable::default_shirt_item, AvatarWearable::default_shirt_asset,
                          AvatarWearable::default_pants_item, AvatarWearable::default_pants_asset,
                          AvatarWearable::default_alpha_item, AvatarWearable::default_alpha_asset,
                          AvatarWearable::default_tattoo_item, AvatarWearable::default_tattoo_asset})
                        prefix += value.str() + ",";
                } else if (command == "limits") {
                    input >> AvatarWearable::max_wearables >> AvatarWearable::max_wearables_pv7;
                    prefix = "LIMITS:" + std::to_string(AvatarWearable::max_wearables) + ":" +
                             std::to_string(AvatarWearable::max_wearables_pv7) + ":";
                } else
                    throw std::runtime_error("Unknown wearable probe operation");
            } catch (const std::exception&) {
                prefix = "EX:";
            }
            std::cout << prefix << snapshot(wearable) << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
