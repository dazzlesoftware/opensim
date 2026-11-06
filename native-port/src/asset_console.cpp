// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/asset_endpoint.hpp"
#include "asset_console_format.hpp"
#include "opensim/framework/console_command_resolver.hpp"
#include "opensim/framework/filesystem.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace opensim::server {
std::string asset_console_flags(std::int32_t flags) {
    constexpr auto known = framework::Maptile | framework::Rewritable | framework::Collectable;
    if (flags & ~known)
        return std::to_string(flags);
    if (flags == 0)
        return "Normal";
    std::string text;
    for (const auto& [flag, name] :
         {std::pair{framework::Maptile, "Maptile"}, std::pair{framework::Rewritable, "Rewritable"},
          std::pair{framework::Collectable, "Collectable"}}) {
        if (!(flags & flag))
            continue;
        if (!text.empty())
            text += ", ";
        text += name;
    }
    return text;
}

void asset_console_hex(std::span<const std::uint8_t> data,
                       const std::function<void(const std::string&)>& output) {
    constexpr std::size_t bytes_per_row = 16;
    constexpr std::size_t row_limit = 5;
    for (std::size_t offset = 0;
         offset < std::min(data.size(), row_limit * bytes_per_row);
         offset += bytes_per_row) {
        std::ostringstream line;
        line << std::hex << std::setfill('0') << std::setw(4) << offset << ": " << std::uppercase;
        const auto end = std::min(offset + bytes_per_row, data.size());
        for (auto index = offset; index < end; ++index) {
            if (index != offset)
                line << '-';
            line << std::setw(2) << static_cast<unsigned>(data[index]);
        }
        output(line.str());
    }
}

namespace {
using services::AssetService;
using Output = std::function<void(const std::string&)>;
using Arguments = std::vector<std::string>;


void show_asset(AssetService& service, const Arguments& args, const Output& output) {
    if (args.size() < 3) {
        output("Syntax: show asset <ID>");
        return;
    }
    const auto asset = service.get_asset(args[2]);
    if (!asset || !asset->data || asset->data->empty()) {
        output("Asset not found");
        return;
    }

    const auto type_number = std::to_string(asset->type());
    const auto type_name = framework::asset_type_enum_name(asset->type());
    output("Name: " + asset->name().value_or(""));
    output("Description: " + asset->description().value_or(""));
    output("Type: " + (type_name.empty() ? type_number : std::string(type_name)) +
           " (type number = " + type_number + ")");
    output("Content-type: " + asset->metadata()->content_type());
    output("Size: " + std::to_string(asset->data->size()) + " bytes");
    output(asset->temporary() ? "Temporary: yes" : "Temporary: no");
    output("Flags: " + asset_console_flags(asset->flags()));

    asset_console_hex(*asset->data, output);
}

void delete_asset(AssetService& service, const Arguments& args, const Output& output) {
    if (args.size() < 3) {
        output("Syntax: delete asset <ID>");
        return;
    }
    const auto asset = service.get_asset(args[2]);
    if (!asset || !asset->data || asset->data->empty()) {
        output("Could not find asset with ID " + args[2]);
        return;
    }
    const auto message = service.erase(asset->id()) ? "Deleted asset " : "ERROR: Could not delete asset ";
    output(message + asset->id() + " " + asset->name().value_or(""));
}

void dump_asset(AssetService& service, const Arguments& args, const Output& output) {
    if (args.size() < 3) {
        output("Usage is dump asset <ID>");
        return;
    }
    const auto& filename = args[2];
    framework::Uuid id;
    if (!framework::try_parse_sdk_uuid(filename, id)) {
        output("ERROR: " + filename + " is not a valid ID format");
        return;
    }
    const auto asset = service.get_asset(id.str());
    if (!asset) {
        output("ERROR: No asset found with ID " + id.str());
        return;
    }

    const auto path = framework::utf8_path(filename);
#ifdef _WIN32
    std::FILE* file = nullptr;
    const int error = ::_wfopen_s(&file, path.c_str(), L"wbx");
#else
    auto* file = std::fopen(path.c_str(), "wbx");
    const int error = file ? 0 : errno;
#endif
    if (!file) {
        if (error == EEXIST) {
            output("File " + filename + " already exists.  Please move or remove it.");
            return;
        }
        throw std::system_error(error, std::generic_category(), "Could not create " + filename);
    }
    if (!asset->data) {
        std::fclose(file);
        throw std::runtime_error("Asset data is missing");
    }
    const auto& bytes = *asset->data;
    const bool written = bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    const bool closed = std::fclose(file) == 0;
    if (!written || !closed)
        throw std::runtime_error("Could not write " + filename);
    output("Asset dumped to file " + filename);
}
}

void AssetEndpoint::register_console_commands(framework::ConsoleCommandResolver& commands,
                                              const Output& output) const {
    const auto bind = [&](auto handler) {
        using Callback = framework::ConsoleCommandResolver::Callback::element_type;
        return std::make_shared<Callback>([service = service_, output, handler](const auto&, auto& args) {
            handler(*service, args, output);
        });
    };
    commands.add({"Assets", "show asset", "show asset <ID>", "Show asset information", "", false},
                 bind(show_asset));
    commands.add({"Assets", "delete asset", "delete asset <ID>", "Delete asset from database", "", false},
                 bind(delete_asset));
    commands.add({"Assets", "dump asset", "dump asset <ID>", "Dump asset to a file",
                  "The filename is the same as the ID given.", false},
                 bind(dump_asset));
}
}
