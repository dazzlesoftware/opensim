#include "opensim/scene/native_profile_context.hpp"
#include "opensim/scene/profile_user_agent.hpp"
#include "opensim/services/open_profile_client.hpp"
#include "opensim/server/grid_identity.hpp"
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/script/asset_text.hpp"
#include "opensim/server/agent_access.hpp"
#include "opensim/server/object_handler.hpp"
#include "opensim/framework/directory_entries.hpp"
#include "opensim/framework/viewer_name.hpp"
#include "opensim/framework/http_host.hpp"
#include "opensim/framework/http_uri.hpp"
#include "opensim/framework/host_resolver.hpp"
#include "opensim/server/config_xml_startup.hpp"
#include "opensim/framework/nini_xml_document_save.hpp"
#include "opensim/framework/nini_xml_reader.hpp"
#include "opensim/framework/nini_xml_resolver.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/vector_text.hpp"
#include <sstream>
#include "opensim/framework/integer_text.hpp"
#include "opensim/framework/vector_text.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/network/script_smtp.hpp"
#include "opensim/network/sdk_osd_stream.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/network/rest_forms.hpp"
#include "opensim/network/simulation_body.hpp"
#include <condition_variable>
#include <deque>
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
#include "opensim/network/web_http_clients.hpp"
#include "opensim/network/osd_service_http.hpp"
#include "opensim/services/sqlite_friends_plugin.hpp"
#include "opensim/services/sqlite_offline_im_store.hpp"
#ifdef OPENSIM_HAVE_MYSQL
#include "opensim/services/mysql_offline_im_store.hpp"
#endif
#ifdef OPENSIM_HAVE_POSTGRESQL
#include "opensim/services/pgsql_offline_im_store.hpp"
#endif
#include "opensim/services/sqlite_avatar_plugin.hpp"
#include "opensim/server/asset_endpoint.hpp"
#include "fs_asset_service.hpp"
#include "opensim/services/hg_asset_service.hpp"
#include "opensim/services/native_asset_storage.hpp"
#include "opensim/services/sqlite_grid_user_plugin.hpp"
#include "opensim/server/config.hpp"
#include "opensim/framework/nini_config_invariant.hpp"
#include "opensim/server/login_application.hpp"
#include "opensim/services/login_orchestration.hpp"
#include "opensim/services/login_recovery.hpp"
#include "opensim/services/login_completion.hpp"
#include "opensim/services/friends_service.hpp"
#include "opensim/services/sqlite_profiles_data.hpp"
#include "opensim/services/profiles_configuration.hpp"
#include "opensim/services/local_profiles_connector.hpp"
#include "opensim/services/friends_connector.hpp"
#include "opensim/services/authentication_connector.hpp"
#include "opensim/services/simulation_connector.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/services/account_provisioner.hpp"
#include "opensim/services/appearance_provisioner.hpp"
#include "opensim/services/avatar_service.hpp"
#include "opensim/framework/identity.hpp"
#include "opensim/framework/expiring_cache.hpp"
#include "opensim/server/mute_list_endpoint.hpp"
#include "opensim/services/presence_connector.hpp"
#include "opensim/services/grid_user_connector.hpp"
#include "opensim/services/sqlite_connection.hpp"
#include "opensim/framework/filesystem.hpp"
#include "opensim/framework/stream_reader_text.hpp"
#include "opensim/scene/region_runtime.hpp"
#include "opensim/scene/remote_data_channels.hpp"
#include "opensim/scene/live_asset_gather.hpp"
#include "opensim/scene/object_asset_xml.hpp"
#include "opensim/services/region_asset_runtime.hpp"
#include "opensim/services/simulator_features.hpp"
#include "opensim/framework/console_parser.hpp"
#include "opensim/framework/console_command_resolver.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/services/region_script_task_runtime.hpp"
#include "opensim/services/region_inventory_asset_runtime.hpp"
#include "opensim/services/region_baked_texture_runtime.hpp"
#include "opensim/services/region_inventory_runtime.hpp"
#include "opensim/services/region_inventory_descendents_runtime.hpp"
#include "opensim/services/region_library_descendents_runtime.hpp"
#include "opensim/services/library_service.hpp"
#include "opensim/server/poll_http_service.hpp"
#include "opensim/server/remote_console.hpp"
#include "opensim/framework/console_appender.hpp"
#include "opensim/framework/text.hpp"
#include <atomic>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <fstream>
#include <algorithm>
#include <ctime>
#include <charconv>
#include "opensim/framework/invariant_number_parse.hpp"
#include <mutex>
#include <random>
#include <set>
#include "opensim/services/sqlite_migration.hpp"
#include "region_migration_resource.hpp"
#include <csignal>
#include <iostream>
#include <thread>
#include <unordered_map>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <conio.h>
#else
#include <poll.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif
namespace {
class CreateInventoryCategory final : public opensim::server::CapabilitySimpleHandler {
  public:
    CreateInventoryCategory(opensim::framework::Uuid agent,
                            opensim::services::InventoryService& inventory,
                            std::weak_ptr<opensim::scene::RegionRuntime> region)
        : agent_(agent), inventory_(inventory), region_(std::move(region)) {}

    std::optional<std::string> path() const override { return path_; }

    opensim::network::HttpResponse handle(const opensim::network::HttpRequest& request) override {
        using namespace opensim;
        using network::Value;
        if (request.method != "POST")
            return {404};
        const auto region = region_.lock();
        if (!region || !region->viewers().agent(agent_))
            return {503, "text/plain", {}, {{"Retry-After", "60"}}};
        try {
            const auto body = network::deserialize_sdk_llsd_xml(request.body);
            const auto& id = body.get("folder_id");
            const auto& parent = body.get("parent_id");
            const auto& name = body.get("name");
            const auto& type = body.get("type");
            if (id.kind != Value::Kind::Uuid || parent.kind != Value::Kind::Uuid ||
                name.kind != Value::Kind::String || name.text.empty() || type.kind != Value::Kind::Integer)
                return {400};
            const auto folder_id = framework::Uuid(id.text);
            const auto parent_id = framework::Uuid(parent.text);
            const auto parent_folder = inventory_.folder(agent_, parent_id);
            if (folder_id.is_zero() || !parent_folder || parent_folder->owner != agent_)
                return {400};
            const auto folder_type = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(type.integer));
            const auto folder_name = framework::truncate_utf16(name.text, 63);
            if (!inventory_.add_folder({folder_name, folder_type, 1, folder_id, agent_, parent_id}))
                return {400};
            const auto saved = inventory_.folder(agent_, folder_id);
            if (!saved)
                return {400};
            auto result = Value::object({});
            result.set("folder_id", Value(saved->id.str()));
            result.set("name", Value(saved->name));
            result.set("parent_id", Value(saved->parent.str()));
            result.set("type", Value(saved->type));
            result.map.at("folder_id").kind = Value::Kind::Uuid;
            result.map.at("parent_id").kind = Value::Kind::Uuid;
            return {200, "application/llsd+xml", network::llsd_xml(result)};
        } catch (const std::exception&) {
            return {400};
        }
    }

  private:
    const std::string path_ = "/" + opensim::framework::Uuid::random().str();
    opensim::framework::Uuid agent_;
    opensim::services::InventoryService& inventory_;
    std::weak_ptr<opensim::scene::RegionRuntime> region_;
};

std::int32_t remote_data_integer(const opensim::network::Value& value) {
    using Kind = opensim::network::Value::Kind;
    switch (value.kind) {
    case Kind::Undefined:
    case Kind::NullReference:
        return 0;
    case Kind::Integer:
        return value.integer;
    case Kind::Boolean:
        return value.boolean ? 1 : 0;
    case Kind::String:
        return opensim::framework::invariant_convert_to_int32(
            opensim::framework::utf16_replacing(value.text));
    case Kind::Real: {
        // Convert.ToInt32 rounds midpoint values to the nearest even integer.
        const double lower = std::floor(value.real);
        const double fraction = value.real - lower;
        const bool round_up = fraction > 0.5 ||
                              (fraction == 0.5 && std::fmod(lower, 2.0) != 0.0);
        const double rounded = round_up ? lower + 1.0 : lower;
        if (rounded >= std::numeric_limits<std::int32_t>::min() &&
            rounded <= std::numeric_limits<std::int32_t>::max())
            return static_cast<std::int32_t>(rounded);
        break;
    }
    default:
        break;
    }
    throw std::invalid_argument("Invalid remote data integer");
}

std::optional<opensim::network::HttpResponse> remote_data_rpc(
    const opensim::network::HttpRequest& request, opensim::scene::RemoteDataChannels& channels) {
    using namespace opensim;
    using namespace opensim::network;
    if (request.method != "POST" || request.path != "/")
        return {};
    RpcCall call;
    try {
        call = parse_xml_rpc(request.body);
    } catch (const std::exception&) {
        return {};
    }
    if (call.method != "llRemoteData")
        return {};
    HttpResponse response;
    response.content_type = "text/xml";
    if (call.parameters.empty()) {
        response.body = xml_rpc_fault(-32602, "Invalid parameters");
        return response;
    }
    const auto& values = call.parameters.front();
    const auto& channel = values.get("Channel");
    const auto& number = values.get("IntValue");
    const auto& text = values.get("StringValue");
    if (!values.contains("Channel") || !values.contains("IntValue") ||
        !values.contains("StringValue")) {
        response.body = xml_rpc_fault(-32602, "Invalid parameters");
        return response;
    }
    framework::Uuid id;
    try {
        id = framework::parse_sdk_uuid(channel.text);
    } catch (const std::invalid_argument&) {
        response.body = xml_rpc_fault(-1, "Invalid channel");
        return response;
    }
    if (!channels.find(id)) {
        response.body = xml_rpc_fault(-1, "Invalid channel");
        return response;
    }
    std::int32_t integer;
    try {
        integer = remote_data_integer(number);
    } catch (const std::exception&) {
        response.body = xml_rpc_fault(-32602, "Invalid parameters");
        return response;
    }
    if (text.kind != Value::Kind::String && text.kind != Value::Kind::Undefined) {
        response.body = xml_rpc_fault(-32602, "Invalid parameters");
        return response;
    }
    const auto result = channels.request(id, {integer, text.text});
    if (!result.data) {
        response.body = xml_rpc_fault(-1, result.status == scene::RemoteDataChannels::Status::InvalidChannel
                                             ? "Invalid channel" : "Script timeout");
        return response;
    }
    Value reply;
    reply.kind = Value::Kind::Array;
    reply.array.push_back(Value::object({{"StringValue", Value(result.data->text)},
                                        {"IntValue", Value(result.data->integer)}}));
    response.body = xml_rpc_response(reply);
    return response;
}

opensim::network::HttpResponse sample_economy_rpc(const opensim::network::HttpRequest& request) {
    using namespace opensim::network;
    HttpResponse response;
    RpcCall call;
    try {
        // The HTTP listener already decodes Content-Encoding. The legacy header
        // is handled here for viewers that still send it.
        const auto legacy_encoding = request.headers.find("x-content-encoding");
        const auto encoding = request.headers.find("content-encoding");
        const bool legacy_gzip = legacy_encoding != request.headers.end() && legacy_encoding->second == "gzip";
        const bool standard_gzip = encoding != request.headers.end() && encoding->second == "gzip";
        const bool needs_inflate = standard_gzip ? request.body.starts_with("\x1f\x8b") : legacy_gzip;
        const auto body = needs_inflate
                              ? inflate_gzip(request.body) : request.body;
        call = parse_xml_rpc(body);
        if (opensim::framework::trim_utf8(call.method).empty())
            throw std::invalid_argument("Missing XML-RPC method");
    } catch (const std::exception&) {
        response.status = 404;
        return response;
    }
    response.content_type = "text/xml";
    Value result = Value::object({{"success", Value(true)}});
    if (call.method == "getCurrencyQuote") {
        std::int32_t amount = 0;
        if (!call.parameters.empty()) {
            const auto& currency_buy = call.parameters.front().get("currencyBuy");
            if (currency_buy.kind == Value::Kind::Integer)
                amount = currency_buy.integer;
        }
        result.set("currency", Value::object({{"estimatedCost", Value(0)}, {"currencyBuy", Value(amount)}}));
        result.set("confirm", Value("asdfad9fj39ma9fj"));
    } else if (call.method == "preflightBuyLandPrep") {
        result.set("currency", Value::object({{"estimatedCost", Value(0)}}));
        result.set("membership", Value::object({}));
        result.set("landuse", Value::object(
            {{"upgrade", Value(false)}, {"action", Value("http://invaliddomaininvalid.com/")}}));
        result.set("confirm", Value("asdfajsdkfjasdkfjalsdfjasdf"));
    } else if (call.method != "buyCurrency" && call.method != "buyLandPrep") {
        response.body = xml_rpc_fault(-32601, "Requested method [" + call.method + "] not found");
        return response;
    }
    response.headers["Access-Control-Allow-Origin"] = "*";
    response.body = xml_rpc_response(result);
    return response;
}

std::optional<opensim::scene::EnvironmentDay> saved_environment_day(std::string_view text,
                                                                   bool* migrated = nullptr) {
    using namespace opensim;
    if (text.empty())
        return std::nullopt;
    try {
        auto environment = network::deserialize_sdk_osd_text(framework::utf16(text));
        if (environment.kind == network::Value::Kind::Array && migrated) {
            auto day = scene::EnvironmentDay::from_windlight(environment);
            *migrated = true;
            return day;
        }
        if (environment.kind != network::Value::Kind::Map)
            return std::nullopt;
        scene::EnvironmentDay day;
        day.apply(std::move(environment));
        return day;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}
class ProfileWorkers {
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<std::function<void()>> pending_;
    std::array<std::jthread, 2> threads_;
    bool accepting_ = true;

  public:
    explicit ProfileWorkers(std::function<void(const std::exception&)> on_error, bool serial = false) {
        for (std::size_t i = 0; i < (serial ? 1 : threads_.size()); ++i)
            threads_[i] = std::jthread([this, on_error](std::stop_token stop) {
                for (;;) {
                    std::unique_lock lock(mutex_);
                    if (!wake_.wait(lock, stop, [this] { return !pending_.empty(); }))
                        return;
                    auto task = std::move(pending_.front());
                    pending_.pop_front();
                    lock.unlock();
                    try {
                        task();
                    } catch (const std::exception& error) {
                        on_error(error);
                    }
                }
            });
    }
    void enqueue(std::function<void()> task) {
        {
            std::lock_guard lock(mutex_);
            if (!accepting_)
                throw std::runtime_error("Profile workers are stopping");
            pending_.push_back(std::move(task));
        }
        wake_.notify_one();
    }
    void stop() {
        {
            std::lock_guard lock(mutex_);
            accepting_ = false;
        }
        for (auto& thread : threads_)
            thread.request_stop();
        for (auto& thread : threads_)
            if (thread.joinable())
                thread.join();
    }
    ~ProfileWorkers() {
        stop();
    }
};
struct ScriptQuiesceGuard {
    opensim::scene::RegionRuntime* runtime = nullptr;
    ~ScriptQuiesceGuard() {
        if (runtime)
            try {
                runtime->quiesce_script_updates();
            } catch (...) {
            }
    }
};
struct RemoteConsoleReader {
    opensim::server::RemoteConsole* console = nullptr;
    std::thread worker;
    std::atomic<bool> stopping = false;
    void stop() {
        stopping = true;
        if (console)
            console->stop();
        if (worker.joinable())
            worker.join();
    }
    ~RemoteConsoleReader() {
        stop();
    }
};
volatile std::sig_atomic_t shutdown_requested = 0;
void shutdown_signal(int) {
    shutdown_requested = 1;
}
bool console_quit(std::string& pending, const std::function<bool(const std::string&)>& execute) {
    char bytes[256];
    int count = 0;
#ifdef _WIN32
    auto input = GetStdHandle(STD_INPUT_HANDLE);
    const auto type = GetFileType(input);
    if (type == FILE_TYPE_PIPE) {
        DWORD available = 0, read = 0;
        if (PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr) && available > 0 &&
            ReadFile(input, bytes, std::min<DWORD>(available, sizeof(bytes)), &read, nullptr))
            count = static_cast<int>(read);
    } else if (type == FILE_TYPE_CHAR) {
        while (_kbhit() && count < 256) {
            const auto c = _getch();
            if (c == 0 || c == 224) {
                _getch();
                continue;
            }
            bytes[count++] = static_cast<char>(c == '\r' ? '\n' : c);
        }
    }
#else
    pollfd input{STDIN_FILENO, POLLIN, 0};
    if (poll(&input, 1, 0) > 0 && (input.revents & POLLIN)) {
        const auto read_count = read(STDIN_FILENO, bytes, sizeof(bytes));
        if (read_count > 0)
            count = static_cast<int>(read_count);
    }
#endif
    pending.append(bytes, static_cast<std::size_t>(count));
    for (auto newline = pending.find('\n'); newline != std::string::npos; newline = pending.find('\n')) {
        auto command = pending.substr(0, newline);
        pending.erase(0, newline + 1);
        if (!command.empty() && command.back() == '\r')
            command.pop_back();
        if (execute(command))
            return true;
    }
    if (pending.size() > 4096)
        pending.clear();
    return false;
}
int parse_number(const std::string& text) {
    int result;
    auto p = std::from_chars(text.data(), text.data() + text.size(), result);
    if (p.ec != std::errc{} || p.ptr != text.data() + text.size())
        throw std::invalid_argument("Invalid command-line integer");
    return result;
}
std::filesystem::path executable_directory(const std::string& argument) {
#ifdef _WIN32
    std::vector<wchar_t> path(32768);
    auto count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (count > 0 && count < path.size())
        return std::filesystem::path(std::wstring(path.data(), count)).parent_path();
#elif defined(__APPLE__)
    std::uint32_t count = 0;
    _NSGetExecutablePath(nullptr, &count);
    std::vector<char> path(count);
    if (count != 0 && _NSGetExecutablePath(path.data(), &count) == 0) {
        const auto executable = opensim::framework::utf8_path(path.data());
        std::error_code error;
        const auto resolved = std::filesystem::canonical(executable, error);
        return (error ? executable : resolved).parent_path();
    }
#elif defined(__linux__)
    std::error_code error;
    auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error)
        return path.parent_path();
#endif
    return std::filesystem::absolute(opensim::framework::utf8_path(argument)).parent_path();
}
std::optional<opensim::services::InventoryCollection>
library_folder_content(const std::shared_ptr<opensim::services::InventoryFolderTree>& root,
                       opensim::framework::Uuid id) {
    if (!root)
        return {};
    auto folder = root->find_folder(id);
    if (!folder)
        return {};
    opensim::services::InventoryCollection result;
    result.folder = folder->id;
    result.owner = folder->owner;
    result.version = folder->version;
    for (const auto& child : folder->children()) {
        if (!child)
            throw std::invalid_argument("Null library category");
        result.folders.push_back(*child);
    }
    for (const auto& item : folder->items()) {
        if (!item)
            throw std::invalid_argument("Null library item");
        result.items.push_back(*item);
    }
    return result;
}
std::function<void(const opensim::scene::EstateRegionSettings&)> estate_region_settings_writer(
    std::shared_ptr<opensim::services::SqliteTableOperations> tables, std::string id) {
    using namespace opensim;
    std::set<std::string> columns;
    tables->query("SELECT name FROM pragma_table_info('regionsettings')", {}, {"name"},
                  [&](const auto& row, const auto&) { columns.insert(row.value_text(0)); });
    return [tables = std::move(tables), columns = std::move(columns), id = std::move(id)](
               const scene::EstateRegionSettings& settings) {
        services::SqliteTableParameters parameters;
        std::string assignments;
        const auto add = [&](const std::string& column, std::string value) {
            if (!columns.contains(column))
                return;
            if (!assignments.empty())
                assignments += ',';
            const auto parameter = ":" + column;
            assignments += column + "=" + parameter;
            parameters.emplace_back(parameter, std::move(value));
        };
        const auto bit = [&](std::uint32_t mask) { return (settings.flags & mask) != 0; };
        add("block_terraform", bit(0x40u) ? "1" : "0");
        add("block_fly", bit(0x80000u) ? "1" : "0");
        add("allow_damage", bit(0x1u) ? "1" : "0");
        add("allow_land_resell", bit(0x80u) ? "0" : "1");
        add("restrict_pushing", bit(0x400000u) ? "1" : "0");
        add("allow_land_join_divide", bit(0x4000000u) ? "1" : "0");
        add("block_show_in_search", bit(0x20000000u) ? "1" : "0");
        add("disable_collisions", bit(0x1000u) ? "1" : "0");
        add("disable_scripts", bit(0x2000u) ? "1" : "0");
        add("disable_physics", bit(0x4000u) ? "1" : "0");
        add("agent_limit", std::to_string(settings.agent_limit));
        add("maturity", std::to_string(settings.maturity));
        std::array<char, 64> buffer;
        const auto encoded = std::to_chars(buffer.data(), buffer.data() + buffer.size(), settings.object_bonus);
        if (encoded.ec != std::errc{})
            throw std::runtime_error("Cannot serialize estate object bonus");
        add("object_bonus", std::string(buffer.data(), encoded.ptr));
        if (assignments.empty())
            return;
        parameters.emplace_back(":id", id);
        if (tables->execute("UPDATE regionsettings SET " + assignments + " WHERE regionUUID=:id", parameters) != 1)
            throw std::runtime_error("Missing or duplicate estate region settings");
    };
}

void load_estate_access(opensim::services::SqliteTableOperations& estate,
                        opensim::scene::EstateSettings& settings) {
    namespace framework = opensim::framework;
    const auto load_list = [&](const char* table, const char* column, auto& result) {
        bool exists = false;
        estate.query("SELECT name FROM sqlite_master WHERE type='table' AND name=:name",
                     {{":name", table}}, {"name"},
                     [&](const auto&, const auto&) { exists = true; });
        if (!exists || settings.estate_id == 0)
            return;
        estate.query(std::string("SELECT ") + column + " FROM " + table +
                         " WHERE EstateID=:estate",
                     {{":estate", std::to_string(settings.estate_id)}}, {column},
                     [&](const auto& row, const auto&) {
                         framework::Uuid agent;
                         if (framework::try_parse_sdk_uuid(row.value_text(0), agent) &&
                             !agent.is_zero())
                             result.push_back(agent);
                     });
    };
    load_list("estate_managers", "uuid", settings.estate_managers);
    load_list("estate_users", "uuid", settings.estate_users);
    load_list("estate_groups", "uuid", settings.estate_groups);
    load_list("estateban", "bannedUUID", settings.estate_bans);
}

bool load_estate_settings(opensim::services::SqliteTableOperations& estate,
                          const std::string& region_id,
                          opensim::scene::EstateSettings& settings) {
    namespace framework = opensim::framework;
    bool found = false;
    estate.query(
        "SELECT s.EstateOwner,s.BillableFactor,s.EstateID,s.ParentEstateID,s.PricePerMeter,"
        "s.RedirectGridX,s.RedirectGridY,s.* FROM estate_map m JOIN estate_settings s ON "
        "s.EstateID=m.EstateID WHERE m.RegionID=:region",
        {{":region", region_id}},
        {"EstateOwner", "BillableFactor", "EstateID", "ParentEstateID", "PricePerMeter",
         "RedirectGridX", "RedirectGridY"},
        [&](const auto& row, const auto&) {
            found = true;
            framework::Uuid owner;
            if (framework::try_parse_sdk_uuid(row.value_text(0), owner))
                settings.owner = owner;
            float factor;
            if (framework::parse_invariant_float(row.value_text(1), factor) &&
                std::isfinite(factor))
                settings.estate_billable_factor = factor;
            settings.estate_id = static_cast<std::uint32_t>(row.int32_value(2));
            settings.parent_estate_id = static_cast<std::uint32_t>(row.int32_value(3));
            settings.estate_price_per_meter = row.int32_value(4);
            settings.estate_redirect_x = row.int32_value(5);
            settings.estate_redirect_y = row.int32_value(6);
            try {
                settings.estate_name = row.value_text(row.column("EstateName"));
            } catch (const std::out_of_range&) {
            }
            try {
                settings.estate_abuse_email = row.value_text(row.column("AbuseEmail"));
            } catch (const std::out_of_range&) {
            }
            const auto read_flag = [&](std::uint32_t& flags, const char* column,
                                       std::uint32_t bit, bool invert = false) {
                try {
                    const bool enabled = row.int32_value(row.column(column)) != 0;
                    if (enabled != invert)
                        flags |= bit;
                    else
                        flags &= ~bit;
                } catch (const std::out_of_range&) {
                }
            };
            auto& menu = settings.estate_menu_flags;
            read_flag(menu, "AllowLandmark", 0x2);
            read_flag(menu, "AllowSetHome", 0x4);
            read_flag(menu, "ResetHomeOnTeleport", 0x8);
            read_flag(menu, "AllowEnviromentOverride", 0x200);
            read_flag(menu, "BlockDwell", 0x40000);
            read_flag(menu, "AllowVoice", 0x10000000);
            auto& handshake = settings.estate_handshake_only_flags;
            read_flag(handshake, "TaxFree", 0x20, true);
            read_flag(handshake, "PublicAccess", 0x8000);
            read_flag(handshake, "AllowDirectTeleport", 0x100000);
            read_flag(handshake, "EstateSkipScripts", 0x200000);
            read_flag(handshake, "DenyAnonymous", 0x800000);
            read_flag(handshake, "DenyMinors", 0x40000000);
            auto& detail = settings.estate_detail_only_flags;
            read_flag(detail, "PublicAccess", 0x20000);
            read_flag(detail, "DenyIdentified", 0x1000000);
            read_flag(detail, "DenyTransacted", 0x2000000);
            read_flag(detail, "AllowParcelChanges", 0x4000000);
            read_flag(detail, "AbuseEmailToEstateOwner", 0x8000000);
        });
    if (!found)
        return false;
    load_estate_access(estate, settings);
    return true;
}

constexpr int restart_region_result = 75;

opensim::server::IniConfig download_region_config(const std::string& url) {
    using namespace opensim;
    const auto uris = framework::native_config_uri_object_backend();
    const auto uri = uris->create(framework::nini_writer_text(framework::utf16_replacing(url)),
                                  framework::ConfigUriObjectKind::Absolute);
    if (*uri->scheme() != u"http" && *uri->scheme() != u"https")
        throw std::invalid_argument("Region configuration URL must use HTTP or HTTPS");
    network::WebHttpClients clients(network::native_web_http_handler_factory());
    clients.setup(false, false, nullptr, 32);
    framework::ConfigXmlHttpOperation request;
    request.method = "GET";
    auto stream = clients.get_new_global_client(30000)->send_headers(uri, std::move(request), true);
    const auto bytes = stream->copy_to_memory();
    stream->close();
    auto arena = std::make_shared<framework::NiniConfigArena>(framework::native_nini_invariant_config_backend());
    auto readers = framework::native_nini_xml_url_reader_backend(
        uris, [](auto) -> std::shared_ptr<framework::NativeNiniXmlByteInput> {
            throw std::invalid_argument("External XML entities are unsupported in web region configuration");
        });
    auto xml = framework::native_nini_xml_config_backend(framework::native_xml_document_io(std::move(readers)));
    return server::IniConfig::from_native_source(framework::NiniXmlConfigSource::create(
        arena, xml, framework::NativeNiniXmlReader::from_bytes(bytes)));
}

struct RegionConfigSection {
    opensim::server::IniConfig source;
    std::string name;
};

std::vector<RegionConfigSection> discover_region_sections(const opensim::server::IniConfig& config) {
    using namespace opensim;
    const auto configured_directory = framework::trim_utf8(
        config.get("Startup", "regionload_regionsdir"));
    const bool explicit_directory = !configured_directory.empty();
    const bool allow_regionless = config.boolean("Startup", "allow_regionless", false);
    const bool from_web = config.get("Startup", "region_info_source") == "web";
    std::vector<RegionConfigSection> regions;
    const auto append_sections = [&](const server::IniConfig& source, bool first_only) {
        const auto sections = source.native_source()->configs();
        const auto count = first_only ? std::min(sections->count(), 1) : sections->count();
        for (int index = 0; index < count; ++index)
            regions.push_back({source, framework::utf8(*sections->at(index)->name())});
    };
    if (from_web) {
        const auto url = framework::trim_utf8(config.get("Startup", "regionload_webserver_url"));
        if (url.empty())
            throw std::invalid_argument("Missing regionload_webserver_url");
        for (int attempt = 0; attempt < 3; ++attempt) {
            server::IniConfig source;
            try {
                source = download_region_config(url);
            } catch (const framework::ConfigXmlHttpError& error) {
                if (allow_regionless && error.status == 404)
                    return {};
                throw;
            }
            append_sections(source, false);
            if (!regions.empty())
                return regions;
            if (allow_regionless)
                return {};
            if (attempt < 2)
                std::this_thread::sleep_for(std::chrono::seconds(2));
        }
        throw std::invalid_argument("Web region configuration returned no regions");
    }

    auto directory = framework::utf8_path(explicit_directory ? configured_directory : "Regions");
    if (directory.is_relative())
        directory = config.directory / directory;
    if ((!explicit_directory || allow_regionless) && !std::filesystem::exists(directory))
        return {};

    const auto directory_text = framework::nini_writer_text(directory.u16string());
    const auto ini_files = framework::directory_get_files(directory_text, framework::nini_writer_text(u"*.ini"));
    const auto xml_files = framework::directory_get_files(directory_text, framework::nini_writer_text(u"*.xml"));
    for (const auto& file : ini_files)
        append_sections(server::IniConfig::load(std::filesystem::path(*file)), false);

    if (!xml_files.empty()) {
        auto arena = std::make_shared<framework::NiniConfigArena>(
            framework::native_nini_invariant_config_backend());
        const auto load_xml = server::native_startup_xml_source(std::move(arena), {});
        for (const auto& file : xml_files) {
            const auto path = std::filesystem::path(*file);
            const auto source = server::IniConfig::from_native_source(load_xml(file), path.parent_path());
            append_sections(source, true);
        }
    }
    if (regions.empty()) {
        if (allow_regionless || (!explicit_directory && ini_files.empty() && xml_files.empty()))
            return {};
        throw std::invalid_argument("No region configurations found in " + framework::utf8(directory.u16string()));
    }
    return regions;
}

opensim::server::IniConfig make_region_settings(const opensim::server::IniConfig& config,
                                                  const RegionConfigSection* definition) {
    using namespace opensim;
    auto arena = std::make_shared<framework::NiniConfigArena>(framework::native_nini_invariant_config_backend());
    auto settings = server::IniConfig::from_native_source(
        arena->create_source(framework::nini_writer_text(u"RegionSettings")), config.directory);
    for (const auto& [key, value] : config.ordered_section("NativeRegion"))
        settings.set("NativeRegion", key, value);
    if (!definition)
        return settings;

    const auto& [region_config, region_name] = *definition;
    const auto id_text = framework::trim_utf8(region_config.get(region_name, "RegionUUID"));
    framework::Uuid region_id;
    if (!framework::try_parse_sdk_uuid(id_text, region_id) || region_id.is_zero())
        throw std::invalid_argument("Region " + region_name + " requires a valid RegionUUID");
    settings.set("NativeRegion", "ID", region_id.str());

    const auto location = region_config.get(region_name, "Location");
    const auto separator = location.find(',');
    if (separator == std::string::npos)
        throw std::invalid_argument("Region Location must contain two grid coordinates");
    const auto suffix = location.find(',', separator + 1);
    const auto parse_coordinate = [](std::string_view coordinate) {
        const auto text = framework::trim_utf8(std::string(coordinate));
        const auto number = framework::invariant_convert_to_int32(framework::utf16_replacing(text));
        if (number < 0 || number > std::numeric_limits<int>::max() / 256)
            throw std::invalid_argument("Region Location coordinate is out of range");
        return std::to_string(number);
    };
    settings.set("NativeRegion", "LocationX", parse_coordinate(std::string_view(location).substr(0, separator)));
    const auto y_end = suffix == std::string::npos ? location.size() : suffix;
    settings.set("NativeRegion", "LocationY",
               parse_coordinate(std::string_view(location).substr(separator + 1, y_end - separator - 1)));
    settings.set("NativeRegion", "Name", region_name);

    const auto read_size = [&](const char* key) {
        const auto size = framework::invariant_convert_to_int64(
            framework::utf16_replacing(region_config.get(region_name, key, "256")));
        if (size < 0 || size > std::numeric_limits<std::uint32_t>::max())
            throw std::invalid_argument(std::string("Invalid region dimension ") + key);
        return std::max<std::int64_t>(1, size / 256);
    };
    const auto size = std::min({read_size("SizeX"), read_size("SizeY"), std::int64_t{16}}) * 256;
    settings.set("NativeRegion", "SizeX", std::to_string(size));
    settings.set("NativeRegion", "SizeY", std::to_string(size));

    for (const auto& [source, target] : {
             std::pair{"InternalAddress", "BindAddress"},
             std::pair{"InternalPort", "UDPPort"}, std::pair{"ExternalHostName", "ExternalHost"},
             std::pair{"ScopeID", "ScopeID"},
             std::pair{"DefaultLanding", "DefaultLanding"}, std::pair{"MaxAgents", "MaxAgents"},
             std::pair{"MaxPrims", "MaxPrims"}, std::pair{"ResolveAddress", "ResolveAddress"},
             std::pair{"NonPhysicalPrimMin", "NonPhysicalPrimMin"},
             std::pair{"NonPhysicalPrimMax", "NonPhysicalPrimMax"},
             std::pair{"PhysicalPrimMin", "PhysicalPrimMin"},
             std::pair{"PhysicalPrimMax", "PhysicalPrimMax"},
             std::pair{"LinksetPrims", "LinksetPrims"}, std::pair{"ClampPrimSize", "ClampPrimSize"},
             std::pair{"RegionType", "RegionType"}, std::pair{"MaxPrimsPerUser", "MaxPrimsPerUser"},
             std::pair{"MaptileStaticUUID", "MaptileStaticUUID"},
             std::pair{"MaptileStaticFile", "MaptileStaticFile"}}) {
        if (region_config.contains(region_name, source))
            settings.set("NativeRegion", target, region_config.get(region_name, source));
    }
    for (const auto& [key, value] : region_config.ordered_section(region_name)) {
        for (const auto* permission : {"auto_grant_attach_perms", "auto_grant_all_perms"}) {
            if (framework::ordinal_equals_ignore_case(key, permission))
                settings.set("NativeRegion", permission, value);
        }
    }
    return settings;
}

std::vector<opensim::server::IniConfig> load_local_region_settings(
    const opensim::server::IniConfig& config) {
    if (config.contains("NativeServer", "EnableLocalRegion")) {
        if (config.boolean("NativeServer", "EnableLocalRegion", false))
            return {make_region_settings(config, nullptr)};
        return {};
    }

    const auto definitions = discover_region_sections(config);
    std::vector<opensim::server::IniConfig> settings;
    settings.reserve(definitions.size());
    for (const auto& definition : definitions)
        settings.push_back(make_region_settings(config, &definition));
    return settings;
}

opensim::services::GridRegion describe_local_region(const opensim::server::IniConfig& region_settings,
                                                   int previous_udp_port) {
    using namespace opensim;
    services::GridRegion region;
    region.id =
        framework::Uuid(region_settings.get("NativeRegion", "ID", "11111111-1111-4111-8111-111111111111"));
    region.scope = framework::Uuid(
        region_settings.get("NativeRegion", "ScopeID", "00000000-0000-0000-0000-000000000000"));
    region.name = region_settings.get("NativeRegion", "Name", "Native Region");
    region.external_host = region_settings.get("NativeRegion", "ExternalHost", "127.0.0.1");
    if (*region.external_host == "SYSTEMIP")
        region.external_host = framework::HostResolver::local_host_address()->str();
    else if (region_settings.boolean("NativeRegion", "ResolveAddress", false)) {
        const auto addresses = framework::HostResolver::system_lookup(*region.external_host);
        if (addresses && addresses->size() == 1)
            region.external_host = addresses->front()->str();
    }
    region.estate_owner = framework::Uuid(
        region_settings.get("NativeRegion", "OwnerID", "00000000-0000-0000-0000-000000000000"));
    const auto world_coordinate = [&](const char* key) {
        const auto coordinate = region_settings.integer("NativeRegion", key, 1000);
        if (coordinate < 0 || coordinate > std::numeric_limits<std::int32_t>::max() / 256)
            throw std::invalid_argument(std::string("NativeRegion ") + key + " is out of range");
        return coordinate * 256;
    };
    region.x = world_coordinate("LocationX");
    region.y = world_coordinate("LocationY");
    const auto size_x = region_settings.integer("NativeRegion", "SizeX", 256);
    const auto size_y = region_settings.integer("NativeRegion", "SizeY", 256);
    const auto size = std::clamp(std::min(size_x, size_y) / 256, 1, 16) * 256;
    region.size_x = size;
    region.size_y = size;
    region.access = 13;
    auto udp_port = region_settings.integer("NativeRegion", "UDPPort", 9000);
    if (udp_port < 0 || udp_port > 65535)
        throw std::invalid_argument("NativeRegion UDPPort must be between 0 and 65535");
    if (udp_port == 0)
        udp_port = previous_udp_port;
    region.internal_endpoint =
        services::RegionEndpoint{region_settings.get("NativeRegion", "BindAddress", "0.0.0.0"),
                                 static_cast<std::uint16_t>(udp_port)};
    return region;
}

constexpr std::uint8_t grid_moderation_dialog = 250;

std::optional<opensim::scene::RegionInstantMessage> parse_grid_message(
    const opensim::network::Value& message, std::string_view key) {
    using namespace opensim;

    for (const auto* field :
         {"from_agent_id", "to_agent_id", "im_session_id", "timestamp", "from_agent_name",
          "message", "dialog", "from_group", "offline", "parent_estate_id", "position_x",
          "position_y", "position_z", "region_id", "binary_bucket"}) {
        const auto kind = message.get(field).kind;
        if (kind != network::Value::Kind::String &&
            (kind != network::Value::Kind::Undefined || !message.contains(field)))
            return std::nullopt;
    }
    const auto& message_key = message.get("message_key");
    if (!key.empty() && (message_key.kind != network::Value::Kind::String || message_key.text != key))
        return std::nullopt;
    scene::RegionInstantMessage incoming;
    const auto text = [&](const char* field) -> const std::string& {
        return message.get(field).text;
    };
    if (!framework::try_parse_sdk_uuid(text("to_agent_id"), incoming.recipient))
        return std::nullopt;
    incoming.message = framework::utf16_replacing(text("message"));
    const auto binary = [&](const char* field) {
        network::Value value;
        value.kind = network::Value::Kind::Binary;
        value.text = text(field);
        return network::osd_binary(value);
    };
    try {
        const auto dialog = binary("dialog");
        const auto offline = binary("offline");
        if ((dialog->empty() && !text("dialog").empty()) ||
            (offline->empty() && !text("offline").empty()))
            return std::nullopt;
        incoming.dialog = dialog->empty() ? 0 : dialog->front();
        incoming.offline = offline->empty() ? 0 : offline->front();
        incoming.bucket = *binary("binary_bucket");
    } catch (const std::exception&) {
        return std::nullopt;
    }
    const bool valid_sender = framework::try_parse_sdk_uuid(text("from_agent_id"), incoming.sender);
    if (incoming.dialog == grid_moderation_dialog) {
        if (!valid_sender)
            return std::nullopt;
        return incoming;
    }
    framework::try_parse_sdk_uuid(text("im_session_id"), incoming.session);
    framework::try_parse_sdk_uuid(text("region_id"), incoming.region);
    // The managed receiver converts to Int32 before casting to UInt32.
    const auto number = [&](const char* field) -> std::uint32_t {
        std::int32_t value;
        if (!services::parse_account_integer(text(field), value))
            return 0;
        return static_cast<std::uint32_t>(value);
    };
    incoming.timestamp = number("timestamp");
    incoming.estate = number("parent_estate_id");
    incoming.position.x = static_cast<float>(number("position_x"));
    incoming.position.y = static_cast<float>(number("position_y"));
    incoming.position.z = static_cast<float>(number("position_z"));
    incoming.sender_name = framework::utf16_replacing(text("from_agent_name"));
    incoming.from_group = text("from_group") == "TRUE";
    return incoming;
}

class LocalRegionRouter final : public opensim::services::ISimulationLogin,
                                public opensim::services::ISimulationAgentUpdates,
                                public opensim::services::ISimulationObjects {
public:
    void add(const std::shared_ptr<opensim::scene::RegionRuntime>& region) {
        std::lock_guard lock(mutex_);
        if (!regions_.emplace(region->descriptor().id, region).second)
            throw std::invalid_argument("Duplicate local region ID");
    }
    void remove(const opensim::framework::Uuid& id) {
        std::lock_guard lock(mutex_);
        regions_.erase(id);
    }
    std::shared_ptr<opensim::scene::RegionRuntime> find(const opensim::framework::Uuid& id) const {
        std::lock_guard lock(mutex_);
        const auto region = regions_.find(id);
        return region == regions_.end() ? nullptr : region->second.lock();
    }

    void for_each_region(const std::function<void(opensim::scene::RegionRuntime&)>& action) const {
        for (const auto& region : snapshot())
            action(*region);
    }

    bool deliver_to_viewer(const std::function<bool(opensim::scene::RegionRuntime&)>& deliver) const {
        for (const auto& region : snapshot()) {
            if (deliver(*region))
                return true;
        }
        return false;
    }

    bool deliver_grid_message(const opensim::scene::RegionInstantMessage& message) const {
        if (message.dialog == grid_moderation_dialog) {
            // A broadcast moderation request must reach every local region.
            bool handled = false;
            for (const auto& region : snapshot()) {
                if (region->receive_grid_moderation(message))
                    handled = true;
            }
            return handled;
        }

        return deliver_to_viewer([&](auto& region) {
            return region.deliver_instant_message(message);
        });
    }

    opensim::network::Value land_data(std::uint64_t handle, std::uint32_t x, std::uint32_t y) const {
        // The handle and offsets together identify the parcel's world position.
        for (const auto& region : snapshot()) {
            auto data = region->land_data(handle, x, y);
            if (!data.map.empty())
                return data;
        }
        return opensim::network::Value::object({});
    }

    std::optional<opensim::network::HttpResponse> map_request(
        const opensim::network::HttpRequest& request) const {
        if (request.path != "/index.php" && !request.path.starts_with("/MAP/MapItems/"))
            return {};
        for (const auto& region : snapshot()) {
            auto response = request.path == "/index.php" ? region->region_map_image(request)
                                                         : region->remote_map_items(request);
            if (response)
                return response;
        }
        return {};
    }

    bool query_access(opensim::services::GridRegion& destination, const opensim::framework::Uuid& agent,
                      const std::optional<std::string>& home, bool teleport,
                      const opensim::framework::Vector3& position, std::vector<opensim::framework::Uuid>& features,
                      opensim::services::LoginTransferContext& context, std::string& reason) override {
        if (auto region = find(destination.id))
            return region->query_access(destination, agent, home, teleport, position, features, context, reason);
        reason = "Region unavailable";
        return false;
    }

    bool create_agent(const opensim::services::GridRegion* source, opensim::services::GridRegion& destination,
                      const std::shared_ptr<opensim::framework::AgentCircuitData>& circuit, std::uint32_t flags,
                      opensim::services::LoginTransferContext& context, std::string& reason) override {
        if (auto region = find(destination.id))
            return region->create_agent(source, destination, circuit, flags, context, reason);
        reason = "Region unavailable";
        return false;
    }

    std::shared_ptr<opensim::framework::AgentTransferScene> get_scene(
        const opensim::framework::Uuid& id) override {
        if (auto region = find(id))
            return region->get_scene(id);
        return {};
    }

    bool update_agent(opensim::services::GridRegion& destination,
                      const std::shared_ptr<opensim::framework::AgentData>& data,
                      opensim::framework::EntityTransferContext& context) override {
        auto region = find(destination.id);
        return region && region->update_agent(destination, data, context);
    }

    bool update_agent(opensim::services::GridRegion& destination,
                      const std::shared_ptr<opensim::framework::AgentPosition>& data) override {
        auto region = find(destination.id);
        return region && region->update_agent(destination, data);
    }

    bool create_object(opensim::services::GridRegion* destination,
                       const opensim::framework::Vector3& position,
                       std::shared_ptr<opensim::services::SimulationObject> object, bool local_call) override {
        auto region = destination ? find(destination->id) : nullptr;
        return region && region->create_object(destination, position, std::move(object), local_call);
    }

private:
    std::vector<std::shared_ptr<opensim::scene::RegionRuntime>> snapshot() const {
        std::lock_guard lock(mutex_);
        std::vector<std::shared_ptr<opensim::scene::RegionRuntime>> result;
        result.reserve(regions_.size());
        for (const auto& [id, entry] : regions_) {
            if (auto region = entry.lock())
                result.push_back(std::move(region));
        }
        return result;
    }

    mutable std::mutex mutex_;
    std::map<opensim::framework::Uuid, std::weak_ptr<opensim::scene::RegionRuntime>> regions_;
};

struct RegionListenerPorts {
    int main_http = 0;
    int script_http = 0;
    int script_https = 0;
    std::uint16_t udp = 0;
};

std::string find_service_connector(const opensim::server::IniConfig& config, const char* name,
                                   std::string_view suffix, std::string_view section = {}) {
    auto entry = config.get("ServiceList", name);
    if (!entry.empty())
        return entry;
    const auto matches = [&](std::string_view candidate) {
        if (!section.empty()) {
            const auto qualifier = candidate.substr(0, candidate.find('/'));
            const auto separator = qualifier.find('@');
            const auto candidate_section =
                separator == std::string_view::npos ? "AssetService" : qualifier.substr(0, separator);
            if (candidate_section != section)
                return false;
        }
        return candidate == suffix.substr(1) || candidate.ends_with(suffix);
    };
    for (const auto& [key, candidate] : config.ordered_section("ServiceList"))
        if (matches(candidate))
            return candidate;

    const auto connectors = config.get("Startup", "ServiceConnectors");
    constexpr std::string_view separators = ", \r\n\t";
    for (auto start = connectors.find_first_not_of(separators); start != std::string::npos;) {
        const auto end = connectors.find_first_of(separators, start);
        const auto candidate = std::string_view(connectors).substr(start, end - start);
        if (matches(candidate))
            return std::string(candidate);
        start = connectors.find_first_not_of(separators, end);
    }
    return std::string{};
}

int run(const std::vector<std::string>& args, RegionListenerPorts& ports) {
    using namespace opensim;
    std::string config_path = "native-server.ini", host, first, last;
    server::ConfigurationStartupOptions startup_options;
    std::optional<int> port;
    int level = 0;
    bool initialize = false, create = false, console = false;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const auto& option = args[i];
        auto next = [&]() -> const std::string& {
            if (i + 1 >= args.size())
                throw std::invalid_argument("Missing value for " + option);
            return args[++i];
        };
        if (option == "--features") {
            std::cout << "{\"tls\":" << (network::HttpServer::tls_available() ? "true" : "false")
                      << ",\"gzip\":" << (network::HttpServer::gzip_available() ? "true" : "false") << "}\n";
            return 0;
        }
        if (option == "--help" || option == "-h") {
            std::cout
                << "OpenSim native service server with experimental local region support\n"
                   "  --config FILE       INI configuration (default native-server.ini)\n"
                   "  --inimaster FILE    Startup master (default none; URI XML requires a host provider)\n"
                   "  --inidirectory DIR Startup override directory (default config; original path rules)\n"
                   "  --host ADDRESS      Override listener address\n"
                   "  --port NUMBER       Override HTTP port; 0 selects an unused port\n"
                   "  --init-db           Initialize new SQLite databases for enabled services\n"
                   "  --create-user FIRST LAST  Read password from stdin, create user and exit\n"
                   "  --user-level NUMBER Initial level for --create-user (default 0)\n"
                   "  --console           Accept quit/shutdown, login and cache commands on stdin\n"
                   "  --features          Show compiled TLS and gzip support\n"
                   "SIGINT/SIGTERM stop the listener and join its workers.\n";
            return 0;
        }
        if (option == "--config")
            config_path = next();
        else if (option == "--inimaster" || option == "--ini-master")
            startup_options.inimaster = framework::nini_writer_text(framework::utf16_replacing(next()));
        else if (option == "--inidirectory" || option == "--ini-directory")
            startup_options.inidirectory = framework::nini_writer_text(framework::utf16_replacing(next()));
        else if (option == "--host")
            host = next();
        else if (option == "--port")
            port = parse_number(next());
        else if (option == "--init-db")
            initialize = true;
        else if (option == "--create-user") {
            first = next();
            last = next();
            create = true;
        } else if (option == "--user-level")
            level = parse_number(next());
        else if (option == "--console")
            console = true;
        else
            throw std::invalid_argument("Unknown option: " + option);
    }
    if (initialize && create)
        throw std::invalid_argument("Initialize the databases before creating users");
    if (port && (*port < 0 || *port > 65535))
        throw std::invalid_argument("HTTP port must be between 0 and 65535");
    auto argv_arena =
        std::make_shared<framework::NiniConfigArena>(framework::native_nini_invariant_config_backend());
    auto argv_source = argv_arena->create_source(framework::nini_writer_text(u"NativeCallerOverrides"));
    if (port)
        argv_source->add_config(framework::nini_writer_text(u"Network"))
            ->set_text(framework::nini_writer_text(u"http_listener_port"),
                       framework::nini_writer_text(framework::utf16_replacing(std::to_string(*port))));
    if (!host.empty())
        argv_source->add_config(framework::nini_writer_text(u"NativeServer"))
            ->set_text(framework::nini_writer_text(u"ListenAddress"),
                       framework::nini_writer_text(framework::utf16_replacing(host)));
    startup_options.argv_overrides = std::move(argv_source);
    auto config =
        server::IniConfig::load_startup(framework::utf8_path(config_path), std::move(startup_options));
    const auto relative = framework::utf8_path(config.get("AssetService", "AssetLoaderArgs"));
    if (relative.lexically_normal() == std::filesystem::path("assets") / "AssetSets.xml") {
        auto control_file = config.directory / relative;
        if (!std::filesystem::is_regular_file(control_file) && !std::filesystem::is_regular_file(relative)) {
            auto root = executable_directory(args[0]);
            for (int depth = 0; depth < 4; ++depth, root = root.parent_path()) {
                for (const auto& base : {root / "share/opensim-native", root / "bin"}) {
                    const auto candidate = base / relative;
                    if (std::filesystem::is_regular_file(candidate)) {
                        control_file = candidate;
                        break;
                    }
                }
                if (std::filesystem::is_regular_file(control_file))
                    break;
            }
        }
        if (std::filesystem::is_regular_file(control_file))
            config.set("AssetService", "AssetLoaderArgs", framework::utf8(control_file.u16string()));
    }
    const bool remote_console_mode = config.get("Startup", "console") == "rest";
    const auto authentication_provider = config.get("NativeServer", "AuthenticationProvider", "SQLite");
    if (authentication_provider != "SQLite" && authentication_provider != "Remote")
        throw std::invalid_argument("AuthenticationProvider must be SQLite or Remote");
    const bool local_authentication = authentication_provider == "SQLite";
    const auto authentication_database =
        local_authentication
            ? server::database_path(config, "AuthenticationService", "AuthenticationDatabase")
            : std::string{};
    const auto local_region_settings = load_local_region_settings(config);
    std::vector<services::GridRegion> local_region_descriptors;
    local_region_descriptors.reserve(local_region_settings.size());
    for (const auto& settings : local_region_settings) {
        auto region = describe_local_region(settings, local_region_descriptors.empty() ? ports.udp : 0);
        if (region.id.is_zero())
            throw std::invalid_argument("Invalid local region UUID for " + region.name.value_or(""));
        for (const auto& existing : local_region_descriptors) {
            if (existing.id == region.id)
                throw std::invalid_argument("Duplicate local region ID " + region.id.str());
            const bool overlaps_x = std::int64_t(existing.x) + existing.size_x > region.x &&
                                    std::int64_t(region.x) + region.size_x > existing.x;
            const bool overlaps_y = std::int64_t(existing.y) + existing.size_y > region.y &&
                                    std::int64_t(region.y) + region.size_y > existing.y;
            if (overlaps_x && overlaps_y)
                throw std::invalid_argument("Overlapping local regions: " + existing.name.value_or("") +
                                            " and " + region.name.value_or(""));
            const auto port = region.internal_endpoint->port;
            if (port == existing.internal_endpoint->port)
                throw std::invalid_argument("Duplicate local region UDP port " + std::to_string(port));
        }
        local_region_descriptors.push_back(std::move(region));
    }
    if (local_region_settings.size() > 1)
        throw std::invalid_argument("This native host currently accepts one region per process");
    const bool local_region = !local_region_settings.empty();
    const auto& region_settings = local_region ? local_region_settings.front() : config;
    const auto remote_data_port = local_region ? config.integer("XMLRPC", "XmlRpcPort", 0) : 0;
    const auto money_module = config.get("Startup", "economymodule");
    const auto selected_money_module = money_module.empty() ? config.get("Economy", "economymodule") : money_module;
    const bool sample_economy_enabled =
        local_region && (selected_money_module.empty() || selected_money_module == "BetaGridLikeMoneyModule");
    std::shared_ptr<network::WebHttpClients> shared_http;
    auto outgoing_clients = [&] {
        if (!shared_http) {
            shared_http =
                std::make_shared<network::WebHttpClients>(network::native_web_http_handler_factory());
            const char* section = local_region ? "Startup" : "Server";
            shared_http->setup(config.boolean(section, "NoVerifyCertChain", local_region),
                               config.boolean(section, "NoVerifyCertHostname", local_region), nullptr, 32);
        }
        return shared_http;
    };
    std::shared_ptr<scene::RemoteDataChannels> remote_data;
    if (remote_data_port > 0) {
        remote_data = std::make_shared<scene::RemoteDataChannels>(
            [transport = network::NwcXmlRpcHttpTransport(outgoing_clients())](
                std::string_view channel, std::string_view destination, scene::RemoteDataChannels::Data& data,
                std::stop_token stop) mutable {
                auto request = std::make_shared<network::NwcXmlRpcRequest>();
                auto fields = network::Value::object({
                    {"StringValue", network::Value(data.text)},
                    {"IntValue", network::Value(std::to_string(data.integer))},
                });
                framework::Uuid id;
                request->method_name = u"llRemoteData";
                if (!channel.empty()) {
                    if (framework::try_parse_sdk_uuid(channel, id))
                        fields.set("Channel", network::Value(std::string(channel)));
                    else
                        request->method_name = framework::utf16_replacing(channel);
                }
                request->parameters->push_back(std::make_shared<network::Value>(std::move(fields)));
                const auto response = transport.send(request, framework::utf16_replacing(destination), 30000,
                                                     [stop] { return stop.stop_requested(); });
                if (!response || !response->value)
                    return;
                const auto& value = response->value->view();
                const auto& fields_reply = value.kind == network::Value::Kind::Array && !value.array.empty()
                    ? value.array.front().view() : value;
                const auto read_text = [&](std::string_view name) {
                    if (!fields_reply.contains(name))
                        return;
                    const auto& field = fields_reply.get(name).view();
                    if (field.kind != network::Value::Kind::String &&
                        field.kind != network::Value::Kind::Undefined)
                        throw std::invalid_argument("Invalid remote data string");
                    data.text = field.text;
                };
                const auto read_integer = [&](std::string_view name) {
                    if (fields_reply.contains(name))
                        data.integer = remote_data_integer(fields_reply.get(name).view());
                };
                read_text("StringValue");
                read_integer("IntValue");
                read_text("faultString");
                read_integer("faultCode");
            });
    }
    std::unique_ptr<services::AuthenticationConnector> remote_authentication;
    if (!local_authentication)
        remote_authentication = std::make_unique<services::AuthenticationConnector>(
            config, services::AuthenticationConnector::Request{}, outgoing_clients());
    const bool remote_simulation = config.boolean("NativeServer", "EnableRemoteSimulationConnector", false);
    const auto region_physics = region_settings.get("NativeRegion", "Physics", "None");
    if (local_region && region_physics != "None" && region_physics != "ubODE")
        throw std::invalid_argument("NativeRegion Physics must be None or ubODE");
    const bool physical_region = local_region && region_physics == "ubODE";
    const auto region_poll_interval = std::chrono::milliseconds(physical_region ? 5 : 50);
    if (local_region && remote_simulation)
        throw std::invalid_argument("Select either the local region or remote simulation connector");
    const bool connected_login = local_region || remote_simulation ||
                                 config.boolean("NativeServer", "EnableLoginOrchestration", false);
    const bool need_friends = config.boolean("NativeServer", "EnableFriendsService", false) ||
                              config.boolean("NativeServer", "EnableFriendsEndpoint", false);
    const auto friends_provider = config.get("NativeServer", "FriendsProvider", "SQLite");
    if (need_friends && friends_provider != "SQLite" && friends_provider != "Remote")
        throw std::invalid_argument("FriendsProvider must be SQLite or Remote");
    const bool local_friends = need_friends && friends_provider == "SQLite";
    const bool need_avatar = config.boolean("NativeServer", "EnableAvatarService", false) ||
                             config.boolean("NativeServer", "EnableAvatarEndpoint", false);
    const auto grid_connector = find_service_connector(
        config, "GridServiceConnector", "/OpenSim.Server.Handlers.dll:GridServiceConnector");
    const auto asset_connector = find_service_connector(
        config, "AssetServiceConnector", "/OpenSim.Server.Handlers.dll:AssetServiceConnector", "AssetService");
    const auto hg_asset_connector = find_service_connector(
        config, "HGAssetServiceConnector", "/OpenSim.Server.Handlers.dll:AssetServiceConnector", "HGAssetService");
    const auto grid_user_connector = find_service_connector(
        config, "GridUserServiceConnector", "/OpenSim.Server.Handlers.dll:GridUserServiceConnector");
    const auto presence_connector = find_service_connector(
        config, "PresenceServiceConnector", "/OpenSim.Server.Handlers.dll:PresenceServiceConnector");
    const bool expose_grid = config.boolean("NativeServer", "EnableGridEndpoint", !grid_connector.empty());
    const bool expose_assets =
        config.boolean("NativeServer", "EnableAssetEndpoint", !asset_connector.empty());
    const bool expose_hg_assets =
        config.boolean("NativeServer", "EnableHGAssetEndpoint", !hg_asset_connector.empty());
    const bool expose_grid_user =
        config.boolean("NativeServer", "EnableGridUserEndpoint", !grid_user_connector.empty());
    const bool expose_presence =
        config.boolean("NativeServer", "EnablePresenceEndpoint", !presence_connector.empty());
    const auto validate_service_module = [&](const char* section, const char* native_module,
                                             const std::string& connector) {
        const auto module = config.get(section, "LocalServiceModule", connector.empty() ? native_module : "");
        if (module.empty())
            throw std::invalid_argument(std::string(section) + ".LocalServiceModule is required");
        if (module != native_module)
            throw std::invalid_argument(std::string("Unsupported native ") + section + " module: " + module);
    };
    if (expose_grid)
        validate_service_module("GridService", "OpenSim.Services.GridService.dll:GridService", grid_connector);
    const bool expose_fs_assets = expose_assets &&
        config.get("AssetService", "LocalServiceModule") == services::fs_asset_module;
    if (expose_assets && !expose_fs_assets)
        validate_service_module("AssetService", "OpenSim.Services.AssetService.dll:AssetService", asset_connector);
    const bool expose_hg_fs_assets = expose_hg_assets &&
        config.get("HGAssetService", "LocalServiceModule") ==
            "OpenSim.Services.HypergridService.dll:HGFSAssetService";
    if (expose_hg_assets && !expose_hg_fs_assets)
        validate_service_module("HGAssetService", "OpenSim.Services.HypergridService.dll:HGAssetService",
                                hg_asset_connector);
    if (expose_grid_user)
        validate_service_module("GridUserService", "OpenSim.Services.UserAccountService.dll:GridUserService",
                                grid_user_connector);
    if (expose_presence)
        validate_service_module("PresenceService", "OpenSim.Services.PresenceService.dll:PresenceService",
                                presence_connector);
    const bool grid_required = connected_login || expose_grid;
    const bool grid_user_required = connected_login || expose_grid_user;
    const bool configured_grid_statistics =
        !config.boolean("GridInfoService", "DisableStatsEndpoint", false) &&
        config.get("GridService", "LocalServiceModule") == "OpenSim.Services.GridService.dll:GridService" &&
        (!config.get("NativeServer", "GridUserDatabase").empty() ||
         !config.get("GridUserService", "ConnectionString").empty() ||
         !config.get("DatabaseService", "ConnectionString").empty());
    const bool need_grid = grid_required || configured_grid_statistics;
    const bool need_grid_user = grid_user_required || configured_grid_statistics;
    const bool use_remote_grid_user =
        local_region && config.get("Modules", "GridUserServices") == "RemoteGridUserServicesConnector";
    const bool inherit_grid_user_database = !use_remote_grid_user || expose_grid_user;
    const auto native_grid_user_path = config.get("NativeServer", "GridUserDatabase");
    const auto grid_user_provider = config.get("GridUserService", "StorageProvider",
        native_grid_user_path.empty() && inherit_grid_user_database
            ? config.get("DatabaseService", "StorageProvider") : "OpenSim.Data.SQLite.dll");
    const bool sqlite_grid_users =
        need_grid_user && (grid_user_provider.empty() || grid_user_provider == "OpenSim.Data.SQLite.dll");
    const bool inherit_grid_user_path = inherit_grid_user_database && native_grid_user_path.empty() &&
        config.get("GridUserService", "ConnectionString").empty() &&
        !config.get("DatabaseService", "ConnectionString").empty();
    const auto grid_user_path = sqlite_grid_users
        ? server::database_path(config, inherit_grid_user_path ? "DatabaseService" : "GridUserService",
              "GridUserDatabase")
        : std::string{};
    if (grid_user_required && !sqlite_grid_users
#ifdef OPENSIM_HAVE_MYSQL
        && grid_user_provider != "OpenSim.Data.MySQL.dll"
#endif
#ifdef OPENSIM_HAVE_POSTGRESQL
        && grid_user_provider != "OpenSim.Data.PGSQL.dll"
#endif
    )
        throw std::invalid_argument("Unsupported native grid-user storage provider: " + grid_user_provider);
    const auto open_grid_user_store = [&]() -> std::unique_ptr<services::GridUserStore> {
        const auto realm = config.get("GridUserService", "Realm", "GridUser");
        if (sqlite_grid_users)
            return std::make_unique<services::SqliteGridUserPlugin>(
                services::sqlite_data_source_connection(grid_user_path), realm);
#if defined(OPENSIM_HAVE_MYSQL) || defined(OPENSIM_HAVE_POSTGRESQL)
        const auto connection = config.get("GridUserService", "ConnectionString",
                                           config.get("DatabaseService", "ConnectionString"));
#endif
#ifdef OPENSIM_HAVE_MYSQL
        if (grid_user_provider == "OpenSim.Data.MySQL.dll")
            return services::mysql_grid_user_store(connection, realm);
#endif
#ifdef OPENSIM_HAVE_POSTGRESQL
        if (grid_user_provider == "OpenSim.Data.PGSQL.dll")
            return services::postgres_grid_user_store(connection, realm);
#endif
        throw std::logic_error("Grid-user store was requested without a supported provider");
    };
    const auto region_provider = config.get("NativeServer", "GridRegionProvider", "Null");
    const bool durable_regions = need_grid && region_provider == "SQLite";
    if (grid_required && region_provider != "Null" && region_provider != "SQLite")
        throw std::invalid_argument("GridRegionProvider must be Null or SQLite");
    if (initialize && connected_login) {
        const auto path = server::database_path(config, "InventoryService", "InventoryDatabase");
        if (path != ":memory:" && std::filesystem::exists(framework::utf8_path(path)) &&
            std::filesystem::file_size(framework::utf8_path(path)) > 0)
            throw std::runtime_error("Inventory initialization requires an empty destination");
        if (path == server::database_path(config, "UserAccountService", "UserAccountDatabase") ||
            path == authentication_database ||
            (sqlite_grid_users && path == grid_user_path) ||
            (durable_regions && path == server::database_path(config, "GridService", "GridRegionDatabase")))
            throw std::runtime_error("Inventory initialization requires a separate database file");
    }
    if (initialize && durable_regions) {
        const auto path = server::database_path(config, "GridService", "GridRegionDatabase");
        if (path != ":memory:" && std::filesystem::exists(framework::utf8_path(path)) &&
            std::filesystem::file_size(framework::utf8_path(path)) > 0)
            throw std::runtime_error("Region initialization requires an empty destination");
        if (path == server::database_path(config, "UserAccountService", "UserAccountDatabase") ||
            path == authentication_database ||
            (sqlite_grid_users && path == grid_user_path))
            throw std::runtime_error("Region initialization requires a separate database file");
    }
    if (initialize && sqlite_grid_users) {
        const auto& path = grid_user_path;
        if (path != ":memory:" && std::filesystem::exists(framework::utf8_path(path)) &&
            std::filesystem::file_size(framework::utf8_path(path)) > 0)
            throw std::runtime_error("Grid-user initialization requires an empty destination");
        if (path == server::database_path(config, "UserAccountService", "UserAccountDatabase") ||
            path == authentication_database)
            throw std::runtime_error("Grid-user initialization requires a separate database file");
    }
    if (initialize && need_avatar) {
        const auto path = server::database_path(config, "AvatarService", "AvatarDatabase");
        if (path != ":memory:" && std::filesystem::exists(framework::utf8_path(path)) &&
            std::filesystem::file_size(framework::utf8_path(path)) > 0)
            throw std::runtime_error("Avatar initialization requires an empty destination");
        if (path == server::database_path(config, "UserAccountService", "UserAccountDatabase") ||
            path == authentication_database ||
            (sqlite_grid_users && path == grid_user_path) ||
            (connected_login &&
             path == server::database_path(config, "InventoryService", "InventoryDatabase")) ||
            (durable_regions && path == server::database_path(config, "GridService", "GridRegionDatabase")))
            throw std::runtime_error("Avatar initialization requires a separate database file");
    }
    if (initialize && local_friends) {
        const auto path = server::database_path(config, "FriendsService", "FriendsDatabase");
        if (path != ":memory:" && std::filesystem::exists(framework::utf8_path(path)) &&
            std::filesystem::file_size(framework::utf8_path(path)) > 0)
            throw std::runtime_error("Friends initialization requires an empty destination");
        if (path == server::database_path(config, "UserAccountService", "UserAccountDatabase") ||
            path == authentication_database ||
            (connected_login &&
             path == server::database_path(config, "InventoryService", "InventoryDatabase")) ||
            (sqlite_grid_users && path == grid_user_path) ||
            (durable_regions && path == server::database_path(config, "GridService", "GridRegionDatabase")) ||
            (need_avatar && path == server::database_path(config, "AvatarService", "AvatarDatabase")))
            throw std::runtime_error("Friends initialization requires a separate database file");
    }
    services::AccountStore accounts(
        server::database_path(config, "UserAccountService", "UserAccountDatabase"), authentication_database,
        initialize, true, config.get("UserAccountService", "Realm", "UserAccounts"),
        config.get("AuthenticationService", "Realm", "auth"), local_authentication);
    if (initialize) {
        if (local_friends) {
            services::SqliteFriendsStore friends(
                server::database_path(config, "FriendsService", "FriendsDatabase"), true,
                config.get("FriendsService", "Realm", "Friends"));
        }
        if (need_avatar) {
            services::SqliteAvatarStore avatars(
                server::database_path(config, "AvatarService", "AvatarDatabase"), true,
                config.get("AvatarService", "Realm", "Avatars"));
        }
        if (grid_user_required || (configured_grid_statistics && sqlite_grid_users))
            open_grid_user_store();
        if (connected_login) {
            services::InventoryFolderStore inventory(
                server::database_path(config, "InventoryService", "InventoryDatabase"), true);
        }
        if (durable_regions) {
            services::SqliteRegionStore regions(
                server::database_path(config, "GridService", "GridRegionDatabase"), true,
                config.get("GridService", "Realm", "regions"));
        }
        std::cout << "Initialized selected service databases\n";
        return 0;
    }
    if (create) {
        std::string password;
        std::cerr << "Password (read from stdin): ";
        if (!std::getline(std::cin, password))
            throw std::runtime_error("No password received on stdin");
        auto principal = accounts.create_user(first, last, password, framework::ZeroUuid, level);
        if (remote_authentication)
            remote_authentication->set_password(framework::Uuid(principal), password);
        std::fill(password.begin(), password.end(), '\0');
        if (connected_login) {
            services::InventoryFolderStore store(
                server::database_path(config, "InventoryService", "InventoryDatabase"));
            services::InventoryService inventory(store);
            if (!inventory.create_user_inventory(framework::Uuid(principal)))
                std::cerr << "Inventory creation failed for " << principal << '\n';
        }
        std::cout << "Created user " << principal << '\n';
        return 0;
    }
    framework::ConsoleAppender console_log;
    auto shared_console_commands = std::make_shared<framework::ConsoleCommandResolver>();
    auto& console_commands = *shared_console_commands;
    std::atomic<bool> console_shutdown = false;
    bool restart_region = false;
    std::shared_ptr<server::RemoteConsole> remote_console;
    std::unique_ptr<server::RemoteConsoleHttpRoutes> remote_console_routes;
    std::shared_ptr<server::RegistryCapabilityListener> console_listener;
    auto console_listener_binding = std::make_shared<std::shared_ptr<server::RegistryCapabilityListener>>();
    std::unique_ptr<network::AsyncHttpServer> console_http;
    bool remote_on_main = false;
    auto console_output = [&](const std::string& line) {
        if (remote_console)
            remote_console->output(framework::utf16_replacing(line));
        else {
            static std::mutex output_mutex;
            std::lock_guard lock(output_mutex);
            std::cout << line << '\n' << std::flush;
        }
    };
    auto log_message = [&](std::int32_t level, const std::string& text) {
        console_log.append(level, framework::utf16_replacing(text));
    };
    auto normal_log = [&](const std::string& text) { log_message(framework::console_log_info, text); };
    auto error_log = [&](const std::string& text) { log_message(framework::console_log_error, text); };
    auto descendents_log = [&](const std::string& category) {
        log_message(category == "parse-error" || category == "inventory-worker-error" ||
                            category == "library-inventory-worker-error"
                        ? framework::console_log_error
                    : category == "bad-folders" ? framework::console_log_warn
                                                : framework::console_log_info,
                    category);
    };
    auto inventory_diagnostic = [&](services::InventoryLogLevel level, const std::string& message) {
        log_message(level == services::InventoryLogLevel::Error  ? framework::console_log_error
                    : level == services::InventoryLogLevel::Warn ? framework::console_log_warn
                                                                 : framework::console_log_debug,
                    message);
    };
    auto asset_log = [&](const std::string& level, const std::string& text) {
        log_message(level == "error"  ? framework::console_log_error
                    : level == "warn" ? framework::console_log_warn
                                      : framework::console_log_info,
                    text);
    };
    if (remote_console_mode) {
        server::RemoteConsoleOptions remote_options;
        remote_options.commands = shared_console_commands;
        remote_options.prompt = u"Region";
        remote_options.add_poll = [binding = console_listener_binding](auto handler) {
            if (!*binding)
                throw std::runtime_error("Remote console listener unavailable");
            (*binding)->add_poll(handler);
        };
        remote_options.remove_poll = [binding = console_listener_binding](const auto& method,
                                                                          const auto& path) {
            if (*binding)
                (*binding)->routes.remove_poll(method, path);
        };
        remote_console = std::make_shared<server::RemoteConsole>(std::move(remote_options));
        remote_console->read_config(config);
        console_log.console(std::make_shared<framework::ConsoleAppender::Output>(
            [weak = std::weak_ptr<server::RemoteConsole>(remote_console)](const auto& text,
                                                                          const auto& level) {
                auto target = weak.lock();
                if (!target)
                    throw std::runtime_error("Remote console unavailable");
                target->output(text, level);
            }));
    }
    network::HttpServerOptions options;
    options.host = host.empty() ? config.get("NativeServer", "ListenAddress", "127.0.0.1") : host;
    options.port = port.value_or(config.integer("Network", "http_listener_port", 9000));
    options.workers = config.integer("NativeServer", "HttpWorkers", 8);
    options.queued_requests = config.integer("NativeServer", "HttpQueuedRequests", 64);
    options.timeout_seconds = config.integer("NativeServer", "HttpTimeoutSeconds", 5);
    const auto max_body = config.integer(
        "NativeServer", "MaximumRequestBytes",
        local_region && config.boolean("NativeServer", "EnableRegionAssets", false) ? 16 * 1024 * 1024
                                                                                    : 1048576);
    if (max_body < 1 || max_body > 64 * 1024 * 1024)
        throw std::invalid_argument("MaximumRequestBytes must be between 1 and 67108864");
    options.maximum_body = static_cast<std::size_t>(max_body);
    auto resolve = [&](const std::string& key) {
        auto value = config.get("NativeServer", key);
        if (value.empty())
            return value;
        auto path = framework::utf8_path(value);
        if (path.is_relative())
            path = config.directory / path;
        auto u8 = path.u8string();
        return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
    };
    options.certificate = resolve("TlsCertificate");
    options.private_key = resolve("TlsPrivateKey");
    const auto home_agent_connector = find_service_connector(
        config, "UserAgentServerConnector", "/OpenSim.Server.Handlers.dll:UserAgentServerConnector");
    const auto travel_path = resolve("TravelDatabase");
    const auto home_agent_module = config.get("UserAgentService", "LocalServiceModule");
    const auto travel_store = [&]() -> std::shared_ptr<services::TravelStore> {
        const auto realm = config.get("UserAgentService", "Realm", "hg_traveling_data");
        if (!travel_path.empty())
            return std::make_shared<services::SqliteTravelStore>(travel_path, realm);
        if (home_agent_connector.empty() &&
            home_agent_module != "OpenSim.Services.HypergridService.dll:UserAgentService")
            return {};
        const auto provider = config.get("UserAgentService", "StorageProvider",
                                         config.get("DatabaseService", "StorageProvider"));
        const auto default_connection = config.get("DatabaseService", "ConnectionString");
        const auto connection = config.get("UserAgentService", "ConnectionString", default_connection);
        if (connection.empty())
            throw std::invalid_argument("UserAgentService requires a connection string");
        if (provider == "OpenSim.Data.SQLite.dll") {
            const auto section = config.get("UserAgentService", "ConnectionString").empty()
                ? "DatabaseService" : "UserAgentService";
            return std::make_shared<services::SqliteTravelStore>(
                server::database_path(config, section, "TravelDatabase"), realm);
        }
#ifdef OPENSIM_HAVE_MYSQL
        if (provider == "OpenSim.Data.MySQL.dll")
            return services::mysql_travel_store(connection, realm);
#endif
#ifdef OPENSIM_HAVE_POSTGRESQL
        if (provider == "OpenSim.Data.PGSQL.dll")
            return services::postgres_travel_store(connection, realm);
#endif
        throw std::invalid_argument("Unsupported native UserAgentService storage provider: " + provider);
    }();
    std::mutex travel_launch_mutex;
    auto home_grid_name =
        config.get("UserAgentService", "GatekeeperURI",
                   config.get("Hypergrid", "GatekeeperURI", config.get("Startup", "GatekeeperURI")));
    if (home_grid_name.empty())
        home_grid_name = config.get("UserAgentService", "ExternalName");
    if (home_grid_name.empty())
        home_grid_name = config.get("GatekeeperService", "ExternalName");
    home_grid_name = framework::lower_invariant_culture(home_grid_name);
    if (!home_grid_name.empty() && !home_grid_name.ends_with('/'))
        home_grid_name += '/';
    std::map<int, bool> trip_permissions;
    std::map<int, std::vector<std::string>> allowed_trip_exceptions, denied_trip_exceptions;
    if (travel_store) {
        const auto level_for = [](std::string_view key, std::string_view prefix) -> std::optional<int> {
            std::int32_t level;
            if (key.starts_with(prefix) && services::parse_account_integer(key.substr(prefix.size()), level))
                return level;
            return std::nullopt;
        };
        const auto add_exceptions = [](auto& exceptions, int level, const std::string& value) {
            const auto [entry, inserted] = exceptions.try_emplace(level);
            if (!inserted)
                return;
            std::istringstream domains(value);
            for (std::string domain; std::getline(domains, domain, ',');) {
                if (domain.empty())
                    continue;
                domain = framework::trim_utf8(domain);
                if (!domain.ends_with('/'))
                    domain += '/';
                entry->second.push_back(std::move(domain));
            }
        };
        for (const auto& [key, value] : config.ordered_section("UserAgentService")) {
            if (const auto level = level_for(key, "ForeignTripsAllowed_Level_")) {
                if (!trip_permissions.emplace(*level, config.boolean("UserAgentService", key, true)).second)
                    throw std::invalid_argument("Duplicate UserAgentService travel permission level");
            } else if (const auto allowed_level = level_for(key, "AllowExcept_Level_")) {
                add_exceptions(allowed_trip_exceptions, *allowed_level, value);
            } else if (const auto denied_level = level_for(key, "DisallowExcept_Level_")) {
                add_exceptions(denied_trip_exceptions, *denied_level, value);
            }
        }
    }
    const auto travel_allowed = [&](int user_level, const std::string& remote_grid) {
        const auto permission = trip_permissions.find(user_level);
        if (permission == trip_permissions.end())
            return true;
        auto destination = framework::lower_invariant_culture(remote_grid);
        if (destination == home_grid_name)
            return true;
        if (!destination.ends_with('/'))
            destination += '/';
        const bool allowed = permission->second;
        const auto& exceptions = allowed ? allowed_trip_exceptions : denied_trip_exceptions;
        const auto found = exceptions.find(user_level);
        const bool is_exception = found != exceptions.end() &&
                                  std::ranges::find(found->second, destination) != found->second.end();
        return allowed != is_exception;
    };
    services::LoginService login(accounts, config);
    if (remote_authentication)
        login.set_authenticator([&](auto principal, auto password, int lifetime, bool) {
            framework::Uuid real;
            auto token = remote_authentication->authenticate(framework::Uuid(std::string(principal)),
                                                             std::string(password), lifetime, real);
            return services::AuthenticationResult{std::move(token), real.str()};
        });
    server::LoginApplication app(login, config);
    std::unique_ptr<server::OfflineImEndpoint> offline_im_endpoint;
    constexpr std::string_view offline_connector_suffix =
        "/OpenSim.Addons.OfflineIM.dll:OfflineIMServiceRobustConnector";
    const auto mute_list_connector = find_service_connector(
        config, "MuteListConnector", "/OpenSim.Server.Handlers.dll:MuteListServiceConnector");
    const bool expose_mute_list =
        config.boolean("NativeServer", "EnableMuteListEndpoint", !mute_list_connector.empty());
    const bool local_mute_list =
        local_region && config.get("Messaging", "MuteListModule") == "MuteListModule" &&
        config.get("Modules", "MuteListService") == "LocalMuteListServicesConnector";
    std::unique_ptr<server::MuteListEndpoint> mute_list_service;
    if (local_mute_list || expose_mute_list) {
        try {
            mute_list_service = std::make_unique<server::MuteListEndpoint>(config);
        } catch (const std::exception& error) {
            if (expose_mute_list)
                throw;
            error_log("Cannot load local mute-list service: " + std::string(error.what()));
        }
    }

    const auto offline_connector = find_service_connector(config, "OfflineIMServiceConnector", offline_connector_suffix);
    const auto preferences_connector = find_service_connector(
        config, "AgentPreferencesServiceConnector", "/OpenSim.Server.Handlers.dll:AgentPreferencesServiceConnector");
    const bool preferences_endpoint_enabled =
        config.boolean("NativeServer", "EnableAgentPreferencesEndpoint", !preferences_connector.empty());
    const bool local_preferences_enabled = config.boolean("NativeServer", "EnableAgentPreferencesService",
                                                          config.get("Modules", "AgentPreferencesServices") ==
                                                              "LocalAgentPreferencesServicesConnector");
    std::function<std::optional<scene::AgentPreferences>(framework::Uuid)> load_local_preferences;
    std::function<void(framework::Uuid, const scene::AgentPreferences&)> save_local_preferences;
    if (preferences_endpoint_enabled || (local_region && local_preferences_enabled)) {
        auto database = std::make_shared<services::SqliteDatabase>();
        database->open_path(
            server::database_path(config, "AgentPreferencesService", "AgentPreferencesDatabase"), true);
        auto table =
            std::make_shared<services::SqliteTableOperations>([database] { return database; }, "AgentPrefs");
        table->execute(
            "CREATE TABLE IF NOT EXISTS AgentPrefs (PrincipalID CHAR(36) PRIMARY KEY NOT NULL, "
            "AccessPrefs CHAR(2) NOT NULL DEFAULT 'M', HoverHeight DOUBLE NOT NULL DEFAULT 0, "
            "Language CHAR(5) NOT NULL DEFAULT 'en-us', LanguageIsPublic BOOLEAN NOT NULL DEFAULT 1, "
            "PermEveryone INT NOT NULL DEFAULT 0, PermGroup INT NOT NULL DEFAULT 0, PermNextOwner INT NOT NULL DEFAULT 532480)",
            {});
        load_local_preferences = [table](framework::Uuid agent) -> std::optional<scene::AgentPreferences> {
            std::optional<scene::AgentPreferences> stored;
            const std::vector<std::string> fields{"AccessPrefs",      "HoverHeight",  "Language",
                                                  "LanguageIsPublic", "PermEveryone", "PermGroup",
                                                  "PermNextOwner"};
            table->query(
                "SELECT AccessPrefs,HoverHeight,Language,LanguageIsPublic,PermEveryone,PermGroup,PermNextOwner "
                "FROM AgentPrefs WHERE PrincipalID=:id",
                {{":id", agent.str()}}, fields, [&](const auto& row, const auto&) {
                    auto& prefs = stored.emplace();
                    prefs.access = row.value_text(0);
                    const auto hover = row.value_text(1);
                    std::from_chars(hover.data(), hover.data() + hover.size(), prefs.hover);
                    prefs.language = row.value_text(2);
                    prefs.language_public = row.int32_value(3) != 0;
                    prefs.everyone = row.int32_value(4);
                    prefs.group = row.int32_value(5);
                    prefs.next_owner = row.int32_value(6);
                });
            return stored;
        };
        save_local_preferences = [table](framework::Uuid agent, const scene::AgentPreferences& prefs) {
            char buffer[64];
            const auto hover = std::to_chars(buffer, buffer + sizeof buffer, prefs.hover);
            table->execute(
                "REPLACE INTO AgentPrefs (PrincipalID,AccessPrefs,HoverHeight,Language,LanguageIsPublic,PermEveryone,PermGroup,PermNextOwner) "
                "VALUES (:id,:access,:hover,:language,:public,:everyone,:group,:next)",
                {{":id", agent.str()},
                 {":access", prefs.access},
                 {":hover", std::string(buffer, hover.ptr)},
                 {":language", prefs.language},
                 {":public", prefs.language_public ? "1" : "0"},
                 {":everyone", std::to_string(prefs.everyone)},
                 {":group", std::to_string(prefs.group)},
                 {":next", std::to_string(prefs.next_owner)}});
        };
    }
    std::optional<int> offline_connector_port;
    std::string offline_auth_section = "Messaging";
    if (offline_connector == offline_connector_suffix.substr(1)) {
        offline_connector_port = options.port;
    } else if (offline_connector.ends_with(offline_connector_suffix)) {
        std::string_view prefix(offline_connector.data(),
                                offline_connector.size() - offline_connector_suffix.size());
        if (const auto separator = prefix.find('@'); separator != std::string_view::npos) {
            if (separator != 0)
                offline_auth_section = prefix.substr(0, separator);
            prefix.remove_prefix(separator + 1);
        }
        int listener_port{};
        const auto end = prefix.data() + prefix.size();
        const auto parsed = std::from_chars(prefix.data(), end, listener_port);
        if (parsed.ec != std::errc{} || parsed.ptr != end || listener_port < 0 || listener_port > 65535)
            throw std::invalid_argument("Invalid OfflineIM connector port");
        offline_connector_port = listener_port == 0 ? options.port : listener_port;
    }
    const bool expose_offline_im =
        config.boolean("NativeServer", "EnableOfflineIMEndpoint", offline_connector_port.has_value());
    const bool separate_offline_listener =
        expose_offline_im && offline_connector_port && *offline_connector_port != options.port;
    const bool local_offline_im =
        local_region && config.get("Messaging", "OfflineMessageModule") == "Offline Message Module V2" &&
        config.get("Messaging", "OfflineMessageURL").empty();
    if (expose_offline_im || local_offline_im) {
        const auto provider =
            config.get("Messaging", "StorageProvider", config.get("DatabaseService", "StorageProvider"));
        const auto sql_connection =
            config.get("Messaging", "ConnectionString", config.get("DatabaseService", "ConnectionString"));
        const auto limit = config.integer("Messaging", "MaxOfflineIMs",
                                          config.integer("OfflineIMService", "MaxOfflineIMs", 25));
        const auto* section = "OfflineIMService";
        if (!config.get("Messaging", "ConnectionString").empty())
            section = "Messaging";
        else if (!config.get("DatabaseService", "ConnectionString").empty())
            section = "DatabaseService";
        server::OfflineImOperations operations;
        const auto bind_store = [&](auto store) {
            operations.get = [store](auto id) { return store->drain(id); };
            operations.erase = [store](auto id) { store->erase(id); };
            operations.store = [store, limit](framework::Uuid recipient,
                                              const network::ServiceXmlValue& message) {
                const auto stored = store->store(recipient, message, limit);
                return std::pair{stored,
                                 stored ? std::string{} : std::string("Number of offline IMs has maxed out")};
            };
        };
        if (provider.empty() || provider == "OpenSim.Data.SQLite.dll") {
            bind_store(std::make_shared<services::SqliteOfflineImStore>(
                services::sqlite_data_source_connection(
                    server::database_path(config, section, "OfflineIMDatabase")),
                config.get("Messaging", "Realm", "native_offline_im")));
#ifdef OPENSIM_HAVE_MYSQL
        } else if (provider == "OpenSim.Data.MySQL.dll") {
            bind_store(std::make_shared<services::MysqlOfflineImStore>(
                sql_connection, config.get("Messaging", "Realm", "im_offline")));
#endif
#ifdef OPENSIM_HAVE_POSTGRESQL
        } else if (provider == "OpenSim.Data.PGSQL.dll") {
            bind_store(std::make_shared<services::PgsqlOfflineImStore>(
                sql_connection, config.get("Messaging", "Realm", "im_offline")));
#endif
        } else {
            throw std::invalid_argument("Unsupported native OfflineIM storage provider: " + provider);
        }
        offline_im_endpoint =
            std::make_unique<server::OfflineImEndpoint>(std::move(operations), config, offline_auth_section);
        if (expose_offline_im && !separate_offline_listener)
            app.set_offline_im_endpoint(offline_im_endpoint.get());
    }
    app.json_rpc().logger([&console_log](std::string_view message) {
        console_log.append(framework::console_log_error, framework::utf16_replacing(message));
    });
    // The host owns app and accounts longer than the connector and its delegates.
    std::shared_ptr<services::SqliteProfilesData> profiles_storage;
    std::unique_ptr<services::ProfileServiceConnector> profiles_connector;
    std::unique_ptr<services::LocalProfilesConnector> local_profiles_connector;
    std::shared_ptr<services::LocalProfileScene> local_profiles_scene;
    if (local_region || config.has_section("UserProfilesService")) {
        class ProfileAccountsAdapter final : public services::LocalProfileAccounts {
            services::AccountStore& accounts_;

          public:
            explicit ProfileAccountsAdapter(services::AccountStore& accounts) : accounts_(accounts) {}
            std::shared_ptr<services::ProfileAccount> get(framework::Uuid scope,
                                                          framework::Uuid user) override {
                auto account = accounts_.find_by_id(scope.str(), user.str());
                if (!account)
                    return {};
                auto result = std::make_shared<services::ProfileAccount>();
                if (account->email)
                    result->email = framework::utf16_replacing(*account->email);
                return result;
            }
        };
        services::ProfilesPluginFactories factories;
        factories.storage = [&](auto module, auto connection) -> std::shared_ptr<services::ProfilesData> {
            if (module != services::UserProfilesService::Text(u"OpenSim.Data.SQLite.dll") &&
                module !=
                    services::UserProfilesService::Text(u"OpenSim.Data.SQLite.dll:SQLiteUserProfilesData"))
                throw std::runtime_error("Native profile storage module is not registered: " +
                                         framework::utf8(module.value_or(u"")));
            profiles_storage = std::make_shared<services::SqliteProfilesData>(
                framework::utf8(connection.value_or(u"")), [&](auto message) {
                    console_log.append(framework::console_log_error, std::u16string(message));
                });
            return profiles_storage;
        };
        factories.accounts = [&](auto module, auto) -> std::shared_ptr<services::ProfileAccounts> {
            if (module != services::UserProfilesService::Text(
                              u"OpenSim.Services.UserAccountService.dll:UserAccountService"))
                throw std::runtime_error("Native profile account module is not registered: " +
                                         framework::utf8(module.value_or(u"")));
            return std::make_shared<ProfileAccountsAdapter>(accounts);
        };
        auto dispatcher = std::shared_ptr<server::JsonRpcDispatcher>(&app.json_rpc(), [](auto*) {});
        services::ProfileServiceConnector::Factory profile_factory =
            [factories, &console_log](
                auto module, auto source, auto section) -> std::shared_ptr<services::UserProfilesService> {
            if (module != services::UserProfilesService::Text(
                              u"OpenSim.Services.UserProfilesService.dll:UserProfilesService")) {
                auto dll = module.value_or(u"");
                if (auto split = dll.find(u':'); split != dll.npos)
                    dll.resize(split);
                console_log.append(framework::console_log_error,
                                   u"[SERVER UTILS]: Error loading plugin from " + dll);
                return {};
            }
            try {
                return services::configured_profiles_service(
                    source, section, factories, [&console_log](auto level, auto message) {
                        auto severity = level == services::ProfilesDiagnosticLevel::Warn
                                            ? framework::console_log_warn
                                            : framework::console_log_error;
                        console_log.append(severity, std::u16string(message));
                    });
            } catch (const std::exception& error) {
                console_log.append(
                    framework::console_log_error,
                    u"[SERVER UTILS]: Error loading plugin OpenSim.Services.Interfaces.IUserProfilesService "
                    u"from OpenSim.Services.UserProfilesService.dll. Exception: " +
                        framework::utf16_replacing(error.what()));
                console_log.append(
                    framework::console_log_error,
                    u"[SERVER UTILS]: Error loading plugin OpenSim.Services.UserProfilesService.dll: "
                    u"Exception has been thrown by the target of an invocation. args.Length 2");
                return {};
            }
        };
        server::JsonRpcProfileHandlers::Logger handler_log = [&console_log](auto level, auto message) {
            console_log.append(level == server::ProfileLogLevel::Info ? framework::console_log_info
                                                                      : framework::console_log_debug,
                               std::u16string(message));
        };
        if (local_region) {
            local_profiles_connector = std::make_unique<services::LocalProfilesConnector>(
                [dispatcher] { return dispatcher; }, std::move(profile_factory),
                [&console_log](auto level, auto message) {
                    console_log.append(level == services::LocalProfileLogLevel::Info
                                           ? framework::console_log_info
                                           : framework::console_log_error,
                                       std::u16string(message));
                },
                std::move(handler_log));
            local_profiles_connector->initialise(config.native_source());
            local_profiles_connector->post_initialise();
        } else
            profiles_connector = std::make_unique<services::ProfileServiceConnector>(
                config.native_source(), dispatcher, u"UserProfilesService", std::move(profile_factory),
                std::move(handler_log));
    }
    std::unique_ptr<server::AccountEndpoint> account_endpoint;
    std::unique_ptr<server::AuthenticationEndpoint> authentication_endpoint;
    if (config.boolean("NativeServer", "EnableAuthenticationEndpoint", false)) {
        server::AuthenticationOperations operations;
        operations.authenticate = [&](auto id, auto password, int lifetime) -> std::optional<std::string> {
            return remote_authentication ? remote_authentication->authenticate(id, password, lifetime)
                                         : accounts.authenticate(id.str(), password, lifetime).token;
        };
        operations.verify = [&](auto id, auto token, int lifetime) {
            return remote_authentication ? remote_authentication->verify(id, token, lifetime)
                                         : accounts.verify(id.str(), token, lifetime);
        };
        operations.release = [&](auto id, auto token) {
            return remote_authentication ? remote_authentication->release(id, token)
                                         : accounts.release(id.str(), token);
        };
        operations.set_password = [&](auto id, auto password) {
            return remote_authentication ? remote_authentication->set_password(id, password)
                                         : accounts.set_password(id.str(), password);
        };
        operations.get_info = [&](auto id) {
            return remote_authentication ? remote_authentication->get_auth_info(id)
                                         : accounts.get_auth_info(id);
        };
        operations.set_info = [&](auto info) {
            return remote_authentication ? remote_authentication->set_auth_info(info)
                                         : accounts.set_auth_info(info);
        };
        authentication_endpoint =
            std::make_unique<server::AuthenticationEndpoint>(std::move(operations), config);
        app.set_authentication_endpoint(authentication_endpoint.get());
    }
    std::unique_ptr<services::PresenceStore> presence_store;
    std::unique_ptr<services::PresenceService> presence;
    std::unique_ptr<server::PresenceEndpoint> presence_endpoint;
    const bool use_remote_region_presence =
        local_region && config.get("Modules", "PresenceServices") == "RemotePresenceServicesConnector";
    if (connected_login || expose_presence) {
        const bool inherit_database_service = !use_remote_region_presence || expose_presence;
        const auto native_database = config.get("NativeServer", "PresenceDatabase");
        const auto inherited_provider =
            inherit_database_service ? config.get("DatabaseService", "StorageProvider") : std::string{};
        const auto inherited_connection =
            inherit_database_service ? config.get("DatabaseService", "ConnectionString") : std::string{};
        const auto provider = config.get("PresenceService", "StorageProvider",
            native_database.empty() ? inherited_provider : "OpenSim.Data.SQLite.dll");
        const auto connection = config.get("PresenceService", "ConnectionString",
            native_database.empty() ? inherited_connection : native_database);
        const auto realm = config.get("PresenceService", "Realm", "Presence");
        if (provider == "OpenSim.Data.Null.dll" || (provider.empty() && connection.empty())) {
            presence_store = std::make_unique<services::NullPresenceStore>();
#ifdef OPENSIM_HAVE_MYSQL
        } else if (provider == "OpenSim.Data.MySQL.dll") {
            presence_store = services::mysql_presence_store(connection, realm);
#endif
#ifdef OPENSIM_HAVE_POSTGRESQL
        } else if (provider == "OpenSim.Data.PGSQL.dll") {
            presence_store = services::postgres_presence_store(connection, realm);
#endif
        } else if (provider.empty() || provider == "OpenSim.Data.SQLite.dll") {
            if (!connection.empty() && native_database.empty() &&
                config.get("PresenceService", "ConnectionString").empty()) {
                presence_store = std::make_unique<services::SqlitePresenceStore>(
                    server::database_path(config, "DatabaseService", "PresenceDatabase"));
            } else {
                presence_store = std::make_unique<services::SqlitePresenceStore>(
                    server::database_path(config, "PresenceService", "PresenceDatabase"));
            }
        } else {
            throw std::invalid_argument("Unsupported native presence storage provider: " + provider);
        }
        presence = std::make_unique<services::PresenceService>(
            *presence_store, config.boolean("PresenceService", "AllowDuplicatePresences", false));
        if (expose_presence)
            presence_endpoint = std::make_unique<server::PresenceEndpoint>(*presence, config);
    }
    std::unique_ptr<services::IPresenceService> remote_region_presence;
    if (use_remote_region_presence)
        remote_region_presence = std::make_unique<services::PresenceConnector>(
            config, outgoing_clients(), error_log);
    services::IPresenceService* const region_presence =
        remote_region_presence ? remote_region_presence.get() : presence.get();
    std::unique_ptr<services::GridUserStore> grid_user_store;
    std::unique_ptr<services::GridUserService> grid_user;
    std::unique_ptr<server::GridUserEndpoint> grid_user_endpoint;
    if (need_grid_user) {
        try {
            grid_user_store = open_grid_user_store();
            if (grid_user_required) {
                grid_user = std::make_unique<services::GridUserService>(*grid_user_store);
                if (expose_grid_user)
                    grid_user_endpoint = std::make_unique<server::GridUserEndpoint>(*grid_user, config);
            }
        } catch (const std::exception& failure) {
            if (grid_user_required)
                throw;
            error_log(std::string("Grid statistics could not open the grid-user store: ") + failure.what());
        }
    }
    std::unique_ptr<services::GridUserConnector> remote_grid_user;
    if (use_remote_grid_user)
        remote_grid_user = std::make_unique<services::GridUserConnector>(config, outgoing_clients(), error_log);
    std::unique_ptr<services::RegionStore> region_store;
    std::unique_ptr<services::GridService> grid;
    std::unique_ptr<server::GridEndpoint> grid_endpoint;
    if (need_grid) {
        try {
            if (durable_regions)
                region_store = std::make_unique<services::SqliteRegionStore>(
                    server::database_path(config, "GridService", "GridRegionDatabase"), false,
                    config.get("GridService", "Realm", "regions"));
            else if (region_provider == "Null")
                region_store = std::make_unique<services::NullRegionStore>();
        } catch (const std::exception& failure) {
            if (grid_required)
                throw;
            error_log(std::string("Grid statistics could not open the region store: ") + failure.what());
        }
    }
    if (region_store) {
        services::GridService::Link link;
        if (config.boolean("GridService", "AllowHypergridMapSearch", false)) {
            auto transport = std::make_shared<network::NwcXmlRpcHttpTransport>(outgoing_clients());
            link = [&, transport](const framework::Uuid& scope,
                                  const framework::RegionUri& uri) -> std::optional<services::GridRegion> {
                try {
                    thread_local std::mt19937 random_map_position{std::random_device{}()};
                    const int map_x = std::uniform_int_distribution<int>{0, 32766}(random_map_position) * 256;
                    if (grid->by_position(scope, map_x, 0))
                        return std::nullopt;
                    auto params = std::make_shared<network::NwcXmlRpcRequest::Parameters>();
                    params->push_back(std::make_shared<network::Value>(
                        network::Value::object({{"region_name", uri.region_name}})));
                    auto request = std::make_shared<network::NwcXmlRpcRequest>(u"link_region", params);
                    auto reply =
                        transport->send(request, framework::utf16_replacing(uri.host_url_slash()), 10000);
                    auto linked = reply && !reply->is_fault ? reply->value : nullptr;
                    if (!linked || linked->view().kind != network::Value::Kind::Map)
                        return std::nullopt;
                    for (const auto* key : {"result", "uuid", "handle", "region_image", "external_name",
                                            "size_x", "size_y"}) {
                        const auto kind = linked->get(key).view().kind;
                        if (kind != network::Value::Kind::String && kind != network::Value::Kind::Undefined)
                            return std::nullopt;
                    }
                    framework::Uuid id;
                    if (!framework::ordinal_equals_ignore_case(
                            framework::trim_utf8(linked->get("result").as_string()), "true") ||
                        !framework::try_parse_sdk_uuid(linked->get("uuid").as_string(), id) || id.is_zero())
                        return std::nullopt;
                    std::uint64_t remote_handle = 0;
                    const auto handle_text = linked->get("handle").as_string();
                    if (linked->get("handle").view().kind == network::Value::Kind::String &&
                        !framework::try_parse_unsigned_integer(handle_text, remote_handle))
                        return std::nullopt;
                    if (auto existing = grid->by_id(scope, id))
                        return existing;
                    services::GridRegion region;
                    region.id = id;
                    region.scope = scope;
                    const auto read_size = [&](const char* key, int fallback) {
                        const auto& field = linked->get(key);
                        if (field.view().kind == network::Value::Kind::String)
                            services::parse_account_integer(field.as_string(), fallback);
                        return fallback;
                    };
                    region.size_x = read_size("size_x", region.size_x);
                    region.size_y = read_size("size_y", region.size_y);
                    const auto external_name = linked->get("external_name").as_string();
                    region.set_server_uri(uri.host_url());
                    region.name = external_name.empty() ? region.server_uri() : external_name;
                    region.external_host = uri.host;
                    region.http_port = static_cast<std::uint32_t>(uri.port);
                    region.internal_endpoint = services::RegionEndpoint{"0.0.0.0", 0};
                    region.region_secret = std::to_string(remote_handle);
                    region.terrain_image = framework::Uuid("00000000-0000-1111-9999-000000000013");
                    region.flags = services::Hyperlink | services::NoDirectLogin | services::RegionOnline;
                    region.x = map_x;
                    auto row = std::make_shared<services::RegionData>(services::region_data(region));
                    row->data["flags"] = std::to_string(*region.flags);
                    if (!region_store->store(row))
                        return std::nullopt;
                    return region;
                } catch (const std::exception&) {
                    return std::nullopt;
                }
            };
        }
        grid = std::make_unique<services::GridService>(
            *region_store, config, services::GridService::Clock{}, services::GridService::Verify{},
            framework::RegionUri::LocalGrid{}, framework::RegionUri::Resolver{}, std::move(link));
        if (expose_grid)
            grid_endpoint = std::make_unique<server::GridEndpoint>(
                *grid, config, [&config] { return server::grid_extra_features(config); });
    }
    std::unique_ptr<services::AvatarStore> avatar_store;
    std::unique_ptr<services::AvatarService> avatars;
    std::shared_ptr<services::SqliteAttachmentScriptStore> attachment_script_store;
    if (need_avatar) {
        avatar_store = std::make_unique<services::SqliteAvatarPlugin>(
            services::sqlite_data_source_connection(
                server::database_path(config, "AvatarService", "AvatarDatabase")),
            config.get("AvatarService", "Realm", "Avatars"));
        avatars = std::make_unique<services::AvatarService>(*avatar_store);
        attachment_script_store = std::make_shared<services::SqliteAttachmentScriptStore>(
            services::SqliteAvatarPlugin::shared_state()->connection.load());
    }
    std::unique_ptr<services::FriendsStore> friends_store;
    std::unique_ptr<services::IFriendsService> friends;
    if (local_friends) {
        friends_store = std::make_unique<services::SqliteFriendsPlugin>(
            services::sqlite_data_source_connection(
                server::database_path(config, "FriendsService", "FriendsDatabase")),
            config.get("FriendsService", "Realm", "Friends"));
        friends = std::make_unique<services::FriendsService>(*friends_store);
    }
    if (need_friends && !local_friends)
        friends = std::make_unique<services::FriendsConnector>(config, services::FriendsConnector::Request{},
                                                               outgoing_clients());
    std::unique_ptr<server::FriendsEndpoint> friends_endpoint;
    if (config.boolean("NativeServer", "EnableFriendsEndpoint", false)) {
        server::FriendsOperations operations;
        auto package =
            [](const std::vector<services::FriendInfo>& infos) -> server::FriendsOperations::Friends {
            std::vector<std::shared_ptr<services::FriendInfo>> result;
            for (const auto& info : infos)
                result.push_back(std::make_shared<services::FriendInfo>(info));
            return result;
        };
        operations.get = [&, package](auto id) { return package(friends->get(id)); };
        operations.get_string = [&, package](const std::string& id) {
            return package(friends->get(std::optional<std::string>(id)));
        };
        operations.store = [&](const auto& id, const auto& friend_id, auto flags) {
            return friends->store(id, friend_id, flags);
        };
        operations.erase = [&](auto id, const auto& friend_id) { return friends->erase(id, friend_id); };
        operations.erase_string = [&](const std::string& id, const auto& friend_id) {
            return friends->erase(std::optional<std::string>(id), friend_id);
        };
        friends_endpoint = std::make_unique<server::FriendsEndpoint>(std::move(operations), config);
        app.set_friends_endpoint(friends_endpoint.get());
    }
    std::unique_ptr<services::InventoryFolderStore> inventory_store;
    std::unique_ptr<services::InventoryService> inventory;
    std::shared_ptr<services::LibraryService> library_service;
    std::unique_ptr<services::LoginOrchestrator> orchestrator;
    std::unique_ptr<services::SimulationConnector> simulation_connector;
    std::function<bool(const framework::AgentCircuitData&, std::string_view)> verify_foreign_client;
    const auto grid_user_identity = [](const framework::AgentCircuitData& circuit) {
        auto user = circuit.agent_id.str();
        if (!circuit.verified_foreign_login || !circuit.service_urls)
            return user;
        const auto home = circuit.service_urls->find("HomeURI");
        if (home == circuit.service_urls->end())
            return user;
        auto first = circuit.first_name.value_or("");
        auto last = circuit.last_name.value_or("");
        if (last.starts_with('@')) {
            const auto dot = first.find('.');
            if (dot != std::string::npos) {
                const auto last_start = dot + 1;
                const auto last_end = first.find('.', last_start);
                last = first.substr(last_start, last_end == first.npos ? first.npos : last_end - last_start);
                first.resize(dot);
            }
        }
        return user + ";" + framework::service_url_object_text(home->second) + ";" + first + " " + last;
    };
    std::unique_ptr<services::UserAgentServiceConnector> login_user_agent;
    std::function<void(framework::Uuid, framework::Uuid, std::string_view, bool)> send_friend_answer;
    std::function<void(const framework::AgentCircuitData&, const std::string&, const std::string&)>
        confirm_visitor_friendship;
    std::mutex live_friend_offers_mutex;
    std::map<framework::Uuid, std::string> visited_friend_endpoints;
    std::map<std::pair<framework::Uuid, framework::Uuid>, std::pair<framework::Uuid, framework::Uuid>>
        live_friend_offers;
    std::function<void(const scene::RegionInstantMessage&)> send_friend_offer;
    std::function<void(framework::Uuid, framework::Uuid)> forward_friend_termination;
    std::function<void(framework::Uuid, framework::Uuid, std::int32_t, std::int32_t)> forward_friend_rights;
    std::function<void(const std::string&, const std::string&, const std::string&, const std::string&, bool)>
        sync_foreign_friend;
    std::function<void(framework::Uuid, framework::Uuid, bool)> publish_friend_status;
    std::function<void(framework::Uuid, framework::Uuid)> create_friend_card;
    struct RegionPersistence {
        std::function<void(const scene::Terrain&)> save_baked_terrain;
        std::function<void(framework::Uuid, std::uint32_t)> save_estate_covenant;
        std::function<void(framework::Uuid, const std::vector<scene::RegionSpawnPoint>&)> save_telehub;
        std::function<void(std::size_t, float, float)> save_terrain_texture_heights;
        std::function<void(bool, std::size_t, framework::Uuid)> save_terrain_texture_asset;
        std::function<void(float, float, float)> save_region_terrain_settings;
        std::function<void(bool)> save_region_sun_settings;
        std::function<void(std::optional<framework::Uuid>, std::optional<std::string>)> save_environment;
        std::function<void(const scene::EstateRegionSettings&)> save_estate_region_settings;
        std::function<void()> save_terrain;
        std::function<void(framework::Uuid, framework::Uuid)> save_map_images;
        std::function<void()> save_prim_metadata;
        std::chrono::steady_clock::time_point next_terrain_save = std::chrono::steady_clock::now();
    };
    using ParcelCells = std::vector<std::shared_ptr<const scene::ScriptParcel>>;
    enum class ParcelSave { Access, Ownership, Reclaim, CleanTime, Music, Media, Properties, Purchase, MarkContent };
    struct RegionParcels {
        std::optional<ParcelCells> persisted_parcels;
        std::vector<std::shared_ptr<const scene::ScriptParcel>> persisted_parcel_records;
        std::shared_ptr<const scene::ScriptParcel> default_parcel;
        std::mutex parcels_mutex;
        std::function<void(const scene::ScriptParcel&, bool, ParcelSave)> save_parcel;
        std::function<void(const ParcelCells&, const std::vector<framework::Uuid>&)> save_parcel_topology;
        std::function<void(framework::Uuid, float)> save_parcel_dwell;
        std::atomic<double> parcel_object_bonus{1.0};
    };
    LocalRegionRouter local_regions;
    std::function<network::ServiceXmlValue(const services::OrderedRegionFields&)> remote_grid_request;
    struct HostedRegion {
        RegionPersistence persistence;
        RegionParcels parcels;
        std::unique_ptr<services::RegionAssetRuntime> assets;
        std::function<void()> publish_map_images;
        std::shared_ptr<scene::RegionRuntime> runtime;
    } hosted_region;
    auto& region_persistence = hosted_region.persistence;
    auto& region_parcels = hosted_region.parcels;
    auto& region_assets = hosted_region.assets;
    auto& region_runtime = hosted_region.runtime;
    auto& publish_map_images = hosted_region.publish_map_images;
    std::weak_ptr<network::ScriptHttpService> script_http_service;
    std::unique_ptr<server::GridIdentity> profile_grid_identity;
    std::shared_ptr<scene::UserProfileRequests> advanced_profiles;

    std::shared_ptr<server::RegistryCapabilityListener> capability_listener;
    server::PollHttpService capability_polls(2, [&](const auto& message, const std::exception& error) {
        log_message(framework::console_log_warn, message + error.what());
    });
    std::unique_ptr<ProfileWorkers> asset_transfer_workers;
    std::unique_ptr<ProfileWorkers> region_lookup_workers;
    std::unique_ptr<ProfileWorkers> profile_workers;
    const bool estate_communications =
        config.get("Estates", "EstateCommunicationsHandler", "EstateModule") == "EstateModule";
    const bool message_transfer_enabled =
        config.get("Messaging", "MessageTransferModule", "MessageTransferModule") == "MessageTransferModule";
    const auto estate_token = config.get("Estates", "Token", "7db8eh2gvgg45jj");
    std::unique_ptr<ProfileWorkers> bakes_workers;
    std::unique_ptr<ProfileWorkers> bakes_upload_worker;
    std::unique_ptr<services::RegionScriptTaskRuntime> region_scripts;
    std::unique_ptr<services::RegionInventoryAssetRuntime> region_inventory_uploads;
    std::unique_ptr<services::RegionBakedTextureRuntime> region_bakes;
    std::shared_ptr<services::SimulatorFeaturesModule> simulator_features;
    const auto lookup_remote_region =
        [&](framework::Uuid scope,
            services::OrderedRegionFields fields) -> std::optional<services::GridRegion> {
        if (!remote_grid_request)
            return {};
        fields.emplace_back("SCOPEID", scope.str());
        try {
            for (const auto& [name, value] : remote_grid_request(fields).fields) {
                if (name != "result" || !value.list)
                    continue;
                services::RegionFields descriptor;
                for (const auto& [key, field] : value.fields)
                    descriptor[key] = field.text;
                auto region = services::GridRegion::from_fields(descriptor);
                region.scope = scope;
                return region;
            }
        } catch (const std::exception& error) {
            error_log(std::string("[GRID CONNECTOR]: Region lookup failed: ") + error.what());
        }
        return {};
    };
    std::unique_ptr<ProfileWorkers> estate_workers;
    std::unique_ptr<services::RegionInventoryRuntime> region_inventory;
    std::unique_ptr<services::RegionInventoryDescendentsRuntime> region_folders;
    std::unique_ptr<services::RegionLibraryDescendentsRuntime> region_library_folders;
    std::unique_ptr<services::AgentLauncher> agent_launcher;
    std::unique_ptr<services::LoginAgentFactory> agent_factory;
    std::unique_ptr<services::LoginCompletion> login_completion;
    auto make_simulation_requester = [&] {
        auto clients = outgoing_clients();
        network::OsdServiceNativeHttp transport(clients, clients->shared_handler.load()->error_factory());
        auto request_number = std::make_shared<std::atomic<std::int32_t>>(0);
        network::OsdServiceServices web;
        web.next_request_number = [request_number] {
            return request_number->fetch_add(1, std::memory_order_relaxed);
        };
        web.debug_level = [] { return 0; };
        web.debug = [&console_log](const auto& text) {
            console_log.append(framework::console_log_debug, text);
        };
        web.info = [&console_log](const auto& text) {
            console_log.append(framework::console_log_info, text);
        };
        transport.bind(web);
        return std::make_shared<network::OsdServiceRequester>(std::move(web));
    };
    if (connected_login) {
        inventory_store = std::make_unique<services::InventoryFolderStore>(
            server::database_path(config, "InventoryService", "InventoryDatabase"));
        inventory = std::make_unique<services::InventoryService>(*inventory_store);
        services::LoginDependencies dependencies;
        dependencies.grid_user = [&](const std::string& user) { return grid_user->info(user); };
        dependencies.logged_out = [&](const std::string& user, const framework::Uuid& session,
                                      const framework::Uuid& region, const framework::Vector3& position,
                                      const framework::Vector3& look) {
            return grid_user->logged_out(user, session, region, position, look);
        };
        dependencies.inventory = [&](const framework::Uuid& user) { return inventory->skeleton(user); };
        if (avatars)
            dependencies.appearance = [&](const framework::Uuid& id) { return avatars->get_appearance(id); };
        dependencies.grid = grid.get();
        dependencies.presence = presence.get();
        dependencies.recover_duplicate = [&, key = config.get("Messaging", "MessageKey"),
                                          clients = outgoing_clients()](const framework::Uuid& scope,
                                                                        const framework::Uuid& user,
                                                                        const services::GridUserInfo& info) {
            return services::recover_duplicate_session(*grid, *grid_user, scope, user, info, key, clients);
        };
        orchestrator = std::make_unique<services::LoginOrchestrator>(std::move(dependencies), config);
        login.set_orchestrator(orchestrator.get());
        if (remote_simulation || local_region) {
            auto simulation_requester = make_simulation_requester();
            simulation_connector = std::make_unique<services::SimulationConnector>(simulation_requester);
            if (local_region) {
                auto region = local_region_descriptors.front();
                const auto size = region.size_x;
                // OpenSimulator applies Map first and then overrides it with Startup.
                const auto map_setting = [&](const char* key, std::string fallback = {}) {
                    return config.get("Startup", key, config.get("Map", key, std::move(fallback)));
                };
                const auto map_flag = [&](const char* key, bool fallback) {
                    return config.boolean("Startup", key, config.boolean("Map", key, fallback));
                };
                const auto map_integer = [&](const char* key, int fallback) {
                    return config.integer("Startup", key, config.integer("Map", key, fallback));
                };
                const bool generate_map_tiles = map_flag("GenerateMaptiles", true);
                if (!generate_map_tiles) {
                    const auto static_map = map_setting("MaptileStaticUUID");
                    if (!framework::try_parse_sdk_uuid(static_map, region.terrain_image) ||
                        region.terrain_image.is_zero())
                        framework::try_parse_sdk_uuid(region_settings.get("NativeRegion", "MaptileStaticUUID"),
                                                       region.terrain_image);
                }
                scene::RegionPolicies policies;
                policies.script_remote_data_channels = remote_data;
                policies.auto_grant_attach_permissions =
                    region_settings.get("NativeRegion", "auto_grant_attach_perms") == "true";
                policies.auto_grant_all_script_permissions =
                    region_settings.get("NativeRegion", "auto_grant_all_perms") == "true";
                const auto landing = region_settings.get("NativeRegion", "DefaultLanding", "<128, 128, 30>");
                framework::Vector3 landing_position;
                if (framework::try_parse_sdk_vector3(framework::utf16_replacing(landing), landing_position) &&
                    std::isfinite(landing_position.x) && std::isfinite(landing_position.y) &&
                    std::isfinite(landing_position.z))
                    policies.default_landing = landing_position;
                else
                    error_log("Unable to parse DefaultLanding for " + region.name.value_or(""));
                if (config.get("Startup", "emailmodule", "DefaultEmailModule") == "DefaultEmailModule" &&
                    config.boolean("SMTP", "enabled", false)) {
                    const auto local_recipient_limit = config.integer("SMTP", "MailsToPrimAddressPerHour", 50);
                    const auto mailbox = std::make_shared<scene::ScriptMailbox>(
                        config.integer("SMTP", "MailsFromOwnerPerHour", 500),
                        local_recipient_limit, config.integer("SMTP", "SMTP_MailsPerDay", 100),
                        config.integer("SMTP", "MailsToSMTPAddressPerHour", local_recipient_limit));
                    const auto host = config.get("SMTP", "internal_object_host", "lsl.opensim.local");
                    const bool enable_smtp = config.boolean("SMTP", "enableEmailToSMTP", false);
                    const auto smtp_host = config.get("SMTP", "SMTP_SERVER_HOSTNAME");
                    const auto smtp_port = config.integer("SMTP", "SMTP_SERVER_PORT", 25);
                    const auto smtp_from = config.get("SMTP", "SMTP_SERVER_FROM");
                    const auto smtp_domain = config.get("SMTP", "host_domain_header_from");
                    const bool smtp_start_tls = config.boolean("SMTP", "SMTP_SERVER_TLS", false);
                    const network::ScriptSmtpSecurity smtp_security{
                        smtp_start_tls ? network::ScriptSmtpTls::StartTls :
                            smtp_port == 465 ? network::ScriptSmtpTls::Implicit : network::ScriptSmtpTls::Automatic,
                        !smtp_start_tls || config.boolean("SMTP", "SMTP_VerifyCertChain", true),
                        !smtp_start_tls || config.boolean("SMTP", "SMTP_VerifyCertNames", true),
                        config.get("SMTP", "SMTP_SERVER_LOGIN"), config.get("SMTP", "SMTP_SERVER_PASSWORD")};
                    const bool restrict_email = config.boolean("LL-Functions", "RestrictEmail", false);
                    const auto max_size = config.integer("SMTP", "email_max_size", 4096);
                    const auto message_limit = max_size >= 256 && max_size <= 1000000 ? max_size : 4096;
                    const auto pause = config.integer("SMTP", "email_pause_time", 20);
                    policies.script_email_delay_ms = static_cast<std::int32_t>(
                        std::clamp<std::int64_t>(static_cast<std::int64_t>(pause) * 1000, 0, INT32_MAX));
                    policies.script_email_events = [mailbox] {
                        return scene::PartEmailEvents{
                            [mailbox](framework::Uuid part) { mailbox->add(part); },
                            [mailbox](framework::Uuid part) { mailbox->remove(part); },
                        };
                    };
                    policies.next_script_email = [mailbox](framework::Uuid part, std::string_view sender,
                                                            std::string_view subject) {
                        return mailbox->next(part, sender, subject);
                    };
                    policies.send_script_email = [mailbox, host, message_limit, restrict_email, enable_smtp,
                                                  smtp_host, smtp_port, smtp_from, smtp_domain, smtp_security,
                                                  &accounts, &error_log, scope = region.scope](
                            scene::ScriptEmailOrigin origin, std::string address,
                            std::string subject, std::string body) {
                        if (!origin.targeted && restrict_email && address.find(host) == std::string::npos) {
                            const auto account = accounts.find_by_id(scope.str(), origin.part_owner.str());
                            if (!account || !account->email || account->email->empty())
                                return;
                            address = *account->email;
                        }
                        address = framework::trim_utf8(address);
                        if (address.empty() || framework::utf16(address).size() > 320 ||
                            !mailbox->allow_owner(origin.owner))
                            return;
                        const auto separator = address.find('@');
                        if (separator == std::string::npos ||
                            framework::utf16(subject).size() + framework::utf16(body).size() >
                                static_cast<std::size_t>(message_limit))
                            return;
                        const auto domain = std::string_view(address).substr(separator + 1);
                        const bool local = framework::invariant_equals_ignore_case(domain, host) ||
                            framework::invariant_equals_ignore_case(domain, "lsl.secondlife.com");
                        if (!local && (!enable_smtp || smtp_host.empty() || smtp_port < 1 || smtp_port > 65535))
                            return;
                        const auto coordinate = [](float value) {
                            return value >= -2147483648.f && value < 2147483648.f
                                ? static_cast<std::int32_t>(value) : INT32_MIN;
                        };
                        const auto position = "(" + std::to_string(coordinate(origin.position.x)) + ", " +
                            std::to_string(coordinate(origin.position.y)) + ", " +
                            std::to_string(coordinate(origin.position.z)) + ")";
                        auto text = "Object-Name: " + origin.name + "\nRegion: " + origin.region +
                            "\nLocal-Position: " + position + "\n\n" + body;
                        const auto object_id = origin.part.str();
                        auto throttle_address = framework::utf8(
                            framework::invariant_lower_utf16(framework::utf16(address)));
                        if (!local) {
                            if (!mailbox->allow_smtp(std::move(throttle_address)))
                                return;
                            try {
                                const auto sender = smtp_from.empty() ? object_id + "@" + smtp_domain : smtp_from;
                                if (!smtp_from.empty())
                                    subject = "(OSObj" + object_id + ") " + subject;
                                network::send_script_smtp(smtp_host, smtp_port, sender, address, subject,
                                                          text, origin.owner.str(), object_id, smtp_security);
                            } catch (const std::exception& error) {
                                error_log("[EMAIL]: DefaultEmailModule Exception: " + std::string(error.what()));
                            }
                            return;
                        }
                        if (!mailbox->allow_recipient(std::move(throttle_address)))
                            return;
                        framework::Uuid recipient;
                        if (!framework::try_parse_sdk_uuid(std::string_view(address).substr(0, separator), recipient))
                            return;
                        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
                        mailbox->insert(recipient, {std::to_string(now), object_id + "@" + host,
                                                    std::move(subject), std::move(text)});
                    };
                }
                policies.attachments_enabled = config.boolean("Attachments", "Enabled", true);
                policies.wear_replaces_all = config.boolean("Attachments", "WearReplacesAll", true);
                const auto permission_modules = config.get(
                    "Permissions", "permissionmodules",
                    config.get("Startup", "permissionmodules", "DefaultPermissionsModule"));
                std::istringstream permission_names(permission_modules);
                for (std::string name; std::getline(permission_names, name, ',');)
                    policies.prim_limits_enabled |= framework::trim_utf8(name) == "PrimLimitsModule";
                policies.max_prims_per_user = region_settings.integer("NativeRegion", "MaxPrimsPerUser", -1);
                policies.allow_viewer_restart =
                    config.boolean("EstateManagement", "AllowRegionRestartFromClient", true);
                policies.skip_restart_delay_when_empty =
                    config.boolean("Startup", "SkipDelayOnEmptyRegion", false);
                const bool restart_shuts_down = config.boolean("Startup", "InworldRestartShutsDown", false);
                std::filesystem::path restart_marker;
                if (const auto marker = config.get("RestartModule", "MarkerPath"); !marker.empty()) {
                    restart_marker = framework::utf8_path(marker) / region.id.str();
                    std::error_code ignored;
                    std::filesystem::remove(restart_marker, ignored);
                    policies.clear_restart_marker = [restart_marker] {
                        if (!std::filesystem::is_directory(std::filesystem::symlink_status(restart_marker)))
                            std::filesystem::remove(restart_marker);
                    };
                }
                policies.restart_region = [&, restart_shuts_down, restart_marker](bool write_marker) {
                    if (write_marker && !restart_marker.empty()) {
#ifdef _WIN32
                        const auto pid = GetCurrentProcessId();
#else
                        const auto pid = getpid();
#endif
                        std::ofstream(restart_marker, std::ios::binary) << pid;
                    }
                    restart_region = !restart_shuts_down;
                    console_shutdown = true;
                };
                policies.parcel_dwell_enabled = config.native_section("Dwell") &&
                    config.get("Dwell", "DwellModule", "DefaultDwellModule") == "DefaultDwellModule";
                if (policies.parcel_dwell_enabled) {
                    policies.persist_parcel_dwell = [&](framework::Uuid parcel, float dwell) {
                        if (region_parcels.save_parcel_dwell)
                            region_parcels.save_parcel_dwell(parcel, dwell);
                    };
                }
                policies.force_parcel_bans = !config.boolean("LandManagement", "DisableParcelBans", false);
                policies.show_parcel_ban_lines = config.boolean("LandManagement", "ShowParcelBansLines", true);
                policies.administrator_parcel_owner = framework::Uuid(config.get(
                    "LandManagement", "DefaultAdministratorOwnerUUID", framework::Uuid{}.str()));
                policies.administrator_parcel_group = framework::Uuid(config.get(
                    "LandManagement", "DefaultAdministratorGroupUUID", framework::Uuid{}.str()));
                policies.administrator_parcel_name =
                    config.get("LandManagement", "DefaultAdministratorParcelName", "Admin Parcel");
                float ban_line_height = 0;
                if (framework::parse_invariant_float(config.get("LandManagement", "BanLineSafeHeight"),
                                                     ban_line_height) &&
                    ban_line_height > 20.f && ban_line_height <= 5000.f)
                    policies.ban_line_safe_height = ban_line_height;
                policies.estate_communications = estate_communications;
                policies.telehub_allow_landmarks =
                    config.boolean("Startup", "TelehubAllowLandmark", false);
                policies.spawn_point_routing = config.get("Startup", "SpawnPointRouting", "closest");
                policies.use_sl_landing_behavior =
                    config.get("EntityTransfer", "LandingPointBehavior") == "LandingPointBehavior_SL";
                auto level_upload = config.get("Permissions", "LevelUpload");
                if (level_upload.empty())
                    level_upload = config.get("Startup", "LevelUpload");
                if (!level_upload.empty() &&
                    !services::parse_account_integer(level_upload, policies.minimum_asset_upload_level))
                    throw std::invalid_argument("LevelUpload must be a valid 32-bit integer");
                policies.asset_upload_user_level = [&, scope = region.scope](framework::Uuid agent) {
                    const auto account = accounts.find_by_id(scope.str(), agent.str());
                    return account ? account->user_level : 0;
                };
                if (local_mute_list && mute_list_service) {
                    policies.request_mute_list = [&mute_list_service](auto agent, auto crc) {
                        return mute_list_service->get(agent, crc);
                    };
                    policies.store_mute = [&mute_list_service](auto agent, auto mute, const auto& name,
                                                               auto type, auto flags) {
                        mute_list_service->store(agent, mute, name, type, flags);
                    };
                    policies.remove_mute = [&mute_list_service](auto agent, auto mute, const auto& name) {
                        mute_list_service->remove(agent, mute, name);
                    };
                } else if (config.get("Messaging", "MuteListModule") == "MuteListModule" &&
                           config.get("Modules", "MuteListService") == "RemoteMuteListServicesConnector") {
                    auto endpoint = config.get("MuteListService", "MuteListServerURI");
                    if (!framework::HttpHost(endpoint).valid())
                        throw std::invalid_argument("MuteListService.MuteListServerURI must be an absolute HTTP URL");
                    while (endpoint.ends_with('/'))
                        endpoint.pop_back();
                    endpoint += "/mutelist";
                    std::optional<std::pair<std::string, std::string>> auth;
                    constexpr auto section = "MuteListService";
                    if (config.get(section, "AuthType", config.get("Network", "AuthType")) ==
                        "BasicHttpAuthentication")
                        auth.emplace(config.get(section, "HttpAuthUsername", config.get("Network", "HttpAuthUsername")),
                                     config.get(section, "HttpAuthPassword", config.get("Network", "HttpAuthPassword")));
                    const auto transport = network::rest_forms_transport(outgoing_clients(), auth);
                    const auto request = [transport, endpoint, &error_log](
                        const std::vector<std::pair<std::string, std::optional<std::string>>>& fields) {
                        try {
                            return transport(endpoint, network::build_service_form(fields));
                        } catch (const std::exception& error) {
                            error_log("Mute-list service: " + std::string(error.what()));
                            return std::string{};
                        }
                    };
                    policies.request_mute_list = [request](framework::Uuid agent, std::uint32_t crc)
                        -> std::optional<std::vector<std::uint8_t>> {
                        try {
                            const auto xml = network::parse_service_xml(
                                request({{"METHOD", "get"}, {"agentid", agent.str()}, {"mutecrc", std::to_string(crc)}}));
                            for (const auto& [key, field] : xml.fields) {
                                if (key != "result" || framework::trim_utf8(field.text).empty())
                                    continue;
                                network::Value value;
                                value.kind = network::Value::Kind::Binary;
                                value.text = field.text;
                                if (const auto bytes = network::osd_binary(value))
                                    return *bytes;
                            }
                        } catch (const std::exception&) {
                        }
                        return std::nullopt;
                    };
                    policies.store_mute = [request](auto agent, auto mute, const auto& name, int type, std::int32_t flags) {
                        std::vector<std::pair<std::string, std::optional<std::string>>> fields{
                            {"METHOD", "update"}, {"agentid", agent.str()}, {"muteid", mute.str()}};
                        if (type != 0)
                            fields.emplace_back("mutetype", std::to_string(type));
                        if (flags != 0)
                            fields.emplace_back("muteflags", std::to_string(flags));
                        fields.emplace_back("mutestamp", std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count()));
                        if (!name.empty())
                            fields.emplace_back("mutename", name);
                        request(fields);
                    };
                    policies.remove_mute = [request](auto agent, auto mute, const auto& name) {
                        std::vector<std::pair<std::string, std::optional<std::string>>> fields{
                            {"METHOD", "delete"}, {"agentid", agent.str()}, {"muteid", mute.str()}};
                        if (!name.empty())
                            fields.emplace_back("mutename", name);
                        request(fields);
                    };
                }
                constexpr auto udp_section = "ClientStack.LindenUDP";
                if (config.has_section(udp_section)) {
                    policies.udp_receive_buffer_bytes =
                        config.integer(udp_section, "client_socket_rcvbuf_size", 0);
                    policies.disable_facelights = config.boolean(udp_section, "DisableFacelights", false);
                    policies.texture_send_limit = config.integer(udp_section, "TextureSendLimit", 20);
                    auto& transport = policies.circuit_transport;
                    if (const auto value = config.integer(udp_section, "DefaultRTO", 0); value != 0)
                        transport.default_rto_ms = value;
                    if (const auto value = config.integer(udp_section, "MaxRTO", 0); value != 0)
                        transport.maximum_rto_ms = value;
                    transport.ack_timeout_ms = static_cast<std::int32_t>(
                        1000LL * config.integer(udp_section, "AckTimeout", 60));
                    transport.paused_ack_timeout_ms = static_cast<std::int32_t>(
                        1000LL * config.integer(udp_section, "PausedAckTimeout", 300));
                    transport.scene_throttle_max_bps =
                        config.integer(udp_section, "scene_throttle_max_bps", 6250000);
                }
                policies.strict_estate_access = config.boolean("Startup", "StrictAccessControl", true);
                policies.legacy_sit_offsets = config.boolean("Startup", "LegacySitOffsets", true);
                const bool has_estate_management = config.has_section("EstateManagement");
                policies.enforce_estate_age = !config.boolean(
                    "EstateManagement", "IgnoreEstateMinorAccessControl", has_estate_management);
                policies.enforce_estate_identity = !config.boolean(
                    "EstateManagement", "IgnoreEstatePaymentAccessControl", has_estate_management);
                policies.account_profile_flags = [&, scope = region.scope](framework::Uuid agent) {
                    const auto account = accounts.find_by_id(scope.str(), agent.str());
                    return account ? static_cast<std::uint32_t>(account->user_flags) : 0u;
                };
                policies.persist_estate_covenant = [&](framework::Uuid covenant, std::uint32_t timestamp) {
                    if (region_persistence.save_estate_covenant)
                        region_persistence.save_estate_covenant(covenant, timestamp);
                };
                policies.persist_telehub = [&](framework::Uuid hub, const auto& points) {
                    if (region_persistence.save_telehub)
                        region_persistence.save_telehub(hub, points);
                };
                policies.region_agent_capacity =
                    std::max(0, region_settings.integer("NativeRegion", "MaxAgents", 100));
                policies.region_object_capacity =
                    std::max(0, region_settings.integer("NativeRegion", "MaxPrims", 15000));
                policies.region_type = region_settings.get("NativeRegion", "RegionType");
                policies.script_whisper_range = config.integer("SMTP", "whisper_distance", 10);
                policies.script_chat_range = config.integer("SMTP", "say_distance", 20);
                policies.script_shout_range = config.integer("SMTP", "shout_distance", 100);
                policies.object_chat_enabled = config.boolean("Chat", "enabled", true);
                policies.object_whisper_range = config.integer("Chat", "whisper_distance", 10);
                policies.object_say_range = config.integer("Chat", "say_distance", 20);
                policies.object_shout_range = config.integer("Chat", "shout_distance", 100);
                policies.max_listens_per_region = config.integer("LL-Functions", "max_listens_per_region", 1000);
                policies.max_listens_per_script = config.integer("LL-Functions", "max_listens_per_script", 65);
                const auto god_option = [&](const char* key, bool fallback) {
                    return config.boolean("Permissions", key, config.boolean("Startup", key, fallback));
                };
                const bool grid_gods_only = god_option("force_grid_gods_only", false);
                const bool grid_gods = grid_gods_only || god_option("allow_grid_gods", false);
                const bool owner_god = !grid_gods_only && god_option("region_owner_is_god", true);
                const bool manager_god = !grid_gods_only && god_option("region_manager_is_god", false);
                policies.estate_manager_is_god = manager_god;
                auto estate_owner = region.estate_owner;
                if (const auto configured = region_settings.get("NativeRegion", "EstateDatabase");
                    !configured.empty()) {
                    auto path = framework::utf8_path(configured);
                    if (path.is_relative())
                        path = config.directory / path;
                    const auto bytes = path.u8string();
                    auto database = std::make_shared<services::SqliteDatabase>();
                    database->open_path(
                        std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()), false);
                    services::SqliteTableOperations estate([database] { return database; },
                                                           "estate_settings");
                    policies.owner = estate_owner;
                    const auto region_id = region.id.str();
                    load_estate_settings(estate, region_id, policies);
                    policies.reload_estate_settings = [database, region_id, estate_owner] {
                        scene::EstateSettings settings;
                        settings.owner = estate_owner;
                        services::SqliteTableOperations tables([database] { return database; }, "estate_settings");
                        std::lock_guard lock(database->mutex());
                        tables.execute("BEGIN", {});
                        try {
                            if (!load_estate_settings(tables, region_id, settings))
                                throw std::runtime_error("Region estate settings are missing");
                            tables.execute("COMMIT", {});
                        } catch (...) {
                            tables.execute("ROLLBACK", {});
                            throw;
                        }
                        return settings;
                    };
                    estate_owner = policies.owner;
                    policies.persist_estate_settings = [database](const scene::EstateSettings& settings) {
                        services::SqliteTableOperations tables([database] { return database; }, "estate_settings");
                        const auto flag = [](std::uint32_t flags, std::uint32_t mask) {
                            return (flags & mask) ? "1" : "0";
                        };
                        const auto handshake = settings.estate_handshake_only_flags;
                        const auto menu = settings.estate_menu_flags;
                        const auto detail = settings.estate_detail_only_flags;
                        if (tables.execute(
                                "UPDATE estate_settings SET PublicAccess=:public,TaxFree=:tax_free,"
                                "AllowEnviromentOverride=:environment,AllowDirectTeleport=:direct,"
                                "DenyAnonymous=:anonymous,DenyIdentified=:identified,DenyTransacted=:transacted,"
                                "AllowVoice=:voice,DenyMinors=:minors WHERE EstateID=:estate",
                                {{":public", flag(handshake, 0x8000u)},
                                 {":tax_free", flag(~handshake, 0x20u)},
                                 {":environment", flag(menu, 0x200u)},
                                 {":direct", flag(handshake, 0x100000u)},
                                 {":anonymous", flag(handshake, 0x800000u)},
                                 {":identified", flag(detail, 0x1000000u)},
                                 {":transacted", flag(detail, 0x2000000u)},
                                 {":voice", flag(menu, 0x10000000u)},
                                 {":minors", flag(handshake, 0x40000000u)},
                                 {":estate", std::to_string(settings.estate_id)}}) != 1)
                            throw std::runtime_error("Estate settings row is missing");
                    };
                    policies.persist_estate_access = [database](
                        const scene::EstateSettings& current, framework::Uuid id, framework::Uuid actor,
                        const scene::EstateAccessChanges& changes, const scene::RelatedEstateAccessEdit& related) {
                        services::SqliteTableOperations tables([database] { return database; }, "estate_settings");
                        const auto persist = [&](std::uint32_t estate_id, const scene::EstateAccessChanges& edit) {
                            const services::SqliteTableParameters parameters{{":estate", std::to_string(estate_id)},
                                                                             {":id", id.str()}};
                            const auto update = [&](const char* table, const std::optional<bool>& present) {
                                if (!present)
                                    return;
                                if (*present)
                                    tables.execute(std::string("INSERT INTO ") + table +
                                                       " (EstateID,uuid) VALUES (:estate,:id)",
                                                   parameters);
                                else
                                    tables.execute(std::string("DELETE FROM ") + table +
                                                       " WHERE EstateID=:estate AND uuid=:id",
                                                   parameters);
                            };
                            update("estate_users", edit.user);
                            update("estate_groups", edit.group);
                            update("estate_managers", edit.manager);
                            if (edit.ban) {
                                if (*edit.ban) {
                                    auto ban_parameters = parameters;
                                    ban_parameters.emplace_back(":actor", actor.str());
                                    const auto now =
                                        std::chrono::duration_cast<std::chrono::seconds>(
                                            std::chrono::system_clock::now().time_since_epoch())
                                            .count();
                                    ban_parameters.emplace_back(":time", std::to_string(now));
                                    tables.execute(
                                        "INSERT INTO estateban (EstateID,bannedUUID,bannedIp,bannedIpHostMask,bannedNameMask,banningUUID,banTime)"
                                        " VALUES (:estate,:id,'0.0.0.0','0.0.0.0','',:actor,:time)",
                                        ban_parameters);
                                } else
                                    tables.execute(
                                        "DELETE FROM estateban WHERE EstateID=:estate AND bannedUUID=:id",
                                        parameters);
                            }
                        };
                        std::lock_guard lock(database->mutex());
                        tables.execute("BEGIN IMMEDIATE", {});
                        bool changed = static_cast<bool>(changes);
                        try {
                            if (changes)
                                persist(current.estate_id, changes);
                            if (related) {
                                std::vector<std::uint32_t> ids;
                                tables.query(
                                    "SELECT EstateID FROM estate_settings WHERE EstateOwner=:owner AND EstateID<>:estate",
                                    {{":owner", current.owner.str()}, {":estate", std::to_string(current.estate_id)}},
                                    {"EstateID"}, [&](const auto& row, const auto&) {
                                        ids.push_back(static_cast<std::uint32_t>(row.int32_value(0)));
                                    });
                                for (const auto estate_id : ids) {
                                    scene::EstateSettings settings;
                                    settings.estate_id = estate_id;
                                    settings.owner = current.owner;
                                    load_estate_access(tables, settings);
                                    if (const auto edit = related(settings)) {
                                        persist(estate_id, edit);
                                        changed = true;
                                    }
                                }
                            }
                            tables.execute("COMMIT", {});
                        } catch (...) {
                            tables.execute("ROLLBACK", {});
                            throw;
                        }
                        return changed;
                    };
                    if (estate_communications) {
                        estate_workers = std::make_unique<ProfileWorkers>(
                            [&](const std::exception& error) {
                                error_log(std::string("Estate peer notification failed: ") + error.what());
                            },
                            true);
                        const auto forward = [&, database, scope = region.scope, own_id = region.id,
                                              request = network::rest_forms_transport(outgoing_clients())](
                                                 std::uint32_t estate_id, services::OrderedRegionFields fields) {
                            if (estate_id == 0)
                                return;
                            estate_workers->enqueue([&, database, scope, own_id, estate_id, request,
                                                     fields = std::move(fields)]() mutable {
                                services::SqliteTableOperations tables([database] { return database; },
                                                                       "estate_map");
                                std::vector<framework::Uuid> peers;
                                tables.query("SELECT RegionID FROM estate_map WHERE EstateID=:estate",
                                             {{":estate", std::to_string(estate_id)}}, {"RegionID"},
                                             [&](const auto& row, const auto&) {
                                                 framework::Uuid peer;
                                                 if (framework::try_parse_sdk_uuid(row.value_text(0), peer) &&
                                                     !peer.is_zero() && peer != own_id)
                                                     peers.push_back(peer);
                                             });
                                if (peers.empty())
                                    return;
                                fields.emplace_back("TOKEN", estate_token);
                                fields.emplace_back("EstateID", std::to_string(estate_id));
                                const auto body = network::build_service_form(fields);
                                std::set<std::string> sent;
                                if (const auto local = grid->by_id(scope, own_id))
                                    sent.insert(local->server_uri());
                                for (const auto& peer : peers) {
                                    auto target = grid->by_id(scope, peer);
                                    if (!target)
                                        target = lookup_remote_region(
                                            scope,
                                            {{"REGIONID", peer.str()}, {"METHOD", "get_region_by_uuid"}});
                                    if (!target)
                                        continue;
                                    const auto uri = target->server_uri();
                                    if (uri.empty() || !sent.insert(uri).second)
                                        continue;
                                    try {
                                        request(uri + "estate", body);
                                    } catch (const std::exception& error) {
                                        error_log(std::string("Estate peer notification failed: ") + error.what());
                                    }
                                }
                            });
                        };
                        policies.forward_estate_update = [forward](std::uint32_t estate_id) {
                            forward(estate_id, {{"METHOD", "update_estate"}});
                        };
                        policies.forward_estate_covenant = [forward](std::uint32_t estate_id,
                                                                     framework::Uuid covenant) {
                            forward(estate_id, {{"METHOD", "update_covenant"}, {"CovenantID", covenant.str()}});
                        };
                        policies.forward_estate_message = [forward](std::uint32_t estate_id, framework::Uuid sender,
                                                                    std::string name, std::string message) {
                            forward(estate_id, {{"METHOD", "estate_message"}, {"FromID", sender.str()},
                                                {"FromName", std::move(name)}, {"Message", std::move(message)}});
                        };
                        policies.forward_estate_teleport_home = [forward](std::uint32_t estate_id,
                                                                          std::optional<framework::Uuid> target) {
                            if (target)
                                forward(estate_id, {{"METHOD", "teleport_home_one_user"}, {"PreyID", target->str()}});
                            else
                                forward(estate_id, {{"METHOD", "teleport_home_all_users"}});
                        };
                    }
                }
                region.estate_owner = estate_owner;
                policies.implicit_gods = god_option("implicit_gods", false);
                policies.automatic_viewer_gods =
                    god_option("automatic_gods", false) && !policies.implicit_gods;
                policies.estate_owner_is_god = owner_god;
                policies.god_rights_level = [&, scope = region.scope, grid_gods](framework::Uuid agent) {
                    if (agent.is_zero())
                        return 0;
                    int level = 0;
                    if (grid_gods)
                        if (const auto account = accounts.find_by_id(scope.str(), agent.str());
                            account && account->user_level >= 200)
                            level = account->user_level;
                    return level;
                };
                policies.appearance_save_delay_seconds =
                    config.integer("Appearance", "DelayBeforeAppearanceSave", 5);
                policies.appearance_send_delay_seconds =
                    config.integer("Appearance", "DelayBeforeAppearanceSend", 2);
                const auto groups_connector = config.get("Groups", "ServicesConnectorModule");
                const bool groups_enabled = config.boolean("Groups", "Enabled", false);
                using GroupMemberships = std::map<framework::Uuid, services::EventGroupMembership>;
                std::function<GroupMemberships(framework::Uuid, framework::Uuid)> load_group_memberships;
                if (groups_enabled && groups_connector == "Groups Local Service Connector") {
                    const auto configured = config.get("Groups", "SQLiteMembershipDatabase");
                    if (!configured.empty()) {
                        auto path = framework::utf8_path(configured);
                        if (path.is_relative())
                            path = config.directory / path;
                        const auto bytes = path.u8string();
                        auto database = std::make_shared<services::SqliteDatabase>();
                        database->open_path(
                            std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()), false);
                        auto tables = std::make_shared<services::SqliteTableOperations>(
                            [database] { return database; }, "os_groups_membership");
                        policies.group_record_name = [tables, &error_log](framework::Uuid group) {
                            std::optional<std::string> name;
                            try {
                                tables->query("SELECT Name FROM os_groups_groups WHERE GroupID=:group",
                                              {{":group", group.str()}}, {"Name"},
                                              [&](const auto& row, const auto&) { name = row.value_text(0); });
                            } catch (const std::exception& error) {
                                error_log(std::string("Local group record lookup failed: ") + error.what());
                            }
                            return name;
                        };
                        policies.set_viewer_group_preferences = [tables](framework::Uuid agent,
                                                                         framework::Uuid group, bool notices,
                                                                         bool profile) {
                            tables->execute(
                                "UPDATE os_groups_membership SET AcceptNotices=:notices,ListInProfile=:profile "
                                "WHERE PrincipalID=:agent AND GroupID=:group",
                                {{":agent", agent.str()},
                                 {":group", group.str()},
                                 {":notices", notices ? "1" : "0"},
                                 {":profile", profile ? "1" : "0"}});
                        };
                        bool has_roles = false;
                        tables->query(
                            "SELECT name FROM sqlite_master WHERE type='table' AND name='os_groups_roles'",
                            {}, {"name"}, [&](const auto&, const auto&) { has_roles = true; });
                        bool has_principals = false;
                        tables->query(
                            "SELECT name FROM sqlite_master WHERE type='table' AND name='os_groups_principals'",
                            {}, {"name"}, [&](const auto&, const auto&) { has_principals = true; });
                        if (has_principals)
                            policies.viewer_active_group =
                                [tables, has_roles, &error_log](framework::Uuid agent,
                                                                std::optional<framework::Uuid> selected)
                                -> std::optional<scene::ActiveViewerGroup> {
                                try {
                                    if (selected)
                                        tables->execute(
                                            "INSERT INTO os_groups_principals (PrincipalID,ActiveGroupID) VALUES (:agent,:group) "
                                            "ON CONFLICT(PrincipalID) DO UPDATE SET ActiveGroupID=excluded.ActiveGroupID",
                                            {{":agent", agent.str()}, {":group", selected->str()}});
                                    scene::ActiveViewerGroup result;
                                    const auto sql =
                                        has_roles
                                            ? "SELECT g.GroupID,g.Name,CAST(r.Powers AS TEXT) AS Powers,r.Title FROM os_groups_principals p "
                                              "JOIN os_groups_membership m ON m.PrincipalID=p.PrincipalID AND m.GroupID=p.ActiveGroupID "
                                              "JOIN os_groups_groups g ON g.GroupID=m.GroupID LEFT JOIN os_groups_roles r "
                                              "ON r.GroupID=m.GroupID AND r.RoleID=m.SelectedRoleID WHERE p.PrincipalID=:agent"
                                            : "SELECT g.GroupID,g.Name,'0' AS Powers,'' AS Title FROM os_groups_principals p "
                                              "JOIN os_groups_membership m ON m.PrincipalID=p.PrincipalID AND m.GroupID=p.ActiveGroupID "
                                              "JOIN os_groups_groups g ON g.GroupID=m.GroupID WHERE p.PrincipalID=:agent";
                                    tables->query(sql, {{":agent", agent.str()}},
                                                  {"GroupID", "Name", "Powers", "Title"},
                                                  [&](const auto& row, const auto&) {
                                                      framework::try_parse_sdk_uuid(row.value_text(0),
                                                                                    result.id);
                                                      result.name = row.value_text(1);
                                                      framework::try_parse_unsigned_integer(row.value_text(2),
                                                                                            result.powers);
                                                      result.title = row.value_text(3);
                                                  });
                                    return result;
                                } catch (const std::exception& error) {
                                    error_log(std::string("Local active group request failed: ") +
                                              error.what());
                                    return std::nullopt;
                                }
                            };
                        if (has_roles) {
                            policies.select_viewer_group_title = [tables](framework::Uuid agent,
                                                                          framework::Uuid group,
                                                                          framework::Uuid role) {
                                tables->execute(
                                    "UPDATE os_groups_membership SET SelectedRoleID=:role WHERE PrincipalID=:agent AND GroupID=:group "
                                    "AND EXISTS (SELECT 1 FROM os_groups_rolemembership a JOIN os_groups_roles r "
                                    "ON r.GroupID=a.GroupID AND r.RoleID=a.RoleID WHERE a.PrincipalID=:agent AND a.GroupID=:group AND a.RoleID=:role)",
                                    {{":agent", agent.str()},
                                     {":group", group.str()},
                                     {":role", role.str()}});
                            };
                            policies.viewer_group_titles = [tables](framework::Uuid agent,
                                                                    framework::Uuid group) {
                                std::vector<scene::ViewerGroupTitle> titles;
                                tables->query(
                                    "SELECT r.RoleID,r.Name,m.SelectedRoleID FROM os_groups_rolemembership a "
                                    "JOIN os_groups_roles r ON r.GroupID=a.GroupID AND r.RoleID=a.RoleID "
                                    "LEFT JOIN os_groups_membership m ON m.GroupID=a.GroupID AND m.PrincipalID=a.PrincipalID "
                                    "WHERE a.PrincipalID=:agent AND a.GroupID=:group",
                                    {{":agent", agent.str()}, {":group", group.str()}},
                                    {"RoleID", "Name", "SelectedRoleID"}, [&](const auto& row, const auto&) {
                                        scene::ViewerGroupTitle title;
                                        if (!framework::try_parse_sdk_uuid(row.value_text(0), title.role))
                                            return;
                                        title.name = row.value_text(1);
                                        framework::Uuid selected;
                                        title.selected =
                                            framework::try_parse_sdk_uuid(row.value_text(2), selected) &&
                                            selected == title.role;
                                        titles.push_back(std::move(title));
                                    });
                                return titles;
                            };
                        }
                        load_group_memberships = [tables, has_roles, &error_log](framework::Uuid agent,
                                                                                 framework::Uuid) {
                            GroupMemberships memberships;
                            try {
                                const auto sql =
                                    has_roles
                                        ? "SELECT g.GroupID,CAST(r.Powers AS TEXT) AS Powers,g.*,m.* FROM os_groups_membership m "
                                          "JOIN os_groups_groups g ON g.GroupID=m.GroupID LEFT JOIN os_groups_roles r "
                                          "ON r.GroupID=m.GroupID AND r.RoleID=m.SelectedRoleID WHERE m.PrincipalID=:agent"
                                        : "SELECT g.GroupID,'0' AS Powers,g.*,m.* FROM os_groups_membership m "
                                          "JOIN os_groups_groups g ON g.GroupID=m.GroupID WHERE m.PrincipalID=:agent";
                                tables->query(
                                    sql, {{":agent", agent.str()}}, {"GroupID", "Powers"},
                                    [&](const auto& row, const auto&) {
                                        framework::Uuid group;
                                        if (framework::try_parse_sdk_uuid(row.value_text(0), group) &&
                                            !group.is_zero()) {
                                            services::EventGroupMembership membership;
                                            membership.group = group;
                                            framework::try_parse_unsigned_integer(row.value_text(1),
                                                                                  membership.powers);
                                            const auto read = [&](const char* field) {
                                                try {
                                                    return row.value_text(row.column(field));
                                                } catch (const std::out_of_range&) {
                                                    return std::string{};
                                                }
                                            };
                                            membership.name = read("Name");
                                            framework::try_parse_sdk_uuid(read("InsigniaID"),
                                                                          membership.picture);
                                            services::parse_account_integer(read("Contribution"),
                                                                            membership.contribution);
                                            const auto notices = read("AcceptNotices");
                                            membership.notices = notices == "1" || notices == "True";
                                            const auto profile = read("ListInProfile");
                                            membership.profile = profile == "1" || profile == "True";
                                            memberships.emplace(group, std::move(membership));
                                        }
                                    });
                            } catch (const std::exception& error) {
                                error_log(std::string("Local group membership lookup failed: ") +
                                          error.what());
                            }
                            return memberships;
                        };
                    }
                } else if (groups_enabled && groups_connector == "Groups Remote Service Connector") {
                    auto endpoint = config.get("Groups", "GroupsServerURI");
                    if (!framework::HttpHost{endpoint}.valid())
                        throw std::invalid_argument(
                            "Groups.GroupsServerURI must be an absolute HTTP or HTTPS URL");
                    while (endpoint.ends_with('/'))
                        endpoint.pop_back();
                    endpoint += "/groups";
                    std::optional<std::pair<std::string, std::string>> auth;
                    if (config.get("Groups", "AuthType", config.get("Network", "AuthType")) ==
                        "BasicHttpAuthentication")
                        auth.emplace(config.get("Groups", "HttpAuthUsername",
                                                config.get("Network", "HttpAuthUsername")),
                                     config.get("Groups", "HttpAuthPassword",
                                                config.get("Network", "HttpAuthPassword")));
                    const auto request = network::rest_forms_transport(outgoing_clients(), auth);
                    policies.group_record_name = [request, endpoint, &error_log](framework::Uuid group)
                        -> std::optional<std::string> {
                        try {
                            const auto reply = network::parse_service_xml(request(
                                endpoint, network::build_service_form({{"METHOD", "GETGROUP"},
                                                                       {"GroupID", group.str()},
                                                                       {"RequestingAgentID", framework::Uuid{}.str()}})));
                            for (const auto& [name, result] : reply.fields)
                                if (name == "RESULT" && result.list)
                                    for (const auto& [key, value] : result.fields)
                                        if (key == "GroupName" && !value.list)
                                            return value.text;
                        } catch (const std::exception& error) {
                            error_log(std::string("Remote group record lookup failed: ") + error.what());
                        }
                        return std::nullopt;
                    };
                    policies.set_viewer_group_preferences = [request, endpoint](framework::Uuid agent,
                                                                                framework::Uuid group,
                                                                                bool notices, bool profile) {
                        request(endpoint,
                                network::build_service_form({{"METHOD", "UPDATEMEMBERSHIP"},
                                                             {"AgentID", agent.str()},
                                                             {"RequestingAgentID", agent.str()},
                                                             {"GroupID", group.str()},
                                                             {"AcceptNotices", notices ? "True" : "False"},
                                                             {"ListInProfile", profile ? "True" : "False"}}));
                    };
                    policies.select_viewer_group_title = [request, endpoint](framework::Uuid agent,
                                                                             framework::Uuid group,
                                                                             framework::Uuid role) {
                        request(endpoint, network::build_service_form({{"METHOD", "SETACTIVE"},
                                                                       {"OP", "ROLE"},
                                                                       {"AgentID", agent.str()},
                                                                       {"RequestingAgentID", agent.str()},
                                                                       {"GroupID", group.str()},
                                                                       {"RoleID", role.str()}}));
                    };
                    policies.viewer_group_titles = [request, endpoint](framework::Uuid agent,
                                                                       framework::Uuid group) {
                        const auto fetch = [&](const char* method) {
                            return network::parse_service_xml(request(
                                endpoint, network::build_service_form({{"METHOD", method},
                                                                       {"AgentID", agent.str()},
                                                                       {"RequestingAgentID", agent.str()},
                                                                       {"GroupID", group.str()}})));
                        };
                        const auto roles = fetch("GETAGENTROLES");
                        const auto membership = fetch("GETMEMBERSHIP");
                        std::optional<framework::Uuid> selected;
                        for (const auto& [key, value] : membership.fields)
                            if (key == "RESULT" && value.list)
                                for (const auto& [field, data] : value.fields)
                                    if (field == "ActiveRole") {
                                        framework::Uuid role;
                                        if (framework::try_parse_sdk_uuid(data.text, role))
                                            selected = role;
                                    }
                        std::vector<scene::ViewerGroupTitle> titles;
                        for (const auto& [key, value] : roles.fields)
                            if (key == "RESULT" && value.list)
                                for (const auto& [label, row] : value.fields)
                                    if (row.list) {
                                        scene::ViewerGroupTitle title;
                                        bool valid = false;
                                        for (const auto& [field, data] : row.fields) {
                                            if (field == "RoleID")
                                                valid = framework::try_parse_sdk_uuid(data.text, title.role);
                                            if (field == "Name")
                                                title.name = data.text;
                                        }
                                        if (valid) {
                                            title.selected = selected && *selected == title.role;
                                            titles.push_back(std::move(title));
                                        }
                                    }
                        return titles;
                    };
                    policies.viewer_active_group = [request, endpoint,
                                                    &error_log](framework::Uuid agent,
                                                                std::optional<framework::Uuid> selected)
                        -> std::optional<scene::ActiveViewerGroup> {
                        try {
                            const auto id = agent.str();
                            if (selected)
                                request(endpoint,
                                        network::build_service_form({{"METHOD", "SETACTIVE"},
                                                                     {"OP", "GROUP"},
                                                                     {"AgentID", id},
                                                                     {"RequestingAgentID", id},
                                                                     {"GroupID", selected->str()}}));
                            const auto reply = network::parse_service_xml(
                                request(endpoint, network::build_service_form({{"METHOD", "GETMEMBERSHIP"},
                                                                               {"AgentID", id},
                                                                               {"RequestingAgentID", id}})));
                            for (const auto& [name, value] : reply.fields)
                                if (name == "RESULT") {
                                    scene::ActiveViewerGroup result;
                                    if (value.list) {
                                        for (const auto& [key, field] : value.fields) {
                                            if (key == "GroupID")
                                                framework::try_parse_sdk_uuid(field.text, result.id);
                                            if (key == "GroupPowers")
                                                framework::try_parse_unsigned_integer(field.text,
                                                                                      result.powers);
                                            if (key == "GroupName")
                                                result.name = field.text;
                                            if (key == "GroupTitle")
                                                result.title = field.text;
                                        }
                                        return result;
                                    }
                                    if (framework::ordinal_equals_ignore_case(value.text, "NULL"))
                                        return result;
                                }
                        } catch (const std::exception& error) {
                            error_log(std::string("Remote active group request failed: ") + error.what());
                        }
                        return std::nullopt;
                    };
                    load_group_memberships = [request, endpoint, &error_log](framework::Uuid agent,
                                                                             framework::Uuid requester) {
                        GroupMemberships memberships;
                        try {
                            const auto reply = network::parse_service_xml(request(
                                endpoint, network::build_service_form({{"METHOD", "GETMEMBERSHIP"},
                                                                       {"AgentID", agent.str()},
                                                                       {"RequestingAgentID", requester.str()},
                                                                       {"ALL", "true"}})));
                            for (const auto& [name, value] : reply.fields)
                                if (name == "RESULT" && value.list)
                                    for (const auto& [label, membership] : value.fields)
                                        if (membership.list) {
                                            services::EventGroupMembership group;
                                            group.notices = false;
                                            for (const auto& [key, field] : membership.fields) {
                                                if (key == "GroupID")
                                                    framework::try_parse_sdk_uuid(field.text, group.group);
                                                if (key == "GroupPowers")
                                                    framework::try_parse_unsigned_integer(field.text,
                                                                                          group.powers);
                                                if (key == "GroupName")
                                                    group.name = field.text;
                                                if (key == "GroupPicture")
                                                    framework::try_parse_sdk_uuid(field.text, group.picture);
                                                if (key == "Contribution")
                                                    services::parse_account_integer(field.text,
                                                                                    group.contribution);
                                                if (key == "AcceptNotices")
                                                    group.notices = framework::ordinal_equals_ignore_case(
                                                        field.text, "true");
                                                if (key == "ListInProfile")
                                                    group.profile = framework::ordinal_equals_ignore_case(
                                                        field.text, "true");
                                            }
                                            if (!group.group.is_zero())
                                                memberships.emplace(group.group, std::move(group));
                                        }
                        } catch (const std::exception& error) {
                            error_log(std::string("Estate group membership lookup failed: ") + error.what());
                        }
                        return memberships;
                    };
                }
                if (load_group_memberships) {
                    policies.estate_group_memberships = [load_group_memberships](framework::Uuid agent) {
                        std::vector<framework::Uuid> ids;
                        for (const auto& [id, group] : load_group_memberships(agent, {}))
                            ids.push_back(id);
                        return ids;
                    };
                    policies.inventory_group_member = [load_group_memberships](framework::Uuid agent,
                                                                               framework::Uuid group) {
                        return load_group_memberships(agent, agent).contains(group);
                    };
                    policies.inventory_group_powers = [load_group_memberships](framework::Uuid agent,
                                                                               framework::Uuid group) {
                        const auto powers = load_group_memberships(agent, agent);
                        const auto match = powers.find(group);
                        return match == powers.end() ? std::uint64_t{0} : match->second.powers;
                    };
                    policies.viewer_group_memberships = [load_group_memberships](framework::Uuid agent) {
                        std::vector<std::shared_ptr<services::EventGroupMembership>> groups;
                        for (auto& [id, group] : load_group_memberships(agent, agent))
                            groups.push_back(
                                std::make_shared<services::EventGroupMembership>(std::move(group)));
                        return groups;
                    };
                }
                auto agent_preferences_endpoint =
                    config.get("AgentPreferencesService", "AgentPreferencesServerURI");
                if (!agent_preferences_endpoint.empty() ||
                    config.get("Modules", "AgentPreferencesServices") ==
                        "RemoteAgentPreferencesServicesConnector") {
                    auto endpoint = std::move(agent_preferences_endpoint);
                    if (endpoint.empty())
                        throw std::invalid_argument(
                            "AgentPreferencesService.AgentPreferencesServerURI is required");
                    while (endpoint.ends_with('/'))
                        endpoint.pop_back();
                    endpoint += "/agentprefs";
                    std::optional<std::pair<std::string, std::string>> auth;
                    if (config.get("AgentPreferencesService", "AuthType",
                                   config.get("Network", "AuthType")) == "BasicHttpAuthentication")
                        auth.emplace(config.get("AgentPreferencesService", "HttpAuthUsername",
                                                config.get("Network", "HttpAuthUsername")),
                                     config.get("AgentPreferencesService", "HttpAuthPassword",
                                                config.get("Network", "HttpAuthPassword")));
                    const auto request = network::rest_forms_transport(outgoing_clients(), auth);
                    policies.load_preference_language = [request, endpoint,
                                                         &error_log](framework::Uuid agent) {
                        try {
                            const auto reply = network::parse_service_xml(
                                request(endpoint, network::build_service_form({{"METHOD", "getagentlang"},
                                                                               {"UserID", agent.str()}})));
                            for (const auto& [key, value] : reply.fields)
                                if (key == "Language")
                                    return value.text;
                        } catch (const std::exception& e) {
                            error_log(std::string("Agent language request failed: ") + e.what());
                        }
                        return std::string("en-us");
                    };
                    policies.load_preferences = [request, endpoint, &error_log](framework::Uuid agent) {
                        scene::AgentPreferences prefs;
                        try {
                            const auto reply = network::parse_service_xml(
                                request(endpoint, network::build_service_form({{"METHOD", "getagentprefs"},
                                                                               {"UserID", agent.str()}})));
                            for (const auto& [key, value] : reply.fields) {
                                if (key == "result" && (value.text == "null" || value.text == "Failure"))
                                    return scene::AgentPreferences{};
                                if (key == "AccessPrefs")
                                    prefs.access = value.text;
                                else if (key == "HoverHeight")
                                    framework::parse_invariant_float(value.text, prefs.hover);
                                else if (key == "Language")
                                    prefs.language = value.text;
                                else if (key == "LanguageIsPublic")
                                    prefs.language_public =
                                        value.text == "1" ||
                                        (!value.text.empty() &&
                                         (value.text.front() == 't' || value.text.front() == 'T'));
                                else if (key == "PermEveryone")
                                    prefs.everyone = parse_number(value.text);
                                else if (key == "PermGroup")
                                    prefs.group = parse_number(value.text);
                                else if (key == "PermNextOwner")
                                    prefs.next_owner = parse_number(value.text);
                            }
                        } catch (const std::exception& e) {
                            error_log(std::string("Agent preferences request failed: ") + e.what());
                            return scene::AgentPreferences{};
                        }
                        return prefs;
                    };
                    policies.save_preferences = [request, endpoint,
                                                 &error_log](framework::Uuid agent,
                                                             const scene::AgentPreferences& prefs) {
                        try {
                            char buffer[64];
                            const auto hover = std::to_chars(buffer, buffer + sizeof buffer, prefs.hover);
                            const auto reply = network::parse_service_xml(request(
                                endpoint, network::build_service_form(
                                              {{"METHOD", "setagentprefs"},
                                               {"PrincipalID", agent.str()},
                                               {"AccessPrefs", prefs.access},
                                               {"HoverHeight", std::string(buffer, hover.ptr)},
                                               {"Language", prefs.language},
                                               {"LanguageIsPublic", prefs.language_public ? "True" : "False"},
                                               {"PermEveryone", std::to_string(prefs.everyone)},
                                               {"PermGroup", std::to_string(prefs.group)},
                                               {"PermNextOwner", std::to_string(prefs.next_owner)}})));
                            if (std::none_of(reply.fields.begin(), reply.fields.end(), [](const auto& field) {
                                    return field.first == "result" && framework::ordinal_equals_ignore_case(
                                                                          field.second.text, "Success");
                                }))
                                error_log("Remote agent preferences service rejected the update");
                        } catch (const std::exception& e) {
                            error_log(std::string("Agent preferences update failed: ") + e.what());
                        }
                    };
                }
                if (local_preferences_enabled) {
                    policies.load_preference_language = {};
                    policies.load_preferences = [load_local_preferences](auto agent) {
                        return load_local_preferences(agent).value_or(scene::AgentPreferences{});
                    };
                    policies.save_preferences = save_local_preferences;
                }
                policies.baked_texture_cache = [&region_assets] {
                    return region_assets ? region_assets->asset_cache() : nullptr;
                };
                auto bakes_url = config.get("XBakes", "URL");
                if (!bakes_url.empty()) {
                    bakes_workers =
                        std::make_unique<ProfileWorkers>([&error_log](const std::exception& error) {
                            error_log(std::string("XBakes fetch failed: ") + error.what());
                        });
                    bakes_upload_worker = std::make_unique<ProfileWorkers>(
                        [&error_log](const std::exception& error) {
                            error_log(std::string("XBakes upload failed: ") + error.what());
                        },
                        true);
                    policies.schedule_baked_fetch = [&bakes_workers](auto task) {
                        bakes_workers->enqueue(std::move(task));
                    };
                    if (bakes_url.back() != '/')
                        bakes_url += '/';
                    std::optional<std::pair<std::string, std::string>> auth;
                    if (config.get("XBakes", "AuthType", config.get("Network", "AuthType")) ==
                        "BasicHttpAuthentication")
                        auth.emplace(config.get("XBakes", "HttpAuthUsername",
                                                config.get("Network", "HttpAuthUsername")),
                                     config.get("XBakes", "HttpAuthPassword",
                                                config.get("Network", "HttpAuthPassword")));
                    policies.store_baked_appearance =
                        [worker = bakes_upload_worker.get(), url = bakes_url,
                         request = network::rest_forms_transport(outgoing_clients(), auth)](
                            framework::Uuid agent, const framework::WearableCacheItems& cache) {
                            worker->enqueue([request, endpoint = url + "bakes/" + agent.str(), cache] {
                                request(endpoint, services::serialize_baked_appearance(cache));
                            });
                        };
                    policies.fetch_baked_appearance = [url = std::move(bakes_url),
                                                       clients = outgoing_clients(),
                                                       auth](framework::Uuid agent) {
                        const auto uris = framework::native_config_uri_object_backend();
                        auto uri = uris->create(framework::nini_writer_text(
                                                    framework::utf16_replacing(url + "bakes/" + agent.str())),
                                                framework::ConfigUriObjectKind::Absolute);
                        framework::ConfigXmlHttpOperation operation;
                        if (auth) {
                            const auto credentials = auth->first + ":" + auth->second;
                            network::Value bytes;
                            bytes.binary_data = std::make_shared<std::vector<std::uint8_t>>(
                                credentials.begin(), credentials.end());
                            operation.headers.emplace_back("Authorization",
                                                           framework::nini_writer_text(framework::utf16(
                                                               "Basic " + network::binary_base64(bytes))));
                        }
                        auto stream = clients->get_new_global_client(5000)->send_headers(
                            std::move(uri), std::move(operation), true);
                        const auto body = stream->copy_to_memory();
                        stream->close();
                        return services::deserialize_baked_appearance(
                            {reinterpret_cast<const char*>(body.data()), body.size()});
                    };
                }
                policies.target_poll_ms = region_poll_interval.count();
                float scene_frame_time = 0.f;
                if (framework::parse_invariant_float(config.get("Startup", "FrameTime"), scene_frame_time) &&
                    std::isfinite(scene_frame_time) && scene_frame_time > 0.f)
                    policies.scene_frame_time = scene_frame_time;
                if (const auto wind = config.native_section("Wind"); wind && config.boolean("Wind", "enabled", true)) {
                    using Model = scene::RegionPolicies::Wind::Model;
                    const auto plugin = config.get("Wind", "wind_plugin", "SimpleRandomWind");
                    policies.wind.update_frames = std::max(1, config.integer("Wind", "wind_update_rate", 150));
                    if (plugin == "SimpleRandomWind") {
                        policies.wind.model = Model::SimpleRandom;
                        policies.wind.strength = wind->get_float(framework::nini_writer_text(u"strength"), 1.f);
                    } else if (plugin == "ConfigurableWind") {
                        policies.wind.model = Model::Configurable;
                        // The managed plugin parses the legacy key before replacing it with avg_strength.
                        wind->get_float(framework::nini_writer_text(u"strength"), 5.f);
                        policies.wind.average_strength = wind->get_float(framework::nini_writer_text(u"avg_strength"), 5.f);
                        policies.wind.average_direction = wind->get_float(framework::nini_writer_text(u"avg_direction"), 0.f);
                        policies.wind.strength_variation = wind->get_float(framework::nini_writer_text(u"var_strength"), 5.f);
                        policies.wind.direction_variation = wind->get_float(framework::nini_writer_text(u"var_direction"), 30.f);
                        policies.wind.rate = wind->get_float(framework::nini_writer_text(u"rate_change"), 1.f);
                    } else {
                        error_log("[WIND] Could not find specified wind plug-in: " + plugin);
                        error_log("[WIND] Defaulting to no wind.");
                    }
                }
                policies.update_objects_every_n_frames =
                    config.integer("Startup", "UpdateObjectsEveryNFrames", 1);
                if (config.boolean("Startup", "Normalized55FPS", true))
                    policies.statistics_fps_factor =
                        55.f * std::chrono::duration<float>(region_poll_interval).count();
                // Match the default LandData flags used when a region has no saved parcels.
                constexpr std::uint32_t default_parcel_flags =
                    0x1u | 0x8u | 0x8000000u | 0x2000u | 0x40u | 0x2u | 0x20000000u;
                region_parcels.default_parcel = std::make_shared<const scene::ScriptParcel>(
                            scene::ScriptParcel{region.id,
                                                region.estate_owner,
                                                default_parcel_flags,
                                                size * size,
                                                {},
                                                "Your Parcel",
                                                {},
                                                0,
                                                0,
                                                {},
                                                {},
                                                {size / 2., size / 2., 0}});
                policies.script_parcel_at = [&, size](
                        framework::Vector3 position) -> std::shared_ptr<const scene::ScriptParcel> {
                    if (!(position.x >= 0 && position.x < size && position.y >= 0 && position.y < size))
                        return {};
                    std::lock_guard lock(region_parcels.parcels_mutex);
                    if (!region_parcels.persisted_parcels)
                        return region_parcels.default_parcel;
                    const auto x = static_cast<unsigned>(position.x) / 4;
                    const auto y = static_cast<unsigned>(position.y) / 4;
                    return (*region_parcels.persisted_parcels)[y * (size / 4) + x];
                };
                policies.list_parcels = [&] {
                    std::lock_guard lock(region_parcels.parcels_mutex);
                    if (!region_parcels.persisted_parcels)
                        return std::vector{region_parcels.default_parcel};
                    return region_parcels.persisted_parcel_records;
                };
                const auto update_parcel = [&](std::shared_ptr<const scene::ScriptParcel> parcel, bool persist,
                                               ParcelSave change) {
                    if (persist && region_parcels.save_parcel)
                        region_parcels.save_parcel(*parcel, !region_parcels.persisted_parcels, change);
                    std::lock_guard lock(region_parcels.parcels_mutex);
                    if (!region_parcels.persisted_parcels) {
                        region_parcels.default_parcel = std::move(parcel);
                        return;
                    }
                    for (auto& record : region_parcels.persisted_parcel_records)
                        if (record->id == parcel->id)
                            record = parcel;
                    for (auto& cell : *region_parcels.persisted_parcels)
                        if (cell && cell->id == parcel->id)
                            cell = parcel;
                };
                policies.update_parcel_access = [update_parcel](auto parcel, bool persist) {
                    update_parcel(std::move(parcel), persist, ParcelSave::Access);
                };
                policies.update_parcel_clean_time = [update_parcel](auto parcel) {
                    update_parcel(std::move(parcel), true, ParcelSave::CleanTime);
                };
                policies.update_parcel_music = [update_parcel](auto parcel) {
                    update_parcel(std::move(parcel), true, ParcelSave::Music);
                };
                policies.update_parcel_media = [update_parcel](auto parcel) {
                    update_parcel(std::move(parcel), true, ParcelSave::Media);
                };
                policies.update_parcel_properties = [update_parcel](auto parcel) {
                    update_parcel(std::move(parcel), true, ParcelSave::Properties);
                };
                policies.update_parcel_topology = [&, size](const ParcelCells& changed,
                                                            const std::vector<framework::Uuid>& removed) {
                    std::lock_guard lock(region_parcels.parcels_mutex);
                    auto records = region_parcels.persisted_parcels ? region_parcels.persisted_parcel_records : ParcelCells{region_parcels.default_parcel};
                    std::erase_if(records, [&](const auto& parcel) {
                        return std::ranges::find(removed, parcel->id) != removed.end() ||
                               std::ranges::any_of(changed, [&](const auto& edit) { return edit->id == parcel->id; });
                    });
                    records.insert(records.end(), changed.begin(), changed.end());
                    std::ranges::sort(records, {}, [](const auto& parcel) { return parcel->local_id; });
                    ParcelCells cells(static_cast<std::size_t>(size / 4) * (size / 4));
                    for (const auto& parcel : records)
                        for (std::size_t i = 0; i < cells.size(); ++i)
                            if (i / 8 < parcel->land_bitmap.size() && (parcel->land_bitmap[i / 8] & (1u << (i % 8))))
                                cells[i] = parcel;
                    if (region_parcels.save_parcel_topology)
                        region_parcels.save_parcel_topology(changed, removed);
                    region_parcels.persisted_parcel_records = std::move(records);
                    region_parcels.persisted_parcels = std::move(cells);
                };
                policies.update_parcel_ownership = [update_parcel](auto parcel, bool reset_claim) {
                    update_parcel(std::move(parcel), true, reset_claim ? ParcelSave::Reclaim : ParcelSave::Ownership);
                };
                policies.update_parcel_purchase = [update_parcel](auto parcel) {
                    update_parcel(std::move(parcel), true, ParcelSave::Purchase);
                };
                policies.mark_parcel_as_content = [update_parcel](auto parcel) {
                    update_parcel(std::move(parcel), true, ParcelSave::MarkContent);
                };
                policies.script_parcel_capacity =
                    [&, size, capacity = std::max(0, region_settings.integer("NativeRegion", "MaxPrims", 15000))](
                        std::int32_t area) {
                        const auto rounded =
                            area * static_cast<double>(capacity) * region_parcels.parcel_object_bonus / (size * size) + 0.5;
                        return static_cast<std::int32_t>(std::min(rounded, static_cast<double>(capacity)));
                    };
                policies.is_local_grid_user = [&, scope = region.scope](framework::Uuid agent) {
                    return accounts.find_by_id(scope.str(), agent.str()).has_value();
                };
                policies.script_account = [&, scope = region.scope](framework::Uuid agent) {
                    return accounts.find_by_id(scope.str(), agent.str());
                };
                policies.script_grid_user_online = [&](framework::Uuid agent) {
                    const auto user = agent.str();
                    const auto info = remote_grid_user ? remote_grid_user->info(user) : grid_user->info(user);
                    return info && info->online;
                };
                policies.script_account_key = services::make_script_user_key_lookup(accounts, outgoing_clients());
                policies.script_grid_region = services::make_script_grid_lookup(*grid, region.scope, remote_grid_request);
                const auto world_map = map_setting("WorldMapModule", "WorldMap");
                policies.map_layer_enabled = world_map == "WorldMap" || world_map == "HGWorldMap";
                const auto remote_map_regions = [&, scope = region.scope](services::OrderedRegionFields query) {
                    query.emplace_back("SCOPEID", scope.str());
                    const auto reply = remote_grid_request(query);
                    std::vector<services::GridRegion> results;
                    for (const auto& [key, value] : reply.fields) {
                        if (!key.starts_with("region") || !value.list)
                            continue;
                        services::RegionFields fields;
                        for (const auto& [field, item] : value.fields)
                            fields[field] = item.text;
                        results.push_back(services::GridRegion::from_fields(fields));
                    }
                    return results;
                };
                policies.map_region_search = [&, scope = region.scope, remote_map_regions](const std::string& name) {
                    constexpr int maximum = 20;
                    if (!remote_grid_request)
                        return grid->names(scope, name, maximum);
                    return remote_map_regions({{"METHOD", "get_regions_by_name"}, {"NAME", name},
                                               {"MAX", std::to_string(maximum)}});
                };
                if (policies.map_layer_enabled) {
                    policies.map_region_range = [&, scope = region.scope, remote_map_regions](
                                                    int min_x, int max_x, int min_y, int max_y) {
                        if (!remote_grid_request)
                            return grid->range(scope, min_x, max_x, min_y, max_y);
                        return remote_map_regions({{"METHOD", "get_region_range"},
                                                   {"XMIN", std::to_string(min_x)}, {"XMAX", std::to_string(max_x)},
                                                   {"YMIN", std::to_string(min_y)}, {"YMAX", std::to_string(max_y)}});
                    };
                }
                policies.set_home = [&](framework::Uuid agent, framework::Uuid region_id,
                                        framework::Vector3 position, framework::Vector3 look) {
                    return remote_grid_user ? remote_grid_user->set_home(agent.str(), region_id, position, look)
                                            : grid_user->set_home(agent.str(), region_id, position, look);
                };
                policies.persist_baked_terrain = [&](const scene::Terrain& terrain) {
                    if (region_persistence.save_baked_terrain)
                        region_persistence.save_baked_terrain(terrain);
                };
                policies.persist_terrain_texture_heights = [&](std::size_t corner, float low, float high) {
                    if (region_persistence.save_terrain_texture_heights)
                        region_persistence.save_terrain_texture_heights(corner, low, high);
                };
                policies.persist_terrain_texture_asset = [&](bool pbr, std::size_t corner, framework::Uuid asset) {
                    if (region_persistence.save_terrain_texture_asset)
                        region_persistence.save_terrain_texture_asset(pbr, corner, asset);
                };
                policies.persist_region_terrain_settings = [&](float water, float raise, float lower) {
                    if (region_persistence.save_region_terrain_settings)
                        region_persistence.save_region_terrain_settings(water, raise, lower);
                };
                policies.persist_region_sun_settings = [&](bool use_estate_sun) {
                    if (region_persistence.save_region_sun_settings)
                        region_persistence.save_region_sun_settings(use_estate_sun);
                };
                policies.persist_estate_region_settings = [&](const auto& settings) {
                    if (region_persistence.save_estate_region_settings)
                        region_persistence.save_estate_region_settings(settings);
                    region_parcels.parcel_object_bonus = settings.object_bonus;
                };
                policies.resolve_landmark = [&](const scene::ViewerAgent& viewer, framework::Uuid landmark,
                                                network::lludp::Packet& request) -> std::string {
                    framework::Uuid region_id;
                    framework::Vector3 position{}, look_at{};
                    std::optional<std::uint64_t> landmark_handle;
                    if (landmark.is_zero()) {
                        const auto user = viewer.circuit->agent_id.str();
                        const auto home = remote_grid_user ? remote_grid_user->info(user) : grid_user->info(user);
                        if (!home)
                            return "Your home region not found";
                        if (home->home_region.is_zero())
                            return "Home set not";
                        region_id = home->home_region;
                        position = home->home_position;
                        look_at = home->home_look_at;
                    } else {
                        constexpr auto missing = "Could not find the landmark asset data";
                        if (!region_assets)
                            return missing;
                        auto asset = region_assets->get(landmark);
                        if (!asset && viewer.circuit->service_urls) {
                            const auto& urls = *viewer.circuit->service_urls;
                            const auto entry = urls.find("AssetServerURI");
                            if (entry != urls.end())
                                if (const auto uri = std::any_cast<std::string>(&entry->second))
                                    asset = region_assets->get_foreign(landmark, *uri, false);
                        }
                        if (!asset || !asset->data || asset->data->empty())
                            return missing;
                        try {
                            const auto parsed = script::parse_landmark(asset->data);
                            region_id = parsed.region;
                            position = parsed.position;
                            landmark_handle = parsed.handle;
                        } catch (const std::invalid_argument&) {
                            return missing;
                        }
                    }
                    auto destination = grid->by_id({}, region_id);
                    if (!destination)
                        destination = lookup_remote_region(
                            {}, {{"REGIONID", region_id.str()}, {"METHOD", "get_region_by_uuid"}});
                    if (!destination)
                        return landmark.is_zero() ? "Home region not found" : "Landmark region not found";
                    if (landmark_handle) {
                        const double x = double(static_cast<std::uint32_t>(*landmark_handle >> 32)) -
                                         destination->x + position.x;
                        const double y = double(static_cast<std::uint32_t>(*landmark_handle)) -
                                         destination->y + position.y;
                        if (x >= 0 && x < destination->size_x && y >= 0 && y < destination->size_y) {
                            position.x = static_cast<float>(x);
                            position.y = static_cast<float>(y);
                        }
                    }
                    auto& info = request.block("Info");
                    info["RegionHandle"] = destination->handle();
                    info["Position"] = std::vector<double>{position.x, position.y, position.z};
                    info["LookAt"] = std::vector<double>{look_at.x, look_at.y, look_at.z};
                    return {};
                };
                policies.grid_region_at = [&, lookup_remote_region, scope = region.scope](std::int32_t x, std::int32_t y) {
                    if (!remote_grid_request)
                        return grid->by_position(scope, x, y).has_value();
                    return lookup_remote_region(scope, {{"X", std::to_string(x)}, {"Y", std::to_string(y)},
                                                        {"METHOD", "get_region_by_position"}}).has_value();
                };
                policies.teleport_destination = [&, lookup_remote_region, scope = region.scope](std::uint64_t handle) {
                    auto destination = grid->by_handle(scope, handle);
                    if (destination)
                        return destination;
                    const auto x = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(handle >> 32));
                    const auto y = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(handle));
                    return lookup_remote_region(scope, {{"X", std::to_string(x)},
                                                        {"Y", std::to_string(y)},
                                                        {"METHOD", "get_region_by_position"}});
                };
                policies.teleport_destination_by_name = [&, lookup_remote_region, scope = region.scope](
                                                            const std::string& name) {
                    auto destination = grid->by_name(scope, name);
                    return destination ? destination : lookup_remote_region(
                        scope, {{"NAME", name}, {"METHOD", "get_region_by_name"}});
                };
                if (policies.map_layer_enabled) {
                    policies.map_item_failure_seconds = map_integer("BlacklistTimeout", 600);
                    using MapFailureCache = framework::ExpiringCacheOS<std::string, bool>;
                    auto failed_map_servers = std::make_shared<MapFailureCache>(1000);
                    auto failed_map_urls = std::make_shared<MapFailureCache>(1000);
                    policies.request_remote_map_items = [&, locate = policies.teleport_destination,
                        failed_map_servers, failed_map_urls, failure_seconds = policies.map_item_failure_seconds](
                            std::uint64_t handle) {
                        const auto region = locate(handle);
                        if (!region || region->server_uri().empty())
                            throw std::runtime_error("Map item destination is unavailable");
                        const auto server = region->server_uri();
                        const auto url = server + "MAP/MapItems/" + std::to_string(handle);
                        if (failed_map_servers->contains_key(server) || failed_map_urls->contains_key(url))
                            throw std::runtime_error("Map item destination is cooling down after a failure");
                        framework::ConfigUriObjectPtr uri;
                        try {
                            uri = framework::native_config_uri_object_backend()->create(
                                framework::nini_writer_text(framework::utf16_replacing(url)),
                                framework::ConfigUriObjectKind::Absolute);
                        } catch (const std::exception&) {
                            failed_map_servers->add(server, true, 120000);
                            throw;
                        }
                        framework::ConfigXmlHttpOperation operation;
                        operation.headers.emplace_back("Content-Type", framework::nini_writer_text(u"application/xml+llsd"));
                        std::vector<std::uint8_t> body;
                        try {
                            auto stream = outgoing_clients()->get_new_global_client(5000)->send_headers(
                                std::move(uri), std::move(operation), true);
                            body = stream->copy_to_memory();
                            stream->close();
                        } catch (const framework::ConfigXmlHttpError&) {
                            failed_map_servers->add(server, true, 60000);
                            if (failure_seconds > 0)
                                failed_map_urls->add_or_update(url, true, static_cast<double>(failure_seconds));
                            throw;
                        }
                        return network::deserialize_sdk_llsd_xml(
                            {reinterpret_cast<const char*>(body.data()), body.size()});
                    };
                }
                if (config.boolean("NativeServer", "EnableSimulationEndpoint", false)) {
                    policies.send_crossing_object = [&](auto& destination, auto position, auto object) {
                        if (local_regions.find(destination.id))
                            return local_regions.create_object(&destination, position, std::move(object), true);
                        return simulation_connector->create_object(destination, position, std::move(object), true);
                    };
                }
                policies.allow_script_crossing = config.boolean("Startup", "AllowScriptCrossing", true);
                policies.allow_avatar_crossing = config.boolean("EntityTransfer", "AllowAvatarCrossing", true);
                policies.parcel_region_by_id = [&, scope = region.scope](framework::Uuid id) {
                    auto destination = grid->by_id(scope, id);
                    return destination ? destination : lookup_remote_region(
                        scope, {{"REGIONID", id.str()}, {"METHOD", "get_region_by_uuid"}});
                };
                region_lookup_workers = std::make_unique<ProfileWorkers>(
                    [&](const std::exception& failure) { error_log("Region lookup: " + std::string(failure.what())); });
                policies.schedule_region_lookup = [&](auto task) {
                    region_lookup_workers->enqueue(std::move(task));
                };
                policies.lookup_region_handle = [&](framework::Uuid id) {
                    auto destination = grid->by_id({}, id);
                    if (!destination)
                        destination = lookup_remote_region({}, {{"REGIONID", id.str()}, {"METHOD", "get_region_by_uuid"}});
                    return destination ? destination->handle() : std::uint64_t{0};
                };
                if (config.get("Modules", "LandServices") == "RemoteLandServicesConnector") {
                    policies.remote_parcel_data = [&](const services::GridRegion& destination, std::uint64_t handle,
                                                      std::uint32_t x, std::uint32_t y) {
                        const auto target_handle = static_cast<std::uint32_t>(handle) == 0
                            ? destination.region_secret.value_or("") : std::to_string(destination.handle());
                        auto parameters = std::make_shared<network::NwcXmlRpcRequest::Parameters>();
                        parameters->push_back(std::make_shared<network::Value>(network::Value::object({
                            {"region_handle", target_handle}, {"x", std::to_string(x)}, {"y", std::to_string(y)},
                        })));
                        auto request = std::make_shared<network::NwcXmlRpcRequest>(u"land_data", parameters);
                        network::NwcXmlRpcHttpTransport transport(outgoing_clients());
                        const auto reply = transport.send(
                            request, framework::utf16_replacing(destination.server_uri()), 10000);
                        return reply && !reply->is_fault && reply->value ? *reply->value : network::Value{};
                    };
                }
                policies.teleport_arrived = [&](const framework::Uuid& session,
                                                const framework::Uuid& destination) {
                    if (remote_region_presence) {
                        const auto record = remote_region_presence->agent(session);
                        return record && record->region_id == destination;
                    }
                    const auto record = presence_store->get(session);
                    return record && record->region_id == destination;
                };
                policies.cancel_teleport_destination = [&](auto& destination, auto agent, auto session) {
                    if (auto local = local_regions.find(destination.id))
                        local->close_agent_session(agent, session.str());
                    else
                        simulation_connector->close_agent(destination, agent, session.str());
                };
                const bool disable_teleport_cancellation =
                    config.boolean("EntityTransfer", "DisableInterRegionTeleportCancellation", false);
                policies.disable_teleport_cancellation = disable_teleport_cancellation;
                policies.release_teleport_source = [&](const std::string& uri) {
                    const auto authority = uri.find("://");
                    const auto path_start = authority == std::string::npos
                        ? std::string::npos : uri.find('/', authority + 3);
                    if (path_start != std::string::npos) {
                        const std::string_view target = std::string_view(uri).substr(path_start);
                        server::AgentPathParameters path;
                        if (server::agent_path_parameters(target.substr(0, target.find('?')), path) &&
                            path.action == "release") {
                            if (auto source = local_regions.find(path.region)) {
                                const auto descriptor = source->descriptor();
                                const auto registered = grid->by_id(descriptor.scope, path.region);
                                if (registered && uri.starts_with(registered->server_uri() + "agent/")) {
                                    const auto auth = server::agent_delete_auth(target);
                                    framework::Uuid token;
                                    if (auth && framework::try_parse_sdk_uuid(*auth, token))
                                        source->confirm_teleport_arrival(path.agent, token);
                                    return;
                                }
                            }
                        }
                    }
                    simulation_connector->release_agent({}, {}, uri);
                };
                policies.teleport_access = [&](services::GridRegion& destination,
                                               const scene::ViewerAgent& agent, framework::Vector3 position,
                                               bool via_teleport,
                                               services::LoginTransferContext& context) -> std::string {
                    if (!simulation_connector)
                        return {};
                    std::vector<framework::Uuid> features;
                    std::string reason;
                    std::optional<std::string> home;
                    if (agent.circuit->service_urls) {
                        const auto entry = agent.circuit->service_urls->find("HomeURI");
                        if (entry != agent.circuit->service_urls->end()) {
                            if (const auto* uri = std::any_cast<std::string>(&entry->second);
                                uri && !uri->empty())
                                home = *uri;
                        }
                    }
                    if (destination.flags.value_or(0) & services::Hyperlink) {
                        try {
                            auto fields =
                                network::Value::object({{"region_uuid", destination.id.str()},
                                                        {"agent_id", agent.circuit->agent_id.str()}});
                            if (home)
                                fields.set("agent_home_uri", *home);
                            auto params = std::make_shared<network::NwcXmlRpcRequest::Parameters>();
                            params->push_back(std::make_shared<network::Value>(std::move(fields)));
                            auto request = std::make_shared<network::NwcXmlRpcRequest>(u"get_region", params);
                            network::NwcXmlRpcHttpTransport transport(outgoing_clients());
                            auto reply = transport.send(
                                request, framework::utf16_replacing(destination.server_uri()), 10000);
                            if (!reply || reply->is_fault || !reply->value)
                                return "Error contacting grid.";
                            const auto& result = *reply->value;
                            const auto string_field = [&](std::string_view key) {
                                const auto kind = result.get(key).view().kind;
                                return kind == network::Value::Kind::String ||
                                       kind == network::Value::Kind::Undefined;
                            };
                            if (result.view().kind != network::Value::Kind::Map ||
                                !string_field("result") || !string_field("message"))
                                return "Error parsing response from grid.";
                            if (!framework::ordinal_equals_ignore_case(
                                    framework::trim_utf8(result.get("result").as_string()), "true")) {
                                const auto message = result.get("message").as_string();
                                return message.empty() ? "The teleport destination could not be found." : message;
                            }
                            services::GridRegion remote_region;
                            try {
                                const auto read_text = [&](std::string_view key) -> std::optional<std::string> {
                                    if (!string_field(key))
                                        throw std::invalid_argument("Invalid Gatekeeper region field");
                                    const auto& field = result.get(key);
                                    if (field.view().kind == network::Value::Kind::Undefined)
                                        return std::nullopt;
                                    return field.as_string();
                                };
                                const auto read_integer = [&](std::string_view key, std::int32_t& target) {
                                    if (const auto value = read_text(key))
                                        services::parse_account_integer(*value, target);
                                };
                                if (const auto id = read_text("uuid"))
                                    framework::try_parse_sdk_uuid(*id, remote_region.id);
                                read_integer("x", remote_region.x);
                                read_integer("y", remote_region.y);
                                read_integer("size_x", remote_region.size_x);
                                read_integer("size_y", remote_region.size_y);
                                remote_region.name = read_text("region_name");
                                remote_region.external_host = read_text("hostname");
                                if (const auto port = read_text("http_port"))
                                    framework::try_parse_unsigned_integer(*port, remote_region.http_port);
                                if (const auto port = read_text("internal_port")) {
                                    std::int32_t internal_port;
                                    services::parse_account_integer(*port, internal_port);
                                    if (internal_port < 0 || internal_port > 65535)
                                        throw std::out_of_range("Invalid Gatekeeper region port");
                                    remote_region.internal_endpoint = services::RegionEndpoint{
                                        "0.0.0.0", static_cast<std::uint16_t>(internal_port)};
                                }
                                if (const auto uri = read_text("server_uri"))
                                    remote_region.set_server_uri(*uri);
                            } catch (const std::exception&) {
                                return "Error parsing response from grid.";
                            }
                            if (remote_region.id.is_zero())
                                return "Unable to locate destination";
                            remote_region.flags = destination.flags;
                            destination = std::move(remote_region);
                        } catch (const std::exception&) {
                            return "Error contacting grid.";
                        }
                    }
                    if (!destination.internal_endpoint ||
                        !framework::host_resolver().endpoint(destination.external_host,
                                                             destination.internal_endpoint->port))
                        return "Could not resolve destination Address";
                    services::ISimulationLogin& admission = local_regions.find(destination.id)
                        ? static_cast<services::ISimulationLogin&>(local_regions) : *simulation_connector;
                    if (admission.query_access(destination, agent.circuit->agent_id, home, via_teleport,
                                               position, features, context, reason))
                        return {};
                    return reason.empty() ? "Destination region denied access" : reason;
                };
                policies.teleport_create =
                    [&, simulation_requester, disable_teleport_cancellation](
                        const services::GridRegion& source, services::GridRegion& gatekeeper,
                        services::GridRegion& destination, const scene::ViewerAgent& agent,
                        framework::Vector3 position, framework::Vector3 look_at,
                        services::LoginTransferContext& context, const framework::Uuid& token,
                        std::uint32_t requested_flags, const framework::AgentData* script_snapshot,
                        std::string& reason) -> std::optional<network::lludp::Packet> {
                    const bool crossing = token.is_zero();
                    const bool foreign = (gatekeeper.flags.value_or(0) & services::Hyperlink) != 0;
                    auto local_destination = foreign ? nullptr : local_regions.find(destination.id);
                    if (!foreign && !local_destination && (!simulation_connector ||
                                     (!remote_region_presence &&
                                      (!presence_store || !presence_store->persistent())) ||
                                     agent.circuit->verified_foreign_login))
                        return std::nullopt;
                    auto* home_agent = login_user_agent.get();
                    std::unique_ptr<services::UserAgentServiceConnector> visitor_home_agent;
                    if (foreign && agent.circuit->verified_foreign_login) {
                        const auto& urls = agent.circuit->service_urls;
                        if (!urls)
                            return std::nullopt;
                        const auto entry = urls->find("HomeURI");
                        const auto* home =
                            entry == urls->end() ? nullptr : std::any_cast<std::string>(&entry->second);
                        if (!home || home->empty())
                            return std::nullopt;
                        services::UserAgentConnectorServices connector;
                        auto transport =
                            std::make_shared<network::NwcXmlRpcHttpTransport>(outgoing_clients());
                        connector.client = [transport](std::int32_t timeout) {
                            return network::xml_rpc_http_client(transport, timeout);
                        };
                        connector.debug = [&console_log](const auto& message) {
                            console_log.append(framework::console_log_debug, message);
                        };
                        connector.error = connector.warn = [&console_log](const auto& message) {
                            console_log.append(framework::console_log_warn, message);
                        };
                        connector.simulation_request =
                            [simulation_requester](const auto& uri, const auto& data, const auto& method,
                                                   bool compressed, bool keepalive) {
                                return simulation_requester->send(framework::utf16_replacing(uri), &data,
                                                                  framework::utf16_replacing(method), 30000,
                                                                  compressed, false, keepalive);
                            };
                        visitor_home_agent = std::make_unique<services::UserAgentServiceConnector>(
                            framework::utf16_replacing(*home), std::move(connector));
                        home_agent = visitor_home_agent.get();
                    }
                    const bool native_home =
                        foreign && !home_agent && travel_store && !agent.circuit->verified_foreign_login;
                    if (foreign && !home_agent && !native_home)
                        return std::nullopt;
                    const auto endpoint =
                        destination.internal_endpoint
                            ? framework::host_resolver().endpoint(destination.external_host,
                                                                  destination.internal_endpoint->port)
                            : nullptr;
                    if (!endpoint || endpoint->address->v6()) {
                        reason = "Could not resolve destination Address";
                        return std::nullopt;
                    }
                    auto circuit = std::make_shared<framework::AgentCircuitData>(*agent.circuit);
                    circuit->appearance = agent.appearance();
                    circuit->active_group_id = agent.active_group_id();
                    circuit->start_position = position;
                    circuit->caps_path = framework::Uuid::random().str();
                    if (crossing) {
                        using Seeds = framework::AgentCircuitData::ChildrenSeeds;
                        circuit->children_cap_seeds = agent.circuit->children_cap_seeds
                            ? std::make_shared<Seeds>(*agent.circuit->children_cap_seeds)
                            : std::make_shared<Seeds>();
                        auto& seeds = *circuit->children_cap_seeds;
                        const auto known = seeds.find(destination.handle());
                        if (known != seeds.end() && known->second && !known->second->empty())
                            circuit->caps_path = known->second;
                        seeds[source.handle()] = agent.circuit->caps_path;
                        seeds[destination.handle()] = circuit->caps_path;
                    }
                    circuit->child = false;
                    const std::uint32_t flags = crossing ? 0u :
                        requested_flags | (agent.flying ? 8192u : 0u) |
                        (disable_teleport_cancellation ? 2048u : 0u);
                    auto advertised_source = grid->by_id(source.scope, source.id).value_or(source);
                    std::unique_lock travel_lock(travel_launch_mutex, std::defer_lock);
                    std::optional<services::TravelRecord> previous_travel;
                    const auto rollback_travel = [&] {
                        if (!native_home)
                            return;
                        if (previous_travel)
                            travel_store->store(*previous_travel);
                        else
                            travel_store->erase(circuit->session_id);
                    };
                    bool accepted;
                    if (native_home) {
                        const auto account = accounts.find_by_id(source.scope.str(), circuit->agent_id.str());
                        if (!account) {
                            reason = "Forbidden to launch your agents from here";
                            return std::nullopt;
                        }
                        if (!travel_allowed(account->user_level, gatekeeper.server_uri())) {
                            reason = "Your world does not allow you to visit the destination";
                            return std::nullopt;
                        }
                        const auto home_uri = config.get(
                            "Startup", "HomeURI",
                            config.get("Hypergrid", "HomeURI", config.get("LoginService", "SRV_HomeURI")));
                        if (!home_uri.empty()) {
                            circuit->service_urls =
                                circuit->service_urls
                                    ? std::make_shared<framework::AgentCircuitData::ServiceUrls>(
                                          *circuit->service_urls)
                                    : std::make_shared<framework::AgentCircuitData::ServiceUrls>();
                            (*circuit->service_urls)["HomeURI"] = home_uri;
                        }
                        travel_lock.lock();
                        auto launch = destination;
                        launch.raw_server_uri = gatekeeper.server_uri();
                        previous_travel = travel_store->replaced_by(circuit->session_id, circuit->agent_id);
                        circuit->service_session_id =
                            launch.server_uri() + ";" + framework::Uuid::random().str();
                        if (previous_travel && previous_travel->session_id == circuit->session_id &&
                            !previous_travel->client_ip.empty())
                            circuit->ip_address = previous_travel->client_ip;
                        if (circuit->ip_address.value_or("").empty())
                            circuit->ip_address = agent.endpoint.address;
                        travel_store->store({circuit->session_id,
                                             circuit->agent_id,
                                             launch.server_uri(),
                                             *circuit->service_session_id,
                                             circuit->ip_address.value_or(""),
                                             {}});
                        try {
                            services::GatekeeperSimulationConnector connector(simulation_requester);
                            accepted = connector.create_agent(&advertised_source, launch, circuit, 1u << 7,
                                                              context, reason);
                        } catch (...) {
                            rollback_travel();
                            throw;
                        }
                        if (!accepted)
                            rollback_travel();
                    } else if (local_destination) {
                        accepted = local_destination->create_agent(&advertised_source, destination,
                                                                   circuit, flags, context, reason);
                    } else {
                        accepted =
                            foreign ? home_agent->login_agent_to_grid(&advertised_source, circuit, gatekeeper,
                                                                      destination, false, reason)
                                    : simulation_connector->create_agent(&advertised_source, destination,
                                                                         circuit, flags, context, reason);
                    }
                    if (!accepted)
                        return std::nullopt;
                    auto update = std::make_shared<framework::AgentData>();
                    update->agent_id = circuit->agent_id;
                    update->session_id = circuit->session_id;
                    update->circuit_code = circuit->circuit_code;
                    update->region_id = source.id;
                    update->position = position;
                    update->center = agent.camera_position;
                    update->up_axis = agent.camera_up_axis;
                    update->velocity = agent.velocity;
                    update->body_rotation = agent.rotation;
                    update->always_run = agent.always_run;
                    if (script_snapshot) {
                        update->control_flags = script_snapshot->control_flags;
                        update->attachment_objects = script_snapshot->attachment_objects;
                        update->attachment_object_states = script_snapshot->attachment_object_states;
                        update->controllers = script_snapshot->controllers;
                        update->animations = script_snapshot->animations;
                        update->default_animation = script_snapshot->default_animation;
                        update->animation_state = script_snapshot->animation_state;
                        update->motion_state = script_snapshot->motion_state;
                        update->movement_animation_overrides = script_snapshot->movement_animation_overrides;
                        update->cross_extra_flags = script_snapshot->cross_extra_flags;
                    } else {
                        region_runtime->snapshot_attachments(agent, *update);
                    }
                    if (crossing)
                        update->crossing_flags = 1;
                    if (crossing && agent.seat) {
                        update->parent_part = agent.seat->part;
                        update->sit_offset = agent.seat->offset;
                        update->body_rotation = agent.seat->rotation;
                        update->crossing_flags = agent.seat->explicit_target ? 7 : 3;
                    }
                    update->active_group_id = circuit->active_group_id;
                    update->god_data = std::make_shared<network::Value>(network::Value::object(
                        {{"ViewerUiIsGod", network::Value(region_runtime->viewer_god_mode(
                                               circuit->agent_id, circuit->session_id))}}));
                    if (crossing) {
                        update->at_axis = agent.camera_at_axis;
                        update->left_axis = agent.camera_left_axis;
                    } else {
                        update->set_look_at(look_at);
                    }
                    if (!crossing)
                        update->new_callback_uri = advertised_source.server_uri() + "agent/" +
                                                   circuit->agent_id.str() + "/" + source.id.str() +
                                                   "/release/?auth=" + token.str();
                    const bool updated = local_destination
                        ? local_destination->update_agent(destination, update, context)
                        : simulation_connector->update_agent(destination, update, context);
                    if (!updated) {
                        if (local_destination)
                            local_destination->close_agent_session(circuit->agent_id, circuit->session_id.str());
                        else
                            simulation_connector->close_agent(destination, circuit->agent_id,
                                                              circuit->session_id.str());
                        rollback_travel();
                        reason = "Destination did not accept the agent update";
                        return std::nullopt;
                    }
                    const auto seed = destination.server_uri() + "CAPS/" + *circuit->caps_path + "0000/";
                    if (crossing) {
                        auto packet = network::lludp::make_packet("CrossedRegion");
                        auto& identity = packet.block("AgentData");
                        identity["AgentID"] = circuit->agent_id;
                        identity["SessionID"] = circuit->session_id;
                        auto& region = packet.block("RegionData");
                        region["SimIP"] = std::uint64_t(endpoint->address->ipv4_address());
                        region["SimPort"] = std::uint64_t(endpoint->port);
                        region["RegionHandle"] = destination.handle();
                        region["SeedCapability"] =
                            std::vector<std::uint8_t>(seed.c_str(), seed.c_str() + seed.size() + 1);
                        auto& info = packet.block("Info");
                        info["Position"] = std::vector<double>{position.x, position.y, position.z};
                        info["LookAt"] = std::vector<double>{agent.velocity.x, agent.velocity.y, agent.velocity.z};
                        return packet;
                    }
                    auto finish = network::lludp::make_packet("TeleportFinish");
                    auto& info = finish.block("Info");
                    info["AgentID"] = circuit->agent_id;
                    info["LocationID"] = std::uint64_t{4};
                    info["SimIP"] = std::uint64_t(endpoint->address->ipv4_address());
                    info["SimPort"] = std::uint64_t(endpoint->port);
                    info["RegionHandle"] =
                        (std::uint64_t(std::uint32_t(destination.x)) << 32) | std::uint32_t(destination.y);
                    info["SeedCapability"] =
                        std::vector<std::uint8_t>(seed.c_str(), seed.c_str() + seed.size() + 1);
                    info["SimAccess"] = std::uint64_t(destination.access);
                    info["TeleportFlags"] = std::uint64_t(flags);
                    return finish;
                };
                std::function<std::pair<bool, std::string>(const scene::RegionInstantMessage&)> store_offline;
                if (config.get("Messaging", "OfflineMessageModule") == "Offline Message Module V2") {
                    auto url = config.get("Messaging", "OfflineMessageURL");
                    if (!url.empty() || local_offline_im) {
                        while (!url.empty() && url.back() == '/')
                            url.pop_back();
                        std::optional<std::pair<std::string, std::string>> auth;
                        if (config.get("Messaging", "AuthType", config.get("Network", "AuthType")) ==
                            "BasicHttpAuthentication")
                            auth.emplace(config.get("Messaging", "HttpAuthUsername",
                                                    config.get("Network", "HttpAuthUsername")),
                                         config.get("Messaging", "HttpAuthPassword",
                                                    config.get("Network", "HttpAuthPassword")));
                        network::RestFormsRequest request;
                        if (local_offline_im)
                            request = [&](const auto&, const auto& body) {
                                return offline_im_endpoint->process(body);
                            };
                        else
                            request = network::rest_forms_transport(outgoing_clients(), auth);
                        store_offline = [request, endpoint = url + "/offlineim"](
                                            const scene::RegionInstantMessage& message) {
                            std::string bucket;
                            constexpr char digits[] = "0123456789ABCDEF";
                            for (auto byte : message.bucket) {
                                if (!bucket.empty())
                                    bucket += ' ';
                                bucket += digits[byte >> 4];
                                bucket += digits[byte & 15];
                            }
                            try {
                                auto result = network::parse_service_xml(request(
                                    endpoint, network::build_service_form(
                                                  {{"BinaryBucket", bucket},
                                                   {"Dialog", std::to_string(message.dialog)},
                                                   {"FromAgentID", message.sender.str()},
                                                   {"FromAgentName", framework::utf8(message.sender_name)},
                                                   {"FromGroup", message.from_group ? "True" : "False"},
                                                   {"SessionID", message.session.str()},
                                                   {"Message", framework::utf8(message.message)},
                                                   {"Offline", std::to_string(message.offline)},
                                                   {"EstateID", std::to_string(message.estate)},
                                                   {"Position", framework::vector3_string(message.position)},
                                                   {"RegionID", message.region.str()},
                                                   {"Timestamp", std::to_string(message.timestamp)},
                                                   {"ToAgentID", message.recipient.str()},
                                                   {"METHOD", "STORE"}})));
                                std::string failure = "Unknown error";
                                bool denied = false;
                                for (const auto& [name, value] : result.fields) {
                                    if (name == "RESULT")
                                        denied = value.text == "NULL" ||
                                                 framework::ordinal_equals_ignore_case(value.text, "false");
                                    else if (name == "REASON")
                                        failure = value.text;
                                }
                                return std::pair{!denied, denied ? failure : std::string{}};
                            } catch (const std::exception&) {
                                return std::pair{false, std::string("Bad response from server")};
                            }
                        };
                        policies.retrieve_offline_messages = [request, endpoint = url + "/offlineim"](
                                                                 framework::Uuid user) {
                            std::vector<scene::RegionInstantMessage> messages;
                            auto result = network::parse_service_xml(
                                request(endpoint, network::build_service_form(
                                                      {{"PrincipalID", user.str()}, {"METHOD", "GET"}})));
                            for (const auto& [key, list] : result.fields) {
                                if (key != "RESULT" || !list.list)
                                    continue;
                                for (const auto& [index, row] : list.fields) {
                                    if (!row.list)
                                        continue;
                                    const auto find_field = [&](std::string_view name) -> const std::string* {
                                        for (const auto& [key, value] : row.fields)
                                            if (key == name && !value.list)
                                                return &value.text;
                                        return nullptr;
                                    };
                                    const auto field = [&](std::string_view name) -> std::string_view {
                                        const auto* value = find_field(name);
                                        return value ? std::string_view(*value) : std::string_view{};
                                    };
                                    const auto number = [&](std::string_view name) {
                                        std::uint32_t value{};
                                        if (const auto* field_value = find_field(name)) {
                                            const auto trimmed = framework::trim_utf8(*field_value);
                                            std::string_view input(trimmed);
                                            if (input.starts_with('+'))
                                                input.remove_prefix(1);
                                            const auto end = input.data() + input.size();
                                            auto parsed = std::from_chars(input.data(), end, value);
                                            if (parsed.ec != std::errc{} || parsed.ptr != end)
                                                throw std::invalid_argument("Invalid offline message number");
                                        }
                                        return value;
                                    };
                                    scene::RegionInstantMessage message;
                                    const auto read_id = [&](std::string_view name, framework::Uuid& id) {
                                        const auto* value = find_field(name);
                                        if (value && !framework::try_parse_sdk_uuid(*value, id))
                                            throw std::invalid_argument("Invalid offline message UUID");
                                    };
                                    read_id("FromAgentID", message.sender);
                                    read_id("ToAgentID", message.recipient);
                                    read_id("SessionID", message.session);
                                    read_id("RegionID", message.region);
                                    message.sender_name = framework::utf16_replacing(field("FromAgentName"));
                                    message.message = framework::utf16_replacing(field("Message"));
                                    if (const auto* value = find_field("FromGroup")) {
                                        const auto flag = framework::trim_utf8(*value);
                                        message.from_group =
                                            framework::ordinal_equals_ignore_case(flag, "true");
                                        if (!message.from_group &&
                                            !framework::ordinal_equals_ignore_case(flag, "false"))
                                            throw std::invalid_argument("Invalid offline message group flag");
                                    }
                                    message.timestamp = number("Timestamp");
                                    message.estate = number("EstateID");
                                    const auto dialog = number("Dialog");
                                    const auto offline = number("Offline");
                                    if (dialog > 255 || offline > 255)
                                        throw std::invalid_argument("Invalid offline message byte");
                                    message.dialog = static_cast<std::uint8_t>(dialog);
                                    message.offline = static_cast<std::uint8_t>(offline);
                                    if (auto position = field("Position"); !position.empty())
                                        message.position = framework::parse_sdk_vector3(position);
                                    std::string hex;
                                    for (char digit : field("BinaryBucket")) {
                                        if ((digit >= '0' && digit <= '9') ||
                                            (digit >= 'a' && digit <= 'f') || (digit >= 'A' && digit <= 'F'))
                                            hex.push_back(digit);
                                    }
                                    std::string_view bucket(hex);
                                    while (bucket.size() >= 2) {
                                        unsigned value{};
                                        auto parsed =
                                            std::from_chars(bucket.data(), bucket.data() + 2, value, 16);
                                        if (parsed.ec != std::errc{} || parsed.ptr != bucket.data() + 2)
                                            break;
                                        message.bucket.push_back(static_cast<std::uint8_t>(value));
                                        bucket.remove_prefix(2);
                                    }
                                    messages.push_back(std::move(message));
                                }
                            }
                            return messages;
                        };
                    }
                }
                policies.basic_profile =
                    std::make_shared<scene::BasicProfileModule>([&console_log](auto text) {
                        console_log.append(framework::console_log_debug, std::u16string(text));
                    });
                policies.basic_profile->initialise(config.native_source());
                policies.basic_profile->post_initialise();
                class BasicAccounts final : public scene::BasicProfileAccounts {
                    services::AccountStore& accounts_;

                  public:
                    explicit BasicAccounts(services::AccountStore& accounts) : accounts_(accounts) {}
                    std::shared_ptr<scene::BasicProfileAccount> get(framework::Uuid scope,
                                                                    framework::Uuid avatar) override {
                        auto account = accounts_.find_by_id(scope.str(), avatar.str());
                        if (!account)
                            return {};
                        auto value = std::make_shared<scene::BasicProfileAccount>();
                        if (account->first_name)
                            value->first = framework::utf16_replacing(*account->first_name);
                        if (account->last_name)
                            value->last = framework::utf16_replacing(*account->last_name);
                        value->created = account->created;
                        return value;
                    }
                };
                policies.basic_profile_accounts = std::make_shared<BasicAccounts>(accounts);
                auto profile_url = config.get("UserProfiles", "ProfileServiceURL");
                if (!profile_url.empty()) {
                    framework::HttpUri uri(profile_url, true);
                    if (!uri.resolved())
                        throw std::runtime_error("UserProfileModule init error");
                    profile_grid_identity = std::make_unique<server::GridIdentity>(config, std::nullopt);
                    profile_workers =
                        std::make_unique<ProfileWorkers>([&console_log](const std::exception& error) {
                            console_log.append(framework::console_log_error,
                                               framework::utf16_replacing(error.what()));
                        });
                    scene::NativeProfileContextServices profile;
                    profile.scope = region.scope;
                    profile.profile_uri = framework::utf16_replacing(uri.uri);
                    profile.allow_web_urls = config.boolean("UserProfiles", "AllowUserProfileWebURLs", true);
                    profile.system_born = u"01/01/2007 00:00:00";
                    profile.grid = std::make_shared<scene::NativeProfileHomeGrid>(*profile_grid_identity);
                    profile.regions = std::make_shared<scene::NativeProfileGridService>(*grid);
                    profile.accounts = std::make_shared<scene::NativeProfileAccounts>(accounts);
                    profile.presence_service =
                        std::make_shared<scene::NativeProfilePresenceService>(*region_presence);
                    profile.users = std::make_shared<scene::NativeProfileUserDirectory>(
                        accounts, region.scope, [&region_runtime](auto id) {
                            return region_runtime ? region_runtime->profile_service_urls(id) : nullptr;
                        });
                    profile.presence = [&region_runtime](auto id) {
                        return region_runtime ? region_runtime->profile_presence(id) : nullptr;
                    };
                    profile.foreign_image = [&region_assets](auto image, const scene::ProfileText& uri,
                                                             bool local) {
                        if (region_assets && uri)
                            region_assets->get_foreign(image, framework::utf8(*uri), local);
                    };
                    profile.schedule = [&profile_workers](auto task) {
                        profile_workers->enqueue(std::move(task));
                    };
                    profile.log = [&console_log](auto message) {
                        console_log.append(framework::console_log_debug, std::u16string(message));
                    };
                    auto profile_transport =
                        std::make_shared<network::NwcXmlRpcHttpTransport>(outgoing_clients());
                    auto profile_rpc_client = [profile_transport](std::int32_t timeout) {
                        return network::xml_rpc_http_client(profile_transport, timeout);
                    };
                    profile.user_agent = [profile_rpc_client,
                                          log = profile.log](const scene::ProfileText& home) {
                        services::UserAgentConnectorServices services;
                        services.client = profile_rpc_client;
                        services.debug = services.error = services.warn = log;
                        return std::make_shared<scene::ConnectorProfileUserAgent>(home, std::move(services));
                    };
                    profile.legacy_profile = [profile_rpc_client, log = profile.log](auto& properties,
                                                                                     const auto& uri) {
                        services::OpenProfileClient client(
                            uri, [profile_rpc_client, log](const auto& body, auto method, const auto& url) {
                                network::XmlRpcRequesterServices services;
                                services.client = profile_rpc_client;
                                services.debug = services.error = services.info = log;
                                return network::XmlRpcRequester(std::move(services))
                                    .send_request(std::make_shared<network::Value>(body),
                                                  std::u16string(method), url);
                            });
                        return client.request_avatar_properties(properties);
                    };
                    advanced_profiles = std::make_shared<scene::UserProfileRequests>(
                        std::make_shared<scene::NativeProfileRequestContext>(std::move(profile)));
                    policies.advanced_profiles = advanced_profiles;
                }
                auto runtime_file = [&](const char* key, const std::filesystem::path& original,
                                        const std::filesystem::path& installed) {
                    auto explicit_file = region_settings.get("NativeRegion", key);
                    if (!explicit_file.empty()) {
                        auto path = framework::utf8_path(explicit_file);
                        return path.is_relative() ? config.directory / path : path;
                    }
                    for (const auto& relative : {original, installed}) {
                        auto path = config.directory / relative;
                        if (std::filesystem::is_regular_file(path))
                            return path;
                    }
                    auto root = executable_directory(args[0]);
                    for (int depth = 0; depth < 4; ++depth) {
                        for (const auto& relative : {installed, original}) {
                            auto path = root / relative;
                            if (std::filesystem::is_regular_file(path))
                                return path;
                        }
                        root = root.parent_path();
                    }
                    throw std::runtime_error(std::string("Configure NativeRegion.") + key +
                                             "; bundled runtime file was not found");
                };
                policies.animations = std::make_shared<scene::DefaultAnimations>(
                    runtime_file("DefaultAnimationsFile", "bin/data/avataranimations.xml",
                                 "share/opensim-native/avataranimations.xml"));
                if (config.boolean("NativeServer", "EnableRegionAssets", false)) {
#ifdef _WIN32
                    constexpr auto jpeg_library = "openjpeg-dotnet-x86_64.dll";
#elif defined(__APPLE__)
                    constexpr auto jpeg_library = "libopenjpeg-dotnet.dylib";
#elif defined(__aarch64__) || defined(__arm64__)
                    constexpr auto jpeg_library = "libopenjpeg-dotnet-arm64.so";
#else
                    constexpr auto jpeg_library = "libopenjpeg-dotnet-x86_64.so";
#endif
                    try {
                        const auto library =
                            runtime_file("TextureDecoderLibrary", std::filesystem::path("bin/lib64") / jpeg_library,
                                         std::filesystem::path("lib/opensim-native") / jpeg_library);
                        if (policies.map_layer_enabled)
                            policies.encode_map_jpeg = scene::open_map_jpeg_encoder(library);
                        if (policies.map_layer_enabled) {
                            policies.generate_map_on_startup = generate_map_tiles;
                            policies.load_map_asset =
                                [&, decode = scene::open_map_bitmap_decoder(library)](framework::Uuid id) {
                                    const auto asset = region_assets ? region_assets->get(id) : nullptr;
                                    return asset && asset->data ? decode(*asset->data) : scene::MapBitmap{};
                                };
                            policies.map_export_scale = map_flag("ExportMapAddScale", false);
                            policies.map_export_region_name = map_flag("ExportMapAddRegionName", false);
                            const auto map_tiles_directory = map_setting("MapTilesDirectory");
                            policies.map_tiles_directory = framework::utf8_path(map_tiles_directory);
                            if (!policies.map_tiles_directory.empty()) {
                                std::error_code failure;
                                std::filesystem::create_directories(policies.map_tiles_directory, failure);
                                if (failure) {
                                    error_log("[WORLD MAP]: Failed to create local map tile directory " +
                                              map_tiles_directory + ": " + failure.message());
                                    policies.map_tiles_directory.clear();
                                }
                            }
                            float view_distance = 255.f;
                            float draw_distance = 512.f;
                            framework::parse_invariant_float(
                                config.get("Startup", "MaxRegionsViewDistance", "255"), view_distance);
                            framework::parse_invariant_float(
                                config.get("Startup", "MaxDrawDistance", "512"), draw_distance);
                            const auto export_distance = std::min(view_distance, draw_distance);
                            if (std::isfinite(export_distance))
                                policies.map_export_view_distance = static_cast<int>(
                                    std::clamp(export_distance, 0.f, 32767.f));
                            policies.static_map_file = framework::utf8_path(
                                region_settings.get("NativeRegion", "MaptileStaticFile"));
                            policies.warp_map_tiles =
                                map_setting("MapImageModule", "MapImageModule") == "Warp3DImageModule";
                            policies.draw_map_object_volumes = map_flag("DrawPrimOnMapTile", true);
                            policies.average_warp_map_textures = policies.warp_map_tiles &&
                                map_flag("AverageTextureColorOnMapTile", false);
                            if (policies.warp_map_tiles) {
                                policies.texture_warp_prims = map_flag("TexturePrims", true);
                                policies.render_warp_meshes = map_flag("RenderMeshes", false);
                                float minimum = -100.f, maximum = 4096.f;
                                framework::parse_invariant_float(
                                    map_setting("RenderMinHeight", "-100"), minimum);
                                framework::parse_invariant_float(
                                    map_setting("RenderMaxHeight", "4096"), maximum);
                                if (std::isfinite(maximum))
                                    policies.warp_prim_max_height = std::clamp(maximum, 100.f, 4086.f);
                                if (std::isfinite(minimum))
                                    policies.warp_prim_min_height =
                                        std::clamp(minimum, -100.f, policies.warp_prim_max_height - 10.f);
                            }
                            policies.textured_map_tiles = map_flag("TextureOnMapTile", policies.warp_map_tiles) ||
                                policies.average_warp_map_textures;
                            policies.encode_map_jpeg2000 = scene::open_map_jpeg2000_encoder(library);
                            const auto map_color = [&](const char* key, scene::MapColor fallback) {
                                const auto text = map_setting(key);
                                return scene::parse_map_color(text).value_or(fallback);
                            };
                            auto& palette = policies.map_palette;
                            palette.water = map_color("MapColorWater", palette.water);
                            if (policies.textured_map_tiles && !policies.warp_map_tiles) {
                                constexpr std::array color_keys{"MapColor1", "MapColor2", "MapColor3", "MapColor4"};
                                for (std::size_t index = 0; index < color_keys.size(); ++index)
                                    palette.land[index] = map_color(color_keys[index], palette.land[index]);
                                policies.map_texture_average =
                                    [&, average = scene::open_map_texture_average(library)](framework::Uuid id)
                                        -> std::optional<scene::MapColor> {
                                        const auto asset = region_assets ? region_assets->get(id) : nullptr;
                                        return asset && asset->data ? average(*asset->data) : std::nullopt;
                                    };
                            }
                            if (generate_map_tiles)
                                policies.map_tile_refresh = std::chrono::seconds(map_integer("MaptileRefresh", 0));
                            policies.local_map_assets = map_flag("LocalV1MapAssets", false);
                            policies.store_map_tile = [&](auto asset) {
                                if (!region_assets || !region_assets->store(std::move(asset)))
                                    throw std::runtime_error("Could not store terrain map tile");
                            };
                            policies.save_map_tiles = [&](framework::Uuid terrain, framework::Uuid parcel) {
                                if (region_persistence.save_map_images)
                                    region_persistence.save_map_images(terrain, parcel);
                            };
                            policies.publish_map_tiles = [&] {
                                if (publish_map_images)
                                    publish_map_images();
                            };
                        }
                        policies.decode_texture_layers = scene::cache_texture_layer_decoder(
                            scene::open_texture_layer_decoder(library, config.boolean("Startup", "UseCSJ2K", true)),
                            [&] { return region_assets ? region_assets->asset_cache() : nullptr; }, region.id);
                    } catch (const std::exception& error) {
                        error_log("JPEG 2000 layer decoder: " + std::string(error.what()));
                    }
                }
                if (physical_region) {
#ifdef _WIN32
                    const char* native_library = "ubode.dll";
#elif defined(__APPLE__)
                    const char* native_library = "libubode.dylib";
#elif defined(__aarch64__) || defined(__arm64__)
                    const char* native_library = "libubode-arm64.so";
#else
                    const char* native_library = "libubode-x86_64.so";
#endif
                    policies.physics_library =
                        runtime_file("PhysicsLibrary", std::filesystem::path("bin/lib64") / native_library,
                                     std::filesystem::path("lib/opensim-native") / native_library);
                }
                policies.access = [](auto, const auto& position, std::string& reason) {
                    if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
                        !std::isfinite(position.z)) {
                        reason = "Invalid agent position";
                        return false;
                    }
                    reason.clear();
                    return true;
                };
                policies.verify_foreign_client = [&](const auto& circuit, auto peer) {
                    return verify_foreign_client && verify_foreign_client(circuit, peer);
                };
                policies.authorize = [&, scope = region.scope](const auto& circuit, std::string& reason) {
                    if (!circuit->verified_foreign_login &&
                        !accounts.find_by_id(scope.str(), circuit->agent_id.str())) {
                        reason = "Unknown local account";
                        return false;
                    }
                    reason.clear();
                    return true;
                };
                scene::LegacyInventoryBackend legacy_inventory;
                legacy_inventory.item = [&](auto owner, auto id) { return inventory->item(owner, id); };
                legacy_inventory.content = [&](auto owner, auto id) { return inventory->content(owner, id); };
                legacy_inventory.folder = [&](auto owner, auto id) { return inventory->folder(owner, id); };
                legacy_inventory.library_folder =
                    [&](auto id) -> std::optional<services::InventoryCollection> {
                    return library_service ? library_folder_content(library_service->root(), id)
                                           : std::nullopt;
                };
                policies.inventory = std::move(legacy_inventory);
                policies.inventory_service = inventory.get();
                policies.coalesce_returned_objects =
                    config.boolean("Inventory", "CoalesceMultipleObjectsToInventory", true);
                policies.use_trash_on_delete = config.boolean("Startup", "UseTrashOnDelete", true);
                if (avatars)
                    policies.save_appearance = [&](framework::Uuid agent,
                                                   const framework::AvatarAppearance& appearance) {
                        return avatars->set_appearance(agent, &appearance);
                    };
                if (attachment_script_store) {
                    policies.load_attachment_scripts = [attachment_script_store](auto owner, auto item, auto asset) {
                        return attachment_script_store->load(owner, item, asset);
                    };
                    policies.save_attachment_scripts = [attachment_script_store](auto owner, auto item, auto asset,
                                                                                const auto& states) {
                        attachment_script_store->save(owner, item, asset, states);
                    };
                }
                if (friends) {
                    policies.is_friend = [&, hypergrid = config.get("Modules", "FriendsModule") == "HGFriendsModule"](
                                             framework::Uuid principal, framework::Uuid other) {
                        return services::friend_is_confirmed(*friends, principal, other, hypergrid);
                    };
                    policies.friend_status_changed = [&](framework::Uuid principal, framework::Uuid session,
                                                         bool online) {
                        publish_friend_status(principal, session, online);
                    };
                    policies.online_friends = [&](framework::Uuid principal) {
                        std::vector<framework::Uuid> online;
                        if (!region_presence)
                            return online;
                        std::vector<std::string> eligible;
                        for (const auto& entry : friends->get(principal)) {
                            framework::Uuid other;
                            if (entry.their_flags != -1 && (entry.their_flags & 1) && entry.friend_id &&
                                framework::try_parse_sdk_uuid(*entry.friend_id, other))
                                eligible.push_back(other.str());
                        }
                        if (eligible.empty())
                            return online;
                        for (const auto& entry : region_presence->agents(eligible)) {
                            framework::Uuid other;
                            if (framework::try_parse_sdk_uuid(entry.user_id, other))
                                online.push_back(other);
                        }
                        return online;
                    };
                    policies.grant_friend_rights = [&](framework::Uuid principal, framework::Uuid other,
                                                       std::int32_t rights) -> std::optional<std::int32_t> {
                        const auto id = other.str();
                        for (const auto& entry : friends->get(principal)) {
                            if (!entry.friend_id ||
                                (*entry.friend_id != id && !entry.friend_id->starts_with(id + ";")))
                                continue;
                            if (friends->store(principal.str(), entry.friend_id, rights)) {
                                forward_friend_rights(other, principal, entry.my_flags, rights);
                                return entry.my_flags;
                            }
                            region_runtime->alert_viewer(principal, "Unable to grant rights.");
                            break;
                        }
                        return std::nullopt;
                    };
                    policies.terminate_friendship = [&](framework::Uuid principal, framework::Uuid other) {
                        if (principal == other || other.is_zero())
                            return false;
                        const auto other_id = other.str();
                        auto identifier = other_id;
                        for (const auto& entry : friends->get(principal)) {
                            if (entry.friend_id && entry.friend_id->starts_with(other_id + ";")) {
                                identifier = *entry.friend_id;
                                break;
                            }
                        }
                        const bool removed = friends->erase(principal, identifier);
                        const bool reverse_removed =
                            friends->erase(std::optional<std::string>(identifier), principal.str());
                        const auto still_has = [&](const std::string& owner, const std::string& target) {
                            if (friends_store) {
                                const auto rows = friends_store->get(std::optional<std::string>(owner));
                                return std::any_of(rows.begin(), rows.end(), [&](const auto& row) {
                                    return row && row->friend_id == target;
                                });
                            }
                            return false;
                        };
                        if (still_has(principal.str(), identifier) || still_has(identifier, principal.str()))
                            return false;
                        if (removed || reverse_removed) {
                            if (identifier != other_id)
                                sync_foreign_friend(principal.str(), other_id, identifier, {}, false);
                            forward_friend_termination(other, principal);
                        }
                        return removed || reverse_removed;
                    };
                    policies.pending_friendships = [&](framework::Uuid recipient) {
                        std::vector<scene::RegionInstantMessage> offers;
                        for (const auto& entry : friends->get(recipient)) {
                            framework::Uuid sender;
                            if (entry.their_flags != -1 || !entry.friend_id)
                                continue;
                            const std::string_view identifier = *entry.friend_id;
                            const bool foreign = identifier.size() > 36;
                            if (!framework::try_parse_sdk_uuid(identifier.substr(0, 36), sender))
                                continue;
                            std::string foreign_name;
                            if (foreign) {
                                const auto home_end = identifier.find(';', 37);
                                if (identifier[36] != ';' || home_end == std::string_view::npos ||
                                    sender.is_zero())
                                    continue;
                                const auto home = identifier.substr(37, home_end - 37);
                                const auto name_end = identifier.find(';', home_end + 1);
                                const auto display = framework::trim_utf8(identifier.substr(
                                    home_end + 1,
                                    name_end == std::string_view::npos ? name_end : name_end - home_end - 1));
                                const framework::HttpHost host{std::string(home)};
                                if (!host.valid() || display.empty())
                                    continue;
                                foreign_name = display;
                                std::replace(foreign_name.begin(), foreign_name.end(), ' ', '.');
                                foreign_name += " @" + host.host;
                                if (host.port != 80 && host.port != 443)
                                    foreign_name += ":" + std::to_string(host.port);
                            }
                            const auto account = accounts.find_by_id(framework::Uuid{}.str(), sender.str());
                            scene::RegionInstantMessage offer;
                            offer.sender = offer.session = sender;
                            offer.recipient = recipient;
                            offer.dialog = 38;
                            offer.timestamp = static_cast<std::uint32_t>(std::time(nullptr));
                            offer.sender_name =
                                foreign   ? framework::utf16_replacing(foreign_name)
                                : account ? framework::utf16_replacing(account->first_name.value_or("") +
                                                                       " " + account->last_name.value_or(""))
                                          : u"Unknown UserFMGAI";
                            offer.message =
                                foreign
                                    ? u"Please confirm this friendship you made while you were on another HG grid"
                                    : u"Will you be my friend?";
                            offers.push_back(std::move(offer));
                        }
                        return offers;
                    };
                    policies.answer_friendship = [&](framework::Uuid recipient, framework::Uuid sender,
                                                     bool accept) {
                        const auto sender_id = sender.str();
                        const auto recipient_id = recipient.str();
                        const auto entries = friends->get(recipient);
                        const auto pending =
                            std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
                                return entry.their_flags == -1 && entry.friend_id &&
                                       (entry.friend_id == sender_id ||
                                        entry.friend_id->starts_with(sender_id + ";"));
                            });
                        if (pending == entries.end()) {
                            const auto visitor = region_runtime->viewers().agent(sender);
                            if (!visitor || !visitor->root || visitor->logout_requested ||
                                !visitor->circuit->verified_foreign_login || !visitor->circuit->service_urls)
                                return;
                            {
                                std::lock_guard lock(live_friend_offers_mutex);
                                const auto offer = live_friend_offers.find({recipient, sender});
                                const auto target = region_runtime->viewers().agent(recipient);
                                if (!target || offer == live_friend_offers.end() ||
                                    offer->second !=
                                        std::pair{target->circuit->session_id, visitor->circuit->session_id})
                                    return;
                                live_friend_offers.erase(offer);
                            }
                            if (!accept) {
                                send_friend_answer(sender, recipient, {}, false);
                                return;
                            }
                            const auto home = visitor->circuit->service_urls->find("HomeURI");
                            if (home == visitor->circuit->service_urls->end())
                                return;
                            auto uri = framework::service_url_object_text(home->second);
                            if (!framework::HttpHost{uri}.valid())
                                return;
                            if (!uri.ends_with('/'))
                                uri += '/';
                            const auto name = visitor->circuit->first_name.value_or("") + " " +
                                              visitor->circuit->last_name.value_or("");
                            const auto secret = framework::Uuid::random().str().substr(0, 8);
                            const auto identifier = sender_id + ";" + uri + ";" + name + ";" + secret;
                            const auto local = accounts.find_by_id(framework::Uuid{}.str(), recipient_id);
                            if (!local) {
                                const auto target = region_runtime->viewers().agent(recipient);
                                if (!target || !target->circuit->verified_foreign_login ||
                                    !target->circuit->service_urls)
                                    return;
                                const auto target_home = target->circuit->service_urls->find("HomeURI");
                                if (target_home == target->circuit->service_urls->end())
                                    return;
                                auto target_uri = framework::service_url_object_text(target_home->second);
                                if (!framework::HttpHost{target_uri}.valid())
                                    return;
                                if (!target_uri.ends_with('/'))
                                    target_uri += '/';
                                const auto target_name = target->circuit->first_name.value_or("") + " " +
                                                         target->circuit->last_name.value_or("");
                                confirm_visitor_friendship(
                                    *visitor->circuit, recipient_id + ";" + target_uri + ";" + target_name,
                                    secret);
                                confirm_visitor_friendship(
                                    *target->circuit, identifier.substr(0, identifier.rfind(';')), secret);
                                send_friend_answer(sender, recipient, target_name, true);
                                return;
                            }
                            if (!friends->store(recipient_id, identifier, 1))
                                return;
                            if (!friends->store(identifier, recipient_id, 1)) {
                                friends->erase(recipient, identifier);
                                return;
                            }
                            confirm_visitor_friendship(*visitor->circuit, recipient_id,
                                                       identifier.substr(identifier.rfind(';') + 1));
                            send_friend_answer(sender, recipient,
                                               local ? local->first_name.value_or("") + " " +
                                                           local->last_name.value_or("")
                                                     : "",
                                               true);
                            return;
                        }
                        const auto& identifier = *pending->friend_id;
                        const auto visitor = region_runtime->viewers().agent(recipient);
                        if (accept && identifier == sender_id && visitor &&
                            visitor->circuit->verified_foreign_login) {
                            const auto& circuit = *visitor->circuit;
                            if (!circuit.service_urls)
                                return;
                            const auto home = circuit.service_urls->find("HomeURI");
                            if (home == circuit.service_urls->end())
                                return;
                            const auto uri = framework::service_url_object_text(home->second);
                            if (!framework::HttpHost{uri}.valid())
                                return;
                            const auto secret = framework::Uuid::random().str().substr(0, 8);
                            const auto visitor_name =
                                circuit.first_name.value_or("") + " " + circuit.last_name.value_or("");
                            const auto foreign = recipient_id + ";" + uri + (uri.ends_with('/') ? "" : "/") +
                                                 ";" + visitor_name + ";" + secret;
                            if (!friends->store(sender_id, foreign, 1))
                                return;
                            if (!friends->store(foreign, sender_id, 1)) {
                                friends->erase(sender, foreign);
                                return;
                            }
                            if (!friends->erase(recipient, sender_id)) {
                                friends->erase(sender, foreign);
                                friends->erase(std::optional<std::string>(foreign), sender_id);
                                return;
                            }
                            confirm_visitor_friendship(circuit, sender_id, secret);
                            send_friend_answer(sender, recipient, visitor_name, true);
                            return;
                        }
                        if (accept) {
                            if (!friends->store(identifier, recipient_id, 1))
                                return;
                            if (!friends->store(recipient_id, identifier, 1)) {
                                friends->erase(std::optional<std::string>(identifier), recipient_id);
                                return;
                            }
                        } else if (!friends->erase(recipient, identifier)) {
                            return;
                        }
                        if (accept && create_friend_card)
                            create_friend_card(recipient, sender);
                        const auto account = accounts.find_by_id(framework::Uuid{}.str(), recipient_id);
                        const auto name =
                            account ? account->first_name.value_or("") + " " + account->last_name.value_or("")
                                    : std::string{};
                        if (identifier != sender_id)
                            sync_foreign_friend(recipient_id, sender_id, identifier, name, accept);
                        send_friend_answer(sender, recipient, name, accept);
                    };
                    policies.offer_friendship = [&](const scene::RegionInstantMessage& request) {
                        const auto recipient = request.recipient.str();
                        if (request.sender == request.recipient || request.recipient.is_zero())
                            return;
                        const auto sender =
                            accounts.find_by_id(framework::Uuid{}.str(), request.sender.str());
                        const auto foreign_sender = sender ? std::optional<scene::ViewerAgent>{}
                                                           : region_runtime->viewers().agent(request.sender);
                        if (!sender && (!foreign_sender || !foreign_sender->root ||
                                        !foreign_sender->circuit->verified_foreign_login))
                            return;
                        const bool local_recipient =
                            accounts.find_by_id(framework::Uuid{}.str(), recipient).has_value();
                        if (!local_recipient) {
                            const auto visitor = region_runtime->viewers().agent(request.recipient);
                            if (!visitor || !visitor->root || visitor->logout_requested ||
                                !visitor->circuit->verified_foreign_login) {
                                std::lock_guard lock(live_friend_offers_mutex);
                                if (!sender || !visited_friend_endpoints.contains(request.recipient))
                                    return;
                            }
                        }
                        for (const auto& entry : friends->get(request.sender)) {
                            if (entry.friend_id && entry.friend_id->starts_with(recipient)) {
                                region_runtime->alert_viewer(
                                    request.sender,
                                    "This person is already your friend. Please delete it first if you want to reestablish the friendship.");
                                return;
                            }
                        }
                        if (sender && !friends->store(recipient, request.sender.str(), 0))
                            return;
                        if (foreign_sender) {
                            const auto target = region_runtime->viewers().agent(request.recipient);
                            if (!target || !target->root || target->logout_requested)
                                return;
                            std::lock_guard lock(live_friend_offers_mutex);
                            live_friend_offers.insert_or_assign(
                                std::pair{request.recipient, request.sender},
                                std::pair{target->circuit->session_id, foreign_sender->circuit->session_id});
                        }
                        auto offer = request;
                        offer.session = request.sender;
                        if (sender)
                            offer.sender_name = framework::utf16_replacing(
                                sender->first_name.value_or("") + " " + sender->last_name.value_or(""));
                        else
                            offer.sender_name = framework::utf16_replacing(
                                foreign_sender->circuit->first_name.value_or("") + " " +
                                foreign_sender->circuit->last_name.value_or(""));
                        send_friend_offer(offer);
                    };
                }
                policies.library_inventory_item = [&](auto id) -> std::optional<services::InventoryItem> {
                    if (!library_service)
                        return {};
                    auto item = library_service->item(id);
                    return item ? std::optional(*item) : std::nullopt;
                };
                policies.library_inventory_owner = [&]() -> std::optional<framework::Uuid> {
                    if (!library_service)
                        return {};
                    auto root = library_service->root();
                    return root ? std::optional(root->owner) : std::nullopt;
                };
                policies.can_rez = policies.access;
                policies.enforce_parcel_rez_permissions = true;
                policies.script_ossl_grid_gods = grid_gods;
                policies.media_on_a_prim_enabled =
                    !config.has_section("MediaOnAPrim") || config.boolean("MediaOnAPrim", "Enabled", false);
                policies.materials_enabled = config.boolean("Materials", "enable_materials", true);
                policies.estate_change_info_cap_enabled =
                    config.get("ClientStack.LindenCaps", "Cap_EstateChangeInfo") == "localhost";
                policies.environment_enabled =
                    config.get("ClientStack.LindenCaps", "Cap_EnvironmentSettings") == "localhost";
                policies.persist_environment = [&](auto parcel, auto settings) {
                    if (region_persistence.save_environment)
                        region_persistence.save_environment(std::move(parcel), std::move(settings));
                };
                auto ossl = config.native_section("OSSL");
                if (!ossl)
                    ossl = config.native_section("YEngine");
                if (ossl) {
                    const auto read = [&](std::string_view key, std::string fallback = {}) {
                        const auto value = ossl->get_string(
                            framework::nini_writer_text(framework::utf16(key)), {});
                        return value ? framework::utf8(*value) : fallback;
                    };
                    policies.script_ossl_enabled = ossl->get_boolean(
                        framework::nini_writer_text(u"AllowOSFunctions"), true);
                    policies.script_ossl_permission_error_to_owner = ossl->get_boolean(
                        framework::nini_writer_text(u"PermissionErrorToOwner"), false);
                    policies.script_ossl_threat_level = read("OSFunctionThreatLevel", "VeryLow");
                    for (const std::string function :
                         {"osRequestURL", "osRequestSecureURL", "osForceOtherSit", "osGetAgents", "osGetAgentIP",
                          "osAvatarPlayAnimation", "osAvatarStopAnimation", "osSetRegionWaterHeight",
                          "osSetRegionSunSettings", "osSunGetParam", "osReplaceAgentEnvironment",
                          "osSetHealRate", "osGetHealRate", "osListenRegex", "osRegexIsMatch",
                          "osTeleportAgent", "osTeleportOwner",
                          "osMatchString", "osReplaceString", "osGetScriptEngineName", "osGetGridLoginURI",
                          "osGetGridHomeURI", "osGetGridGatekeeperURI", "osGetGridCustom"})
                        policies.script_ossl_rules.emplace(function, scene::RegionPolicies::OsslRule{
                            read("Allow_" + function), read("Creators_" + function)});
                }
                if (auto network = config.native_section("Network")) {
                    policies.script_url_host.emplace();
                    if (auto host = network->get_string(framework::nini_writer_text(u"ExternalHostNameForLSL"), {}))
                        policies.script_url_host = framework::utf8(*host);
                    if (auto shard = network->get_string(framework::nini_writer_text(u"shard"), {}))
                        policies.script_http_shard = framework::utf8(*shard);
                    if (auto agent = network->get_string(framework::nini_writer_text(u"user_agent"), {}))
                        policies.script_http_user_agent = framework::utf8(*agent);
                }
                if (auto functions = config.native_section("LL-Functions"))
                    policies.script_url_limit = functions->get_int(
                        framework::nini_writer_text(u"max_external_urls_per_simulator"), policies.script_url_limit);
                auto outbound_service = script_http_service.lock();
                if (!outbound_service) {
                    const auto outbound_filter = std::make_shared<framework::OutboundUrlFilter>(
                        config.get("Network", "OutboundDisallowForUserScripts",
                                   std::string(framework::OutboundUrlFilter::default_disallow)),
                        config.get("Network", "OutboundDisallowForUserScriptsExcept", ""),
                        framework::HostResolver::Lookup{},
                        [&](std::string_view entry, bool network) {
                            error_log("[OUTBOUND URL FILTER]: Invalid " +
                                      std::string(network ? "network" : "endpoint") +
                                      " for script HTTP: " + std::string(entry));
                        });
                    outbound_service = std::make_shared<network::ScriptHttpService>(
                        outbound_filter, config.integer("Network", "HttpBodyMaxLenMAX", 16384),
                        config.integer("ScriptsHttpRequestModule", "RequestsTimeOut", 30000),
                        config.integer("ScriptsHttpRequestModule", "MaxPoolThreads", 8),
                        network::make_script_http_proxy(config.get("Startup", "HttpProxy", ""),
                                                       config.get("Startup", "HttpProxyExceptions", "")));
                    script_http_service = outbound_service;
                }
                script::HttpRequestThrottleSettings http_throttle;
                if (auto http = config.native_section("ScriptsHttpRequestModule")) {
                    http_throttle.prim_rate = http->get_float(
                        framework::nini_writer_text(u"PrimRequestsPerSec"), http_throttle.prim_rate);
                    http_throttle.prim_burst = http->get_float(
                        framework::nini_writer_text(u"PrimRequestsBurst"), http_throttle.prim_burst);
                    http_throttle.owner_rate = http->get_float(
                        framework::nini_writer_text(u"PrimOwnerRequestsPerSec"), http_throttle.owner_rate);
                    http_throttle.owner_burst = http->get_float(
                        framework::nini_writer_text(u"PrimOwnerRequestsBurst"), http_throttle.owner_burst);
                }
                policies.script_outbound_http = std::make_shared<network::ScriptHttpModule>(
                    std::move(outbound_service), http_throttle);
                const auto grid_identity = [&](const std::string& key) {
                    auto value = config.get("Const", key);
                    value = config.get("GridInfo", key, value);
                    return config.get("SimulatorFeatures", key, value);
                };
                policies.script_grid_name = grid_identity("GridName");
                if (policies.script_grid_name.empty())
                    policies.script_grid_name = grid_identity("gridname");
                if (policies.script_grid_name.empty())
                    policies.script_grid_name = "Another bad configured grid";
                policies.script_grid_nick = grid_identity("GridNick");
                policies.script_grid_info = [&config, &options, &ports, clients = outgoing_clients(),
                                             host = region.external_host.value_or("127.0.0.1")](
                    script::LslGridQuery query, std::string_view key) -> std::string {
                    using Query = script::LslGridQuery;
                    if (query == Query::Login || query == Query::Custom)
                        return server::script_grid_service_value(config, *clients, query == Query::Login ? "login" : key);
                    try {
                        const auto scheme = options.certificate.empty() && options.pkcs12_bundle.empty()
                            ? "http://" : "https://";
                        const auto authority = host.find(':') == std::string::npos ? host : "[" + host + "]";
                        const server::GridIdentity identity(
                            config, scheme + authority + ":" + std::to_string(ports.main_http) + "/");
                        return (query == Query::Home ? identity.home_uri() : identity.gate_uri()).value_or("");
                    } catch (const server::GridIdentityError&) {
                        return {};
                    }
                };
                policies.yengine_state_root = "ScriptEngines";
                if (auto yengine = config.native_section("YEngine")) {
                    auto state_root = yengine->get_string(framework::nini_writer_text(u"ScriptEnginesPath"), {});
                    if (!state_root || framework::trim_utf8(framework::utf8(*state_root)).empty())
                        state_root = yengine->get_string(framework::nini_writer_text(u"ScriptBasePath"), {});
                    if (state_root && !framework::trim_utf8(framework::utf8(*state_root)).empty())
                        policies.yengine_state_root = std::filesystem::path(*state_root);
                    policies.min_timer_interval = yengine->get_float(
                        framework::nini_writer_text(u"MinTimerInterval"), policies.min_timer_interval);
                    const auto heap_kib = yengine->get_int(
                        framework::nini_writer_text(u"ScriptHeapSize"), policies.script_heap_limit_bytes / 1024);
                    policies.script_heap_limit_bytes =
                        std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(heap_kib) << 10);
                    policies.allow_god_functions = yengine->get_boolean(
                        framework::nini_writer_text(u"AllowGodFunctions"), false);
                    policies.script_delay_factor = yengine->get_float(
                        framework::nini_writer_text(u"ScriptDelayFactor"), policies.script_delay_factor);
                    policies.script_distance_factor = yengine->get_float(
                        framework::nini_writer_text(u"ScriptDistanceLimitFactor"), policies.script_distance_factor);
                    policies.disable_underground_movement = yengine->get_boolean(
                        framework::nini_writer_text(u"DisableUndergroundMovement"),
                        policies.disable_underground_movement);
                    policies.notecard_line_chars = std::min(65535, yengine->get_int(
                        framework::nini_writer_text(u"NotecardLineReadCharsMax"), policies.notecard_line_chars));
                    policies.async_command_loop_ms = yengine->get_int(
                        framework::nini_writer_text(u"AsyncLLCommandLoopms"), policies.async_command_loop_ms);
                    policies.sensor_max_range = yengine->get_float(
                        framework::nini_writer_text(u"SensorMaxRange"), policies.sensor_max_range);
                    policies.sensor_max_results = yengine->get_int(
                        framework::nini_writer_text(u"SensorMaxResults"), policies.sensor_max_results);
                    policies.automatic_link_permission =
                        yengine->get_boolean(framework::nini_writer_text(u"AutomaticLinkPermission"),
                                             policies.automatic_link_permission);
                    policies.linkset_data_limit = yengine->get_int(
                        framework::nini_writer_text(u"LinksetDataLimit"), policies.linkset_data_limit);
                }
                const auto region_linkset_capacity = region_settings.integer("NativeRegion", "LinksetPrims", 0);
                policies.linkset_capacity = region_linkset_capacity > 0
                    ? region_linkset_capacity : config.integer("Startup", "LinksetPrims", 0);
                if (auto startup = config.native_section("Startup")) {
                    policies.physical_linkset_capacity =
                        startup->get_int(framework::nini_writer_text(u"LinksetPhysPrims"), 0);
                    policies.nonphysical_prim_min =
                        std::max(startup->get_float(framework::nini_writer_text(u"NonPhysicalPrimMin"),
                                                    policies.nonphysical_prim_min),
                                 0.001f);
                    policies.nonphysical_prim_max =
                        std::min(startup->get_float(framework::nini_writer_text(u"NonPhysicalPrimMax"),
                                                    policies.nonphysical_prim_max),
                                 65536.f);
                    policies.physical_prim_min =
                        std::max(startup->get_float(framework::nini_writer_text(u"PhysicalPrimMin"),
                                                    policies.physical_prim_min),
                                 0.01f);
                    policies.physical_prim_max =
                        std::min(startup->get_float(framework::nini_writer_text(u"PhysicalPrimMax"),
                                                    policies.physical_prim_max),
                                 2048.f);
                }
                const auto region_prim_limit = [&](const char* key, float fallback) {
                    const auto value = region_settings.integer("NativeRegion", key, 0);
                    return value > 0 ? static_cast<float>(value) : fallback;
                };
                const auto region_prim_minimum = [&](std::u16string_view key, float fallback) {
                    const auto section = region_settings.native_section("NativeRegion");
                    const auto value = section ? section->get_float(
                        framework::nini_writer_text(std::u16string(key)), 0.f) : 0.f;
                    return value > 0.f ? value : fallback;
                };
                policies.nonphysical_prim_min = std::max(
                    region_prim_minimum(u"NonPhysicalPrimMin", policies.nonphysical_prim_min), 0.001f);
                policies.nonphysical_prim_max = std::min(
                    region_prim_limit("NonPhysicalPrimMax", policies.nonphysical_prim_max), 65536.f);
                policies.physical_prim_min = std::max(
                    region_prim_minimum(u"PhysicalPrimMin", policies.physical_prim_min), 0.01f);
                policies.physical_prim_max = std::min(
                    region_prim_limit("PhysicalPrimMax", policies.physical_prim_max), 2048.f);
                policies.clamp_prim_size = config.boolean("Startup", "ClampPrimSize", false) ||
                                          region_settings.boolean("NativeRegion", "ClampPrimSize", false);
                constexpr std::uint32_t modify = 1u << 14;
                constexpr std::uint32_t move = 1u << 19;
                const bool check_permissions =
                    config.boolean("Permissions", "serverside_object_permissions",
                                   config.boolean("Startup", "serverside_object_permissions", true));
                policies.bypass_object_permissions = !check_permissions;
                policies.bypass_sale_permissions = !check_permissions;
                policies.bypass_estate_permissions = !check_permissions;
                const auto has_owner_rights =
                    [&, hypergrid = config.get("Modules", "FriendsModule") ==
                                    "HGFriendsModule"](framework::Uuid id, const scene::SceneObject& object) {
                        if (id == object.owner)
                            return true;
                        return friends && !object.is_attachment && object.owner != object.group &&
                               services::friend_can_modify_objects(*friends, id, object.owner, hypergrid);
                    };
                const auto has_god_permissions = [&](framework::Uuid id) {
                    return region_runtime && region_runtime->agent_has_god_permissions(id);
                };
                policies.object_owner_rights = has_owner_rights;
                policies.inventory_is_god = has_god_permissions;
                policies.can_edit = [check_permissions, has_owner_rights,
                                     has_god_permissions](auto id, const auto& object) {
                    if (check_permissions && has_god_permissions(id))
                        return id != object.owner || (object.owner_mask & move) != 0;
                    return !check_permissions || ((object.owner_mask & move) != 0 &&
                                                  (object.base_mask & object.owner_mask & modify) != 0 &&
                                                  has_owner_rights(id, object));
                };
                policies.can_move = [check_permissions, has_owner_rights,
                                     has_god_permissions](auto id, const auto& object) {
                    if (!check_permissions)
                        return id == object.owner || !object.is_attachment;
                    if (has_god_permissions(id))
                        return id != object.owner || (object.owner_mask & move) != 0;
                    return (object.base_mask & object.owner_mask & move) != 0 &&
                           ((!object.is_attachment && (object.everyone_mask & move) != 0) ||
                            has_owner_rights(id, object));
                };
                if (config.boolean("NativeServer", "EnableRegionAssets", false)) {
                    policies.store_inventory_asset = [&](auto asset) {
                        if (!region_assets)
                            throw std::runtime_error("Region asset service unavailable");
                        return region_assets->store(std::move(asset));
                    };
                    policies.store_asset = [&](auto asset) {
                        if (!region_assets)
                            throw std::runtime_error("Region asset service unavailable");
                        region_assets->store(std::move(asset));
                    };
                    policies.load_asset = [&](auto id) {
                        if (!region_assets)
                            throw std::runtime_error("Region asset service unavailable");
                        return region_assets->get(id);
                    };
                    policies.expire_material_asset_cache = [&](auto id) {
                        if (region_assets)
                            if (const auto cache = region_assets->asset_cache())
                                cache->expire(id.str());
                    };
                    const auto separate_assets =
                        config.boolean("HGInventoryAccessModule", "CheckSeparateAssets", false);
                    const auto local_asset_url =
                        config.get("HGInventoryAccessModule", "RegionHGAssetServerURI");
                    policies.load_foreign_asset =
                        [&, scope = region.scope, separate_assets, local_asset_url, clients = outgoing_clients()](
                            const framework::AgentCircuitData& circuit, framework::Uuid id)
                        -> std::shared_ptr<framework::AssetBase> {
                        if (!region_assets || !circuit.service_urls)
                            return {};
                        const bool local_user = accounts.find_by_id(scope.str(), circuit.agent_id.str()).has_value();
                        if (!separate_assets && local_user)
                            return {};
                        const auto circuit_url = [&](const char* name) -> std::string {
                            const auto entry = circuit.service_urls->find(name);
                            if (entry != circuit.service_urls->end())
                                if (const auto value = std::any_cast<std::string>(&entry->second))
                                    return *value;
                            return {};
                        };
                        const framework::HttpHost advertised(circuit_url("AssetServerURI"));
                        auto remote = advertised;
                        if (!local_user && (separate_assets || !advertised.valid())) {
                            const framework::HttpHost home(circuit_url("HomeURI"));
                            if (home.valid()) {
                                try {
                                    network::NwcXmlRpcHttpTransport transport(clients);
                                    auto parameters = std::make_shared<network::NwcXmlRpcRequest::Parameters>();
                                    parameters->push_back(std::make_shared<network::Value>(
                                        network::Value::object({{"userID", circuit.agent_id.str()}})));
                                    auto request =
                                        std::make_shared<network::NwcXmlRpcRequest>(u"get_server_urls", parameters);
                                    const auto reply = transport.send(request, framework::utf16_replacing(home.uri),
                                                                      10000);
                                    if (reply && !reply->is_fault && reply->value) {
                                        framework::HttpHost discovered(
                                            reply->value->get("SRV_AssetServerURI").as_string());
                                        if (discovered.valid())
                                            remote = std::move(discovered);
                                    }
                                } catch (const std::exception& error) {
                                    error_log("Home-grid asset URL lookup: " + std::string(error.what()));
                                }
                            }
                        }
                        if (!remote.valid())
                            return {};
                        if (separate_assets) {
                            const framework::HttpHost local(local_asset_url);
                            if (local.valid() && local.uri == remote.uri)
                                return {};
                        }
                        return region_assets->get_foreign(id, remote.uri, false);
                    };
                    asset_transfer_workers = std::make_unique<ProfileWorkers>(
                        [&](const std::exception& e) { error_log("Viewer texture fetch: " + std::string(e.what())); });
                    policies.schedule_asset_fetch = [&](auto task) {
                        asset_transfer_workers->enqueue(std::move(task));
                    };
                }
                policies.client_flags = [](auto id, const auto& object) {
                    return object.flags | (object.inventory_empty ? 0x800u : 0u) | 0x10u |
                           (id == object.owner ? 0x1002012cu : 0u);
                };
                if (auto filename = region_settings.get("NativeRegion", "DefaultScriptFile"); !filename.empty()) {
                    auto path = framework::utf8_path(filename);
                    if (path.is_relative())
                        path = config.directory / path;
                    std::ifstream input(path, std::ios::binary);
                    if (!input)
                        throw std::runtime_error("Cannot open NativeRegion default script asset");
                    policies.default_script.assign(std::istreambuf_iterator<char>(input), {});
                    if (!config.boolean("NativeServer", "EnableRegionAssets", false))
                        throw std::runtime_error(
                            "NativeRegion.DefaultScriptFile requires EnableRegionAssets and a configured asset service");
                    policies.create_script_asset = [&](framework::Uuid agent,
                                                       const services::InventoryItem& item,
                                                       const std::string& text) {
                        if (!region_assets)
                            throw std::runtime_error("Region asset service unavailable");
                        auto asset = std::make_shared<framework::AssetBase>(
                            framework::Uuid::random(), item.name, std::int8_t(10), agent.str());
                        asset->description(item.description);
                        asset->data = std::make_shared<std::vector<std::uint8_t>>(text.begin(), text.end());
                        region_assets->store(asset);
                        return asset->full_id();
                    };
                }
                std::shared_ptr<const server::GridIdentity> activity_grid;
                try {
                    activity_grid = std::make_shared<server::GridIdentity>(config, std::nullopt);
                } catch (const server::GridIdentityError&) {
                    // Region activity can still use a stored foreign identity.
                }
                const auto resolve_grid_user = [&, scope = region.scope, activity_grid](
                                                   const framework::AgentCircuitData& circuit)
                    -> std::optional<std::string> {
                    const auto user = grid_user_identity(circuit);
                    if (circuit.verified_foreign_login || accounts.find_by_id(scope.str(), user))
                        return user;

                    const auto stored = remote_grid_user ? remote_grid_user->info(user) : grid_user->info(user);
                    const auto prefix = user + ";";
                    if (!stored || !stored->user_id.starts_with(prefix))
                        return std::nullopt;

                    const auto identity = std::string_view(stored->user_id).substr(prefix.size());
                    const auto home_end = identity.find(';');
                    if (home_end == identity.npos)
                        return std::nullopt;
                    const auto name_start = home_end + 1;
                    const auto name_end = identity.find(';', name_start);
                    auto name = identity.substr(name_start, name_end == identity.npos ? identity.npos
                                                                                     : name_end - name_start);
                    const auto first_start = name.find_first_not_of(' ');
                    if (first_start == name.npos)
                        return std::nullopt;
                    name.remove_prefix(first_start);
                    const auto first_end = name.find(' ');
                    const auto first = name.substr(0, first_end);
                    const auto last_start = first_end == name.npos ? name.npos : name.find_first_not_of(' ', first_end);
                    const auto last = last_start == name.npos ? std::string_view{} : name.substr(last_start);
                    const auto dotted_name = std::string(first) + "." + std::string(last.substr(0, last.find(' ')));

                    const auto home_url = framework::trim_utf8(identity.substr(0, home_end));
                    if (home_url.empty())
                        return user;
                    const framework::HttpHost home{home_url};
                    if (!home.valid())
                        return std::nullopt;
                    if (activity_grid && activity_grid->is_local_grid(home) == 1)
                        return user;
                    const auto dot = dotted_name.find('.');
                    const auto next_dot = dotted_name.find('.', dot + 1);
                    return prefix + home.uri + ";" + dotted_name.substr(0, dot) + " " +
                           dotted_name.substr(dot + 1, next_dot == dotted_name.npos ? dotted_name.npos
                                                                                   : next_dot - dot - 1);
                };
                policies.entered = [&, resolve_grid_user, region_id = region.id](const auto& a) {
                    const auto user = resolve_grid_user(*a.circuit);
                    if (!user)
                        return;
                    if (remote_grid_user)
                        remote_grid_user->set_last_position(*user, region_id, a.position, a.look_at);
                    else
                        grid_user->set_last_position(*user, a.circuit->session_id, region_id, a.position, a.look_at);
                };
                const auto record_grid_logout = [&, resolve_grid_user, region_id = region.id](const auto& a) {
                    const auto user = resolve_grid_user(*a.circuit);
                    if (!user)
                        return;
                    if (remote_grid_user)
                        remote_grid_user->logged_out(*user, region_id, a.position, a.look_at);
                    else
                        grid_user->logged_out(*user, a.circuit->session_id, region_id, a.position, a.look_at);
                };
                policies.departed_to_another_grid = record_grid_logout;
                policies.logged_out = [&, record_grid_logout](const auto& a) {
                    {
                        std::lock_guard lock(live_friend_offers_mutex);
                        const auto session = a.circuit->session_id;
                        std::erase_if(live_friend_offers, [&](const auto& offer) {
                            return offer.second.first == session || offer.second.second == session;
                        });
                        if (a.root && a.circuit->verified_foreign_login && a.circuit->service_urls) {
                            const auto service = a.circuit->service_urls->find("FriendsServerURI");
                            if (service != a.circuit->service_urls->end()) {
                                const auto endpoint = framework::service_url_object_text(service->second);
                                if (framework::HttpHost{endpoint}.valid())
                                    visited_friend_endpoints.insert_or_assign(a.circuit->agent_id, endpoint);
                            }
                        }
                    }
                    if (!a.root) {
                        // Login records GridUser before the viewer connects.
                        constexpr std::uint32_t via_login = 1u << 7;
                        const bool login = a.circuit->verified_foreign_login ||
                                           (a.circuit->teleport_flags & via_login) != 0;
                        if (!a.connected && login)
                            record_grid_logout(a);
                        return;
                    }
                    record_grid_logout(a);
                    if (!a.circuit->verified_foreign_login || !a.circuit->service_urls)
                        return;
                    const auto home = a.circuit->service_urls->find("HomeURI");
                    if (home == a.circuit->service_urls->end())
                        return;
                    const auto uri = framework::service_url_object_text(home->second);
                    if (uri.empty())
                        return;
                    auto transport = std::make_shared<network::NwcXmlRpcHttpTransport>(outgoing_clients());
                    services::UserAgentConnectorServices connector;
                    connector.debug = [&console_log](const auto& message) {
                        console_log.append(framework::console_log_debug, message);
                    };
                    connector.error = connector.warn = [&console_log](const auto& message) {
                        console_log.append(framework::console_log_warn, message);
                    };
                    connector.client = [transport](std::int32_t timeout) {
                        return network::xml_rpc_http_client(transport, timeout);
                    };
                    services::UserAgentServiceConnector home_agent(framework::utf16_replacing(uri),
                                                                   std::move(connector));
                    home_agent.logout_agent(a.circuit->agent_id, a.circuit->session_id);
                };
                policies.log = normal_log;
                if (auto key = config.get("Messaging", "MessageKey"); message_transfer_enabled || store_offline) {
                    const bool forward_group_messages =
                        config.boolean("Messaging", "ForwardOfflineGroupMessages", true);
                    if (!profile_workers)
                        profile_workers =
                            std::make_unique<ProfileWorkers>([&console_log](const std::exception& error) {
                                console_log.append(framework::console_log_error,
                                                   framework::utf16_replacing(error.what()));
                            });
                    policies.forward_instant_message =
                        [&, key, store_offline, forward_group_messages, scope = region.scope,
                         own_region = region.id,
                         clients = outgoing_clients()](const scene::RegionInstantMessage& message) {
                            profile_workers->enqueue([&, key, store_offline, forward_group_messages, scope,
                                                      own_region, clients, message] {
                                const auto locations = region_presence->agents({message.recipient.str()});
                                std::optional<services::GridRegion> destination;
                                if (message_transfer_enabled && !locations.empty() &&
                                    !locations.front().region_id.is_zero() &&
                                    locations.front().region_id != own_region) {
                                    destination = grid->by_id(scope, locations.front().region_id);
                                    if (!destination)
                                        destination = lookup_remote_region(
                                            scope, {{"REGIONID", locations.front().region_id.str()},
                                                    {"METHOD", "get_region_by_uuid"}});
                                }
                                const auto binary = [](std::vector<std::uint8_t> bytes) {
                                    return network::binary_base64(network::osd_from_binary(
                                        std::make_shared<std::vector<std::uint8_t>>(std::move(bytes))));
                                };
                                auto payload = network::Value::object(
                                    {{"from_agent_id", message.sender.str()},
                                     {"from_agent_session", framework::Uuid{}.str()},
                                     {"to_agent_id", message.recipient.str()},
                                     {"im_session_id", message.session.str()},
                                     {"timestamp", std::to_string(message.timestamp)},
                                     {"from_agent_name", framework::utf8(message.sender_name)},
                                     {"message", framework::utf8(message.message)},
                                     {"dialog", binary({message.dialog})},
                                     {"from_group", message.from_group ? "TRUE" : "FALSE"},
                                     {"offline", binary({message.offline})},
                                     {"parent_estate_id", std::to_string(message.estate)},
                                     {"position_x", framework::invariant_float_string(message.position.x)},
                                     {"position_y", framework::invariant_float_string(message.position.y)},
                                     {"position_z", framework::invariant_float_string(message.position.z)},
                                     {"region_id", message.region.str()},
                                     {"binary_bucket", binary(message.bucket)},
                                     {"region_handle", network::Value(std::int32_t{0})}});
                                if (!key.empty())
                                    payload.map.emplace("message_key", network::Value(key));
                                if (destination && !destination->server_uri().empty() &&
                                    services::send_grid_instant_message(destination->server_uri(), payload,
                                                                        clients))
                                    return;
                                if (store_offline) {
                                    constexpr auto message_from_agent = 0;
                                    constexpr auto group_invitation = 3;
                                    constexpr auto inventory_offered = 4;
                                    constexpr auto task_inventory_offered = 9;
                                    constexpr auto message_from_object = 19;
                                    constexpr auto group_notice = 32;
                                    switch (message.dialog) {
                                    case group_invitation:
                                    case group_notice:
                                        if (!forward_group_messages)
                                            return;
                                        break;
                                    case message_from_agent:
                                    case inventory_offered:
                                    case task_inventory_offered:
                                    case message_from_object:
                                        break;
                                    default:
                                        return;
                                    }
                                    auto [saved, reason] = store_offline(message);
                                    if (message.dialog != message_from_agent)
                                        return;
                                    scene::RegionInstantMessage reply;
                                    reply.sender = message.recipient;
                                    reply.recipient = message.sender;
                                    reply.sender_name = u"System";
                                    reply.message = saved ? u"User is not logged in. Message saved."
                                                          : u"User is not logged in. Message not saved: " +
                                                                framework::utf16_replacing(reason);
                                    reply.timestamp = message.timestamp;
                                    region_runtime->deliver_instant_message(reply);
                                }
                            });
                        };
                }
                struct FeatureBackend final : services::SimulatorFeaturesBackend {
                    std::function<void(const std::string&)> log;
                    void error(const std::string& text) override {
                        log(text);
                    }
                    void warn(const std::string& text) override {
                        log(text);
                    }
                };
                auto feature_backend = std::make_shared<FeatureBackend>();
                feature_backend->log = error_log;
                simulator_features = std::make_shared<services::SimulatorFeaturesModule>(feature_backend);
                simulator_features->initialise(&config);
                if (world_map == "HGWorldMap") {
                    auto map_url = config.get("LoginService", "MapTileURL");
                    map_url = config.get("HGWorldMap", "MapTileURL", map_url);
                    map_url = config.get("SimulatorFeatures", "MapTileURL", map_url);
                    if (!map_url.empty()) {
                        map_url = framework::trim_utf8(map_url);
                        if (!map_url.ends_with('/'))
                            map_url += '/';
                        auto map_feature = services::SimulatorFeatureValue::from_value(network::Value(map_url));
                        simulator_features->on_request.add({
                            {simulator_features.get(), "HGWorldMap.SimulatorFeatures"},
                            [is_local = policies.is_local_grid_user, map_feature](
                                framework::Uuid agent, services::FeatureData& features) {
                                if (is_local(agent))
                                    return;
                                auto& extras = features->map["OpenSimExtras"];
                                if (!extras)
                                    extras = services::SimulatorFeatureValue::object();
                                extras->map["map-server-url"] = map_feature;
                            }});
                    }
                }
                simulator_features->add_feature(
                    "AvatarHoverHeightEnabled",
                    services::SimulatorFeatureValue::from_value(network::Value(true)));
                if (policies.materials_enabled) {
                    const auto max_materials = config.integer("Materials", "MaxMaterialsPerTransaction", 50);
                    simulator_features->add_feature(
                        "MaxMaterialsPerTransaction",
                        services::SimulatorFeatureValue::from_value(network::Value(max_materials)));
                    simulator_features->add_feature(
                        "RenderMaterialsCapability",
                        services::SimulatorFeatureValue::from_value(network::Value(std::int32_t{4})));
                }
                const auto add_scale_feature = [&](const char* name, float scale) {
                    network::Value value;
                    value.kind = network::Value::Kind::Real;
                    value.real = scale;
                    simulator_features->add_extra(name, services::SimulatorFeatureValue::from_value(value));
                };
                add_scale_feature("MinPrimScale", policies.nonphysical_prim_min);
                add_scale_feature("SimulatorFPSFactor", policies.statistics_fps_factor);
                add_scale_feature("MaxPrimScale", policies.nonphysical_prim_max);
                add_scale_feature("MinPhysPrimScale", policies.physical_prim_min);
                add_scale_feature("MaxPhysPrimScale", policies.physical_prim_max);
                auto grid_features = server::grid_extra_features(config);
                const auto remote_grid_uri = config.get("GridService", "GridServerURI");
                if (!remote_grid_uri.empty()) {
                    std::optional<std::pair<std::string, std::string>> auth;
                    if (config.get("GridService", "AuthType", config.get("Network", "AuthType")) ==
                        "BasicHttpAuthentication") {
                        auth.emplace(config.get("GridService", "HttpAuthUsername",
                                                config.get("Network", "HttpAuthUsername")),
                                     config.get("GridService", "HttpAuthPassword",
                                                config.get("Network", "HttpAuthPassword")));
                    }
                    remote_grid_request = [uri = remote_grid_uri + "/grid",
                                           request = network::rest_forms_transport(outgoing_clients(), auth)](
                                              const services::OrderedRegionFields& fields) {
                        const auto reply = request(uri, network::build_service_form(fields));
                        return reply.empty() ? network::ServiceXmlValue{} : network::parse_service_xml(reply);
                    };
                }
                if (grid_features.fields.empty() && remote_grid_request) {
                    try {
                        grid_features = remote_grid_request({{"METHOD", "get_grid_extra_features"}});
                    } catch (const std::exception& error) {
                        error_log(std::string("[GRID CONNECTOR]: GetExtraFeatures failed: ") + error.what());
                    }
                }
                if (std::any_of(grid_features.fields.begin(), grid_features.fields.end(),
                                [](const auto& field) {
                                    return field.first == "Result" && field.second.text == "Failure";
                                })) {
                    error_log("[SIMULATOR FEATURES MODULE]: Unable to retrieve grid-wide features");
                    grid_features.fields.clear();
                }
                for (const auto& [name, feature] : grid_features.fields) {
                    if (name == "currency-base-uri")
                        continue;
                    auto export_flag = framework::trim_utf8(feature.text);
                    for (auto& c : export_flag)
                        if (c >= 'A' && c <= 'Z')
                            c += 'a' - 'A';
                    const auto value = name == "ExportSupported" ? network::Value(export_flag == "true")
                                                                 : network::Value(feature.text);
                    simulator_features->add_extra(name, services::SimulatorFeatureValue::from_value(value));
                }
                constexpr auto sound_module = "OpenSim.Region.CoreModules.dll:SoundModule";
                if (sample_economy_enabled) {
                    policies.sample_economy_sell_enabled = config.boolean("Economy", "SellEnabled", true);
                    auto packet = network::lludp::make_packet("EconomyData");
                    packet.reliable = true;
                    auto& info = packet.block("Info");
                    info["ObjectCapacity"] = std::int64_t(region_settings.integer("NativeRegion", "MaxPrims", 15000));
                    const std::pair<const char*, int> prices[]{{"PriceEnergyUnit", 0},
                                                               {"PriceGroupCreate", -1},
                                                               {"PriceObjectClaim", 0},
                                                               {"PriceParcelClaim", 0},
                                                               {"PriceParcelRent", 0},
                                                               {"PricePublicObjectDecay", 0},
                                                               {"PricePublicObjectDelete", 0},
                                                               {"PriceRentLight", 0},
                                                               {"PriceUpload", 0},
                                                               {"TeleportMinPrice", 0}};
                    for (const auto& [key, fallback] : prices)
                        info[key] = std::int64_t(config.integer(
                            "Economy", key,
                            config.has_section("Economy") && std::string_view(key) == "PricePublicObjectDecay"
                                ? 4
                                : fallback));
                    const std::pair<const char*, float> factors[]{{"EnergyEfficiency", 1.f},
                                                                  {"PriceObjectRent", 0.f},
                                                                  {"PriceObjectScaleFactor", 10.f},
                                                                  {"PriceParcelClaimFactor", 1.f},
                                                                  {"TeleportPriceExponent", 2.f}};
                    for (const auto& [key, fallback] : factors) {
                        float value;
                        if (!framework::parse_invariant_float(config.get("Economy", key), value))
                            value = fallback;
                        info[key] = static_cast<double>(value);
                    }
                    policies.sample_economy_data = std::move(packet);
                }
                if (config.get("Sounds", "Module", sound_module) != sound_module)
                    policies.sound_max_distance.reset();
                else {
                    float distance;
                    if (framework::parse_invariant_float(config.get("Sounds", "MaxDistance", "100"),
                                                         distance))
                        policies.sound_max_distance = distance;
                }
                policies.object_capacity = static_cast<std::uint32_t>(
                    std::max(0, region_settings.integer("NativeRegion", "MaxPrims", 15000)));
                region_runtime =
                    std::make_shared<scene::RegionRuntime>(std::move(region), *region_presence, std::move(policies));
                const auto initialize_terrain = [&] {
                    if (config.get("Terrain", "InitialTerrain", "pinhead-island") != "flat")
                        region_runtime->world().terrain().initialize_island();
                };
                if (!region_settings.get("NativeRegion", "TerrainDatabase").empty()) {
                    auto path = framework::utf8_path(region_settings.get("NativeRegion", "TerrainDatabase"));
                    if (path.is_relative())
                        path = config.directory / path;
                    const auto path_bytes = path.u8string();
                    auto database = std::make_shared<services::SqliteDatabase>();
                    database->open_path(
                        std::string(reinterpret_cast<const char*>(path_bytes.data()), path_bytes.size()),
                        true);
                    auto tables = std::make_shared<services::SqliteTableOperations>(
                        [database] { return database; }, "terrain");
                    bool has_object_schema = false;
                    tables->query(
                        "SELECT name FROM sqlite_master WHERE type='table' AND name IN ('prims','primshapes','primitems')",
                        {}, {"name"}, [&](const auto&, const auto&) { has_object_schema = true; });
                    struct RegionResources final : services::MigrationResources {
                        std::vector<std::string> names() override {
                            return {"OpenSim.Data.SQLite.Resources.RegionStore.migrations"};
                        }
                        std::optional<std::string> bytes(const std::string& name) override {
                            if (name == names().front())
                                return services::RegionMigration;
                            return std::nullopt;
                        }
                    };
                    services::Migration migration(services::sqlite_migration_database(database->handle()),
                                                  std::make_shared<RegionResources>(), "RegionStore");
                    if (!has_object_schema || migration.version() > 0)
                        migration.update();
                    tables->execute(
                        "CREATE TABLE IF NOT EXISTS terrain(RegionUUID varchar(255), Revision integer, Heightfield blob);"
                        "CREATE TABLE IF NOT EXISTS bakedterrain(RegionUUID varchar(255), Revision integer, Heightfield blob)",
                        {});
                    const auto id = region_runtime->descriptor().id.str();
                    bool has_region_environment = false;
                    tables->query("SELECT name FROM sqlite_master WHERE type='table' AND name='regionenvironment'",
                                  {}, {"name"}, [&](const auto&, const auto&) { has_region_environment = true; });
                    if (has_region_environment) {
                        std::optional<std::string> migrated_settings;
                        tables->query("SELECT llsd_settings FROM regionenvironment WHERE region_id=:id",
                                      {{":id", id}}, {"llsd_settings"}, [&](const auto& row, const auto&) {
                            bool migrated = false;
                            if (const auto day = saved_environment_day(row.value_text(0), &migrated)) {
                                if (migrated)
                                    migrated_settings = network::llsd_xml(*day->settings);
                                region_runtime->load_environment_day(*day);
                            }
                        });
                        if (migrated_settings)
                            tables->execute("UPDATE regionenvironment SET llsd_settings=:settings WHERE region_id=:id",
                                            {{":settings", migrated_settings}, {":id", id}});
                    }
                    region_persistence.save_environment = [tables, id](std::optional<framework::Uuid> parcel,
                                                    std::optional<std::string> settings) {
                        if (parcel) {
                            bool has_environment_column = false;
                            tables->query("SELECT name FROM pragma_table_info('land') WHERE name='environment'",
                                          {}, {"name"}, [&](const auto&, const auto&) { has_environment_column = true; });
                            if (!has_environment_column)
                                tables->execute("ALTER TABLE land ADD COLUMN environment TEXT", {});
                            if (tables->execute("UPDATE land SET environment=:settings WHERE UUID=:parcel AND RegionUUID=:id",
                                                {{":settings", settings}, {":parcel", parcel->str()}, {":id", id}}) != 1)
                                throw std::runtime_error("Missing parcel row for environment update");
                            return;
                        }
                        tables->execute("CREATE TABLE IF NOT EXISTS regionenvironment "
                                        "(region_id TEXT PRIMARY KEY, llsd_settings TEXT)", {});
                        if (!settings) {
                            tables->execute("DELETE FROM regionenvironment WHERE region_id=:id", {{":id", id}});
                        } else {
                            tables->execute("INSERT INTO regionenvironment(region_id,llsd_settings) VALUES(:id,:settings) "
                                            "ON CONFLICT(region_id) DO UPDATE SET llsd_settings=excluded.llsd_settings",
                                            {{":id", id}, {":settings", settings}});
                        }
                    };
                    bool has_prims = false, has_shapes = false, has_items = false;
                    tables->query(
                        "SELECT name FROM sqlite_master WHERE type='table' AND name IN ('prims','primshapes','primitems')",
                        {}, {"name"}, [&](const auto& row, const auto&) {
                            const auto name = row.string_value(0);
                            has_prims |= name == "prims";
                            has_shapes |= name == "primshapes";
                            has_items |= name == "primitems";
                        });
                    if (has_prims && has_items)
                        tables->execute("CREATE TABLE IF NOT EXISTS native_script_states ("
                                        "region_id TEXT NOT NULL, part_id TEXT NOT NULL, item_id TEXT NOT NULL, "
                                        "asset_id TEXT NOT NULL, checkpoint BLOB NOT NULL, "
                                        "PRIMARY KEY (region_id, part_id, item_id))", {});
                    using InventoryRows = std::vector<services::SqliteTableParameters>;
                    const auto inventory_rows = [](const std::vector<framework::TaskInventoryItem>& items) {
                        InventoryRows rows;
                        for (const auto& item : items)
                            rows.push_back({{":item", item.item_id.str()},
                                            {":part", item.parent_part_id.str()},
                                            {":asset", item.asset_id.str()},
                                            {":parent", item.parent_id.str()},
                                            {":inv", std::to_string(item.inventory_type)},
                                            {":type", std::to_string(item.type)},
                                            {":name", item.name.value_or("")},
                                            {":description", item.description.value_or("")},
                                            {":created", std::to_string(item.creation_date)},
                                            {":creator", item.creator_identification()},
                                            {":owner", item.owner_id.str()},
                                            {":last", item.last_owner_id.str()},
                                            {":group", item.group_id.str()},
                                            {":next", std::to_string(item.next_permissions)},
                                            {":current", std::to_string(item.current_permissions)},
                                            {":base", std::to_string(item.base_permissions)},
                                            {":everyone", std::to_string(item.everyone_permissions)},
                                            {":groupmask", std::to_string(item.group_permissions)},
                                            {":flags", std::to_string(item.flags)}});
                        return rows;
                    };
                    auto saved_inventory = std::make_shared<std::map<framework::Uuid, InventoryRows>>();
                    if (has_prims) {
                        std::vector<std::pair<framework::Uuid, scene::SceneObject>> stored_parts;
                        const auto sql = std::string("SELECT p.*") + (has_shapes ? ",s.*" : "") +
                                         " FROM prims p" +
                                         (has_shapes ? " LEFT JOIN primshapes s ON s.UUID=p.UUID" : "") +
                                         " WHERE p.RegionUUID=:id ORDER BY p.UUID";
                        tables->query(sql, {{":id", id}}, {}, [&](const auto& row, const auto&) {
                            try {
                                const auto text = [&](const char* key) {
                                    return row.value_text(row.column(key));
                                };
                                const auto optional_text = [&](const char* key) {
                                    try {
                                        return text(key);
                                    } catch (const std::out_of_range&) {
                                        return std::string{};
                                    }
                                };
                                const auto read_optional_blob = [&](const char* key, auto& destination) {
                                    try {
                                        const auto bytes = row.blob_value(row.column(key));
                                        if (!bytes.empty())
                                            destination.assign(bytes.begin(), bytes.end());
                                    } catch (const std::out_of_range&) {
                                        // Older region databases may omit this column.
                                    } catch (const std::invalid_argument&) {
                                        // Optional blobs can be NULL.
                                    }
                                };
                                const auto integer = [&](const char* key) {
                                    return row.int32_value(row.column(key));
                                };
                                const auto real = [&](const char* key, float fallback = 0.f) {
                                    float value;
                                    return framework::parse_invariant_float(text(key), value) &&
                                                   std::isfinite(value)
                                               ? value
                                               : fallback;
                                };
                                const auto uuid = [&](const char* key) {
                                    framework::Uuid value;
                                    framework::try_parse_sdk_uuid(text(key), value);
                                    return value;
                                };
                                scene::SceneObject object;
                                object.id = uuid("UUID");
                                const auto group = uuid("SceneGroupID");
                                if (object.id.is_zero() || group.is_zero())
                                    return;
                                if (object.id == group) {
                                    if (auto start = optional_text("StartStr"); !start.empty())
                                        object.rez_start_string = std::move(start);
                                    try {
                                        const auto data = row.blob_value(row.column("lnkstBinData"));
                                        object.linkset_data.assign(data.begin(), data.end());
                                    } catch (const std::out_of_range&) {
                                    } catch (const std::invalid_argument&) {
                                    }
                                }
                                object.name = text("Name");
                                object.description = text("Description");
                                object.owner = uuid("OwnerID");
                                object.group = uuid("GroupID");
                                object.last_owner = uuid("LastOwnerID");
                                framework::try_parse_sdk_uuid(optional_text("RezzerID"), object.rezzer);
                                object.touch_name = optional_text("TouchName");
                                object.sit_name = optional_text("SitName");
                                object.floating_text = optional_text("Text");
                                object.media_url = optional_text("MediaURL");
                                framework::try_parse_sdk_uuid(optional_text("LoopedSound"),
                                                              object.looped_sound);
                                framework::try_parse_sdk_uuid(optional_text("CollisionSound"),
                                                              object.collision_sound);
                                object.sound_flags = object.looped_sound.is_zero() ? 0 : 1;
                                for (const auto* channel : {"ColorA", "ColorR", "ColorG", "ColorB"}) {
                                    std::uint32_t value = 0;
                                    const auto color = optional_text(channel);
                                    std::from_chars(color.data(), color.data() + color.size(), value);
                                    object.text_argb = (object.text_argb << 8) | (value & 255u);
                                }
                                const auto creator = text("CreatorID");
                                framework::try_parse_sdk_uuid(creator.substr(0, creator.find(';')),
                                                              object.creator);
                                object.creation_date = integer("CreationDate");
                                object.flags = static_cast<std::uint32_t>(integer("ObjectFlags"));
                                const auto die_at_edge = optional_text("DieAtEdge");
                                int edge_flag = 0;
                                std::from_chars(die_at_edge.data(), die_at_edge.data() + die_at_edge.size(), edge_flag);
                                object.die_at_edge = edge_flag != 0;
                                if (const auto allowed_drop = optional_text("AllowedDrop"); !allowed_drop.empty()) {
                                    if (allowed_drop == "1" ||
                                        framework::ordinal_compare_ignore_case(allowed_drop, "true") == 0)
                                        object.flags |= 0x10000u;
                                    else
                                        object.flags &= ~0x10000u;
                                }
                                object.base_mask = static_cast<std::uint32_t>(integer("BaseMask"));
                                object.owner_mask = static_cast<std::uint32_t>(integer("OwnerMask"));
                                object.group_mask = static_cast<std::uint32_t>(integer("GroupMask"));
                                object.everyone_mask = static_cast<std::uint32_t>(integer("EveryoneMask"));
                                object.next_mask = static_cast<std::uint32_t>(integer("NextOwnerMask"));
                                object.sale_type = static_cast<std::uint8_t>(integer("SaleType"));
                                object.sale_price = integer("SalePrice");
                                const std::array pay_columns{"PayPrice", "PayButton1", "PayButton2",
                                                             "PayButton3", "PayButton4"};
                                for (std::size_t i = 0; i < pay_columns.size(); ++i) {
                                    const auto value = optional_text(pay_columns[i]);
                                    std::from_chars(value.data(), value.data() + value.size(),
                                                    object.pay_prices[i]);
                                }
                                object.material = static_cast<std::uint8_t>(integer("Material"));
                                object.click_action = static_cast<std::uint8_t>(integer("ClickAction"));
                                const auto pass_touches = optional_text("PassTouches");
                                object.pass_touches = pass_touches == "1" ||
                                                      framework::ordinal_compare_ignore_case(pass_touches, "true") == 0;
                                object.position =
                                    object.id == group
                                        ? framework::Vector3{real("GroupPositionX"), real("GroupPositionY"),
                                                             real("GroupPositionZ")}
                                        : framework::Vector3{real("PositionX"), real("PositionY"),
                                                             real("PositionZ")};
                                object.rotation = {real("RotationX"), real("RotationY"), real("RotationZ"),
                                                   real("RotationW", 1.f)};
                                const auto optional_real = [&](const char* key, float fallback = 0.f) {
                                    float value;
                                    return framework::parse_invariant_float(optional_text(key), value) &&
                                                   std::isfinite(value)
                                               ? value
                                               : fallback;
                                };
                                object.saved_attachment_position = {optional_real("AttachedPosX"),
                                                                    optional_real("AttachedPosY"),
                                                                    optional_real("AttachedPosZ")};
                                object.velocity = {optional_real("VelocityX"), optional_real("VelocityY"),
                                                   optional_real("VelocityZ")};
                                object.sit_target_position = {optional_real("SitTargetOffsetX"),
                                                              optional_real("SitTargetOffsetY"),
                                                              optional_real("SitTargetOffsetZ")};
                                object.sit_active_range = optional_real("sitactrange");
                                object.stand_target = {optional_real("standtargetx"), optional_real("standtargety"),
                                                       optional_real("standtargetz")};
                                object.camera_eye_offset = {optional_real("CameraEyeOffsetX"),
                                                             optional_real("CameraEyeOffsetY"),
                                                             optional_real("CameraEyeOffsetZ")};
                                object.camera_at_offset = {optional_real("CameraAtOffsetX"),
                                                            optional_real("CameraAtOffsetY"),
                                                            optional_real("CameraAtOffsetZ")};
                                object.force_mouselook = optional_real("ForceMouselook") != 0;
                                const auto script_pin = optional_text("ScriptAccessPin");
                                std::from_chars(script_pin.data(), script_pin.data() + script_pin.size(),
                                                object.script_access_pin);
                                object.sit_target_orientation = {optional_real("SitTargetOrientX"),
                                                                 optional_real("SitTargetOrientY"),
                                                                 optional_real("SitTargetOrientZ"),
                                                                 optional_real("SitTargetOrientW", 1.f)};
                                object.looped_sound_gain = optional_real("LoopedSoundGain");
                                object.collision_sound_volume =
                                    std::max(0.f, optional_real("CollisionSoundVolume"));
                                object.angular_velocity = {optional_real("AngularVelocityX"),
                                                           optional_real("AngularVelocityY"),
                                                           optional_real("AngularVelocityZ")};
                                object.acceleration = {optional_real("AccelerationX"),
                                                       optional_real("AccelerationY"),
                                                       optional_real("AccelerationZ")};
                                object.inertia_override = scene::read_prim_inertia_xml(optional_text("PhysInertia"));
                                const auto locks = optional_text("RotationAxisLocks");
                                std::from_chars(locks.data(), locks.data() + locks.size(), object.rotation_axis_locks);
                                const auto shape_type = optional_text("PhysicsShapeType");
                                if (shape_type == "1" && object.id != group)
                                    object.physics.shape = 1;
                                else if (shape_type == "2")
                                    object.physics.shape = 2;
                                object.physics.density = optional_real("Density", object.physics.density);
                                object.physics.friction = optional_real("Friction", object.physics.friction);
                                object.physics.restitution =
                                    optional_real("Restitution", object.physics.restitution);
                                object.physics.gravity =
                                    optional_real("GravityModifier", object.physics.gravity);
                                if (has_shapes && !text("PCode").empty()) {
                                    object.face_media_xml = optional_text("Media");
                                    object.shape.pcode = static_cast<std::uint8_t>(integer("PCode"));
                                    object.shape.state = static_cast<std::uint8_t>(integer("State"));
                                    const auto last_point = optional_text("LastAttachPoint");
                                    std::from_chars(last_point.data(), last_point.data() + last_point.size(),
                                                    object.last_attachment_point);
                                    object.shape.path_begin =
                                        static_cast<std::uint16_t>(integer("PathBegin"));
                                    object.shape.path_end = static_cast<std::uint16_t>(integer("PathEnd"));
                                    object.shape.profile_begin =
                                        static_cast<std::uint16_t>(integer("ProfileBegin"));
                                    object.shape.profile_end =
                                        static_cast<std::uint16_t>(integer("ProfileEnd"));
                                    object.shape.profile_hollow =
                                        static_cast<std::uint16_t>(integer("ProfileHollow"));
                                    object.shape.path_scale_x =
                                        static_cast<std::uint8_t>(integer("PathScaleX"));
                                    object.shape.path_scale_y =
                                        static_cast<std::uint8_t>(integer("PathScaleY"));
                                    object.shape.path_shear_x =
                                        static_cast<std::uint8_t>(integer("PathShearX"));
                                    object.shape.path_shear_y =
                                        static_cast<std::uint8_t>(integer("PathShearY"));
                                    object.shape.path_curve = static_cast<std::uint8_t>(integer("PathCurve"));
                                    object.shape.path_revolutions =
                                        static_cast<std::uint8_t>(integer("PathRevolutions"));
                                    object.shape.profile_curve =
                                        static_cast<std::uint8_t>(integer("ProfileCurve"));
                                    object.shape.path_skew = static_cast<std::int8_t>(integer("PathSkew"));
                                    object.shape.path_radius_offset =
                                        static_cast<std::int8_t>(integer("PathRadiusOffset"));
                                    object.shape.path_taper_x =
                                        static_cast<std::int8_t>(integer("PathTaperX"));
                                    object.shape.path_taper_y =
                                        static_cast<std::int8_t>(integer("PathTaperY"));
                                    object.shape.path_twist = static_cast<std::int8_t>(integer("PathTwist"));
                                    object.shape.path_twist_begin =
                                        static_cast<std::int8_t>(integer("PathTwistBegin"));
                                    object.scale = {real("ScaleX", .5f), real("ScaleY", .5f),
                                                    real("ScaleZ", .5f)};
                                    const auto texture = row.blob_value(row.column("Texture"));
                                    object.texture_entry.assign(texture.begin(), texture.end());
                                    read_optional_blob("ExtraParams", object.extra_params);
                                    read_optional_blob("MatOvrd", object.material_overrides);
                                }
                                read_optional_blob("sopanims", object.object_animations);
                                const auto read_base64 = [&](const char* column, auto& bytes) {
                                    auto value = optional_text(column);
                                    if (value.empty())
                                        return;
                                    network::Value encoded;
                                    encoded.kind = network::Value::Kind::Binary;
                                    encoded.text = std::move(value);
                                    bytes = *network::osd_binary(encoded);
                                };
                                read_base64("TextureAnimation", object.texture_animation);
                                read_base64("ParticleSystem", object.particle_system);
                                object.dynamic_attributes_xml = optional_text("DynAttrs");
                                if (object.shape.pcode == 9 ||
                                    (group == object.id && scene::is_vegetation(object.shape.pcode)))
                                    stored_parts.emplace_back(group, std::move(object));
                            } catch (const std::exception& error) {
                                error_log(std::string("Failed to load stored prim: ") + error.what());
                            }
                        });
                        auto& world = region_runtime->world();
                        for (auto& [group, part] : stored_parts)
                            if (group == part.id)
                                world.add(part, true, false);
                        for (auto& [group, part] : stored_parts)
                            if (group != part.id)
                                if (const auto root = world.object(group)) {
                                    part.parent_id = root->local_id;
                                    world.add(part, true, false);
                                }
                        using PrimFields = std::map<std::string, std::string>;
                        std::set<std::string> prim_columns;
                        tables->query("SELECT lower(name) AS name FROM pragma_table_info('prims')", {}, {},
                                      [&](const auto& row, const auto&) {
                                          prim_columns.insert(row.value_text(row.column("name")));
                                      });
                        std::set<std::string> shape_columns;
                        if (has_shapes)
                            tables->query("SELECT name FROM pragma_table_info('primshapes')", {}, {},
                                          [&](const auto& row, const auto&) {
                                              shape_columns.insert(row.value_text(row.column("name")));
                                          });
                        std::set<framework::Uuid> stored_shapes;
                        if (has_shapes)
                            tables->query("SELECT s.UUID FROM primshapes s JOIN prims p ON p.UUID=s.UUID "
                                          "WHERE p.RegionUUID=:id",
                                          {{":id", id}}, {}, [&](const auto& row, const auto&) {
                                              framework::Uuid key;
                                              if (framework::try_parse_sdk_uuid(
                                                      row.value_text(row.column("UUID")), key))
                                                  stored_shapes.insert(key);
                                          });
                        const auto metadata = [prim_columns, shape_columns](const scene::SceneObject& part) {
                            PrimFields fields{{"Name", part.name},
                                              {"Description", part.description},
                                              {"SaleType", std::to_string(part.sale_type)},
                                              {"SalePrice", std::to_string(part.sale_price)},
                                              {"Material", std::to_string(part.material)},
                                              {"ClickAction", std::to_string(part.click_action)}};
                            const auto add = [&](const std::string& column, std::string value) {
                                std::string key = column;
                                for (auto& c : key)
                                    if (c >= 'A' && c <= 'Z')
                                        c += 'a' - 'A';
                                if (prim_columns.contains(key))
                                    fields.emplace(column, std::move(value));
                            };
                            add("PhysicsShapeType", std::to_string(part.physics.shape));
                            add("sopanims", {part.object_animations.begin(), part.object_animations.end()});
                            add("TextureAnimation", network::binary_base64(network::osd_from_binary(
                                std::make_shared<std::vector<std::uint8_t>>(part.texture_animation))));
                            add("ParticleSystem", network::binary_base64(network::osd_from_binary(
                                std::make_shared<std::vector<std::uint8_t>>(part.particle_system))));
                            add("DynAttrs", part.dynamic_attributes_xml);
                            add("PassTouches", part.pass_touches ? "1" : "0");
                            add("SitTargetOffsetX", services::avatar_float_string(part.sit_target_position.x));
                            add("sitactrange", services::avatar_float_string(part.sit_active_range));
                            add("standtargetx", services::avatar_float_string(part.stand_target.x));
                            add("standtargety", services::avatar_float_string(part.stand_target.y));
                            add("standtargetz", services::avatar_float_string(part.stand_target.z));
                            add("CameraEyeOffsetX", services::avatar_float_string(part.camera_eye_offset.x));
                            add("CameraEyeOffsetY", services::avatar_float_string(part.camera_eye_offset.y));
                            add("CameraEyeOffsetZ", services::avatar_float_string(part.camera_eye_offset.z));
                            add("CameraAtOffsetX", services::avatar_float_string(part.camera_at_offset.x));
                            add("CameraAtOffsetY", services::avatar_float_string(part.camera_at_offset.y));
                            add("CameraAtOffsetZ", services::avatar_float_string(part.camera_at_offset.z));
                            add("ForceMouselook", part.force_mouselook ? "1" : "0");
                            add("ScriptAccessPin", std::to_string(part.script_access_pin));
                            add("SitTargetOffsetY", services::avatar_float_string(part.sit_target_position.y));
                            add("SitTargetOffsetZ", services::avatar_float_string(part.sit_target_position.z));
                            add("SitTargetOrientX", services::avatar_float_string(part.sit_target_orientation.x));
                            add("SitTargetOrientY", services::avatar_float_string(part.sit_target_orientation.y));
                            add("SitTargetOrientZ", services::avatar_float_string(part.sit_target_orientation.z));
                            add("SitTargetOrientW", services::avatar_float_string(part.sit_target_orientation.w));
                            add("AllowedDrop", (part.flags & 0x10000u) ? "1" : "0");
                            add("Density", services::avatar_float_string(part.physics.density));
                            add("RotationAxisLocks", std::to_string(part.rotation_axis_locks));
                            add("PhysInertia", part.inertia_override ? scene::write_prim_inertia_xml(*part.inertia_override)
                                                                     : std::string{});
                            add("GravityModifier", services::avatar_float_string(part.physics.gravity));
                            add("Friction", services::avatar_float_string(part.physics.friction));
                            add("Restitution", services::avatar_float_string(part.physics.restitution));
                            add("RezzerID", part.rezzer.str());
                            add("lnkstBinData", {part.linkset_data.begin(), part.linkset_data.end()});
                            if (part.rez_start_string)
                                add("StartStr", *part.rez_start_string);
                            add("TouchName", part.touch_name);
                            add("SitName", part.sit_name);
                            add("Text", part.floating_text);
                            add("MediaURL", part.media_url);
                            add("LoopedSound", part.looped_sound.str());
                            add("LoopedSoundGain", services::avatar_float_string(part.looped_sound_gain));
                            add("CollisionSound", part.collision_sound.str());
                            add("CollisionSoundVolume",
                                services::avatar_float_string(part.collision_sound_volume));
                            add("PayPrice", std::to_string(part.pay_prices.front()));
                            for (std::size_t i = 1; i < part.pay_prices.size(); ++i)
                                add("PayButton" + std::to_string(i), std::to_string(part.pay_prices[i]));
                            add("ColorA", std::to_string(part.text_argb >> 24));
                            add("ColorR", std::to_string((part.text_argb >> 16) & 255u));
                            add("ColorG", std::to_string((part.text_argb >> 8) & 255u));
                            add("ColorB", std::to_string(part.text_argb & 255u));
                            add("ObjectFlags", std::to_string(part.flags));
                            add("DieAtEdge", part.die_at_edge ? "1" : "0");
                            const auto motion = [&](const std::string& prefix,
                                                    const framework::Vector3& value) {
                                add(prefix + "X", services::avatar_float_string(value.x));
                                add(prefix + "Y", services::avatar_float_string(value.y));
                                add(prefix + "Z", services::avatar_float_string(value.z));
                            };
                            motion("Velocity", part.velocity);
                            motion("AngularVelocity", part.angular_velocity);
                            motion("Acceleration", part.acceleration);
                            motion("AttachedPos", part.saved_attachment_position);
                            if (!shape_columns.empty()) {
                                const auto& shape = part.shape;
                                const std::pair<const char*, int> geometry[]{
                                    {"PCode", shape.pcode},
                                    {"State", shape.state},
                                    {"LastAttachPoint", part.last_attachment_point},
                                    {"PathBegin", shape.path_begin},
                                    {"PathEnd", shape.path_end},
                                    {"PathScaleX", shape.path_scale_x},
                                    {"PathScaleY", shape.path_scale_y},
                                    {"PathShearX", shape.path_shear_x},
                                    {"PathShearY", shape.path_shear_y},
                                    {"PathSkew", shape.path_skew},
                                    {"PathCurve", shape.path_curve},
                                    {"PathRadiusOffset", shape.path_radius_offset},
                                    {"PathRevolutions", shape.path_revolutions},
                                    {"PathTaperX", shape.path_taper_x},
                                    {"PathTaperY", shape.path_taper_y},
                                    {"PathTwist", shape.path_twist},
                                    {"PathTwistBegin", shape.path_twist_begin},
                                    {"ProfileBegin", shape.profile_begin},
                                    {"ProfileEnd", shape.profile_end},
                                    {"ProfileCurve", shape.profile_curve},
                                    {"ProfileHollow", shape.profile_hollow}};
                                for (const auto& [column, value] : geometry)
                                    fields.emplace(std::string("shape.") + column, std::to_string(value));
                                fields["shape.ScaleX"] = services::avatar_float_string(part.scale.x);
                                fields["shape.ScaleY"] = services::avatar_float_string(part.scale.y);
                                fields["shape.ScaleZ"] = services::avatar_float_string(part.scale.z);
                                fields["shape.Texture"] = {part.texture_entry.begin(),
                                                           part.texture_entry.end()};
                                fields["shape.ExtraParams"] = {part.extra_params.begin(),
                                                               part.extra_params.end()};
                                fields["shape.MatOvrd"] = {part.material_overrides.begin(),
                                                            part.material_overrides.end()};
                                fields["shape.Shape"] = "0";
                                if (!part.face_media_xml.empty())
                                    fields["shape.Media"] = part.face_media_xml;
                                std::erase_if(fields, [&](const auto& field) {
                                    return field.first.starts_with("shape.") &&
                                           !shape_columns.contains(field.first.substr(6));
                                });
                            }
                            return fields;
                        };
                        const auto transform = [](const scene::SceneObject& part, PrimFields& fields) {
                            const auto position = part.parent_id ? "Position" : "GroupPosition";
                            fields[std::string(position) + "X"] =
                                services::avatar_float_string(part.position.x);
                            fields[std::string(position) + "Y"] =
                                services::avatar_float_string(part.position.y);
                            fields[std::string(position) + "Z"] =
                                services::avatar_float_string(part.position.z);
                            fields["RotationX"] = services::avatar_float_string(part.rotation.x);
                            fields["RotationY"] = services::avatar_float_string(part.rotation.y);
                            fields["RotationZ"] = services::avatar_float_string(part.rotation.z);
                            fields["RotationW"] = services::avatar_float_string(part.rotation.w);
                        };
                        std::map<framework::Uuid, PrimFields> saved;
                        for (const auto& [group, part] : stored_parts)
                            if (const auto loaded = world.object(part.id)) {
                                auto fields = metadata(part);
                                transform(*loaded, fields);
                                fields["SceneGroupID"] = group.str();
                                saved.emplace(part.id, std::move(fields));
                            }
                        using ScriptStates = std::map<framework::Uuid, scene::RegionRuntime::TaskScriptStates>;
                        region_persistence.save_prim_metadata = [database, tables, id, &world, runtime = region_runtime.get(),
                                              metadata, transform, has_shapes, has_items, prim_columns,
                                              inventory_rows, saved_inventory, saved = std::move(saved),
                                              stored_shapes = std::move(stored_shapes),
                                              saved_script_states = std::optional<ScriptStates>{}]() mutable {
                            std::vector<std::pair<framework::Uuid, PrimFields>> changed;
                            std::set<framework::Uuid> inserted;
                            std::set<framework::Uuid> inserted_shapes;
                            std::map<framework::Uuid, InventoryRows> inventory_changes;
                            ScriptStates script_states;
                            const auto parts = world.objects();
                            std::set<framework::Uuid> present;
                            std::set<std::uint32_t> temporary_roots;
                            std::set<std::uint32_t> attachment_roots;
                            std::map<std::uint32_t, framework::Uuid> stored_roots;
                            for (const auto& part : parts) {
                                present.insert(part.id);
                                if (part.is_attachment)
                                    attachment_roots.insert(part.local_id);
                                if (!part.is_attachment && !part.parent_id && (part.flags & 0x20000000u))
                                    temporary_roots.insert(part.local_id);
                                if (!part.is_attachment && !part.parent_id && !(part.flags & 0x20000000u) &&
                                    (saved.contains(part.id) || (has_shapes && part.attached_to_backup)))
                                    stored_roots.emplace(part.local_id, part.id);
                            }
                            std::vector<framework::Uuid> removed;
                            for (const auto& [key, previous] : saved)
                                if (!present.contains(key))
                                    removed.push_back(key);
                            for (const auto& part : parts) {
                                const auto previous = saved.find(part.id);
                                const auto root_local = part.parent_id ? part.parent_id : part.local_id;
                                if (part.is_attachment || attachment_roots.contains(root_local)) {
                                    if (previous != saved.end())
                                        removed.push_back(part.id);
                                    continue;
                                }
                                if (previous != saved.end() && temporary_roots.contains(root_local)) {
                                    removed.push_back(part.id);
                                    continue;
                                }
                                const auto root = stored_roots.find(root_local);
                                const bool is_new = previous == saved.end();
                                if (root == stored_roots.end() || (is_new && !has_shapes))
                                    continue;
                                auto current = metadata(part);
                                if (has_items)
                                    if (const auto snapshot = runtime->snapshot_task_inventory(part.id)) {
                                        auto rows = inventory_rows(*snapshot);
                                        const auto old = saved_inventory->find(part.id);
                                        if (old == saved_inventory->end() ? !rows.empty()
                                                                          : old->second != rows)
                                            inventory_changes.emplace(part.id, std::move(rows));
                                        auto states = runtime->snapshot_task_script_states(part.id);
                                        if (!states.empty())
                                            script_states.emplace(part.id, std::move(states));
                                    }
                                current["SceneGroupID"] = root->second.str();
                                transform(part, current);
                                if (is_new) {
                                    current.insert({{"RegionUUID", id},
                                                    {"OwnerID", part.owner.str()},
                                                    {"GroupID", part.group.str()},
                                                    {"LastOwnerID", part.last_owner.str()},
                                                    {"CreatorID", part.creator.str()},
                                                    {"CreationDate", std::to_string(part.creation_date)},
                                                    {"ObjectFlags", std::to_string(part.flags)},
                                                    {"BaseMask", std::to_string(part.base_mask)},
                                                    {"OwnerMask", std::to_string(part.owner_mask)},
                                                    {"GroupMask", std::to_string(part.group_mask)},
                                                    {"EveryoneMask", std::to_string(part.everyone_mask)},
                                                    {"NextOwnerMask", std::to_string(part.next_mask)}});
                                    for (const auto* prefix : {"Position", "GroupPosition"})
                                        for (const char axis : std::string_view("XYZ"))
                                            current.try_emplace(std::string(prefix) + axis, "0");
                                    std::erase_if(current, [&](const auto& field) {
                                        if (field.first.starts_with("shape."))
                                            return false;
                                        auto column = field.first;
                                        for (auto& c : column)
                                            if (c >= 'A' && c <= 'Z')
                                                c += 'a' - 'A';
                                        return !prim_columns.contains(column);
                                    });
                                    inserted.insert(part.id);
                                } else
                                    for (auto field = current.begin(); field != current.end();)
                                        if (const auto old = previous->second.find(field->first);
                                            old != previous->second.end() && old->second == field->second)
                                            field = current.erase(field);
                                        else
                                            ++field;
                                if (!is_new && !stored_shapes.contains(part.id) &&
                                    std::any_of(current.begin(), current.end(), [](const auto& field) {
                                        return field.first.starts_with("shape.");
                                    })) {
                                    for (auto& [field, value] : metadata(part))
                                        if (field.starts_with("shape."))
                                            current.try_emplace(field, std::move(value));
                                    inserted_shapes.insert(part.id);
                                }
                                if (!current.empty())
                                    changed.emplace_back(part.id, std::move(current));
                            }
                            const bool script_states_changed = has_items && saved_script_states != script_states;
                            if (changed.empty() && removed.empty() && inventory_changes.empty() &&
                                !script_states_changed)
                                return;
                            std::lock_guard lock(database->mutex());
                            tables->execute("BEGIN IMMEDIATE", {});
                            try {
                                for (const auto& key : removed) {
                                    const services::SqliteTableParameters parameters{{":uuid", key.str()},
                                                                                     {":region", id}};
                                    if (has_items)
                                        tables->execute(
                                            "DELETE FROM primitems WHERE primID=:uuid AND "
                                            "EXISTS (SELECT 1 FROM prims WHERE UUID=:uuid AND RegionUUID=:region)",
                                            parameters);
                                    if (has_shapes)
                                        tables->execute(
                                            "DELETE FROM primshapes WHERE UUID=:uuid AND "
                                            "EXISTS (SELECT 1 FROM prims WHERE UUID=:uuid AND RegionUUID=:region)",
                                            parameters);
                                    tables->execute(
                                        "DELETE FROM prims WHERE UUID=:uuid AND RegionUUID=:region",
                                        parameters);
                                }
                                for (const auto& [key, current] : changed) {
                                    const auto is_blob_column = [](std::string_view column, bool shape) {
                                        if (shape)
                                            return column == "Texture" || column == "ExtraParams" ||
                                                   column == "MatOvrd";
                                        return column == "lnkstBinData" || column == "sopanims";
                                    };
                                    for (const bool shape : {false, true}) {
                                        if (inserted.contains(key) ||
                                            (shape && inserted_shapes.contains(key))) {
                                            std::string columns = "UUID", values = ":uuid";
                                            services::SqliteTableParameters parameters{{":uuid", key.str()}};
                                            for (const auto& [field, value] : current) {
                                                if (field.starts_with("shape.") != shape)
                                                    continue;
                                                const auto column = shape ? field.substr(6) : field;
                                                columns += "," + column;
                                                if (is_blob_column(column, shape))
                                                    values += ",CAST(:" + column + " AS BLOB)";
                                                else
                                                    values += ",:" + column;
                                                parameters.emplace_back(":" + column, value);
                                            }
                                            tables->execute(std::string("INSERT INTO ") +
                                                                (shape ? "primshapes" : "prims") + " (" +
                                                                columns + ") VALUES (" + values + ")",
                                                            parameters);
                                            continue;
                                        }
                                        std::string sql =
                                            shape ? "UPDATE primshapes SET " : "UPDATE prims SET ";
                                        services::SqliteTableParameters parameters{{":uuid", key.str()},
                                                                                   {":region", id}};
                                        for (const auto& [field, value] : current) {
                                            if (field.starts_with("shape.") != shape)
                                                continue;
                                            const auto column = shape ? field.substr(6) : field;
                                            if (parameters.size() > 2)
                                                sql += ",";
                                            sql += column + "=";
                                            if (is_blob_column(column, shape))
                                                sql += "CAST(:" + column + " AS BLOB)";
                                            else
                                                sql += ":" + column;
                                            parameters.emplace_back(":" + column, value);
                                        }
                                        if (parameters.size() > 2)
                                            tables->execute(
                                                sql +
                                                    (shape
                                                         ? " WHERE UUID=:uuid AND EXISTS (SELECT 1 FROM prims WHERE UUID=:uuid AND RegionUUID=:region)"
                                                         : " WHERE UUID=:uuid AND RegionUUID=:region"),
                                                parameters);
                                    }
                                }
                                for (const auto& [key, rows] : inventory_changes)
                                    tables->execute(
                                        "DELETE FROM primitems WHERE primID=:uuid AND "
                                        "EXISTS (SELECT 1 FROM prims WHERE UUID=:uuid AND RegionUUID=:region)",
                                        {{":uuid", key.str()}, {":region", id}});
                                for (const auto& [key, rows] : inventory_changes) {
                                    for (const auto& row : rows)
                                        tables->execute(
                                            "INSERT INTO primitems(itemID,primID,assetID,parentFolderID,invType,assetType,name,"
                                            "description,creationDate,creatorID,ownerID,lastOwnerID,groupID,nextPermissions,"
                                            "currentPermissions,basePermissions,everyonePermissions,groupPermissions,flags) "
                                            "VALUES(:item,:part,:asset,:parent,CAST(:inv AS INTEGER),CAST(:type AS INTEGER),"
                                            ":name,:description,CAST(:created AS INTEGER),:creator,:owner,:last,:group,"
                                            ":next,:current,:base,:everyone,:groupmask,CAST(:flags AS INTEGER))",
                                            row);
                                }
                                if (script_states_changed || (has_items && !inventory_changes.empty())) {
                                    tables->execute("DELETE FROM native_script_states WHERE region_id=:region",
                                                    {{":region", id}});
                                    for (const auto& [part, states] : script_states)
                                        for (const auto& [item, checkpoint] : states)
                                            tables->execute(
                                                "INSERT INTO native_script_states "
                                                "SELECT :region,i.primID,i.itemID,i.assetID,CAST(:state AS BLOB) "
                                                "FROM primitems i JOIN prims p ON p.UUID=i.primID "
                                                "WHERE p.RegionUUID=:region AND i.primID=:part AND i.itemID=:item",
                                                {{":region", id}, {":part", part.str()}, {":item", item.str()},
                                                 {":state", std::string(checkpoint.begin(), checkpoint.end())}});
                                }
                                tables->execute("COMMIT", {});
                            } catch (...) {
                                tables->execute("ROLLBACK", {});
                                throw;
                            }
                            for (auto& [key, current] : changed)
                                for (auto& [column, value] : current)
                                    saved[key][column] = std::move(value);
                            for (const auto& key : removed)
                                saved.erase(key);
                            for (const auto& key : removed)
                                stored_shapes.erase(key);
                            stored_shapes.insert(inserted.begin(), inserted.end());
                            stored_shapes.insert(inserted_shapes.begin(), inserted_shapes.end());
                            for (const auto& key : removed)
                                saved_inventory->erase(key);
                            for (auto& [key, rows] : inventory_changes)
                                (*saved_inventory)[key] = std::move(rows);
                            if (has_items)
                                saved_script_states = std::move(script_states);
                        };
                    }
                    if (has_prims && has_items) {
                        std::map<framework::Uuid, std::vector<std::shared_ptr<framework::TaskInventoryItem>>>
                            inventories;
                        std::map<framework::Uuid, scene::RegionRuntime::TaskScriptStates> script_states;
                        tables->query(
                            "SELECT i.*,COALESCE(s.checkpoint,X'') AS NativeCheckpoint FROM primitems i "
                            "JOIN prims p ON p.UUID=i.primID "
                            "LEFT JOIN native_script_states s ON s.region_id=p.RegionUUID AND s.part_id=i.primID "
                            "AND s.item_id=i.itemID AND s.asset_id=i.assetID "
                            "WHERE p.RegionUUID=:id ORDER BY i.primID,i.itemID",
                            {{":id", id}}, {}, [&](const auto& row, const auto&) {
                                try {
                                    const auto text = [&](const char* key) {
                                        return row.value_text(row.column(key));
                                    };
                                    const auto number = [&](const char* key) {
                                        return row.int32_value(row.column(key));
                                    };
                                    const auto uuid = [&](const char* key) {
                                        framework::Uuid value;
                                        framework::try_parse_sdk_uuid(text(key), value);
                                        return value;
                                    };
                                    auto item = std::make_shared<framework::TaskInventoryItem>();
                                    item->item_id = uuid("itemID");
                                    item->parent_part_id = uuid("primID");
                                    if (item->item_id.is_zero() || item->parent_part_id.is_zero())
                                        return;
                                    item->parent_id = uuid("parentFolderID");
                                    item->asset_id = uuid("assetID");
                                    item->owner_id = uuid("ownerID");
                                    item->last_owner_id = uuid("lastOwnerID");
                                    item->group_id = uuid("groupID");
                                    item->creator_identification(text("creatorID"));
                                    item->name = text("name");
                                    item->description = text("description");
                                    item->type = number("assetType");
                                    item->inventory_type = number("invType");
                                    item->creation_date = static_cast<std::uint32_t>(number("creationDate"));
                                    item->flags = static_cast<std::uint32_t>(number("flags"));
                                    item->next_permissions =
                                        static_cast<std::uint32_t>(number("nextPermissions"));
                                    item->current_permissions =
                                        static_cast<std::uint32_t>(number("currentPermissions"));
                                    item->base_permissions =
                                        static_cast<std::uint32_t>(number("basePermissions"));
                                    item->everyone_permissions =
                                        static_cast<std::uint32_t>(number("everyonePermissions"));
                                    item->group_permissions =
                                        static_cast<std::uint32_t>(number("groupPermissions"));
                                    if (item->inventory_type == 10) {
                                        const auto checkpoint = row.blob_value(row.column("NativeCheckpoint"));
                                        if (!checkpoint.empty())
                                            script_states[item->parent_part_id].emplace(
                                                item->item_id,
                                                std::vector<std::uint8_t>(checkpoint.begin(), checkpoint.end()));
                                    }
                                    inventories[item->parent_part_id].push_back(std::move(item));
                                } catch (const std::exception& error) {
                                    error_log(std::string("Failed to load stored task inventory: ") +
                                              error.what());
                                }
                            });
                        for (const auto& [part, items] : inventories) {
                            region_runtime->restore_task_inventory(
                                part, items, script_states[part],
                                scene::RegionRuntime::TaskInventoryRestoreSource::RegionStart);
                            if (const auto snapshot = region_runtime->snapshot_task_inventory(part))
                                saved_inventory->emplace(part, inventory_rows(*snapshot));
                        }
                    }
                    bool has_land = false;
                    tables->query("SELECT name FROM sqlite_master WHERE type='table' AND name='land'", {},
                                  {"name"}, [&](const auto&, const auto&) { has_land = true; });
                    if (has_land) {
                        const auto parcel_width = region_runtime->world().terrain().width() / 4;
                        const auto parcel_height = region_runtime->world().terrain().height() / 4;
                        const auto parcel_cell_count = std::size_t(parcel_width) * parcel_height;
                        std::unordered_map<framework::Uuid, std::vector<scene::ParcelAccessEntry>,
                                           framework::UuidHash>
                            parcel_access_lists;
                        bool has_land_access_list = false;
                        tables->query(
                            "SELECT name FROM sqlite_master WHERE type='table' AND name='landaccesslist'",
                            {}, {"name"}, [&](const auto&, const auto&) { has_land_access_list = true; });
                        if (has_land_access_list) {
                            tables->query(
                                "SELECT a.LandUUID, a.AccessUUID, a.Flags FROM landaccesslist a "
                                "JOIN land l ON l.UUID=a.LandUUID COLLATE NOCASE WHERE l.RegionUUID=:id",
                                {{":id", id}}, {"LandUUID", "AccessUUID", "Flags"},
                                [&](const auto& row, const auto&) {
                                    framework::Uuid parcel, agent;
                                    if (!framework::try_parse_sdk_uuid(row.value_text(0), parcel) ||
                                        !framework::try_parse_sdk_uuid(row.value_text(1), agent))
                                        return;
                                    parcel_access_lists[parcel].push_back(
                                        {agent, static_cast<std::uint32_t>(row.int32_value(2)), 0});
                                });
                        }
                        tables->query(
                            "SELECT UUID,OwnerUUID,Bitmap,CAST(LandFlags AS INTEGER) AS LandFlags,Name,Desc,GroupUUID,"
                            "ClaimDate,LandingType,UserLocationX,UserLocationY,UserLocationZ,UserLookAtX,UserLookAtY,"
                            "UserLookAtZ,SeeAVs,Dwell,land.* FROM land WHERE RegionUUID=:id ORDER BY LocalLandID",
                            {{":id", id}},
                            {"UUID", "OwnerUUID", "Bitmap", "LandFlags", "Name", "Desc", "GroupUUID",
                             "ClaimDate", "LandingType", "UserLocationX", "UserLocationY", "UserLocationZ",
                             "UserLookAtX", "UserLookAtY", "UserLookAtZ", "SeeAVs", "Dwell"},
                            [&](const auto& row, const auto&) {
                                scene::ScriptParcel parcel;
                                parcel.local_id = row.int32_value(row.column("LocalLandID"));
                                try {
                                    parcel.environment_day = saved_environment_day(
                                        row.value_text(row.column("environment")));
                                } catch (const std::out_of_range&) {
                                    // Older land rows do not have an environment column.
                                }
                                try {
                                    const auto owned = row.value_text(row.column("IsGroupOwned"));
                                    parcel.group_owned = owned == "1" ||
                                        framework::ordinal_equals_ignore_case(owned, "true");
                                } catch (const std::out_of_range&) {
                                    // Legacy fixtures and databases may omit group ownership.
                                }
                                if (!framework::try_parse_sdk_uuid(row.string_value(0), parcel.id) ||
                                    !framework::try_parse_sdk_uuid(row.string_value(1), parcel.owner))
                                    return;
                                const auto flags_text = row.value_text(3);
                                std::int64_t flags = 0;
                                const auto parsed = std::from_chars(flags_text.data(),
                                    flags_text.data() + flags_text.size(), flags);
                                if (parsed.ec != std::errc{} || parsed.ptr != flags_text.data() + flags_text.size() ||
                                    flags < std::numeric_limits<std::int32_t>::min() ||
                                    flags > std::numeric_limits<std::uint32_t>::max())
                                    throw std::invalid_argument("Invalid saved parcel flags");
                                parcel.flags = static_cast<std::uint32_t>(flags);
                                const auto bitmap = row.blob_value(2);
                                const auto byte_count = std::min(bitmap.size(), parcel_cell_count / 8);
                                const auto bitmap_width = byte_count == 512 ? 64u : parcel_width;
                                parcel.land_bitmap.resize(parcel_cell_count / 8);
                                for (std::size_t bit = 0; bit < byte_count * 8; ++bit) {
                                    if (!(bitmap[bit / 8] & (1u << (bit % 8))))
                                        continue;
                                    const auto cell = (bit / bitmap_width) * parcel_width + bit % bitmap_width;
                                    parcel.land_bitmap[cell / 8] |= 1u << (cell % 8);
                                }
                                int cells = 0;
                                float average_x = 0, average_y = 0;
                                for (unsigned x = 0; x < parcel_width; ++x) {
                                    for (unsigned y = 0; y < parcel_height; ++y) {
                                        const auto cell = y * parcel_width + x;
                                        if (!(parcel.land_bitmap[cell / 8] & (1u << (cell % 8))))
                                            continue;
                                        average_x = (average_x * cells + x) / (cells + 1);
                                        average_y = (average_y * cells + y) / (cells + 1);
                                        ++cells;
                                    }
                                }
                                parcel.area = cells * 16;
                                if (cells)
                                    parcel.center = {average_x * 4 + 2, average_y * 4 + 2, 0};
                                parcel.claim_date = row.int32_value(7);
                                parcel.landing_type = row.int32_value(8);
                                parcel.see_avatars = row.int32_value(15) != 0;
                                parcel.dwell = row.int32_value(16);
                                const auto sound_permission = [&](const char* column) {
                                    try {
                                        const auto index = row.column(column);
                                        return row.value_text(index).empty() || row.int32_value(index) != 0;
                                    } catch (const std::out_of_range&) {
                                        return true;
                                    }
                                };
                                parcel.any_avatar_sounds = sound_permission("AnyAVSounds");
                                parcel.group_avatar_sounds = sound_permission("GroupAVSounds");
                                const auto component = [&](int column) {
                                    float value = 0;
                                    framework::parse_invariant_float(row.value_text(column), value);
                                    return static_cast<double>(value);
                                };
                                parcel.landing_point = {component(9), component(10), component(11)};
                                parcel.landing_look_at = {component(12), component(13), component(14)};
                                parcel.name = row.string_value(4);
                                parcel.description = row.string_value(5);
                                framework::try_parse_sdk_uuid(row.string_value(6), parcel.group);
                                const auto optional_column = [&](const char* name, std::string fallback = {}) {
                                    try {
                                        const auto index = row.column(name);
                                        return row.value_text(index);
                                    } catch (const std::out_of_range&) {
                                        return fallback;
                                    }
                                };
                                const auto integer = [](std::string_view text) {
                                    const auto trimmed = framework::trim_utf8(text);
                                    text = trimmed;
                                    if (text.starts_with('+'))
                                        text.remove_prefix(1);
                                    std::int32_t value = 0;
                                    const auto end = text.data() + text.size();
                                    const auto result = std::from_chars(text.data(), end, value);
                                    return result.ec == std::errc{} && result.ptr == end ? value : 0;
                                };
                                const auto enabled = [&](const char* name) {
                                    const auto value = optional_column(name);
                                    return framework::ordinal_equals_ignore_case(value, "true") || integer(value) != 0;
                                };
                                framework::try_parse_unsigned_integer(optional_column("AuctionID"), parcel.auction_id);
                                framework::try_parse_sdk_uuid(optional_column("AuthbuyerID"), parcel.authorized_buyer);
                                framework::try_parse_sdk_uuid(optional_column("SnapshotUUID"), parcel.snapshot_id);
                                framework::try_parse_sdk_uuid(optional_column("MediaTextureUUID"), parcel.media_id);
                                parcel.category = integer(optional_column("Category"));
                                parcel.claim_price = integer(optional_column("ClaimPrice"));
                                parcel.sale_price = integer(optional_column("SalePrice"));
                                parcel.status = integer(optional_column("LandStatus"));
                                parcel.other_clean_time = integer(optional_column("OtherCleanTime"));
                                parcel.pass_price = integer(optional_column("PassPrice"));
                                framework::parse_invariant_float(optional_column("PassHours"), parcel.pass_hours);
                                parcel.media_url = optional_column("MediaURL");
                                parcel.music_url = optional_column("MusicURL");
                                parcel.media_type = optional_column("MediaType", "none/none");
                                parcel.media_description = optional_column("MediaDescription");
                                const auto media_size = optional_column("MediaSize");
                                if (const auto comma = media_size.find(','); comma != std::string::npos) {
                                    const auto size = std::string_view(media_size);
                                    parcel.media_width = integer(size.substr(0, comma));
                                    const auto height = size.substr(comma + 1);
                                    parcel.media_height = integer(height.substr(0, height.find(',')));
                                }
                                parcel.media_auto_scale = enabled("MediaAutoScale");
                                parcel.media_loop = enabled("MediaLoop");
                                parcel.obscure_media = enabled("ObscureMedia");
                                if (auto lists = parcel_access_lists.find(parcel.id);
                                    lists != parcel_access_lists.end()) {
                                    parcel.access_entries = std::move(lists->second);
                                }
                                const auto record =
                                    std::make_shared<const scene::ScriptParcel>(std::move(parcel));
                                region_parcels.persisted_parcel_records.push_back(record);
                                if (!region_parcels.persisted_parcels)
                                    region_parcels.persisted_parcels.emplace(parcel_cell_count);
                                for (std::size_t cell = 0; cell < parcel_cell_count; ++cell)
                                    if (record->land_bitmap[cell / 8] & (1u << (cell % 8)))
                                        (*region_parcels.persisted_parcels)[cell] = record;
                            });
                        std::set<std::string> land_columns;
                        tables->query("PRAGMA table_info(land)", {}, {"name"},
                                      [&](const auto& row, const auto&) {
                                          land_columns.insert(row.value_text(row.column("name")));
                                      });
                        const auto insert_generated_parcel =
                            [tables, id, land_columns](const scene::ScriptParcel& parcel, std::int32_t dwell) {
                                const auto parcel_id = parcel.id.str();
                                const std::vector<std::pair<std::string, std::string>> values{
                                    {"UUID", parcel_id}, {"RegionUUID", id},
                                    {"LocalLandID", std::to_string(parcel.local_id)},
                                    {"Bitmap", parcel.land_bitmap.empty()
                                                   ? std::string(static_cast<std::size_t>(parcel.area) / 128,
                                                                 static_cast<char>(0xff))
                                                   : std::string(parcel.land_bitmap.begin(), parcel.land_bitmap.end())},
                                    {"Name", parcel.name}, {"Desc", parcel.description},
                                    {"OwnerUUID", parcel.owner.str()},
                                    {"IsGroupOwned", parcel.group_owned ? "True" : "False"},
                                    {"Area", std::to_string(parcel.area)}, {"AuctionID", std::to_string(parcel.auction_id)},
                                    {"AuthbuyerID", parcel.authorized_buyer.str()},
                                    {"Category", std::to_string(parcel.category)}, {"ClaimDate", std::to_string(parcel.claim_date)},
                                    {"ClaimPrice", std::to_string(parcel.claim_price)}, {"GroupUUID", parcel.group.str()},
                                    {"SalePrice", std::to_string(parcel.sale_price)}, {"LandStatus", std::to_string(parcel.status)},
                                    {"LandFlags", std::to_string(parcel.flags)},
                                    {"LandingType", std::to_string(parcel.landing_type)},
                                    {"MediaAutoScale", parcel.media_auto_scale ? "1" : "0"},
                                    {"MediaTextureUUID", parcel.media_id.str()}, {"MediaURL", parcel.media_url},
                                    {"MediaType", parcel.media_type}, {"MediaDescription", parcel.media_description},
                                    {"MediaSize", std::to_string(parcel.media_width) + "," + std::to_string(parcel.media_height)},
                                    {"MusicURL", parcel.music_url}, {"PassHours", std::to_string(parcel.pass_hours)},
                                    {"MediaLoop", parcel.media_loop ? "1" : "0"},
                                    {"ObscureMedia", parcel.obscure_media ? "1" : "0"},
                                    {"OtherCleanTime", std::to_string(parcel.other_clean_time)},
                                    {"PassPrice", std::to_string(parcel.pass_price)}, {"SnapshotUUID", parcel.snapshot_id.str()},
                                    {"UserLocationX", std::to_string(parcel.landing_point.x)},
                                    {"UserLocationY", std::to_string(parcel.landing_point.y)},
                                    {"UserLocationZ", std::to_string(parcel.landing_point.z)},
                                    {"UserLookAtX", std::to_string(parcel.landing_look_at.x)},
                                    {"UserLookAtY", std::to_string(parcel.landing_look_at.y)},
                                    {"UserLookAtZ", std::to_string(parcel.landing_look_at.z)},
                                    {"Dwell", std::to_string(dwell)}, {"SeeAVs", parcel.see_avatars ? "1" : "0"},
                                    {"AnyAVSounds", parcel.any_avatar_sounds ? "1" : "0"},
                                    {"GroupAVSounds", parcel.group_avatar_sounds ? "1" : "0"}
                                };
                                std::string columns, placeholders;
                                services::SqliteTableParameters parameters;
                                for (const auto& [column, value] : values) {
                                    if (!land_columns.contains(column))
                                        continue;
                                    if (!columns.empty()) {
                                        columns += ',';
                                        placeholders += ',';
                                    }
                                    const auto parameter = ":" + column;
                                    columns += column;
                                    placeholders += column == "Bitmap" ? "CAST(:Bitmap AS BLOB)" : parameter;
                                    parameters.emplace_back(parameter, value);
                                }
                                tables->execute("INSERT INTO land (" + columns + ") VALUES (" +
                                                placeholders + ")", parameters);
                            };
                        region_parcels.save_parcel_topology = [tables, database, id, insert_generated_parcel, land_columns](
                                                   const ParcelCells& changed, const std::vector<framework::Uuid>& removed) {
                            std::lock_guard lock(database->mutex());
                            tables->execute("BEGIN IMMEDIATE", {});
                            try {
                                tables->execute("CREATE TABLE IF NOT EXISTS landaccesslist "
                                                "(LandUUID TEXT, AccessUUID TEXT, Flags INTEGER)", {});
                                for (const auto& parcel : removed) {
                                    const auto uuid = parcel.str();
                                    tables->execute("DELETE FROM landaccesslist WHERE LandUUID=:parcel COLLATE NOCASE",
                                                    {{":parcel", uuid}});
                                    tables->execute("DELETE FROM land WHERE UUID=:parcel COLLATE NOCASE AND RegionUUID=:region",
                                                    {{":parcel", uuid}, {":region", id}});
                                }
                                for (const auto& parcel : changed) {
                                    const auto uuid = parcel->id.str();
                                    const auto bitmap = std::string(parcel->land_bitmap.begin(), parcel->land_bitmap.end());
                                    services::SqliteTableParameters parameters{
                                        {":bitmap", bitmap}, {":dwell", std::to_string(parcel->dwell)},
                                        {":landing", std::to_string(parcel->landing_type)},
                                        {":x", std::to_string(parcel->landing_point.x)},
                                        {":y", std::to_string(parcel->landing_point.y)},
                                        {":z", std::to_string(parcel->landing_point.z)},
                                        {":lx", std::to_string(parcel->landing_look_at.x)},
                                        {":ly", std::to_string(parcel->landing_look_at.y)},
                                        {":lz", std::to_string(parcel->landing_look_at.z)},
                                        {":parcel", uuid}, {":region", id},
                                    };
                                    std::string geometry = "Bitmap=CAST(:bitmap AS BLOB), Dwell=:dwell";
                                    if (land_columns.contains("Area")) {
                                        geometry += ", Area=:area";
                                        parameters.emplace_back(":area", std::to_string(parcel->area));
                                    }
                                    const auto updated = tables->execute(
                                        "UPDATE land SET " + geometry +
                                        ", LandingType=:landing, UserLocationX=:x, UserLocationY=:y, UserLocationZ=:z, "
                                        "UserLookAtX=:lx, UserLookAtY=:ly, UserLookAtZ=:lz "
                                        "WHERE UUID=:parcel COLLATE NOCASE AND RegionUUID=:region",
                                        parameters);
                                    if (updated != 0)
                                        continue;
                                    insert_generated_parcel(*parcel, parcel->dwell);
                                    for (const auto& entry : parcel->access_entries)
                                        tables->execute("INSERT INTO landaccesslist (LandUUID,AccessUUID,Flags) VALUES (:p,:a,:f)",
                                                        {{":p", uuid}, {":a", entry.agent.str()}, {":f", std::to_string(entry.flags)}});
                                    if (parcel->environment_day && parcel->environment_day->settings &&
                                        land_columns.contains("environment"))
                                        tables->execute("UPDATE land SET environment=:settings WHERE UUID=:parcel AND RegionUUID=:region",
                                                        {{":settings", network::llsd_xml(*parcel->environment_day->settings)},
                                                         {":parcel", uuid}, {":region", id}});
                                }
                                tables->execute("COMMIT", {});
                            } catch (...) {
                                tables->execute("ROLLBACK", {});
                                throw;
                            }
                        };
                        if (land_columns.contains("Dwell")) {
                            region_parcels.save_parcel_dwell =
                                [&, tables, database, id, insert_generated_parcel](framework::Uuid parcel, float dwell) {
                                const auto rounded = std::nearbyint(static_cast<double>(dwell));
                                if (!std::isfinite(rounded) || rounded < std::numeric_limits<std::int32_t>::min() ||
                                    rounded > std::numeric_limits<std::int32_t>::max())
                                    throw std::out_of_range("Parcel dwell exceeds SQLite Int32 range");
                                const auto saved_dwell = static_cast<std::int32_t>(rounded);
                                std::shared_ptr<const scene::ScriptParcel> generated;
                                {
                                    std::lock_guard lock(region_parcels.parcels_mutex);
                                    if (!region_parcels.persisted_parcels && region_parcels.default_parcel && region_parcels.default_parcel->id == parcel)
                                        generated = region_parcels.default_parcel;
                                }
                                std::lock_guard lock(database->mutex());
                                tables->execute("BEGIN IMMEDIATE", {});
                                try {
                                    const auto changed = tables->execute(
                                        "UPDATE land SET Dwell=:dwell WHERE UUID=:parcel COLLATE NOCASE "
                                        "AND RegionUUID=:region",
                                        {{":dwell", std::to_string(saved_dwell)}, {":parcel", parcel.str()},
                                         {":region", id}});
                                    if (changed == 0 && generated)
                                        insert_generated_parcel(*generated, saved_dwell);
                                    else if (changed != 1)
                                        throw std::runtime_error("Could not identify the saved parcel");
                                    tables->execute("COMMIT", {});
                                } catch (...) {
                                    tables->execute("ROLLBACK", {});
                                    throw;
                                }
                            };
                        }
                        region_parcels.save_parcel = [tables, database, id, insert_generated_parcel, land_columns](
                                          const scene::ScriptParcel& parcel, bool generated, ParcelSave change) {
                            const auto parcel_id = parcel.id.str();
                            std::lock_guard lock(database->mutex());
                            tables->execute("BEGIN IMMEDIATE", {});
                            try {
                                std::string update = "UPDATE land SET LandFlags=:flags";
                                services::SqliteTableParameters parameters{
                                    {":flags", std::to_string(parcel.flags)}, {":parcel", parcel_id}, {":region", id},
                                };
                                const auto add_optional = [&](std::string_view column, std::string value) {
                                    if (!std::ranges::any_of(land_columns, [column](const auto& stored) {
                                            return framework::ordinal_equals_ignore_case(stored, column);
                                        }))
                                        return;
                                    const auto name = std::string(column);
                                    update += ", " + name + "=:" + name;
                                    parameters.emplace_back(":" + name, std::move(value));
                                };
                                const bool mark_content = change == ParcelSave::MarkContent;
                                if (change == ParcelSave::Ownership || change == ParcelSave::Reclaim ||
                                    change == ParcelSave::Purchase || mark_content) {
                                    update += ", OwnerUUID=:owner, GroupUUID=:group";
                                    parameters.emplace_back(":owner", parcel.owner.str());
                                    parameters.emplace_back(":group", parcel.group.str());
                                    add_optional("IsGroupOwned", parcel.group_owned ? "True" : "False");
                                }
                                if (change == ParcelSave::Purchase) {
                                    add_optional("ClaimDate", std::to_string(parcel.claim_date));
                                    add_optional("ClaimPrice", std::to_string(parcel.claim_price));
                                    add_optional("SalePrice", std::to_string(parcel.sale_price));
                                    add_optional("AuthbuyerID", parcel.authorized_buyer.str());
                                }
                                if (change == ParcelSave::CleanTime || mark_content)
                                    add_optional("OtherCleanTime", std::to_string(parcel.other_clean_time));
                                if (change == ParcelSave::Music)
                                    add_optional("MusicURL", parcel.music_url);
                                if (change == ParcelSave::Media || change == ParcelSave::Properties || mark_content) {
                                    add_optional("MediaURL", parcel.media_url);
                                    add_optional("MediaTextureUUID", parcel.media_id.str());
                                    add_optional("MediaAutoScale", parcel.media_auto_scale ? "1" : "0");
                                    add_optional("MediaType", parcel.media_type);
                                    add_optional("MediaSize", std::to_string(parcel.media_width) + "," +
                                                                  std::to_string(parcel.media_height));
                                }
                                if (change == ParcelSave::Properties || mark_content) {
                                    add_optional("AuthbuyerID", parcel.authorized_buyer.str());
                                    if (!mark_content)
                                        add_optional("GroupUUID", parcel.group.str());
                                    add_optional("SalePrice", std::to_string(parcel.sale_price));
                                    add_optional("Category", std::to_string(parcel.category));
                                    add_optional("Name", parcel.name);
                                    add_optional("Desc", parcel.description);
                                    add_optional("SnapshotUUID", parcel.snapshot_id.str());
                                    add_optional("LandingType", std::to_string(parcel.landing_type));
                                    add_optional("UserLocationX", std::to_string(parcel.landing_point.x));
                                    add_optional("UserLocationY", std::to_string(parcel.landing_point.y));
                                    add_optional("UserLocationZ", std::to_string(parcel.landing_point.z));
                                    add_optional("UserLookAtX", std::to_string(parcel.landing_look_at.x));
                                    add_optional("UserLookAtY", std::to_string(parcel.landing_look_at.y));
                                    add_optional("UserLookAtZ", std::to_string(parcel.landing_look_at.z));
                                    add_optional("MusicURL", parcel.music_url);
                                    add_optional("MediaDescription", parcel.media_description);
                                    add_optional("MediaLoop", parcel.media_loop ? "1" : "0");
                                    add_optional("ObscureMedia", parcel.obscure_media ? "1" : "0");
                                    add_optional("ObscureMusic", "0");
                                    add_optional("PassHours", std::to_string(parcel.pass_hours));
                                    add_optional("PassPrice", std::to_string(parcel.pass_price));
                                    add_optional("SeeAVs", parcel.see_avatars ? "1" : "0");
                                    add_optional("AnyAVSounds", parcel.any_avatar_sounds ? "1" : "0");
                                    add_optional("GroupAVSounds", parcel.group_avatar_sounds ? "1" : "0");
                                }
                                if (mark_content) {
                                    add_optional("ClaimDate", std::to_string(parcel.claim_date));
                                    add_optional("Dwell", "0");
                                    add_optional("LandStatus", std::to_string(parcel.status));
                                }
                                if (change == ParcelSave::Reclaim) {
                                    add_optional("ClaimDate", std::to_string(parcel.claim_date));
                                    add_optional("SalePrice", std::to_string(parcel.sale_price));
                                    add_optional("AuthbuyerID", parcel.authorized_buyer.str());
                                    add_optional("SeeAVs", "1");
                                    add_optional("AnyAVSounds", "1");
                                    add_optional("GroupAVSounds", "1");
                                }
                                update += " WHERE UUID=:parcel COLLATE NOCASE AND RegionUUID=:region";
                                const auto changed = tables->execute(update, parameters);
                                if (changed == 0 && generated) {
                                    insert_generated_parcel(parcel, parcel.dwell);
                                } else if (changed != 1) {
                                    throw std::runtime_error("Could not identify the saved parcel");
                                }
                                if (change == ParcelSave::Access || mark_content) {
                                    tables->execute("CREATE TABLE IF NOT EXISTS landaccesslist "
                                                    "(LandUUID TEXT, AccessUUID TEXT, Flags INTEGER)", {});
                                    tables->execute("DELETE FROM landaccesslist WHERE LandUUID=:parcel COLLATE NOCASE",
                                                    {{":parcel", parcel_id}});
                                    for (const auto& entry : parcel.access_entries) {
                                        tables->execute("INSERT INTO landaccesslist (LandUUID,AccessUUID,Flags) "
                                                        "VALUES (:parcel,:agent,:flags)",
                                                        {{":parcel", parcel_id}, {":agent", entry.agent.str()},
                                                         {":flags", std::to_string(entry.flags)}});
                                    }
                                }
                                tables->execute("COMMIT", {});
                            } catch (...) {
                                tables->execute("ROLLBACK", {});
                                throw;
                            }
                        };
                    }
                    bool has_region_settings = false;
                    tables->query(
                        "SELECT name FROM sqlite_master WHERE type='table' AND name='regionsettings'", {},
                        {"name"}, [&](const auto&, const auto&) { has_region_settings = true; });
                    if (has_region_settings) {
                        region_persistence.save_estate_region_settings = estate_region_settings_writer(tables, id);
                        bool has_terrain_image_column = false;
                        bool has_parcel_image_column = false;
                        tables->query("PRAGMA table_info(regionsettings)", {}, {"name"},
                                      [&](const auto& row, const auto&) {
                                          const auto name = row.value_text(row.column("name"));
                                          if (framework::ordinal_equals_ignore_case(name, "map_tile_ID"))
                                              has_terrain_image_column = true;
                                          if (framework::ordinal_equals_ignore_case(name, "parcel_tile_ID"))
                                              has_parcel_image_column = true;
                                      });
                        if (has_terrain_image_column || has_parcel_image_column) {
                            std::string update = "UPDATE regionsettings SET ";
                            if (has_terrain_image_column)
                                update += "map_tile_ID=:terrain";
                            if (has_terrain_image_column && has_parcel_image_column)
                                update += ", ";
                            if (has_parcel_image_column)
                                update += "parcel_tile_ID=:parcel";
                            update += " WHERE regionUUID=:id";
                            region_persistence.save_map_images = [tables, id, update = std::move(update),
                                               has_terrain_image_column, has_parcel_image_column](
                                                  framework::Uuid terrain, framework::Uuid parcel) {
                                services::SqliteTableParameters values{{":id", id}};
                                if (has_terrain_image_column)
                                    values.emplace_back(":terrain", terrain.str());
                                if (has_parcel_image_column)
                                    values.emplace_back(":parcel", parcel.str());
                                if (tables->execute(update, values) != 1)
                                    throw std::runtime_error("Region settings row is missing");
                            };
                        }
                        int agent_limit = 40;
                        struct FlagColumn {
                            const char* column;
                            std::uint32_t bit;
                            bool invert = false;
                        };
                        const FlagColumn flag_columns[] = {{"allow_damage", 0x1},
                                                           {"disable_collisions", 0x1000},
                                                           {"disable_physics", 0x4000},
                                                           {"block_fly", 0x80000},
                                                           {"restrict_pushing", 0x400000},
                                                           {"disable_scripts", 0x2000},
                                                           {"fixed_sun", 0x10},
                                                           {"allow_land_resell", 0x80, true},
                                                           {"sandbox", 0x100},
                                                           {"casino", 0x400},
                                                           {"block_search", 0x800},
                                                           {"allow_land_join_divide", 0x4000000},
                                                           {"block_show_in_search", 0x20000000}};
                        std::uint32_t flags = 0x4000000;
                        for (const auto& [column, bit, invert] : flag_columns) {
                            bool exists = false;
                            tables->query(
                                "SELECT name FROM pragma_table_info('regionsettings') WHERE name=:name",
                                {{":name", column}}, {"name"},
                                [&](const auto&, const auto&) { exists = true; });
                            if (exists)
                                tables->query(std::string("SELECT ") + column +
                                                  " FROM regionsettings WHERE regionUUID=:id",
                                              {{":id", id}}, {column}, [&](const auto& row, const auto&) {
                                                  if ((row.int32_value(0) != 0) != invert)
                                                      flags |= bit;
                                                  else
                                                      flags &= ~bit;
                                              });
                        }
                        region_runtime->set_region_setting_flags(flags);
                        bool has_object_bonus = false;
                        tables->query(
                            "SELECT name FROM pragma_table_info('regionsettings') WHERE name='object_bonus'",
                            {}, {"name"}, [&](const auto&, const auto&) { has_object_bonus = true; });
                        if (has_object_bonus)
                            tables->query(
                                "SELECT object_bonus FROM regionsettings WHERE regionUUID=:id", {{":id", id}},
                                {"object_bonus"}, [&](const auto& row, const auto&) {
                                    double bonus;
                                    if (network::try_parse_invariant_real(row.value_text(0), bonus) &&
                                        std::isfinite(bonus) && bonus >= 0)
                                        region_parcels.parcel_object_bonus = bonus;
                                });
                        bool has_agent_limit = false;
                        tables->query(
                            "SELECT name FROM pragma_table_info('regionsettings') WHERE name='agent_limit'",
                            {}, {"name"}, [&](const auto&, const auto&) { has_agent_limit = true; });
                        if (has_agent_limit)
                            tables->query("SELECT agent_limit FROM regionsettings WHERE regionUUID=:id",
                                          {{":id", id}}, {"agent_limit"}, [&](const auto& row, const auto&) {
                                              agent_limit = row.int32_value(0);
                                          });
                        region_runtime->set_region_menu_limits(agent_limit, region_parcels.parcel_object_bonus.load());
                        std::optional<framework::Uuid> loaded_telehub;
                        constexpr std::array texture_corners{"sw", "nw", "se", "ne"};
                        std::array<bool, 4> saved_texture_heights{};
                        std::array<bool, 4> saved_texture_assets{};
                        std::array<bool, 4> saved_pbr_materials{};
                        bool saved_terrain_settings = false;
                        bool saved_sun_settings = false;
                        tables->query(
                            "SELECT * FROM regionsettings WHERE regionUUID=:id", {{":id", id}}, {},
                            [&](const auto& row, const auto&) {
                                const auto map_image = [&](const char* column) -> std::optional<framework::Uuid> {
                                    try {
                                        framework::Uuid image;
                                        if (framework::try_parse_sdk_uuid(row.value_text(row.column(column)), image))
                                            return image;
                                    } catch (const std::out_of_range&) {
                                    }
                                    return {};
                                };
                                region_runtime->load_region_map_images(
                                    generate_map_tiles ? map_image("map_tile_ID") : std::nullopt,
                                    map_image("parcel_tile_ID"));
                                try {
                                    region_runtime->set_region_maturity(row.int32_value(row.column("maturity")));
                                } catch (const std::out_of_range&) {
                                }
                                try {
                                    row.column("water_height");
                                    row.column("terrain_raise_limit");
                                    row.column("terrain_lower_limit");
                                    saved_terrain_settings = true;
                                } catch (const std::out_of_range&) {
                                }
                                try {
                                    const auto use_estate_sun = row.column("use_estate_sun");
                                    row.column("sun_position");
                                    row.column("fixed_sun");
                                    saved_sun_settings = true;
                                    region_runtime->load_region_sun_settings(row.int32_value(use_estate_sun) != 0);
                                } catch (const std::out_of_range&) {
                                }
                                scene::ViewerRegionOptions::TerrainTextureHeights heights;
                                auto textures = scene::ViewerRegionOptions{}.terrain_details;
                                std::array<framework::Uuid, 4> materials{};
                                for (std::size_t corner = 0; corner < texture_corners.size(); ++corner) {
                                    try {
                                        const auto column = row.column("terrain_texture_" + std::to_string(corner + 1));
                                        saved_texture_assets[corner] = true;
                                        framework::Uuid asset;
                                        if (framework::try_parse_sdk_uuid(row.value_text(column), asset) &&
                                            !asset.is_zero())
                                            textures[corner] = asset;
                                    } catch (const std::out_of_range&) {
                                    }
                                    try {
                                        const auto column = row.column("TerrainPBR" + std::to_string(corner + 1));
                                        saved_pbr_materials[corner] = true;
                                        framework::try_parse_sdk_uuid(row.value_text(column), materials[corner]);
                                    } catch (const std::out_of_range&) {
                                    }
                                    try {
                                        const auto suffix = std::string(texture_corners[corner]);
                                        const auto low = row.value_text(row.column("elevation_1_" + suffix));
                                        const auto high = row.value_text(row.column("elevation_2_" + suffix));
                                        saved_texture_heights[corner] = true;
                                        framework::parse_invariant_float(low, heights.low[corner]);
                                        framework::parse_invariant_float(high, heights.high[corner]);
                                    } catch (const std::out_of_range&) {
                                    }
                                }
                                region_runtime->set_terrain_texture_heights(heights);
                                region_runtime->set_terrain_texture_assets(textures, materials);
                                try {
                                    framework::Uuid telehub;
                                    framework::try_parse_sdk_uuid(row.value_text(row.column("TelehubObject")), telehub);
                                    loaded_telehub = telehub;
                                } catch (const std::out_of_range&) {
                                }
                                framework::Uuid covenant;
                                std::uint32_t timestamp = 0;
                                try {
                                    framework::try_parse_sdk_uuid(row.value_text(row.column("covenant")),
                                                                  covenant);
                                    timestamp = static_cast<std::uint32_t>(
                                        row.int32_value(row.column("covenant_datetime")));
                                    region_persistence.save_estate_covenant = [tables, id](framework::Uuid value,
                                                                        std::uint32_t changed) {
                                        if (tables->execute(
                                                "UPDATE regionsettings SET covenant=:covenant,covenant_datetime=:changed WHERE regionUUID=:id",
                                                {{":covenant", value.str()},
                                                 {":changed",
                                                  std::to_string(std::bit_cast<std::int32_t>(changed))},
                                                 {":id", id}}) != 1)
                                            throw std::runtime_error(
                                                "Estate covenant region settings row missing or duplicated");
                                    };
                                } catch (const std::out_of_range&) {
                                }
                                region_runtime->set_estate_covenant(covenant, timestamp);
                            });
                        region_persistence.save_terrain_texture_heights =
                            [tables, id, saved_texture_heights, texture_corners](std::size_t corner,
                                                                                float low, float high) {
                                if (!saved_texture_heights.at(corner))
                                    return;
                                const auto text = [](float value) {
                                    std::array<char, 64> bytes;
                                    const auto result = std::to_chars(bytes.data(), bytes.data() + bytes.size(),
                                                                      static_cast<double>(value));
                                    if (result.ec != std::errc{})
                                        throw std::runtime_error("Cannot serialize terrain texture height");
                                    return std::string(bytes.data(), result.ptr);
                                };
                                const auto suffix = std::string(texture_corners.at(corner));
                                if (tables->execute("UPDATE regionsettings SET elevation_1_" + suffix +
                                                    "=:low,elevation_2_" + suffix + "=:high WHERE regionUUID=:id",
                                                    {{":low", text(low)}, {":high", text(high)}, {":id", id}}) != 1)
                                    throw std::runtime_error("Missing or duplicate terrain texture settings");
                            };
                        region_persistence.save_terrain_texture_asset =
                            [tables, id, saved_texture_assets, saved_pbr_materials](
                                bool pbr, std::size_t corner, framework::Uuid asset) {
                                if (!(pbr ? saved_pbr_materials : saved_texture_assets).at(corner))
                                    return;
                                const auto column = (pbr ? "TerrainPBR" : "terrain_texture_") +
                                                    std::to_string(corner + 1);
                                if (tables->execute("UPDATE regionsettings SET " + column +
                                                    "=:asset WHERE regionUUID=:id",
                                                    {{":asset", asset.str()}, {":id", id}}) != 1)
                                    throw std::runtime_error("Missing or duplicate terrain texture settings");
                            };
                        if (saved_terrain_settings) {
                            region_persistence.save_region_terrain_settings = [tables, id](float water, float raise, float lower) {
                                const auto text = [](double value) {
                                    std::array<char, 64> bytes;
                                    const auto result = std::to_chars(bytes.data(), bytes.data() + bytes.size(), value);
                                    if (result.ec != std::errc{})
                                        throw std::runtime_error("Cannot serialize region terrain setting");
                                    return std::string(bytes.data(), result.ptr);
                                };
                                if (tables->execute(
                                        "UPDATE regionsettings SET water_height=:water,terrain_raise_limit=:raise,"
                                        "terrain_lower_limit=:lower WHERE regionUUID=:id",
                                        {{":water", text(water)}, {":raise", text(raise)}, {":lower", text(lower)},
                                         {":id", id}}) != 1)
                                    throw std::runtime_error("Missing or duplicate region terrain settings");
                            };
                        }
                        if (saved_sun_settings) {
                            region_persistence.save_region_sun_settings = [tables, id](bool use_estate_sun) {
                                if (tables->execute(
                                        "UPDATE regionsettings SET use_estate_sun=:estate,fixed_sun=0,"
                                        "sun_position=0 WHERE regionUUID=:id",
                                        {{":estate", use_estate_sun ? "1" : "0"}, {":id", id}}) != 1)
                                    throw std::runtime_error("Missing or duplicate region sun settings");
                            };
                        }
                        if (loaded_telehub) {
                            std::vector<scene::RegionSpawnPoint> points;
                            bool has_spawn_points = false;
                            tables->query("SELECT name FROM sqlite_master WHERE type='table'"
                                          " AND name='spawn_points'", {}, {"name"},
                                          [&](const auto&, const auto&) { has_spawn_points = true; });
                            if (has_spawn_points)
                                tables->query("SELECT Yaw,Pitch,Distance FROM spawn_points"
                                              " WHERE RegionID=:id ORDER BY rowid",
                                    {{":id", id}}, {"Yaw", "Pitch", "Distance"},
                                    [&](const auto& spawn, const auto&) {
                                        scene::RegionSpawnPoint point;
                                        if (framework::parse_invariant_float(spawn.value_text(0), point.yaw) &&
                                            framework::parse_invariant_float(spawn.value_text(1), point.pitch) &&
                                            framework::parse_invariant_float(spawn.value_text(2), point.distance))
                                            points.push_back(point);
                                    });
                            region_runtime->set_region_telehub(*loaded_telehub, std::move(points));
                            region_persistence.save_telehub = [tables, database, id](framework::Uuid hub, const auto& points) {
                                const auto number = [](float value) {
                                    char buffer[64];
                                    const auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
                                    if (result.ec != std::errc{})
                                        throw std::runtime_error("Cannot serialize telehub spawn point");
                                    return std::string(buffer, result.ptr);
                                };
                                std::lock_guard lock(database->mutex());
                                tables->execute("BEGIN IMMEDIATE", {});
                                try {
                                    if (tables->execute("UPDATE regionsettings SET TelehubObject=:hub"
                                                        " WHERE regionUUID=:id",
                                            {{":hub", hub.str()}, {":id", id}}) != 1)
                                        throw std::runtime_error("Missing or duplicate telehub region settings");
                                    tables->execute("CREATE TABLE IF NOT EXISTS spawn_points ("
                                        "RegionID varchar(36) NOT NULL, Yaw float NOT NULL,"
                                        "Pitch float NOT NULL, Distance float NOT NULL)", {});
                                    tables->execute("DELETE FROM spawn_points WHERE RegionID=:id", {{":id", id}});
                                    for (const auto& point : points)
                                        tables->execute("INSERT INTO spawn_points (RegionID,Yaw,Pitch,Distance)"
                                                        " VALUES (:id,:yaw,:pitch,:distance)",
                                            {{":id", id}, {":yaw", number(point.yaw)},
                                             {":pitch", number(point.pitch)}, {":distance", number(point.distance)}});
                                    tables->execute("COMMIT", {});
                                } catch (...) {
                                    tables->execute("ROLLBACK", {});
                                    throw;
                                }
                            };
                        }
                        tables->query(
                            "SELECT CAST(terrain_lower_limit AS TEXT) AS terrain_lower_limit,"
                            "CAST(terrain_raise_limit AS TEXT) AS terrain_raise_limit,"
                            "CAST(block_terraform AS INTEGER) AS block_terraform,"
                            "CAST(water_height AS TEXT) AS water_height FROM regionsettings WHERE regionUUID=:id",
                            {{":id", id}},
                            {"terrain_lower_limit", "terrain_raise_limit", "block_terraform", "water_height"},
                            [&](const auto& row, const auto&) {
                                float water;
                                if (!framework::parse_invariant_float(row.string_value(3), water) ||
                                    !std::isfinite(water))
                                    water = 20.f;
                                region_runtime->set_terrain_handshake(row.int32_value(2) != 0, water);
                                float lower, raise;
                                if (framework::parse_invariant_float(row.string_value(0), lower) &&
                                    std::isfinite(lower) &&
                                    framework::parse_invariant_float(row.string_value(1), raise) &&
                                    std::isfinite(raise))
                                    region_runtime->set_terrain_limits(lower, raise);
                            });
                    }
                    auto& terrain = region_runtime->world().terrain();
                    const auto load = [&](const char* table, const auto& consume) {
                        bool found = false;
                        tables->query(
                            std::string("SELECT Revision,Heightfield FROM ") + table +
                                " WHERE RegionUUID=:id ORDER BY Revision DESC LIMIT 1",
                            {{":id", id}}, {"Revision", "Heightfield"}, [&](const auto& row, const auto&) {
                                try {
                                    consume(row.int32_value(0), row.blob_value(1));
                                    found = true;
                                } catch (const std::logic_error& error) {
                                    error_log(std::string("Failed to load ") + table + ": " + error.what());
                                } catch (const std::runtime_error& error) {
                                    error_log(std::string("Failed to load ") + table + ": " + error.what());
                                }
                            });
                        return found;
                    };
                    const auto store = [database, tables, id](const std::string& table,
                                                              const scene::Terrain& data) {
                        const auto blob = data.database_blob();
                        std::lock_guard lock(database->mutex());
                        tables->execute("BEGIN IMMEDIATE", {});
                        try {
                            tables->execute("DELETE FROM " + table + " WHERE RegionUUID=:id", {{":id", id}});
                            tables->execute(
                                "INSERT INTO " + table +
                                    " (RegionUUID,Revision,Heightfield) VALUES (:id,23,CAST(:data AS BLOB))",
                                {{":id", id}, {":data", blob}});
                            tables->execute("COMMIT", {});
                        } catch (...) {
                            tables->execute("ROLLBACK", {});
                            throw;
                        }
                    };
                    const bool has_terrain = load("terrain", [&](int format, const auto& blob) {
                        terrain.load_database_blob(format, blob);
                    });
                    const bool has_baked = load("bakedterrain", [&](int format, const auto& blob) {
                        region_runtime->load_terrain_baseline(format, blob);
                        if (!has_terrain)
                            terrain.load_database_blob(format, blob);
                    });
                    if (!has_terrain && !has_baked)
                        initialize_terrain();
                    if (!has_terrain)
                        store("terrain", terrain);
                    if (!has_baked)
                        store("bakedterrain", terrain);
                    region_persistence.save_baked_terrain = [store](const scene::Terrain& data) { store("bakedterrain", data); };
                    region_persistence.save_terrain = [&terrain, store, saved = terrain.revision()]() mutable {
                        const auto revision = terrain.revision();
                        if (revision == saved)
                            return;
                        store("terrain", terrain);
                        saved = revision;
                    };
                } else
                    initialize_terrain();
                if (local_profiles_connector) {
                    struct ProfileRegionIdentity final : services::LocalProfileScene {
                        framework::Uuid id;
                        explicit ProfileRegionIdentity(framework::Uuid value) : id(value) {}
                        framework::Uuid region_id() const override {
                            return id;
                        }
                    };
                    local_profiles_scene =
                        std::make_shared<ProfileRegionIdentity>(region_runtime->descriptor().id);
                    local_profiles_connector->add_region(local_profiles_scene);
                }
            }
            const auto gatekeeper =
                config.get("LoginService", "GatekeeperURI",
                           config.get("Hypergrid", "GatekeeperURI", config.get("Startup", "GatekeeperURI")));
            if (config.get("LoginService", "UserAgentService") ==
                "OpenSim.Services.Connectors.dll:UserAgentServiceConnector") {
                auto transport = std::make_shared<network::NwcXmlRpcHttpTransport>(outgoing_clients());
                services::UserAgentConnectorServices connector;
                connector.client = [transport](std::int32_t timeout) {
                    return network::xml_rpc_http_client(transport, timeout);
                };
                connector.debug = [&console_log](const auto& message) {
                    console_log.append(framework::console_log_debug, message);
                };
                connector.error = connector.warn = [&console_log](const auto& message) {
                    console_log.append(framework::console_log_warn, message);
                };
                connector.simulation_request = [requester = make_simulation_requester()](
                                                   const auto& uri, const auto& data, const auto& method,
                                                   bool compressed, bool keepalive) {
                    return requester->send(framework::utf16_replacing(uri), &data,
                                           framework::utf16_replacing(method), 30000, compressed, false,
                                           keepalive);
                };
                login_user_agent = std::make_unique<services::UserAgentServiceConnector>(
                    config.native_source(), std::move(connector));
            }
            agent_launcher = std::make_unique<services::AgentLauncher>(
                grid.get(), region_runtime ? &local_regions : nullptr,
                remote_simulation ? simulation_connector.get() : nullptr,
                login_user_agent.get(), gatekeeper);
            services::LoginAgentFactory::Config urls;
            for (const auto& pair : config.ordered_section("LoginService"))
                urls.push_back(pair);
            agent_factory = std::make_unique<services::LoginAgentFactory>(
                std::move(urls), gatekeeper, [&](auto& account) { return accounts.store_account(account); });
            services::LoginCompletionDependencies completion;
            completion.launcher = agent_launcher.get();
            completion.factory = agent_factory.get();
            completion.logged_in = [&](const std::string& user) -> std::optional<services::GridUserInfo> {
                return grid_user->logged_in(user);
            };
            if (friends)
                completion.friends = [&](const framework::Uuid& id)
                    -> std::optional<std::vector<std::shared_ptr<services::LoginFriendInfo>>> {
                    std::vector<std::shared_ptr<services::LoginFriendInfo>> result;
                    for (const auto& item : friends->get(id)) {
                        auto friend_info = std::make_shared<services::LoginFriendInfo>();
                        friend_info->friend_id = item.friend_id;
                        friend_info->my_flags = item.my_flags;
                        friend_info->their_flags = item.their_flags;
                        result.push_back(std::move(friend_info));
                    }
                    return result;
                };
            completion.gestures = [&](const framework::Uuid& id)
                -> std::optional<std::vector<std::shared_ptr<services::InventoryItem>>> {
                std::vector<std::shared_ptr<services::InventoryItem>> result;
                for (const auto& item : inventory->active_gestures(id))
                    result.push_back(std::make_shared<services::InventoryItem>(item));
                return result;
            };
            const auto configured_library = config.get("LibraryService", "DefaultLibrary");
            auto library_path = framework::utf8_path(configured_library.empty()
                ? "../bin/inventory/Libraries.xml" : configured_library);
            if (library_path.is_relative())
                library_path = config.directory / library_path;
            if (configured_library.empty() && !std::filesystem::is_regular_file(library_path)) {
                auto root = executable_directory(args[0]);
                for (int depth = 0; depth < 4; ++depth, root = root.parent_path()) {
                    const auto installed = root / "share/opensim-native/inventory/Libraries.xml";
                    const auto source = root / "bin/inventory/Libraries.xml";
                    if (std::filesystem::is_regular_file(installed)) {
                        library_path = installed;
                        break;
                    }
                    if (std::filesystem::is_regular_file(source)) {
                        library_path = source;
                        break;
                    }
                }
            }
            auto library_u8 = library_path.u8string();
            library_service = std::make_shared<services::LibraryService>(
                std::string(reinterpret_cast<const char*>(library_u8.data()), library_u8.size()),
                config.get("LibraryService", "LibraryName", "OpenSim Library"));
            completion.library = services::LoginResponseLibrary::bind(*library_service);
            services::bind_login_response_network(completion.environment);
            services::bind_login_response_timezones(completion.environment);
            services::LoginCompletionOptions settings;
            auto read = [&](const char* key, const std::string& fallback = std::string{}) {
                return config.get("LoginService", key, fallback);
            };
            settings.welcome = read("WelcomeMessage", "Welcome to OpenSim!");
            const auto message_url = read("MessageUrl");
            if (!message_url.empty()) {
                try {
                    const auto uris = framework::native_config_uri_object_backend();
                    auto uri = uris->try_create(framework::nini_writer_text(framework::utf16(message_url)),
                                                framework::ConfigUriObjectKind::Absolute);
                    std::u16string downloaded;
                    if (!uri || *uri->scheme() == u"file") {
                        const auto path = uri ? framework::utf8(*uri->local_path()) : message_url;
                        downloaded = framework::read_stream_reader_text(framework::utf8_path(path)).text;
                    } else {
                        framework::ConfigXmlHttpOperation operation;
                        auto stream = outgoing_clients()->get_new_global_client(0)->send_headers(
                            std::move(uri), std::move(operation), true);
                        const auto bytes = stream->copy_to_memory();
                        stream->close();
                        downloaded = framework::decode_stream_reader_text(bytes).text;
                    }
                    if (!downloaded.empty())
                        settings.welcome = framework::utf8(downloaded);
                } catch (const std::exception&) {
                    // The original uses WelcomeMessage when the download fails.
                }
            }
            for (std::size_t at = 0; (at = settings.welcome.find("\\n", at)) != std::string::npos; ++at)
                settings.welcome.replace(at, 2, "\n");
            settings.map_url = read("MapTileURL");
            if (!framework::trim_utf8(settings.map_url).empty()) {
                settings.map_url = framework::trim_utf8(settings.map_url);
                if (!settings.map_url.ends_with('/'))
                    settings.map_url += '/';
            }
            settings.profile_url = read("ProfileServerURL");
            settings.openid_url = read("OpenIDServerURL");
            settings.search_url = read("SearchURL");
            settings.currency = read("Currency");
            settings.classified_fee = read("ClassifiedFee");
            settings.destinations_url = read("DestinationGuide");
            settings.avatars_url = read("AvatarPicker");
            settings.dst_zone = read("DSTZone", "America/Los_Angeles;Pacific Standard Time");
            settings.max_agent_groups = config.integer("Groups", "MaxAgentGroups", 60);
            login_completion =
                std::make_unique<services::LoginCompletion>(std::move(completion), std::move(settings));
            login.set_completion(login_completion.get());
        }
    }
    std::unique_ptr<server::AssetEndpoint> asset_endpoint, hg_asset_endpoint;
    std::shared_ptr<services::AssetService> filesystem_assets;
    if (expose_assets || expose_hg_assets) {
        auto storage = std::make_shared<services::NativeAssetStorageBackend>(asset_log);
        services::AssetStorageFactory factory(storage);
        std::shared_ptr<services::AssetService> backing;
        if ((expose_hg_assets && !expose_hg_fs_assets) || (expose_assets && !expose_fs_assets))
            backing = factory.create(&config);
        if (expose_fs_assets || expose_hg_fs_assets)
            filesystem_assets = services::make_fs_asset_service(config, storage);
        if (expose_assets) {
            auto service = expose_fs_assets ? filesystem_assets : backing;
            asset_endpoint = std::make_unique<server::AssetEndpoint>(std::move(service), config);
        }
        if (expose_hg_assets) {
            constexpr auto section = "HGAssetService";
            constexpr auto native_accounts = "OpenSim.Services.UserAccountService.dll:UserAccountService";
            if (config.get(section, "UserAccountsService") != native_accounts)
                throw std::invalid_argument("HGAssetService.UserAccountsService requires the native account service");
            constexpr auto native_backing = "OpenSim.Services.AssetService.dll:AssetService";
            if (!expose_hg_fs_assets && config.get(section, "BackingService", native_backing) != native_backing)
                throw std::invalid_argument("Unsupported HGAssetService.BackingService");
            auto home = config.get(section, "HomeURI",
                                   config.get("Hypergrid", "HomeURI", config.get("Startup", "HomeURI")));
            if (home.empty())
                throw std::invalid_argument("HGAssetService requires a HomeURI");
            auto hg_service = std::make_shared<services::HgAssetService>(
                expose_hg_fs_assets ? filesystem_assets : backing, std::move(home),
                [&accounts](framework::Uuid id) -> std::optional<std::string> {
                    const auto creator = accounts.find_by_id(framework::Uuid{}.str(), id.str());
                    return creator ? std::optional<std::string>(creator->name()) : std::nullopt;
                },
                services::AssetExchangePermissions(config, section, asset_log));
            hg_asset_endpoint = std::make_unique<server::AssetEndpoint>(std::move(hg_service), config, section);
        }
    }
    std::unique_ptr<server::AvatarEndpoint> avatar_endpoint;
    if (config.boolean("NativeServer", "EnableAvatarEndpoint", false)) {
        server::AvatarOperations operations;
        operations.get = [&](auto id) {
            return std::make_shared<services::AvatarData>(avatars->get_avatar(id));
        };
        operations.set = [&](auto id, auto& value) { return avatars->set_avatar(id, value); };
        operations.reset = [&](auto id) { return avatars->reset(id); };
        operations.set_items = [&](auto id, const auto& names, const auto& values) {
            return avatars->set_items(id, &names, &values);
        };
        operations.remove_items = [&](auto id, const auto& names) {
            return avatars->remove_items(id, &names);
        };
        avatar_endpoint = std::make_unique<server::AvatarEndpoint>(std::move(operations), config);
        app.set_avatar_endpoint(avatar_endpoint.get());
    }
    std::unique_ptr<services::AppearanceProvisioner> appearance_provisioner;
    std::unique_ptr<services::AccountProvisioner> provisioner;
    if (config.boolean("NativeServer", "EnableAccountEndpoint", false)) {
        services::AccountProvisioning creation;
        creation.by_name = [&accounts](auto scope, auto first, auto last) {
            auto a = accounts.find(scope.str(), first, last);
            return a ? std::make_shared<services::UserAccount>(std::move(*a)) : nullptr;
        };
        creation.store = [&accounts](auto& account) { return accounts.store_account(account); };
        creation.set_password = [&accounts, &remote_authentication](auto id, auto password) {
            return remote_authentication ? remote_authentication->set_password(id, password)
                                         : accounts.set_password(id.str(), password);
        };
        if (grid)
            creation.default_regions = [&grid](auto scope) {
                std::vector<std::optional<services::GridRegion>> result;
                for (auto& region : grid->defaults(scope))
                    result.push_back(std::move(region));
                return result;
            };
        if (grid_user)
            creation.set_home = [&grid_user](auto id, auto region, auto position, auto look) {
                return grid_user->set_home(id, region, position, look);
            };
        if (inventory) {
            creation.create_inventory = [&inventory](auto id) {
                return inventory->create_user_inventory(id);
            };
            services::AppearanceProvisioning appearance;
            appearance.folder = [&inventory](auto id, int type) {
                auto folder = inventory->for_type(id, type);
                return folder ? std::make_shared<services::InventoryFolder>(*folder) : nullptr;
            };
            appearance.add_item = [&inventory](auto item) { return inventory->add_item(*item); };
            appearance.root = [&inventory](auto id) {
                auto folder = inventory->root(id);
                return folder ? std::make_shared<services::InventoryFolder>(*folder) : nullptr;
            };
            appearance.add_folder = [&inventory](auto folder) { return inventory->add_folder(*folder); };
            appearance.item = [&inventory](auto principal, auto id) {
                auto item = inventory->item(principal, id);
                return item ? std::make_shared<services::InventoryItem>(*item) : nullptr;
            };
            appearance.by_name = creation.by_name;
            if (avatars) {
                appearance.get_appearance = [&avatars](auto id) { return avatars->get_appearance(id); };
                appearance.set_appearance = [&avatars](auto id, auto value) {
                    return avatars->set_appearance(id, value.get());
                };
            }
            appearance_provisioner = std::make_unique<services::AppearanceProvisioner>(std::move(appearance));
            creation.default_appearance = [&appearance_provisioner](auto id) {
                appearance_provisioner->create_defaults(id);
            };
            creation.model_appearance = [&appearance_provisioner](auto id, auto model) {
                appearance_provisioner->establish(id, model);
            };
        }
        provisioner = std::make_unique<services::AccountProvisioner>(
            std::move(creation), config.boolean("UserAccountService", "CreateDefaultAvatarEntries", false));
        server::AccountOperations operations;
        operations.by_id = [&accounts](auto scope, auto id) {
            return accounts.find_by_id(scope.str(), id.str());
        };
        operations.by_email = [&accounts](auto scope, auto email) {
            return accounts.find_by_email(scope.str(), email);
        };
        operations.by_name = [&accounts](auto scope, auto first, auto last) {
            return accounts.find(scope.str(), first, last);
        };
        operations.search = [&accounts](auto scope, auto query) {
            server::AccountOperations::Accounts result;
            for (auto& account : accounts.search_accounts(scope.str(), query))
                result.push_back(std::move(account));
            return result;
        };
        // SQLiteUserAccountData.GetUsersWhere returns null in the source.
        operations.multi = [](auto, auto) { return server::AccountOperations::Accounts{}; };
        operations.store = [&accounts](auto& account) { return accounts.store_account(account); };
        operations.create = [&provisioner](auto scope, auto id, auto first, auto last, auto password,
                                           auto email, auto model) -> server::AccountOperations::Account {
            auto a = provisioner->create(scope, id, first, last, password, email, model);
            return a ? server::AccountOperations::Account(*a) : std::nullopt;
        };
        account_endpoint = std::make_unique<server::AccountEndpoint>(std::move(operations), config);
        app.set_account_endpoint(account_endpoint.get());
    }
    std::function<bool(const framework::Uuid&, std::string_view)> verify_native_travel_client;
    const auto notify_hg_friends = [&](const framework::Uuid& sender, bool online,
                                       const std::vector<std::string>& identifiers, bool first_only) {
        std::vector<std::string> online_friends;
        if (!friends || !region_presence)
            return online_friends;
        std::vector<std::string> recipients;
        const auto sender_prefix = sender.str() + ";";
        for (const auto& value : identifiers) {
            const auto secret_at = value.rfind(';');
            framework::Uuid recipient;
            if (secret_at == std::string::npos || secret_at <= 36 || secret_at + 1 >= value.size() ||
                !framework::try_parse_sdk_uuid(std::string_view(value).substr(0, 36), recipient))
                continue;
            const auto secret = value.substr(secret_at);
            const auto entries = friends->get(recipient);
            if (std::any_of(entries.begin(), entries.end(), [&](const auto& entry) {
                    return entry.friend_id && entry.friend_id->starts_with(sender_prefix) &&
                           entry.friend_id->ends_with(secret);
                }))
                recipients.push_back(recipient.str());
        }
        if (recipients.empty())
            return online_friends;
        std::vector<services::PresenceInfo> sessions;
        if (!remote_region_presence && presence_store && presence_store->persistent()) {
            for (const auto& user : recipients) {
                const auto records = presence_store->get("UserID", user);
                if (!records.empty())
                    sessions.push_back({records.front()->user_id, records.front()->region_id});
            }
        } else {
            sessions = region_presence->agents(recipients);
        }
        for (const auto& session : sessions) {
            if (session.region_id.is_zero())
                continue;
            framework::Uuid recipient;
            if (framework::try_parse_sdk_uuid(session.user_id, recipient)) {
                if (auto runtime = local_regions.find(session.region_id)) {
                    runtime->notify_friend_status(recipient, sender, online);
                } else if (grid && profile_workers) {
                    const auto region = grid->by_id({}, session.region_id);
                    if (region) {
                        const auto uri = region->server_uri() + "friends";
                        const auto body =
                            network::build_service_form({{"METHOD", "status"},
                                                         {"FromID", sender.str()},
                                                         {"ToID", recipient.str()},
                                                         {"Online", online ? "True" : "False"}});
                        auto request = network::rest_forms_transport(outgoing_clients());
                        profile_workers->enqueue(
                            [request = std::move(request), uri, body] { request(uri, body); });
                    }
                }
            }
            if (online && std::find(online_friends.begin(), online_friends.end(), session.user_id) ==
                              online_friends.end())
                online_friends.push_back(session.user_id);
            if (first_only)
                break;
        }
        return online_friends;
    };
    if (friends && grid && !profile_workers)
        profile_workers = std::make_unique<ProfileWorkers>([&console_log](const std::exception& error) {
            console_log.append(framework::console_log_warn, framework::utf16_replacing(error.what()));
        });
    if (travel_store) {
        const auto outside_level = config.integer("UserAgentService", "LevelOutsideContacts", 0);
        app.set_home_user_lookup([&, outside_level](std::string_view method, const network::Value& data,
                                                    std::string_view caller) {
            if (method == "status_notification") {
                if (!data.contains("userID") || !data.contains("online"))
                    return network::Value::object({{"result", "false"}});
                const auto empty = network::Value::object({{"result", "No Friends Online"}});
                framework::Uuid sender;
                if (!friends || !region_presence ||
                    !framework::try_parse_sdk_uuid(data.get("userID").as_string(), sender))
                    return empty;
                const auto online_text = framework::trim_utf8(data.get("online").as_string());
                const bool online = framework::invariant_equals_ignore_case(online_text, "true");
                std::vector<std::string> identifiers;
                for (const auto& [key, field] : data.view().map)
                    if (key.starts_with("friend_"))
                        identifiers.push_back(field.as_string());
                const auto online_friends = notify_hg_friends(sender, online, identifiers, true);
                if (!online_friends.empty())
                    return network::Value::object({{"result", "false"}, {"friend_0", online_friends.front()}});
                return empty;
            }
            if (method == "get_uui") {
                if (!data.contains("userID") || !data.contains("targetUserID"))
                    return network::Value::object({});
                framework::Uuid user, target;
                framework::try_parse_sdk_uuid(data.get("userID").as_string(), user);
                framework::try_parse_sdk_uuid(data.get("targetUserID").as_string(), target);
                const auto account = accounts.find_by_id(framework::Uuid{}.str(), target.str());
                if (account) {
                    return network::Value::object({{"UUI", target.str() + ";" + home_grid_name + ";" +
                                                               account->first_name.value_or("") + " " +
                                                               account->last_name.value_or("")}});
                }
                if (friends) {
                    const auto target_id = target.str();
                    for (const auto& entry : friends->get(user)) {
                        if (!entry.friend_id || !entry.friend_id->starts_with(target_id))
                            continue;
                        const auto& value = *entry.friend_id;
                        if (value.size() == target_id.size())
                            throw std::invalid_argument("Friend identity has no secret");
                        const auto home_separator = value.find(';', target_id.size());
                        if (home_separator == value.npos)
                            continue;
                        const auto name_separator = value.find(';', home_separator + 1);
                        if (name_separator == value.npos)
                            continue;
                        const auto name_start = name_separator + 1;
                        const auto secret_separator = value.find(';', name_start);
                        auto name = std::string_view(value).substr(name_start);
                        if (secret_separator != value.npos)
                            name = name.substr(0, secret_separator - name_start);
                        else if (framework::trim_utf8(name).empty())
                            continue;
                        if (name.find_first_not_of(' ') == name.npos)
                            continue;
                        if (secret_separator == value.npos || secret_separator + 1 == value.size())
                            throw std::invalid_argument("Friend identity has no secret");
                        const auto secret = std::string_view(value).substr(secret_separator + 1);
                        auto public_identity = value;
                        // The managed service replaces every occurrence of the stored secret.
                        for (auto start = public_identity.find(secret); start != value.npos;
                             start = public_identity.find(secret, start + 1))
                            public_identity.replace(start, secret.size(), "0");
                        return network::Value::object({{"UUI", std::move(public_identity)}});
                    }
                }
                return network::Value::object({{"result", "User unknown"}});
            }
            if (method == "get_server_urls" || method == "get_user_info") {
                if (!data.contains("userID"))
                    return network::Value::object({});
                framework::Uuid user;
                framework::try_parse_sdk_uuid(data.get("userID").as_string(), user);
                const auto account = accounts.find_by_id(framework::Uuid{}.str(), user.str());
                if (method == "get_user_info") {
                    if (!account)
                        return network::Value::object({{"result", "failure"}});
                    const bool show = config.boolean("UserAgentService", "ShowUserDetailsInHGProfile", true);
                    return network::Value::object(
                        {{"result", "success"},
                         {"user_firstname", account->first_name.value_or("")},
                         {"user_lastname", account->last_name.value_or("")},
                         {"user_flags", network::Value(show ? account->user_flags : 0)},
                         {"user_created", network::Value(show ? account->created : 0)},
                         {"user_title", show ? account->user_title.value_or("") : ""}});
                }
                if (!account || !account->service_urls || account->service_urls->empty())
                    return network::Value::object({{"result", "No Service URLs"}});
                auto result = network::Value::object({});
                for (const auto& [key, url] : *account->service_urls)
                    result.set("SRV_" + key, framework::service_url_object_text(url));
                return result;
            }
            if (method == "locate_user") {
                if (config.boolean("UserAgentService", "VerifyCallers", false)) {
                    auto allowed = config.get("UserAgentService", "AuthorizedCallers", "127.0.0.1");
                    std::erase(allowed, ' ');
                    std::istringstream peers(allowed);
                    bool authorized = false;
                    for (std::string peer; std::getline(peers, peer, ',');)
                        if (peer == caller) {
                            authorized = true;
                            break;
                        }
                    if (!authorized)
                        return network::Value::object({});
                }
                if (!data.contains("userID"))
                    return network::Value::object({});
                framework::Uuid user;
                if (framework::try_parse_sdk_uuid(data.get("userID").as_string(), user)) {
                    const auto record = travel_store->locate(user, home_grid_name);
                    if (record)
                        return network::Value::object({{"URL", record->grid}});
                }
                return network::Value::object({{"result", "Unable to locate user"}});
            }
            if (!data.contains("first") || !data.contains("last"))
                return network::Value::object({});
            const auto account = accounts.find(framework::Uuid{}.str(), data.get("first").as_string(),
                                               data.get("last").as_string());
            return network::Value::object(
                {{"UUID", account && account->user_level >= outside_level ? account->principal_id
                                                                          : framework::Uuid{}.str()}});
        });
        std::string external_ip;
        if (!home_grid_name.empty()) {
            const auto address = framework::host_resolver().host_from_url(home_grid_name);
            if (!address)
                throw std::invalid_argument("Unable to resolve UserAgentService gatekeeper host");
            external_ip = address->str();
        }
        const bool bypass = config.boolean("UserAgentService", "BypassClientVerification", false);
        verify_native_travel_client = [travel_store, external_ip, bypass](const auto& session,
                                                                          std::string_view peer) {
            if (bypass)
                return true;
            const auto record = travel_store->get(session);
            return record && (peer == record->client_ip || (!external_ip.empty() && peer == external_ip));
        };
        app.set_home_agent_verifier([&, travel_store, verify_native_travel_client,
                                     bypass](std::string_view method, const network::Value& data) {
            const bool client = method == "verify_client";
            if (client && bypass)
                return true;
            framework::Uuid session;
            if (!framework::try_parse_sdk_uuid(data.get("sessionID").as_string(), session))
                return false;
            if (method == "logout_agent") {
                framework::Uuid user;
                if (!framework::try_parse_sdk_uuid(data.get("userID").as_string(), user))
                    return false;
                std::lock_guard lock(travel_launch_mutex);
                const auto record = travel_store->get(session);
                if (!record)
                    return true;
                if (record->user_id != user)
                    return false;
                travel_store->erase(session);
                if (grid_user) {
                    const auto info = grid_user->info(user.str());
                    if (info)
                        grid_user->logged_out(user.str(), session, info->last_region, info->last_position,
                                              info->last_look_at);
                }
                return true;
            }
            const bool returning = method == "agent_is_coming_home";
            const auto token = data.get(returning ? "externalName" : "token");
            if (token.view().kind != network::Value::Kind::String)
                return false;
            const auto value = token.as_string();
            if (client)
                return verify_native_travel_client(session, value);
            const auto record = travel_store->get(session);
            return record && (returning ? framework::invariant_equals_ignore_case(record->grid, value)
                                        : value == record->service_token);
        });
    }
    create_friend_card = [&](framework::Uuid principal, framework::Uuid friend_id) {
        if (inventory) {
            const auto creator = accounts.find_by_id(framework::Uuid{}.str(), friend_id.str());
            if (creator) {
                constexpr int calling_card_type = 2;
                const auto folder = inventory->for_type(principal, calling_card_type);
                if (folder) {
                    services::InventoryItem card;
                    card.id = framework::Uuid::random();
                    card.owner = principal;
                    card.creator_id = friend_id.str();
                    card.parent = folder->id;
                    card.asset_type = card.inventory_type = calling_card_type;
                    card.base_permissions = card.current_permissions = card.next_permissions = 0xc000;
                    card.name = creator->first_name.value_or("") + " " + creator->last_name.value_or("");
                    card.sale_price = 10;
                    card.creation_date = static_cast<int>(std::time(nullptr));
                    if (inventory->add_item(card)) {
                        local_regions.deliver_to_viewer([&](auto& runtime) {
                            return runtime.notify_inventory_item(card);
                        });
                    }
                }
            }
        }
    };
    const auto deliver_friend_answer = [&](framework::Uuid principal, framework::Uuid friend_id,
                                           std::string_view name, bool accepted) {
        scene::RegionInstantMessage approval;
        approval.sender = friend_id;
        approval.recipient = principal;
        auto session_bytes = friend_id.bytes();
        for (std::size_t i = 0; i < session_bytes.size(); ++i)
            session_bytes[i] ^= principal.bytes()[i];
        approval.session = framework::Uuid::from_bytes(session_bytes);
        approval.timestamp = static_cast<std::uint32_t>(std::time(nullptr));
        approval.dialog = accepted ? 39 : 40; // FriendshipAccepted or FriendshipDeclined
        approval.sender_name = framework::utf16_replacing(name);
        approval.message = framework::utf16_replacing(friend_id.str());
        const bool delivered = local_regions.deliver_to_viewer([&](auto& runtime) {
            approval.region = runtime.descriptor().id;
            return runtime.deliver_instant_message(approval);
        });
        if (!delivered)
            return false;
        if (accepted)
            create_friend_card(principal, friend_id);
        return true;
    };
    const auto deliver_friend_offer = [&](framework::Uuid recipient, framework::Uuid sender,
                                          std::string_view name, std::string_view message) {
        scene::RegionInstantMessage offer;
        offer.sender = offer.session = sender;
        offer.recipient = recipient;
        offer.dialog = 38; // FriendshipOffered
        offer.sender_name = framework::utf16_replacing(name);
        offer.message = framework::utf16_replacing(message);
        return local_regions.deliver_to_viewer([&](auto& runtime) {
            offer.region = runtime.descriptor().id;
            return runtime.deliver_instant_message(offer);
        });
    };
    const auto friend_region = [&](const std::string& user) -> std::optional<services::GridRegion> {
        if (!region_presence || !grid)
            return std::nullopt;
        framework::Uuid region_id;
        if (!remote_region_presence && presence_store && presence_store->persistent()) {
            const auto records = presence_store->get("UserID", user);
            if (!records.empty())
                region_id = records.front()->region_id;
        } else {
            const auto sessions = region_presence->agents({user});
            if (!sessions.empty())
                region_id = sessions.front().region_id;
        }
        if (region_id.is_zero() || local_regions.find(region_id))
            return std::nullopt;
        return grid->by_id({}, region_id);
    };
    confirm_visitor_friendship = [&](const framework::AgentCircuitData& circuit, const std::string& local_id,
                                     const std::string& secret) {
        if (!circuit.service_urls)
            return;
        const auto remote = circuit.service_urls->find("FriendsServerURI");
        const auto local_home =
            config.get("Startup", "HomeURI",
                       config.get("Hypergrid", "HomeURI", config.get("LoginService", "SRV_HomeURI")));
        auto identity = local_id;
        if (identity.find(';') == std::string::npos) {
            const auto account = accounts.find_by_id(framework::Uuid{}.str(), local_id);
            if (!account || local_home.empty())
                return;
            identity += ";" + local_home + (local_home.ends_with('/') ? "" : "/") + ";" +
                        account->first_name.value_or("") + " " + account->last_name.value_or("");
        }
        if (profile_workers && remote != circuit.service_urls->end() && circuit.service_session_id) {
            auto endpoint = framework::service_url_object_text(remote->second);
            if (framework::HttpHost{endpoint}.valid()) {
                if (!endpoint.ends_with('/'))
                    endpoint += '/';
                const auto local = identity + ";" + secret;
                const auto body = network::build_service_form({{"METHOD", "newfriendship"},
                                                               {"PrincipalID", circuit.agent_id.str()},
                                                               {"Friend", local},
                                                               {"SESSIONID", circuit.session_id.str()},
                                                               {"KEY", *circuit.service_session_id}});
                auto transport = network::rest_forms_transport(outgoing_clients());
                profile_workers->enqueue([transport = std::move(transport), endpoint, body] {
                    transport(endpoint + "hgfriends", body);
                });
            }
        }
    };
    sync_foreign_friend = [&](const std::string& recipient_id, const std::string& sender_id,
                              const std::string& identifier, const std::string& name, bool accept) {
        if (identifier != sender_id && profile_workers) {
            const auto home =
                config.get("Startup", "HomeURI",
                           config.get("Hypergrid", "HomeURI", config.get("LoginService", "SRV_HomeURI")));
            const auto home_end = identifier.find(';', 37);
            const auto secret_at = identifier.rfind(';');
            if (home_end != std::string::npos && secret_at > home_end && secret_at + 1 < identifier.size() &&
                (!accept || !home.empty())) {
                const auto foreign_home = identifier.substr(37, home_end - 37);
                const auto secret = identifier.substr(secret_at + 1);
                const auto local_identifier =
                    recipient_id + ";" + home + (home.ends_with('/') ? "" : "/") + ";" + name + ";" + secret;
                profile_workers->enqueue([&, foreign_home, sender_id, recipient_id, local_identifier, secret,
                                          accept] {
                    const framework::HttpHost host{foreign_home};
                    if (!host.valid())
                        return;
                    auto clients = outgoing_clients();
                    network::NwcXmlRpcHttpTransport xmlrpc(clients);
                    auto parameters = std::make_shared<network::NwcXmlRpcRequest::Parameters>();
                    parameters->push_back(
                        std::make_shared<network::Value>(network::Value::object({{"userID", sender_id}})));
                    auto lookup = std::make_shared<network::NwcXmlRpcRequest>(u"get_server_urls", parameters);
                    const auto reply = xmlrpc.send(lookup, framework::utf16_replacing(foreign_home), 10000);
                    if (!reply || reply->is_fault || !reply->value)
                        return;
                    auto uri = reply->value->get("SRV_FriendsServerURI").as_string();
                    if (!framework::HttpHost{uri}.valid())
                        return;
                    if (!uri.ends_with('/'))
                        uri += '/';
                    std::vector<std::pair<std::string, std::optional<std::string>>> fields{
                        {"METHOD", accept ? "newfriendship" : "deletefriendship"},
                        {"PrincipalID", sender_id},
                        {"Friend", accept ? local_identifier : recipient_id}};
                    if (!accept)
                        fields.emplace_back("SECRET", secret);
                    network::rest_forms_transport(clients)(uri + "hgfriends",
                                                           network::build_service_form(fields));
                });
            }
        }
    };
    publish_friend_status = [&](framework::Uuid principal, framework::Uuid session, bool online) {
        std::map<std::string, std::vector<std::string>> foreign_friends;
        for (const auto& entry : friends->get(principal)) {
            framework::Uuid recipient;
            if (entry.their_flags == -1 || !(entry.my_flags & 1) || !entry.friend_id ||
                !framework::try_parse_sdk_uuid(std::string_view(*entry.friend_id).substr(0, 36), recipient))
                continue;
            const bool delivered = local_regions.deliver_to_viewer([&](auto& runtime) {
                return runtime.notify_friend_status(recipient, principal, online);
            });
            if (delivered || !profile_workers)
                continue;
            if (entry.friend_id->size() > 36) {
                const auto home_end = entry.friend_id->find(';', 37);
                if ((*entry.friend_id)[36] == ';' && home_end != std::string::npos)
                    foreign_friends[entry.friend_id->substr(37, home_end - 37)].push_back(*entry.friend_id);
                continue;
            }
            const auto region = friend_region(recipient.str());
            if (!region)
                continue;
            const auto uri = region->server_uri() + "friends";
            const auto body = network::build_service_form({{"METHOD", "status"},
                                                           {"FromID", principal.str()},
                                                           {"ToID", recipient.str()},
                                                           {"Online", online ? "True" : "False"}});
            auto transport = network::rest_forms_transport(outgoing_clients());
            profile_workers->enqueue([transport = std::move(transport), uri, body] { transport(uri, body); });
        }
        for (auto& [home, identifiers] : foreign_friends) {
            if (!framework::HttpHost{home}.valid())
                continue;
            profile_workers->enqueue([&, principal, session, online, home,
                                      identifiers = std::move(identifiers)] {
                auto clients = outgoing_clients();
                network::NwcXmlRpcHttpTransport xmlrpc(clients);
                auto parameters = std::make_shared<network::NwcXmlRpcRequest::Parameters>();
                parameters->push_back(std::make_shared<network::Value>(
                    network::Value::object({{"userID", identifiers.front().substr(0, 36)}})));
                auto lookup = std::make_shared<network::NwcXmlRpcRequest>(u"get_server_urls", parameters);
                const auto reply = xmlrpc.send(lookup, framework::utf16_replacing(home), 10000);
                if (!reply || reply->is_fault || !reply->value)
                    return;
                auto uri = reply->value->get("SRV_FriendsServerURI").as_string();
                if (!framework::HttpHost{uri}.valid())
                    return;
                if (!uri.ends_with('/'))
                    uri += '/';
                std::vector<std::pair<std::string, std::optional<std::string>>> fields{
                    {"METHOD", "statusnotification"},
                    {"userID", principal.str()},
                    {"online", online ? "True" : "False"}};
                for (std::size_t i = 0; i < identifiers.size(); ++i)
                    fields.emplace_back("friend_" + std::to_string(i), identifiers[i]);
                const auto response = network::rest_forms_transport(clients)(
                    uri + "hgfriends", network::build_service_form(fields));
                if (!online || response.empty())
                    return;
                for (const auto& [key, value] : network::parse_service_xml(response).fields) {
                    framework::Uuid friend_id;
                    if (!key.starts_with("friend_") || !framework::try_parse_sdk_uuid(value.text, friend_id))
                        continue;
                    const auto prefix = friend_id.str() + ";";
                    if (std::any_of(identifiers.begin(), identifiers.end(),
                                    [&](const auto& identifier) { return identifier.starts_with(prefix); })) {
                        local_regions.deliver_to_viewer([&](auto& runtime) {
                            return runtime.notify_friend_status(principal, friend_id, true, session);
                        });
                    }
                }
            });
        }
    };
    forward_friend_rights = [&](framework::Uuid recipient, framework::Uuid principal, std::int32_t previous,
                                std::int32_t granted) {
        if (!profile_workers)
            return;
        const auto region = friend_region(recipient.str());
        if (!region)
            return;
        const auto uri = region->server_uri() + "friends";
        const auto body = network::build_service_form({{"METHOD", "grant_rights"},
                                                       {"FromID", principal.str()},
                                                       {"ToID", recipient.str()},
                                                       {"UserFlags", std::to_string(previous)},
                                                       {"Rights", std::to_string(granted)}});
        auto transport = network::rest_forms_transport(outgoing_clients());
        profile_workers->enqueue([transport = std::move(transport), uri, body] { transport(uri, body); });
    };
    forward_friend_termination = [&](framework::Uuid recipient, framework::Uuid former_friend) {
        if (!profile_workers)
            return;
        const auto region = friend_region(recipient.str());
        if (!region)
            return;
        const auto uri = region->server_uri() + "friends";
        const auto body = network::build_service_form({{"METHOD", "friendship_terminated"},
                                                       {"FromID", former_friend.str()},
                                                       {"ToID", recipient.str()}});
        auto transport = network::rest_forms_transport(outgoing_clients());
        profile_workers->enqueue([transport = std::move(transport), uri, body] { transport(uri, body); });
    };
    send_friend_offer = [&](const scene::RegionInstantMessage& offer) {
        if (region_runtime->deliver_instant_message(offer) || !profile_workers)
            return;
        const auto region = friend_region(offer.recipient.str());
        std::string uri;
        auto name = framework::utf8(offer.sender_name);
        if (region) {
            uri = region->server_uri() + "friends";
        } else {
            {
                std::lock_guard lock(live_friend_offers_mutex);
                const auto endpoint = visited_friend_endpoints.find(offer.recipient);
                if (endpoint == visited_friend_endpoints.end())
                    return;
                uri = endpoint->second;
            }
            const auto home =
                config.get("Startup", "HomeURI",
                           config.get("Hypergrid", "HomeURI", config.get("LoginService", "SRV_HomeURI")));
            const framework::HttpHost host{home};
            if (!host.valid())
                return;
            auto authority = host.host;
            if (authority.find(':') != std::string::npos && !authority.starts_with('['))
                authority = "[" + authority + "]";
            if (host.port != (home.starts_with("https:") ? 443 : 80))
                authority += ":" + std::to_string(host.port);
            std::replace(name.begin(), name.end(), ' ', '.');
            name += "@" + authority;
            if (!uri.ends_with('/'))
                uri += '/';
            uri += "hgfriends";
        }
        const auto body = network::build_service_form({{"METHOD", "friendship_offered"},
                                                       {"FromID", offer.sender.str()},
                                                       {"ToID", offer.recipient.str()},
                                                       {"FromName", name},
                                                       {"Message", framework::utf8(offer.message)}});
        auto transport = network::rest_forms_transport(outgoing_clients());
        profile_workers->enqueue([transport = std::move(transport), uri, body] { transport(uri, body); });
    };
    send_friend_answer = [&](framework::Uuid recipient, framework::Uuid sender, std::string_view name,
                             bool accepted) {
        if (deliver_friend_answer(recipient, sender, name, accepted) || !profile_workers)
            return;
        const auto region = friend_region(recipient.str());
        if (!region)
            return;
        const auto uri = region->server_uri() + "friends";
        const auto body =
            network::build_service_form({{"METHOD", accepted ? "friendship_approved" : "friendship_denied"},
                                         {"FromID", sender.str()},
                                         {"FromName", std::string(name)},
                                         {"ToID", recipient.str()}});
        auto transport = network::rest_forms_transport(outgoing_clients());
        profile_workers->enqueue([transport = std::move(transport), uri, body] { transport(uri, body); });
    };
    if (friends_endpoint && local_region) {
        friends_endpoint->set_rights_handler(
            [&](auto recipient, auto principal, auto previous, auto granted) {
                return local_regions.deliver_to_viewer([&](auto& runtime) {
                    return runtime.notify_friend_rights(recipient, principal, previous, granted);
                });
            });
        friends_endpoint->set_termination_handler([&](auto recipient, auto former_friend) {
            return local_regions.deliver_to_viewer([&](auto& runtime) {
                return runtime.notify_friend_termination(recipient, former_friend);
            });
        });
        friends_endpoint->set_offer_handler(deliver_friend_offer);
        friends_endpoint->set_answer_handler(deliver_friend_answer);
        friends_endpoint->set_status_handler([&](auto recipient, auto sender, bool online) {
            return local_regions.deliver_to_viewer([&](auto& runtime) {
                return runtime.notify_friend_status(recipient, sender, online);
            });
        });
    }
    if (travel_store && grid && grid_user) {
        app.set_home_region_lookup([&](const network::Value& data) {
            const auto missing = network::Value::object({{"result", "false"}});
            framework::Uuid user;
            if (!framework::try_parse_sdk_uuid(data.get("userID").as_string(), user))
                return missing;
            const auto info = grid_user->info(user.str());
            if (!info)
                return missing;
            auto region = info->home_region.is_zero() ? std::nullopt : grid->by_id({}, info->home_region);
            if (!region) {
                const auto defaults = grid->defaults({});
                if (!defaults.empty())
                    region = defaults.front();
            }
            if (!region || !region->internal_endpoint)
                return missing;
            return network::Value::object(
                {{"result", "true"},
                 {"uuid", region->id.str()},
                 {"x", std::to_string(region->x)},
                 {"y", std::to_string(region->y)},
                 {"size_x", std::to_string(region->size_x)},
                 {"size_y", std::to_string(region->size_y)},
                 {"region_name", region->name.value_or("")},
                 {"hostname", region->external_host.value_or("")},
                 {"http_port", std::to_string(region->http_port)},
                 {"server_uri", region->server_uri()},
                 {"internal_port", std::to_string(region->internal_endpoint->port)},
                 {"position",
                  framework::vector3_string(info->home_region.is_zero() ? framework::Vector3{128, 128, 0}
                                                                        : info->home_position)},
                 {"lookAt",
                  framework::vector3_string(info->home_region.is_zero() ? framework::Vector3{0, 1, 0}
                                                                        : info->home_look_at)}});
        });
    }
    std::mutex gateway_region_mutex;
    std::optional<services::GridRegion> gateway_region;
    const auto gatekeeper_connector = find_service_connector(
        config, "GatekeeperServiceInConnector", "/OpenSim.Server.Handlers.dll:GatekeeperServiceInConnector");
    const bool gatekeeper_enabled =
        grid && config.boolean("NativeServer", "EnableGatekeeperDiscovery", !gatekeeper_connector.empty());
    const auto connector_port = [&](const std::string& connector) {
        const auto separator = connector.find('/');
        if (separator == std::string::npos)
            return options.port;
        auto prefix = connector.substr(0, separator);
        if (const auto section = prefix.find('@'); section != std::string::npos)
            prefix.erase(0, section + 1);
        const auto configured = parse_number(prefix);
        if (configured < 0 || configured > 65535)
            throw std::invalid_argument("Invalid service connector port");
        return configured == 0 ? options.port : configured;
    };
    const int gatekeeper_port =
        gatekeeper_enabled ? connector_port(gatekeeper_connector) : options.port;
    const int home_agent_port = connector_port(home_agent_connector);
    const bool home_agent_on_main = home_agent_port == options.port;
    const int grid_info_port = connector_port(find_service_connector(
        config, "GridInfoServerInConnector", "/OpenSim.Server.Handlers.dll:GridInfoServerInConnector"));
    const auto foreign_grid =
        gatekeeper_enabled
            ? std::make_unique<server::GridIdentity>(config, std::nullopt, framework::RegionUri::Resolver{},
                                                     nullptr, server::GridIdentity::Role::Gatekeeper)
            : nullptr;
    if (gatekeeper_enabled) {
        framework::Uuid scope;
        framework::try_parse_sdk_uuid(config.get("GatekeeperService", "ScopeID"), scope);
        const bool allow_any = config.boolean("GatekeeperService", "AllowTeleportsToAnyRegion", true);
        const auto gateway = foreign_grid->gate_uri_slash();
        app.set_gatekeeper_handler(
            [&, scope, allow_any, gateway](std::string_view method, const network::Value& data) {
                const auto default_region = [&]() -> std::optional<services::GridRegion> {
                    const auto regions = grid->hypergrid_defaults(scope);
                    return regions.empty() ? std::nullopt : std::optional{regions.front()};
                };
                if (method == "link_region") {
                    const auto name = data.get("region_name").as_string();
                    const bool use_default = !allow_any || name.empty();
                    const auto region = use_default ? default_region() : grid->local_by_name(scope, name);
                    if (use_default && region) {
                        std::lock_guard lock(gateway_region_mutex);
                        gateway_region = region;
                    }
                    const auto id = region ? region->id.str() : framework::Uuid{}.str();
                    auto image_id = id;
                    std::erase(image_id, '-');
                    return network::Value::object(
                        {{"result", region ? "True" : "False"},
                         {"uuid", id},
                         {"handle", std::to_string(region ? region->handle() : 0)},
                         {"size_x", std::to_string(region ? region->size_x : 256)},
                         {"size_y", std::to_string(region ? region->size_y : 256)},
                         {"region_image",
                          region ? region->server_uri() + "index.php?method=regionImage" + image_id : ""},
                         {"external_name", gateway + (name.empty() ? "" : " " + name)}});
                }
                framework::Uuid id;
                framework::try_parse_sdk_uuid(data.get("region_uuid").as_string(), id);
                framework::Uuid agent;
                if (data.contains("agent_id") &&
                    !framework::try_parse_sdk_uuid(data.get("agent_id").as_string(), agent))
                    throw std::invalid_argument("Invalid Gatekeeper agent ID");
                auto region = allow_any ? grid->by_id(scope, id) : std::nullopt;
                if (!allow_any) {
                    std::lock_guard lock(gateway_region_mutex);
                    if (!gateway_region)
                        throw std::runtime_error("Gatekeeper default region has not been linked");
                    region = gateway_region;
                }
                if (!region)
                    return network::Value::object(
                        {{"result", "false"}, {"message", "The teleport destination could not be found."}});
                auto result = network::Value::object(
                    {{"result", "true"},
                     {"uuid", region->id.str()},
                     {"x", std::to_string(region->x)},
                     {"y", std::to_string(region->y)},
                     {"size_x", std::to_string(region->size_x)},
                     {"size_y", std::to_string(region->size_y)},
                     {"region_name", region->name.value_or("")},
                     {"hostname", region->external_host.value_or("")},
                     {"http_port", std::to_string(region->http_port)},
                     {"internal_port",
                      std::to_string(region->internal_endpoint ? region->internal_endpoint->port : 0)},
                     {"server_uri", region->server_uri()}});
                if (!allow_any)
                    result.set("message", "Teleporting to the default region.");
                return result;
            });
    }
    std::mutex foreign_presence_mutex;
    server::AgentAccessHandler::CreateAgent admit_foreign_agent;
    server::AgentAccessHandler home_agent_launch(nullptr);
    if (travel_store) {
        home_agent_launch.set_home_agent_handler(
            [&, requester = make_simulation_requester()](
                const services::GridRegion* source, services::GridRegion& gatekeeper,
                services::GridRegion& destination,
                const std::shared_ptr<framework::AgentCircuitData>& circuit, std::string_view caller,
                services::LoginTransferContext& context, std::string& reason) {
                const auto account = accounts.find_by_id(framework::Uuid{}.str(), circuit->agent_id.str());
                if (!account) {
                    reason = "Forbidden to launch your agents from here";
                    return false;
                }
                const auto gate = gatekeeper.server_uri();
                const framework::HttpUri gate_uri{gate};
                if (destination.id.is_zero() || !gate_uri.valid() || gate_uri.host.empty()) {
                    reason = "Invalid destination";
                    return false;
                }
                if (!travel_allowed(account->user_level, gate)) {
                    reason = "Your world does not allow you to visit the destination";
                    return false;
                }
                std::lock_guard lock(travel_launch_mutex);
                const auto previous = travel_store->get(circuit->session_id);
                const auto login_server_ip = config.get("UserAgentService", "LoginServerIP", "127.0.0.1");
                const bool from_login = !login_server_ip.empty() && caller == login_server_ip;
                if ((previous && previous->user_id != circuit->agent_id) ||
                    (!from_login && (!previous || previous->service_token.empty() ||
                                     previous->service_token != circuit->service_session_id.value_or("")))) {
                    reason = "Unable to verify identity";
                    return false;
                }
                const auto replaced = previous ? previous :
                    travel_store->replaced_by(circuit->session_id, circuit->agent_id);
                destination.raw_server_uri = gate;
                circuit->service_session_id =
                    destination.server_uri() + ";" + framework::Uuid::random().str();
                if (previous)
                    circuit->ip_address = previous->client_ip;
                travel_store->store({circuit->session_id, circuit->agent_id, destination.server_uri(),
                                     *circuit->service_session_id, circuit->ip_address.value_or(""),
                                     previous ? previous->external_ip : std::string{}});
                const auto rollback = [&] {
                    if (replaced)
                        travel_store->store(*replaced);
                    else
                        travel_store->erase(circuit->session_id);
                };
                bool accepted;
                try {
                    constexpr std::uint32_t via_login = 1u << 7;
                    if (admit_foreign_agent && framework::invariant_equals_ignore_case(home_grid_name, gate)) {
                        accepted = admit_foreign_agent(source, destination, circuit, via_login, context, reason);
                    } else {
                        services::GatekeeperSimulationConnector connector(requester);
                        accepted = connector.create_agent(source, destination, circuit, via_login, context, reason);
                    }
                } catch (...) {
                    rollback();
                    throw;
                }
                if (!accepted)
                    rollback();
                return accepted;
            });
    }
    server::AgentAccessHandler foreign_agent(nullptr);
    const bool foreign_enabled = gatekeeper_enabled && region_runtime &&
                                 config.boolean("NativeServer", "EnableForeignAgentAdmission", false);
    if (foreign_enabled) {
        if (!config.get("GatekeeperService", "BansService").empty())
            throw std::invalid_argument("Native Gatekeeper cannot load the configured BansService provider");
        if (!foreign_grid->gate_uri())
            throw std::invalid_argument("Foreign agent admission requires GatekeeperURI");
        framework::Uuid foreign_scope;
        framework::try_parse_sdk_uuid(config.get("GatekeeperService", "ScopeID"), foreign_scope);
        const bool visitors_allowed = config.boolean("GatekeeperService", "ForeignAgentsAllowed", true);
        const bool allow_duplicates = config.boolean("PresenceService", "AllowDuplicatePresences", false);
        const auto duplicate_message_key = config.get("Messaging", "MessageKey");
        const auto duplicate_clients = outgoing_clients();
        const auto access_setting = [&](const char* key) {
            return config.get("GatekeeperService", key, config.get("AccessControl", key));
        };
        const auto allowed_clients =
            services::LoginService::compile_client_pattern(access_setting("AllowedClients"));
        const auto denied_clients =
            services::LoginService::compile_client_pattern(access_setting("DeniedClients"));
        const auto denied_identifier = [&](const char* key) {
            auto value = framework::utf16_replacing(access_setting(key));
            if (std::all_of(value.begin(), value.end(), framework::unicode_whitespace))
                value.clear();
            return value;
        };
        const auto denied_macs = denied_identifier("DeniedMacs");
        const auto denied_id0s = denied_identifier("DeniedID0s");
        const auto normalize_home = [](std::string_view value) {
            auto uri = framework::trim_utf8(value);
            if (!uri.empty() && !uri.ends_with('/'))
                uri += '/';
            return uri;
        };
        std::vector<std::string> visitor_exceptions;
        const auto exception_setting =
            config.get("GatekeeperService", visitors_allowed ? "AllowExcept" : "DisallowExcept");
        std::string_view remaining = exception_setting;
        while (!remaining.empty()) {
            const auto comma = remaining.find(',');
            auto uri = normalize_home(remaining.substr(0, comma));
            if (!uri.empty())
                visitor_exceptions.push_back(std::move(uri));
            if (comma == std::string_view::npos)
                break;
            remaining.remove_prefix(comma + 1);
        }
        auto transport = std::make_shared<network::NwcXmlRpcHttpTransport>(outgoing_clients());
        verify_foreign_client = [&, transport](const auto& circuit, std::string_view peer) {
            if (!circuit.service_urls)
                return false;
            const auto entry = circuit.service_urls->find("HomeURI");
            if (entry == circuit.service_urls->end())
                return false;
            const auto home = framework::service_url_object_text(entry->second);
            if (foreign_grid->gate_uri() &&
                framework::HttpHost{home}.equals(framework::HttpHost{foreign_grid->gate_uri()})) {
                if (login_user_agent)
                    return login_user_agent->verify_client(circuit.session_id,
                                                           framework::utf16_replacing(peer));
                return verify_native_travel_client && verify_native_travel_client(circuit.session_id, peer);
            }
            services::UserAgentConnectorServices connector;
            connector.client = [transport](std::int32_t timeout) {
                return network::xml_rpc_http_client(transport, timeout);
            };
            connector.debug = connector.error = connector.warn = [&console_log](const auto& message) {
                console_log.append(framework::console_log_warn, message);
            };
            services::UserAgentServiceConnector verifier(framework::utf16_replacing(home),
                                                         std::move(connector));
            return verifier.verify_client(circuit.session_id, framework::utf16_replacing(peer));
        };
        admit_foreign_agent = [&, transport, foreign_scope, visitors_allowed, normalize_home,
                                                denied_macs, denied_id0s, allowed_clients, denied_clients,
                                                allow_duplicates, duplicate_message_key, duplicate_clients,
                                                visitor_exceptions = std::move(visitor_exceptions)](
                                                   const auto* source, auto& destination, const auto& circuit,
                                                   std::uint32_t, auto& context, std::string& reason) {
            const auto viewer = framework::viewer_name(*circuit);
            if (allowed_clients && !allowed_clients(viewer)) {
                reason = "Login failed: client " + viewer + " is not allowed";
                return false;
            }
            if (denied_clients && denied_clients(viewer)) {
                reason = "Login failed: client " + viewer + " is denied";
                return false;
            }
            const auto denied = [](const auto& list, const auto& identifier) {
                return !list.empty() && framework::invariant_index_of_ignore_case(
                                            list, framework::utf16_replacing(identifier)) >= 0;
            };
            const auto mac = circuit->mac.value_or("");
            const auto id0 = circuit->id0.value_or("");
            if (denied(denied_macs, mac)) {
                reason = "Login failed: client with Mac " + mac + " is denied";
                return false;
            }
            if (denied(denied_id0s, id0)) {
                reason = "Login failed: client with id0 " + id0 + " is denied";
                return false;
            }
            const auto token = circuit->service_session_id.value_or("");
            std::string home;
            if (circuit->service_urls) {
                if (const auto entry = circuit->service_urls->find("HomeURI");
                    entry != circuit->service_urls->end())
                    home = framework::service_url_object_text(entry->second);
            }
            std::shared_ptr<services::UserAgentServiceConnector> remote_verifier;
            if (!server::authenticate_gatekeeper_agent(
                    *foreign_grid, token, circuit->ip_address.value_or(""), home,
                    [&](const framework::HttpHost&, bool local) {
                        if (local) {
                            if (login_user_agent)
                                return login_user_agent->verify_agent(circuit->session_id,
                                                                      framework::utf16_replacing(token));
                            const auto record =
                                travel_store ? travel_store->get(circuit->session_id) : std::nullopt;
                            return record && record->user_id == circuit->agent_id &&
                                   record->service_token == token;
                        }
                        services::UserAgentConnectorServices connector;
                        connector.client = [transport](std::int32_t timeout) {
                            return network::xml_rpc_http_client(transport, timeout);
                        };
                        connector.debug = [&console_log](const auto& message) {
                            console_log.append(framework::console_log_debug, message);
                        };
                        connector.error = connector.warn = [&console_log](const auto& message) {
                            console_log.append(framework::console_log_warn, message);
                        };
                        remote_verifier = std::make_shared<services::UserAgentServiceConnector>(
                            framework::utf16_replacing(home), std::move(connector));
                        return remote_verifier->verify_agent(circuit->session_id,
                                                             framework::utf16_replacing(token));
                    })) {
                reason = "Unable to verify identity";
                return false;
            }
            const auto account = accounts.find_by_id(foreign_scope.str(), circuit->agent_id.str());
            if (account) {
                const auto destination_grid = foreign_grid->gate_uri_slash();
                bool returning_home;
                if (login_user_agent) {
                    returning_home = login_user_agent->is_agent_coming_home(
                        circuit->session_id, framework::utf16_replacing(destination_grid));
                } else {
                    const auto record = travel_store ? travel_store->get(circuit->session_id) : std::nullopt;
                    returning_home = record && record->user_id == circuit->agent_id &&
                                     record->service_token == token &&
                                     framework::invariant_equals_ignore_case(record->grid, destination_grid);
                }
                if (!returning_home) {
                    reason = "Unauthorized";
                    return false;
                }
            }
            if (account) {
                circuit->first_name = account->first_name;
                circuit->last_name = account->last_name;
            } else {
                const auto uri = normalize_home(home);
                const bool exception = std::find(visitor_exceptions.begin(), visitor_exceptions.end(), uri) !=
                                       visitor_exceptions.end();
                if (visitors_allowed == exception) {
                    reason = "Destination does not allow visitors from your world";
                    return false;
                }
                const auto last = circuit->last_name.value_or("");
                if (!last.starts_with('@'))
                    circuit->first_name = circuit->first_name.value_or("") + "." + last;
                const framework::HttpHost home_host{home};
                circuit->last_name = "@" + home_host.host;
                const auto default_port = home_host.uri.starts_with("https://") ? 443 : 80;
                if (home_host.port != default_port)
                    *circuit->last_name += ":" + std::to_string(home_host.port);
            }
            if (circuit->agent_id == framework::Uuid("6571e388-6218-4574-87db-f9379718315e")) {
                reason = "Invalid account ID";
                return false;
            }
            circuit->verified_foreign_login = !account;
            // Keep presence registration and circuit creation together for competing
            // foreign requests so one request cannot roll back another's presence.
            std::lock_guard lock(foreign_presence_mutex);
            const auto existing = presence_store->get(circuit->session_id);
            if (existing) {
                const auto secure_session = existing->data.find("SecureSessionID");
                if (existing->user_id != circuit->agent_id.str() || secure_session == existing->data.end() ||
                    secure_session->second != circuit->secure_session_id.str()) {
                    reason = "Unable to login presence";
                    return false;
                }
            }
            const auto user = grid_user_identity(*circuit);
            if (!allow_duplicates && grid_user) {
                const auto previous = grid_user->info(user);
                if (previous && previous->online && !previous->last_region.is_zero() &&
                    services::recover_duplicate_session(*grid, *grid_user, {}, circuit->agent_id, *previous,
                                                        duplicate_message_key, duplicate_clients, user)) {
                    reason = "You appear to be already logged in on the destination grid "
                             "Please wait a a minute or two and retry. "
                             "If this takes longer than a few minutes please contact the grid owner.";
                    return false;
                }
            }
            const auto registered_destination = grid->by_id(foreign_scope, destination.id);
            auto runtime = registered_destination ? local_regions.find(registered_destination->id) : nullptr;
            if (!runtime) {
                reason = "Destination region not found";
                return false;
            }
            destination = *registered_destination;
            if (!runtime->query_gatekeeper_access(destination, *circuit, reason))
                return false;
            if (!existing &&
                !presence->login(circuit->agent_id.str(), circuit->session_id, circuit->secure_session_id)) {
                reason = "Unable to login presence";
                return false;
            }
            constexpr std::uint32_t via_login = 1u << 7;
            constexpr std::uint32_t via_hypergrid_login = 1u << 30;
            const auto login_flags =
                circuit->teleport_flags | (existing ? via_login : via_hypergrid_login);
            try {
                if (runtime->create_agent(source, destination, circuit, login_flags, context, reason)) {
                    if (!existing && !account && grid_user)
                        grid_user->logged_in(user);
                    return true;
                }
            } catch (...) {
                if (!existing)
                    presence->logout(circuit->session_id);
                throw;
            }
            if (!existing)
                presence->logout(circuit->session_id);
            return false;
        };
        foreign_agent.set_create_agent_handler(admit_foreign_agent);
    }
    const auto foreign_request =
        [&](const network::HttpRequest& request, bool allow_home_agent) -> std::optional<network::HttpResponse> {
        if (auto response = local_regions.map_request(request))
            return response;
        if (estate_communications && local_region && request.path == "/estate") {
            network::HttpResponse response;
            response.keep_alive = false;
            if (request.method != "POST") {
                response.status = 404;
                return response;
            }
            response.content_type = "application/xml";
            bool accepted = false;
            try {
                const auto body = network::read_service_body(request.body);
                const auto fields = network::parse_service_form(framework::trim_utf8(body));
                const auto field = [&](const char* name) {
                    const auto found = fields.find(name);
                    if (found == fields.end())
                        return std::string{};
                    if (const auto* value = std::get_if<std::string>(&found->second))
                        return *value;
                    return std::string("System.Collections.Generic.List`1[System.String]");
                };
                std::int32_t estate;
                framework::Uuid id;
                if (fields.contains("TOKEN") && field("TOKEN") == estate_token &&
                    services::parse_account_integer(field("EstateID"), estate)) {
                    const auto estate_id = static_cast<std::uint32_t>(estate);
                    const auto method = field("METHOD");
                    if (method == "update_covenant" && framework::try_parse_sdk_uuid(field("CovenantID"), id)) {
                        local_regions.for_each_region([&](auto& runtime) {
                            runtime.receive_estate_covenant(estate_id, id);
                        });
                        accepted = true;
                    } else if (method == "update_estate") {
                        local_regions.for_each_region([&](auto& runtime) {
                            runtime.receive_estate_update(estate_id);
                        });
                        accepted = true;
                    } else if (method == "estate_message" && fields.contains("FromName") &&
                               fields.contains("Message") && framework::try_parse_sdk_uuid(field("FromID"), id)) {
                        if (estate >= 0) {
                            const auto message = field("Message");
                            local_regions.for_each_region([&](auto& runtime) {
                                runtime.receive_estate_message(estate_id, message);
                            });
                        }
                        accepted = true;
                    } else if (method == "teleport_home_all_users" ||
                               (method == "teleport_home_one_user" &&
                                framework::try_parse_sdk_uuid(field("PreyID"), id))) {
                        const auto target = method == "teleport_home_one_user" ? std::optional{id} : std::nullopt;
                        if (estate >= 0)
                            local_regions.for_each_region([&](auto& runtime) {
                                runtime.receive_estate_teleport_home(estate_id, target);
                            });
                        accepted = true;
                    }
                }
            } catch (const std::exception&) {
            }
            response.body = network::service_xml(
                network::ServiceXmlValue::object({{"RESULT", accepted ? "True" : "False"}}));
            return response;
        }
        if (travel_store && friends && request.path == "/hgfriends" && request.method == "POST") {
            network::HttpResponse response;
            response.content_type = "application/xml";
            const auto result = [&](bool success, std::string message) {
                response.body = network::service_xml(
                    network::ServiceXmlValue::object({{"RESULT", success ? "Success" : "Failure"},
                                                      {success ? "Value" : "Message", std::move(message)}}));
                return response;
            };
            const auto body = network::read_service_body(request.body);
            const auto fields = network::parse_service_form(framework::trim_utf8(body));
            const auto field = [&](const char* name) {
                const auto found = fields.find(name);
                const auto* value =
                    found == fields.end() ? nullptr : std::get_if<std::string>(&found->second);
                return value ? *value : std::string{};
            };
            const auto bool_result = [&](bool success) {
                response.body = network::service_xml(
                    network::ServiceXmlValue::object({{"RESULT", success ? "True" : "False"}}));
                return response;
            };
            const auto method = field("METHOD");
            framework::Uuid session, principal, friend_id;
            if (method == "deletefriendship") {
                // The legacy handler rejects both present and missing SECRET fields.
                return bool_result(false);
            }
            if (method == "validate_friendship_offered") {
                if (!framework::try_parse_sdk_uuid(field("PrincipalID"), principal) ||
                    !framework::try_parse_sdk_uuid(field("Friend"), friend_id))
                    return bool_result(false);
                const auto prefix = principal.str();
                const auto entries = config.boolean("NativeServer", "EnableHypergridUuidOfferLookup", false)
                                         ? friends->get(friend_id)
                                         : friends->get(std::optional<std::string>(friend_id.str()));
                for (const auto& entry : entries)
                    if (entry.friend_id && entry.friend_id->starts_with(prefix) && entry.their_flags == -1)
                        return bool_result(true);
                return bool_result(false);
            }
            if (method == "friendship_offered") {
                if (!framework::try_parse_sdk_uuid(field("FromID"), principal) ||
                    !framework::try_parse_sdk_uuid(field("ToID"), friend_id) ||
                    !accounts.find_by_id(framework::Uuid{}.str(), friend_id.str()) || !profile_workers)
                    return bool_result(false);
                const auto name = field("FromName");
                const auto message = field("Message");
                profile_workers->enqueue([&, sender = principal, recipient = friend_id, name, message] {
                    const auto at = name.find('@');
                    if (at == std::string::npos || name.find('@', at + 1) != std::string::npos)
                        return;
                    const framework::HttpHost home{"http://" + name.substr(at + 1)};
                    if (!home.valid())
                        return;
                    auto clients = outgoing_clients();
                    network::NwcXmlRpcHttpTransport xmlrpc(clients);
                    auto parameters = std::make_shared<network::NwcXmlRpcRequest::Parameters>();
                    parameters->push_back(
                        std::make_shared<network::Value>(network::Value::object({{"userID", sender.str()}})));
                    auto lookup = std::make_shared<network::NwcXmlRpcRequest>(u"get_server_urls", parameters);
                    const auto reply =
                        xmlrpc.send(lookup, framework::utf16_replacing(home.uri_slash()), 10000);
                    if (!reply || reply->is_fault || !reply->value)
                        return;
                    auto server_uri = reply->value->get("SRV_FriendsServerURI").as_string();
                    if (!framework::HttpHost{server_uri}.valid())
                        return;
                    if (!server_uri.ends_with('/'))
                        server_uri += '/';
                    const auto transport = network::rest_forms_transport(clients);
                    const auto verification = network::parse_service_xml(
                        transport(server_uri + "hgfriends",
                                  network::build_service_form({{"METHOD", "validate_friendship_offered"},
                                                               {"PrincipalID", sender.str()},
                                                               {"Friend", recipient.str()}})));
                    const auto accepted = std::find_if(
                        verification.fields.begin(), verification.fields.end(), [](const auto& entry) {
                            return entry.first == "RESULT" && entry.second.text == "True";
                        });
                    if (accepted == verification.fields.end())
                        return;
                    const auto display = name.substr(0, at);
                    const auto dot = display.find('.');
                    const auto stored_name =
                        dot != std::string::npos && display.find('.', dot + 1) == std::string::npos
                            ? framework::trim_utf8(display.substr(0, dot)) + " " +
                                  framework::trim_utf8(display.substr(dot + 1))
                            : framework::trim_utf8(display) + " @" + name.substr(at + 1);
                    const auto identifier = sender.str() + ";" + home.uri_slash() + ";" + stored_name + ";" +
                                            framework::Uuid::random().str().substr(0, 8);
                    if (!friends->store(recipient.str(), identifier, 0))
                        return;
                    if (deliver_friend_offer(recipient, sender, name, message))
                        return;
                    const auto region = friend_region(recipient.str());
                    if (region)
                        transport(region->server_uri() + "friends",
                                  network::build_service_form({{"METHOD", "friendship_offered"},
                                                               {"FromID", sender.str()},
                                                               {"ToID", recipient.str()},
                                                               {"FromName", name},
                                                               {"Message", message}}));
                });
                return bool_result(true);
            }
            if (method == "statusnotification") {
                const auto status = framework::trim_utf8(field("online"));
                const bool online = framework::invariant_equals_ignore_case(status, "true");
                if (!framework::try_parse_sdk_uuid(field("userID"), principal) ||
                    (!online && !framework::invariant_equals_ignore_case(status, "false")))
                    return result(false, {});
                std::vector<std::string> identifiers;
                for (const auto& [name, value] : fields)
                    if (name.starts_with("friend_"))
                        if (const auto* identifier = std::get_if<std::string>(&value))
                            identifiers.push_back(*identifier);
                const auto online_friends = notify_hg_friends(principal, online, identifiers, false);
                std::vector<std::pair<std::string, network::ServiceXmlValue>> values;
                if (online_friends.empty())
                    values.emplace_back("RESULT", "NULL");
                for (std::size_t i = 0; i < online_friends.size(); ++i)
                    values.emplace_back("friend_" + std::to_string(i), online_friends[i]);
                response.body = network::service_xml(network::ServiceXmlValue::object(std::move(values)));
                return response;
            }
            const auto key = field("KEY");
            if (method != "getfriendperms" && method != "newfriendship")
                return result(false, {});
            const bool verified = [&] {
                if (key.empty() || !framework::try_parse_sdk_uuid(field("SESSIONID"), session))
                    return false;
                const auto travel = travel_store->get(session);
                return travel && travel->service_token == key;
            }();
            if (method == "newfriendship") {
                const auto identifier = field("Friend");
                const auto first_separator = identifier.find(';', 36);
                const auto second_separator = first_separator == std::string::npos
                                                  ? std::string::npos
                                                  : identifier.find(';', first_separator + 1);
                const auto name = second_separator == std::string::npos
                                      ? std::string_view{}
                                      : std::string_view(identifier).substr(second_separator + 1);
                const auto name_end = name.find(';');
                if (!framework::try_parse_sdk_uuid(field("PrincipalID"), principal) ||
                    identifier.size() < 36 ||
                    !framework::try_parse_sdk_uuid(std::string_view(identifier).substr(0, 36), friend_id) ||
                    (identifier.size() != 36 &&
                     name.substr(0, name_end).find_first_not_of(" \t\r\n") == std::string_view::npos))
                    return result(false, {});
                const auto prefix = friend_id.str();
                for (const auto& entry : friends->get(principal))
                    if (entry.friend_id && entry.friend_id->starts_with(prefix))
                        return result(false, {});
                if (verified) {
                    if (!friends->store(principal.str(), identifier, 0))
                        return result(false, {});
                } else {
                    const auto principal_id = principal.str();
                    const auto reverse = friends->get(friend_id);
                    const auto offer = std::find_if(reverse.begin(), reverse.end(), [&](const auto& entry) {
                        return entry.principal == friend_id && entry.friend_id &&
                               entry.friend_id->starts_with(principal_id) && entry.their_flags == -1;
                    });
                    if (offer == reverse.end() || !friends->store(principal_id, identifier, 1))
                        return result(false, {});
                    if (!friends->store(identifier, principal_id, 1)) {
                        friends->erase(principal, identifier);
                        return result(false, {});
                    }
                    const bool replaced_offer =
                        identifier == friend_id.str() && offer->friend_id == principal_id;
                    if (!replaced_offer && !friends->erase(friend_id, offer->friend_id)) {
                        friends->erase(std::optional<std::string>(identifier), principal_id);
                        friends->erase(principal, identifier);
                        return result(false, {});
                    }
                    const auto sender_name = std::string(name.substr(0, name_end));
                    send_friend_answer(principal, friend_id, sender_name, true);
                }
                response.body =
                    network::service_xml(network::ServiceXmlValue::object({{"Result", "Success"}}));
                return response;
            }
            if (!verified || !framework::try_parse_sdk_uuid(field("PRINCIPALID"), principal) ||
                !framework::try_parse_sdk_uuid(field("FRIENDID"), friend_id))
                return result(false, {});
            const auto prefix = friend_id.str();
            for (const auto& entry : friends->get(principal)) {
                if (entry.friend_id && entry.friend_id->starts_with(prefix)) {
                    if (entry.their_flags >= 0)
                        return result(true, std::to_string(entry.their_flags));
                    break;
                }
            }
            return result(false, "Friend not found");
        }
        const bool home_launch = allow_home_agent && travel_store && request.path.starts_with("/homeagent/");
        if (!home_launch && (!foreign_enabled || !request.path.starts_with("/foreignagent/")))
            return std::nullopt;
        network::HttpResponse response;
        if (request.method != "POST") {
            response.status = 405;
            return response;
        }
        if (request.headers.contains("x-secondlife-shard")) {
            response.status = 406;
            response.body = "false";
            return response;
        }
        try {
            (home_launch ? home_agent_launch : foreign_agent).process_admission(request, response);
        } catch (const std::exception&) {
            response.status = 400;
            response.body = "false";
        }
        return response;
    };
    const auto gatekeeper_request = [&](const network::HttpRequest& request) {
        if (auto response = foreign_request(request, home_agent_connector.empty() || home_agent_port == gatekeeper_port))
            return *response;
        return app.handle_gatekeeper(request);
    };
    const auto helo_connector =
        find_service_connector(config, "HeloServiceInConnector", "/OpenSim.Server.Handlers.dll:HeloServiceInConnector");
    const bool helo_enabled = config.boolean("NativeServer", "EnableHeloEndpoint", !helo_connector.empty());
    const int helo_port = helo_enabled ? connector_port(helo_connector) : options.port;
    const auto helo_handler = [](const network::HttpRequest& request) {
        network::HttpResponse response;
        if (request.method == "GET" || request.method == "HEAD")
            response.headers["X-Handlers-Provided"] = "opensim-robust";
        else
            response.status = 405;
        return response;
    };
    const bool helo_on_main = helo_enabled && helo_port == options.port;
    const auto bakes_connector =
        find_service_connector(config, "BakedTextureService", "/OpenSim.Server.Handlers.dll:XBakesConnector");
    const bool bakes_enabled =
        config.boolean("NativeServer", "EnableBakedTextureEndpoint", !bakes_connector.empty());
    const auto bakes_directory = config.get("BakedTextureService", "BaseDirectory");
    if (bakes_enabled && bakes_directory.empty())
        throw std::invalid_argument("BakedTextureService.BaseDirectory is required");
    auto bakes_root = framework::utf8_path(bakes_directory);
    if (bakes_root.is_relative())
        bakes_root = config.directory / bakes_root;
    const auto bake_file = [&](framework::Uuid id) {
        const auto name = id.str();
        return bakes_root / name.substr(0, 2) / name.substr(2, 2) / name.substr(4, 2) / name.substr(6, 4) /
               name;
    };
    const network::ServiceAuth bakes_auth(config, "BakedTextureService");
    const auto bakes_handler = [&](const network::HttpRequest& request) {
        network::HttpResponse response;
        if (!bakes_auth.authenticate(request, response))
            return response;
        if (request.method != "GET" && request.method != "POST") {
            response.status = 405;
            return response;
        }
        if (request.path.size() <= 7)
            return response;
        auto id_text = std::string_view(request.path).substr(7);
        id_text = id_text.substr(0, id_text.find_first_of("/?&"));
        framework::Uuid id;
        if (!framework::try_parse_sdk_uuid(id_text, id)) {
            response.status = 400;
            return response;
        }
        const auto path = bake_file(id);
        if (request.method == "GET") {
            std::ifstream input(path, std::ios::binary);
            if (input)
                response.body.assign(std::istreambuf_iterator<char>(input), {});
            response.content_type = "application/octet-stream";
            return response;
        }
        std::filesystem::create_directories(path.parent_path());
        auto temporary = path;
        temporary += "." + framework::Uuid::random().str() + ".tmp";
        try {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.exceptions(std::ios::badbit | std::ios::failbit);
            output.write(request.body.data(), static_cast<std::streamsize>(request.body.size()));
            output.close();
            std::filesystem::rename(temporary, path);
        } catch (...) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw;
        }
        return response;
    };
    const int bakes_port = bakes_enabled ? connector_port(bakes_connector) : options.port;
    const bool bakes_on_main = bakes_enabled && bakes_port == options.port;
    const int preferences_port =
        preferences_endpoint_enabled ? connector_port(preferences_connector) : options.port;
    const bool preferences_on_main = preferences_endpoint_enabled && preferences_port == options.port;
    const network::ServiceAuth preferences_auth(config, "AgentPreferencesService");
    const auto preferences_handler = [&](const network::HttpRequest& request) {
        network::HttpResponse response;
        if (request.method != "POST") {
            response.status = 404;
            return response;
        }
        if (!preferences_auth.authenticate(request, response))
            return response;
        response.content_type = "text/xml";
        response.body = network::service_result(false);
        try {
            const auto form =
                network::parse_service_form(framework::trim_utf8(network::read_service_body(request.body)));
            const auto field = [&](const char* name) -> const std::string& {
                return std::get<std::string>(form.at(name));
            };
            const auto& method = field("METHOD");
            framework::Uuid agent;
            if (!framework::try_parse_sdk_uuid(field(method == "setagentprefs" ? "PrincipalID" : "UserID"),
                                               agent))
                return response;
            if (method == "setagentprefs") {
                scene::AgentPreferences prefs;
                prefs.access = field("AccessPrefs");
                if (!framework::parse_invariant_float(field("HoverHeight"), prefs.hover))
                    return response;
                prefs.language = field("Language");
                const auto privacy = framework::trim_utf8(field("LanguageIsPublic"));
                if (framework::ordinal_equals_ignore_case(privacy, "True"))
                    prefs.language_public = true;
                else if (framework::ordinal_equals_ignore_case(privacy, "False"))
                    prefs.language_public = false;
                else
                    return response;
                prefs.everyone = parse_number(framework::trim_utf8(field("PermEveryone")));
                prefs.group = parse_number(framework::trim_utf8(field("PermGroup")));
                prefs.next_owner = parse_number(framework::trim_utf8(field("PermNextOwner")));
                save_local_preferences(agent, prefs);
                response.body = network::service_result(true);
            } else if (method == "getagentprefs" || method == "getagentlang") {
                const auto prefs = load_local_preferences(agent);
                std::vector<std::pair<std::string, network::ServiceXmlValue>> fields;
                if (method == "getagentlang")
                    fields.emplace_back("Language",
                                        prefs && prefs->language_public ? prefs->language : "en-us");
                else if (prefs) {
                    char buffer[64];
                    const auto hover = std::to_chars(buffer, buffer + sizeof buffer, prefs->hover);
                    fields = {{"PrincipalID", agent.str()},
                              {"AccessPrefs", prefs->access},
                              {"HoverHeight", std::string(buffer, hover.ptr)},
                              {"Language", prefs->language},
                              {"LanguageIsPublic", prefs->language_public ? "True" : "False"},
                              {"PermEveryone", std::to_string(prefs->everyone)},
                              {"PermGroup", std::to_string(prefs->group)},
                              {"PermNextOwner", std::to_string(prefs->next_owner)}};
                }
                response.body = network::service_xml(network::ServiceXmlValue::object(std::move(fields)));
            }
        } catch (const std::exception& e) {
            error_log(std::string("Agent preferences endpoint: ") + e.what());
        }
        return response;
    };
    const auto is_bakes_request = [](const network::HttpRequest& request) {
        return request.path == "/bakes" || request.path.starts_with("/bakes/");
    };
    const int mute_list_port = expose_mute_list ? connector_port(mute_list_connector) : options.port;
    const int presence_port = expose_presence ? connector_port(presence_connector) : options.port;
    const int grid_port = expose_grid ? connector_port(grid_connector) : options.port;
    const int grid_user_port = expose_grid_user ? connector_port(grid_user_connector) : options.port;
    const int asset_port = expose_assets ? connector_port(asset_connector) : options.port;
    const int hg_asset_port = expose_hg_assets ? connector_port(hg_asset_connector) : options.port;
    if (presence_endpoint && presence_port == options.port)
        app.set_presence_endpoint(presence_endpoint.get());
    if (grid_endpoint && grid_port == options.port)
        app.set_grid_endpoint(grid_endpoint.get());
    if (grid_user_endpoint && grid_user_port == options.port)
        app.set_grid_user_endpoint(grid_user_endpoint.get());
    app.set_grid_statistics(grid.get(), grid_user_store.get());
    const auto service_on_port = [&](int port, const network::HttpRequest& request)
        -> std::optional<network::HttpResponse> {
        if ((!home_agent_on_main && port == home_agent_port) ||
            (home_agent_on_main && port == options.port)) {
            if (request.path.starts_with("/homeagent/"))
                return foreign_request(request, true);
            if (!home_agent_on_main)
                if (auto response = app.handle_home_agent(request))
                    return response;
        }
        if (grid_info_port != options.port && grid_info_port == port)
            if (auto discovery = app.handle_grid_discovery(request, true))
                return discovery;
        if (asset_endpoint && asset_port == port && server::AssetEndpoint::matches(request))
            return asset_endpoint->handle(request);
        if (hg_asset_endpoint && hg_asset_port == port && server::AssetEndpoint::matches(request))
            return hg_asset_endpoint->handle(request);
        if (mute_list_service && expose_mute_list && mute_list_port == port && request.path == "/mutelist")
            return mute_list_service->handle(request);
        if (presence_endpoint && presence_port != options.port && presence_port == port &&
            request.path == "/presence")
            return presence_endpoint->handle(request);
        if (grid_endpoint && grid_port != options.port && grid_port == port && request.path == "/grid")
            return grid_endpoint->handle(request);
        if (grid_user_endpoint && grid_user_port != options.port && grid_user_port == port &&
            request.path == "/griduser")
            return grid_user_endpoint->handle(request);
        return std::nullopt;
    };
    const bool snapshot_enabled = region_runtime && config.boolean("DataSnapshot", "index_sims", false);
    const auto snapshot_period = config.integer("DataSnapshot", "default_snapshot_period", 20);
    const auto snapshot_max_changes = config.integer("DataSnapshot", "max_changes_before_update", 500);
    auto snapshot_directory = framework::utf8_path(config.get("DataSnapshot", "snapshot_cache_directory",
                                                              "DataSnapshot"));
    if (snapshot_directory.is_relative())
        snapshot_directory = config.directory / snapshot_directory;
    const auto snapshot_grid = config.get("DataSnapshot", "gridname", "the lost continent of hippo");
    const bool snapshot_all_parcels = framework::ordinal_equals_ignore_case(
        config.get("DataSnapshot", "data_exposure", "minimum"), "all");
    const auto snapshot_gatekeeper = config.get("GridService", "Gatekeeper");
    const auto snapshot_secret = snapshot_enabled ? framework::Uuid::random().str() : std::string{};
    const auto snapshot_services = [&] {
        std::vector<std::string> configured;
        if (!snapshot_enabled)
            return configured;
        const auto list = config.get("DataSnapshot", "data_services", "noservices");
        if (list != "noservices") {
            std::string_view remaining = list;
            while (!remaining.empty()) {
                const auto separator = remaining.find(';');
                auto url = framework::trim_utf8(remaining.substr(0, separator));
                if (!url.empty())
                    configured.push_back(std::move(url));
                if (separator == std::string_view::npos)
                    break;
                remaining.remove_prefix(separator + 1);
            }
        }
        std::vector<std::string> extra;
        for (const auto& [key, value] : config.ordered_section("DataSnapshot")) {
            if (!key.starts_with("DATA_SRV_"))
                continue;
            auto url = framework::trim_utf8(value);
            if (!url.empty() && std::ranges::find(configured, url) == configured.end())
                extra.push_back(std::move(url));
        }
        extra.insert(extra.end(), configured.begin(), configured.end());
        return extra;
    }();
    const auto notify_snapshot_services = [&](std::string_view status, int port) {
        if (snapshot_services.empty())
            return;
        const auto host = region_runtime->descriptor().external_host.value_or("127.0.0.1");
        const auto query = network::build_service_form({{"service", std::string(status)}, {"host", host},
                                                        {"port", std::to_string(port)}, {"secret", snapshot_secret}});
        const auto uris = framework::native_config_uri_object_backend();
        auto client = outgoing_clients()->get_new_global_client(5000);
        for (auto url : snapshot_services) {
            if (const auto fragment = url.find('#'); fragment != std::string::npos)
                url.resize(fragment);
            if (!url.ends_with('?') && !url.ends_with('&'))
                url += url.find('?') == std::string::npos ? '?' : '&';
            try {
                auto uri = uris->create(framework::nini_writer_text(framework::utf16_replacing(url + query)),
                                        framework::ConfigUriObjectKind::Absolute);
                auto response = client->send_headers(std::move(uri), {}, true);
                response->close();
            } catch (const std::exception&) {
                error_log("DataSnapshot could not notify a configured collector");
            }
        }
    };
    std::mutex snapshot_requests_mutex;
    std::map<std::pair<std::string, std::string>, std::chrono::steady_clock::time_point> snapshot_requests;
    std::timed_mutex snapshot_cache_mutex;
    std::string cached_region_snapshot;
    std::chrono::steady_clock::time_point snapshot_updated;
    std::uint64_t snapshot_revision = 0;
    const auto collect_snapshot = [&](const network::HttpRequest& request) -> std::optional<network::HttpResponse> {
        const auto query = request.raw_target.find('?');
        if (!snapshot_enabled || request.method != "GET" || query == std::string::npos)
            return std::nullopt;
        const auto fields = network::parse_service_form(std::string_view(request.raw_target).substr(query + 1));
        const auto field = [&](const char* name) {
            const auto found = fields.find(name);
            const auto* value = found == fields.end() ? nullptr : std::get_if<std::string>(&found->second);
            return value ? *value : std::string{};
        };
        const auto method = field("method");
        if (method == "validate") {
            network::HttpResponse response;
            response.content_type = "text/xml";
            response.status = field("secret") == snapshot_secret ? 200 : 403;
            return response;
        }
        if (method != "collector")
            return std::nullopt;
        const auto region = field("region");
        const auto throttle_region = framework::trim_utf8(region).empty() ? std::string{} : region;
        network::HttpResponse response;
        {
            std::lock_guard lock(snapshot_requests_mutex);
            const auto now = std::chrono::steady_clock::now();
            std::erase_if(snapshot_requests, [&](const auto& entry) { return entry.second <= now; });
            if (!snapshot_requests.try_emplace({throttle_region, request.remote_ip},
                                               now + std::chrono::seconds(60)).second) {
                response.status = 503;
                response.content_type = "text/plain";
                return response;
            }
        }
        std::unique_lock cache_lock(snapshot_cache_mutex, std::defer_lock);
        if (!cache_lock.try_lock_for(std::chrono::seconds(30))) {
            response.status = 503;
            response.content_type = "text/plain";
            return response;
        }
        response.content_type = "text/xml";
        const auto local_name = region_runtime->descriptor().name.value_or("");
        if (!region.empty() && region != local_name) {
            response.body = region_runtime->data_snapshot(region, snapshot_period, snapshot_grid,
                                                           snapshot_gatekeeper, snapshot_all_parcels);
            return response;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto revision = region_runtime->data_snapshot_revision();
        const auto elapsed = now - snapshot_updated;
        const bool many_changes = snapshot_max_changes <= 0 ||
                                  revision - snapshot_revision >= static_cast<std::uint64_t>(snapshot_max_changes);
        const bool refresh = many_changes ? elapsed >= std::chrono::seconds(20)
                                          : elapsed > std::chrono::seconds(snapshot_period);
        if (cached_region_snapshot.empty() || refresh) {
            cached_region_snapshot = region_runtime->data_snapshot(local_name, snapshot_period, snapshot_grid,
                                                                    snapshot_gatekeeper, snapshot_all_parcels,
                                                                    snapshot_directory);
            snapshot_updated = now;
            snapshot_revision = revision;
        } else {
            response.body = region_runtime->read_data_snapshot_cache(snapshot_directory);
        }
        if (response.body.empty())
            response.body = cached_region_snapshot;
        if (region.empty() && !local_name.empty()) {
            constexpr std::string_view opening = "<regiondata>";
            const auto start = response.body.find(opening);
            if (start != std::string::npos)
                response.body.insert(start + opening.size(), "<expire>" + std::to_string(snapshot_period) + "</expire>");
        }
        return response;
    };
    if (local_region) {
        if (config.boolean("Modules", "LandServiceInConnector", false)) {
            app.set_land_data_lookup([&local_regions](const network::Value& request) {
                std::uint64_t handle;
                std::uint32_t x, y;
                if (!framework::try_parse_unsigned_integer(network::osd_string(request.get("region_handle")),
                                                           handle) ||
                    !framework::try_parse_unsigned_integer(network::osd_string(request.get("x")), x) ||
                    !framework::try_parse_unsigned_integer(network::osd_string(request.get("y")), y))
                    throw std::invalid_argument("Invalid land_data coordinates");
                return local_regions.land_data(handle, x, y);
            });
        }
        if (message_transfer_enabled) {
            auto key = config.get("Messaging", "MessageKey");
            app.set_grid_message_handler([&local_regions, key = std::move(key)](const network::Value& message) {
                const auto incoming = parse_grid_message(message, key);
                return incoming && local_regions.deliver_grid_message(*incoming);
            });
        }
    }

    auto application_handler = [&](const network::HttpRequest& request) {
        if (remote_data && (remote_data_port == options.port || remote_data_port == ports.main_http))
            if (auto response = remote_data_rpc(request, *remote_data))
                return *response;
        if (sample_economy_enabled && (request.path == "/currency.php" || request.path == "/landtool.php"))
            return sample_economy_rpc(request);
        if (auto response = collect_snapshot(request))
            return *response;
        if (auto response = service_on_port(options.port, request))
            return *response;
        if (preferences_on_main && request.path == "/agentprefs")
            return preferences_handler(request);
        if (bakes_on_main && is_bakes_request(request))
            return bakes_handler(request);
        if (region_runtime && config.boolean("NativeServer", "EnableSimulationEndpoint", false) &&
            request.path.starts_with("/object/")) {
            network::HttpResponse response;
            server::ObjectHandler(&local_regions).process_request(request, response);
            return response;
        }
        if (region_runtime && request.method == "DELETE" && request.path.starts_with("/agent/")) {
            server::AgentPathParameters path;
            const bool parsed = server::agent_path_parameters(request.path, path);
            auto destination = parsed ? local_regions.find(path.region) : nullptr;
            if (parsed && path.action == "release") {
                const auto auth = server::agent_delete_auth(request.raw_target);
                framework::Uuid token;
                const bool accepted = destination && auth &&
                                      framework::try_parse_sdk_uuid(*auth, token) &&
                                      destination->confirm_teleport_arrival(path.agent, token);
                network::HttpResponse response;
                response.status = accepted ? 200 : 404;
                response.body = accepted ? "OpenSim agent " + path.agent.str() : "false";
                return response;
            }
            if (path.action.empty() && !path.agent.is_zero() &&
                destination &&
                config.boolean("NativeServer", "EnableSimulationEndpoint", false)) {
                const auto auth = server::agent_delete_auth(request.raw_target);
                if (auth)
                    destination->close_agent_session(path.agent, *auth);
                network::HttpResponse response;
                response.status = 200;
                response.body = "OpenSim agent " + path.agent.str();
                return response;
            }
        }
        if (region_runtime && config.boolean("NativeServer", "EnableSimulationEndpoint", false) &&
            (request.method == "QUERYACCESS" || request.method == "POST" || request.method == "PUT") &&
            request.path.starts_with("/agent/")) {
            network::HttpResponse response;
            if (request.method == "PUT") {
                server::AgentPathParameters path;
                if (!server::agent_path_parameters(request.path, path) ||
                    (!path.region.is_zero() && !local_regions.find(path.region))) {
                    response.status = 404;
                    response.body = "false";
                    return response;
                }
            }
            server::AgentUpdateHandler updates(&local_regions);
            server::AgentAccessHandler access(&local_regions, nullptr, &updates);
            access.set_create_agent_handler([&](const services::GridRegion* source,
                                                services::GridRegion& destination,
                                                const std::shared_ptr<framework::AgentCircuitData>& circuit,
                                                std::uint32_t flags, services::LoginTransferContext& context,
                                                std::string& reason) {
                if (remote_region_presence) {
                    const auto session = remote_region_presence->agent(circuit->session_id);
                    if (session && session->user_id == circuit->agent_id.str())
                        return local_regions.create_agent(source, destination, circuit, flags, context,
                                                            reason);
                    reason = "Invalid agent session";
                    return false;
                }
                const auto session = presence_store ? presence_store->get(circuit->session_id) : nullptr;
                if (session && session->user_id == circuit->agent_id.str()) {
                    const auto secure = session->data.find("SecureSessionID");
                    if (secure != session->data.end() && secure->second == circuit->secure_session_id.str())
                        return local_regions.create_agent(source, destination, circuit, flags, context,
                                                            reason);
                }
                reason = "Invalid agent session";
                return false;
            });
            access.process_admission(request, response);
            return response;
        }
        if (gatekeeper_port == options.port)
            if (auto response = foreign_request(request, home_agent_on_main))
                return *response;
        if (helo_on_main && (request.path == "/helo" || request.path == "/helo/"))
            return helo_handler(request);
        if (remote_on_main && remote_console_routes)
            if (auto response = remote_console_routes->handle(request))
                return *response;
        return app.handle(request, gatekeeper_port == options.port, grid_info_port == options.port,
                          home_agent_on_main);
    };
    std::unique_ptr<network::HttpServer> http;
    std::unique_ptr<network::HttpServer> offline_http;
    std::unique_ptr<network::HttpServer> helo_http;
    std::unique_ptr<network::HttpServer> gatekeeper_http;
    const bool helo_on_offline =
        helo_enabled && separate_offline_listener && helo_port == *offline_connector_port;
    const bool gatekeeper_separate = gatekeeper_enabled && gatekeeper_port != options.port;
    const bool gatekeeper_on_offline =
        gatekeeper_separate && separate_offline_listener && gatekeeper_port == *offline_connector_port;
    const bool gatekeeper_on_helo = gatekeeper_separate && !gatekeeper_on_offline && helo_enabled &&
                                    !helo_on_main && helo_port == gatekeeper_port;
    const bool bakes_on_offline =
        bakes_enabled && separate_offline_listener && bakes_port == *offline_connector_port;
    const bool bakes_on_helo =
        bakes_enabled && helo_enabled && !helo_on_main && !helo_on_offline && bakes_port == helo_port;
    const bool bakes_on_gatekeeper = bakes_enabled && gatekeeper_separate && !gatekeeper_on_offline &&
                                     !gatekeeper_on_helo && bakes_port == gatekeeper_port;
    const bool preferences_on_offline = preferences_endpoint_enabled && separate_offline_listener &&
                                        preferences_port == *offline_connector_port;
    const bool preferences_on_helo = preferences_endpoint_enabled && helo_enabled && !helo_on_main &&
                                     !helo_on_offline && preferences_port == helo_port;
    const bool preferences_on_gatekeeper = preferences_endpoint_enabled && gatekeeper_separate &&
                                           !gatekeeper_on_offline && !gatekeeper_on_helo &&
                                           preferences_port == gatekeeper_port;
    if (separate_offline_listener) {
        auto offline_options = options;
        offline_options.port = *offline_connector_port;
        // Robust creates additional service ports as plain HTTP listeners.
        offline_options.certificate.clear();
        offline_options.private_key.clear();
        offline_http =
            std::make_unique<network::HttpServer>(offline_options, [&](const network::HttpRequest& request) {
                if (auto response = service_on_port(*offline_connector_port, request))
                    return *response;
                if (preferences_on_offline && request.path == "/agentprefs")
                    return preferences_handler(request);
                if (bakes_on_offline && is_bakes_request(request))
                    return bakes_handler(request);
                if (request.path == "/offlineim")
                    return offline_im_endpoint->handle(request);
                if (helo_on_offline && (request.path == "/helo" || request.path == "/helo/"))
                    return helo_handler(request);
                if (gatekeeper_on_offline)
                    return gatekeeper_request(request);
                network::HttpResponse response;
                response.status = 404;
                return response;
            });
        try {
            offline_http->start();
        } catch (const std::exception& error) {
            throw std::runtime_error("Cannot start OfflineIM listener on port " +
                                     std::to_string(*offline_connector_port) + ": " + error.what());
        }
    }
    if (helo_enabled && !helo_on_main && !helo_on_offline) {
        auto helo_options = options;
        helo_options.port = helo_port;
        helo_options.certificate.clear();
        helo_options.private_key.clear();
        helo_http =
            std::make_unique<network::HttpServer>(helo_options, [&](const network::HttpRequest& request) {
                if (auto response = service_on_port(helo_port, request))
                    return *response;
                if (preferences_on_helo && request.path == "/agentprefs")
                    return preferences_handler(request);
                if (bakes_on_helo && is_bakes_request(request))
                    return bakes_handler(request);
                if (request.path == "/helo" || request.path == "/helo/")
                    return helo_handler(request);
                if (gatekeeper_on_helo)
                    return gatekeeper_request(request);
                network::HttpResponse response;
                response.status = 404;
                return response;
            });
        helo_http->start();
    }
    if (gatekeeper_separate && !gatekeeper_on_offline && !gatekeeper_on_helo) {
        auto gatekeeper_options = options;
        gatekeeper_options.port = gatekeeper_port;
        gatekeeper_options.certificate.clear();
        gatekeeper_options.private_key.clear();
        gatekeeper_http = std::make_unique<network::HttpServer>(
            gatekeeper_options, [&](const network::HttpRequest& request) {
                if (auto response = service_on_port(gatekeeper_port, request))
                    return *response;
                if (preferences_on_gatekeeper && request.path == "/agentprefs")
                    return preferences_handler(request);
                if (bakes_on_gatekeeper && is_bakes_request(request))
                    return bakes_handler(request);
                return gatekeeper_request(request);
            });
        gatekeeper_http->start();
    }
    std::unique_ptr<network::HttpServer> bakes_http;
    const bool standalone_bakes =
        bakes_enabled && !bakes_on_main && !bakes_on_offline && !bakes_on_helo && !bakes_on_gatekeeper;
    const bool preferences_on_bakes =
        preferences_endpoint_enabled && standalone_bakes && preferences_port == bakes_port;
    if (standalone_bakes) {
        auto bakes_options = options;
        bakes_options.port = bakes_port;
        bakes_options.certificate.clear();
        bakes_options.private_key.clear();
        bakes_http =
            std::make_unique<network::HttpServer>(bakes_options, [&](const network::HttpRequest& request) {
                if (auto response = service_on_port(bakes_port, request))
                    return *response;
                if (preferences_on_bakes && request.path == "/agentprefs")
                    return preferences_handler(request);
                if (is_bakes_request(request))
                    return bakes_handler(request);
                network::HttpResponse response;
                response.status = 404;
                return response;
            });
        bakes_http->start();
    }
    std::unique_ptr<network::HttpServer> preferences_http;
    const bool standalone_preferences =
        preferences_endpoint_enabled && !preferences_on_main && !preferences_on_offline &&
        !preferences_on_helo && !preferences_on_gatekeeper && !preferences_on_bakes;
    if (standalone_preferences) {
        auto preferences_options = options;
        preferences_options.port = preferences_port;
        preferences_options.certificate.clear();
        preferences_options.private_key.clear();
        preferences_http = std::make_unique<network::HttpServer>(
            preferences_options, [&](const network::HttpRequest& request) {
                if (auto response = service_on_port(preferences_port, request))
                    return *response;
                if (request.path == "/agentprefs")
                    return preferences_handler(request);
                network::HttpResponse response;
                response.status = 404;
                return response;
            });
        preferences_http->start();
    }
    std::vector<std::unique_ptr<network::HttpServer>> private_service_listeners;
    const std::set<int> service_ports{mute_list_port, presence_port, grid_port, grid_user_port, asset_port,
                                      hg_asset_port, grid_info_port, home_agent_port};
    for (const auto port : service_ports) {
        if (port == options.port ||
            (offline_http && port == *offline_connector_port) ||
            (helo_http && port == helo_port) ||
            (gatekeeper_http && port == gatekeeper_port) ||
            (bakes_http && port == bakes_port) ||
            (preferences_http && port == preferences_port))
            continue;
        auto service_options = options;
        service_options.port = port;
        service_options.certificate.clear();
        service_options.private_key.clear();
        auto listener = std::make_unique<network::HttpServer>(
            service_options, [&, port](const network::HttpRequest& request) {
                if (auto response = service_on_port(port, request))
                    return *response;
                network::HttpResponse response;
                response.status = 404;
                return response;
            });
        listener->start();
        private_service_listeners.push_back(std::move(listener));
    }
    if (local_region && config.boolean("NativeServer", "EnableSimulationEndpoint", false))
        options.custom_methods.push_back("QUERYACCESS");
    std::unique_ptr<network::AsyncHttpServer> region_http;
    std::shared_ptr<server::RegistryCapabilityListener> script_https_listener;
    std::shared_ptr<server::RegistryCapabilityListener> script_http_listener;
    std::unique_ptr<network::AsyncHttpServer> script_https;
    std::unique_ptr<network::AsyncHttpServer> script_http;
    std::optional<network::HttpServerOptions> script_https_options;
    std::optional<network::HttpServerOptions> script_http_options;
    const bool primary_tls = local_region && config.boolean("Network", "http_listener_ssl", false);
    const auto primary_tls_name = config.get("Network", "http_listener_cn", "localhost");
    const auto use_certificate_bundle = [&](network::HttpServerOptions& target, const std::string& bundle,
                                            const std::string& password_key) {
        auto path = framework::utf8_path(bundle);
        if (path.is_relative())
            path = config.directory / path;
        const auto utf8 = path.u8string();
        target.pkcs12_bundle.assign(utf8.begin(), utf8.end());
        target.pkcs12_password = config.get("Network", password_key);
    };
    if (local_region && config.boolean("Network", "https_listener", false)) {
        const bool external_tls = config.boolean("Network", "https_external", false);
        script_https_options = options;
        script_https_options->port = config.integer("Network", "https_port", 0);
        const auto bundle = config.get("Network", "cert_path");
        if (external_tls) {
            script_https_options->certificate.clear();
            script_https_options->private_key.clear();
        } else if (!bundle.empty()) {
            use_certificate_bundle(*script_https_options, bundle, "cert_pass");
        } else if (options.certificate.empty() || options.private_key.empty()) {
            throw std::invalid_argument(
                "Network https_listener requires cert_path or NativeServer TlsCertificate and TlsPrivateKey");
        }
        if (!primary_tls) {
            options.certificate.clear();
            options.private_key.clear();
        }
    }
    if (primary_tls) {
        script_http_options = options;
        script_http_options->certificate.clear();
        script_http_options->private_key.clear();
        options.port = config.integer("Network", "http_listener_sslport", 9001);
        const auto bundle = config.get("Network", "http_listener_cert_path");
        if (!bundle.empty())
            use_certificate_bundle(options, bundle, "http_listener_cert_pass");
        else if (options.certificate.empty() || options.private_key.empty())
            throw std::invalid_argument(
                "Network http_listener_ssl requires http_listener_cert_path or NativeServer TLS settings");
    }
    ScriptQuiesceGuard script_quiesce{region_runtime.get()};
    auto bind_options = options;
    if (local_region && bind_options.port == 0)
        bind_options.port = ports.main_http;
    if (local_region || remote_console_mode)
        region_http = std::make_unique<network::AsyncHttpServer>(bind_options, [&](const auto& request,
                                                                              auto channel) {
            capability_polls.dispatch(*capability_listener, request, std::move(channel), application_handler);
        });
    else
        http = std::make_unique<network::HttpServer>(options, application_handler);
    if (primary_tls) {
        if (!region_http->certificate_matches_host(primary_tls_name))
            throw std::invalid_argument("invalid main http server CN (ExternalHostName)");
        const auto external = region_runtime->descriptor().external_host.value_or("127.0.0.1");
        if (!region_http->certificate_matches_host(external))
            throw std::invalid_argument("main http cert CN doesn't match region External IP");
    }
    const std::string scheme = options.certificate.empty() && options.pkcs12_bundle.empty() ? "http" : "https";
    std::signal(SIGINT, shutdown_signal);
    std::signal(SIGTERM, shutdown_signal);
#ifdef SIGBREAK
    std::signal(SIGBREAK, shutdown_signal);
#endif
    const auto register_local_region = [&](const services::GridRegion& region,
                                          const services::GridRegion* previous) {
        auto error = grid->register_region(region.scope, region);
        if (!error.empty())
            throw std::runtime_error("Local region registration failed: " + error);
        if (remote_grid_request) {
            try {
                services::OrderedRegionFields fields{{"SCOPEID", region.scope.str()},
                                                     {"VERSIONMIN", "1"},
                                                     {"VERSIONMAX", "1"},
                                                     {"METHOD", "register"}};
                const auto descriptor = region.fields();
                fields.insert(fields.end(), descriptor.begin(), descriptor.end());
                const auto reply = remote_grid_request(fields);
                std::string message = "Invalid response";
                bool registered = false;
                for (const auto& [name, value] : reply.fields) {
                    if (name == "Message")
                        message = value.text;
                    if (name == "Result") {
                        auto result = value.text;
                        for (auto& c : result)
                            if (c >= 'A' && c <= 'Z')
                                c += 'a' - 'A';
                        registered = !value.list && result == "success";
                    }
                }
                if (!registered)
                    throw std::runtime_error(message);
            } catch (const std::exception& failure) {
                if (previous) {
                    const auto rollback = grid->register_region(previous->scope, *previous);
                    if (!rollback.empty())
                        error_log("Could not restore the previous local region registration: " + rollback);
                } else {
                    grid->deregister_region(region.id);
                }
                throw std::runtime_error(std::string("Remote region registration failed: ") +
                                         failure.what());
            }
        }
    };
    const auto retire_map_tile = [&](framework::Uuid previous, framework::Uuid replacement) {
        if (previous.is_zero() || previous == replacement || !region_assets)
            return;
        try {
            const auto asset = region_assets->get(previous);
            if (asset && (asset->flags() & framework::Maptile) != 0)
                region_assets->asset_service()->erase(previous.str());
        } catch (const std::exception& failure) {
            error_log("Could not delete replaced terrain map tile: " + std::string(failure.what()));
        }
    };
    auto on_bound = [&](int actual_port) {
        ports.main_http = actual_port;
        app.set_default_login_uri(
            scheme + "://" +
            (options.host.find(':') != std::string::npos ? "[" + options.host + "]" : options.host) + ":" +
            std::to_string(actual_port) + "/");
        if (local_region) {
            auto region = region_runtime->descriptor();
            region.http_port = actual_port;
            auto advertised = region.external_host.value_or("127.0.0.1");
            region.set_server_uri(
                scheme + "://" +
                (advertised.find(':') != std::string::npos ? "[" + advertised + "]" : advertised) + ":" +
                std::to_string(actual_port) + "/");
            if (!simulator_features->extra_contains("currency-base-uri")) {
                auto economy_uri = framework::trim_utf8(config.get("Economy", "economy", config.get("GridInfo", "economy")));
                if (economy_uri.empty() && sample_economy_enabled)
                    economy_uri = region.server_uri();
                if (!economy_uri.empty())
                    simulator_features->add_extra("currency-base-uri",
                        services::SimulatorFeatureValue::from_value(network::Value(std::move(economy_uri))));
            }
            capability_listener = std::make_shared<server::RegistryCapabilityListener>(
                server::CapabilityListenerInfo{scheme == "https", static_cast<std::uint32_t>(actual_port),
                    static_cast<std::uint32_t>(actual_port),
                    primary_tls ? primary_tls_name : advertised});
            const auto start_script_listener = [&](auto listener_options, auto& listener, bool secure,
                                                   int& previous_port) {
                if (listener_options.port == 0)
                    listener_options.port = previous_port;
                auto server = std::make_unique<network::AsyncHttpServer>(
                    listener_options, [&capability_polls, &listener](const auto& request, auto channel) {
                        capability_polls.dispatch(*listener, request, std::move(channel),
                            [](const auto&) {
                                network::HttpResponse reply;
                                reply.status = 404;
                                return reply;
                            });
                    });
                server->start([&](int port) {
                    previous_port = port;
                    const auto bound_port = static_cast<std::uint32_t>(port);
                    listener = std::make_shared<server::RegistryCapabilityListener>(
                        server::CapabilityListenerInfo{secure, bound_port, bound_port, advertised});
                });
                return server;
            };
            if (script_https_options)
                script_https = start_script_listener(*script_https_options, script_https_listener, true,
                                                     ports.script_https);
            if (script_http_options)
                script_http = start_script_listener(*script_http_options, script_http_listener, false,
                                                    ports.script_http);
            if (local_profiles_connector)
                local_profiles_connector->region_loaded(local_profiles_scene);
            if (remote_region_presence)
                remote_region_presence->logout_region(region_runtime->descriptor().id);
            auto udp = region_runtime->start(capability_listener, script_https_listener, script_http_listener);
            ports.udp = udp.port;
            region.internal_endpoint = services::RegionEndpoint{udp.address, udp.port};
            if (inventory)
                region_runtime->capabilities()->shared_events()->on_register_caps.add(
                    {{inventory.get(), "CreateInventoryCategory.RegisterCaps"},
                     [service = inventory.get(), runtime = std::weak_ptr(region_runtime)](auto agent, auto caps) {
                         caps->register_simple("CreateInventoryCategory",
                             std::make_shared<CreateInventoryCategory>(agent, *service, runtime));
                     }});
            region_runtime->capabilities()->shared_events()->on_register_caps.add(
                {{simulator_features.get(), "SimulatorFeatures.RegisterCaps"},
                 [features = std::weak_ptr(simulator_features)](auto agent, auto caps) {
                     if (auto module = features.lock())
                         module->register_caps(agent, std::move(caps));
                 }});
            if (config.boolean("NativeServer", "EnableRegionAssets", false))
                region_assets = std::make_unique<services::RegionAssetRuntime>(
                    config, region_runtime->capabilities()->shared_events(), region.name,
                    std::function<void(std::string)>{}, region.id, asset_log, outgoing_clients(),
                    [&, scope = region.scope](framework::Uuid user) -> std::optional<std::string> {
                        if (accounts.find_by_id(scope.str(), user.str()))
                            return {};
                        auto urls = region_runtime->profile_service_urls(user);
                        if (!urls)
                            return {};
                        auto entry = urls->find("AssetServerURI");
                        if (entry == urls->end())
                            return {};
                        auto value = std::any_cast<std::string>(&entry->second);
                        if (!value)
                            return {};
                        framework::HttpHost host(*value);
                        return host.valid() ? std::optional<std::string>(host.uri) : std::nullopt;
                    },
                    [runtime = std::weak_ptr(region_runtime), asset_log](scene::UuidGatherer& gatherer) {
                        const auto live = runtime.lock();
                        if (!live)
                            throw std::runtime_error("Region stopped during Flotsam asset gathering");
                        live->gather_region_asset_references(gatherer);
                        for (const auto& object : live->snapshot_asset_objects()) {
                            gatherer.add_for_inspection(object, [&](const auto& message) {
                                asset_log("warn", message);
                            });
                            gatherer.gather_all();
                        }
                    });
            if (region_assets) {
                region_bakes = std::make_unique<services::RegionBakedTextureRuntime>(
                    region_runtime->capabilities()->shared_events(), region_assets->asset_cache(),
                    config.get("ClientStack.LindenCaps", "Cap_UploadBakedTexture"),
                    [&](const auto& text) { error_log("[UPLOAD BAKED TEXTURE HANDLER]: Error: " + text); });
                services::RegionScriptTaskBackend scripts;
                scripts.object_exists = [&](auto id) {
                    return region_runtime->world().object(id).has_value();
                };
                scripts.can_edit_inventory = [&](auto object, auto agent) {
                    return region_runtime->can_edit_task_inventory(object, agent);
                };
                scripts.can_edit_script = [&](auto item, auto object, auto agent) {
                    return region_runtime->can_edit_task_script(item, object, agent);
                };
                scripts.update = [&](auto agent, auto item, auto object, bool running, auto bytes) {
                    return region_runtime->update_task_script(agent, item, object, running, bytes);
                };
                scripts.log = error_log;
                region_scripts = std::make_unique<services::RegionScriptTaskRuntime>(
                    region_runtime->capabilities()->shared_events(), std::move(scripts));
                services::RegionInventoryAssetBackend uploads;
                uploads.object_exists = [&](auto id) {
                    return region_runtime->world().object(id).has_value();
                };
                uploads.can_edit_inventory = [&](auto object, auto agent) {
                    return region_runtime->can_edit_task_inventory(object, agent);
                };
                uploads.update = [&](auto agent, auto item, auto object, auto bytes) {
                    return region_runtime->update_inventory_asset(agent, item, object, bytes);
                };
                uploads.log = normal_log;
                uploads.region_name = region.name.value_or("");
                region_inventory_uploads = std::make_unique<services::RegionInventoryAssetRuntime>(
                    region_runtime->capabilities()->shared_events(), std::move(uploads));
            }
            if (!config.get("ClientStack.LindenCaps", "Cap_FetchInventory2").empty()) {
                if (!inventory)
                    throw std::runtime_error("Region inventory capabilities require native InventoryService");
                std::optional<services::LibraryItemBackend> library_backend;
                if (library_service) {
                    services::LibraryItemBackend backend;
                    if (auto root = library_service->root())
                        backend.root_owner = root->owner;
                    backend.get_items =
                        [library = library_service](
                            const auto& ids) -> std::optional<services::FetchInventory2Core::Items> {
                        auto items = library->multiple_items(ids);
                        if (!items)
                            return {};
                        services::FetchInventory2Core::Items result;
                        for (auto& item : *items)
                            result.push_back(item ? std::optional(*item) : std::nullopt);
                        return result;
                    };
                    library_backend = std::move(backend);
                }
                region_inventory = std::make_unique<services::RegionInventoryRuntime>(
                    config, region_runtime->capabilities()->shared_events(),
                    [&](auto owner, const auto& ids) -> std::optional<services::FetchInventory2Core::Items> {
                        return inventory->multiple_items(owner, ids);
                    },
                    std::move(library_backend));
            }
            if (!config.get("ClientStack.LindenCaps", "Cap_FetchInventoryDescendents2").empty()) {
                if (!inventory)
                    throw std::runtime_error("Region folder capabilities require native InventoryService");
                services::InventoryDescendentsBackend backend;
                backend.multiple_content = [&](auto owner, const auto& ids)
                    -> std::optional<services::InventoryDescendentsBackend::Collections> {
                    services::InventoryDescendentsBackend::Collections result;
                    for (auto& collection : inventory->multiple_content(owner, ids))
                        result.push_back(std::move(collection));
                    return result;
                };
                backend.folder = [&](auto owner, auto id) { return inventory->folder(owner, id); };
                backend.multiple_items =
                    [&](auto owner, const auto& ids) -> std::optional<services::FetchInventory2Core::Items> {
                    return inventory->multiple_items(owner, ids);
                };
                backend.log = descendents_log;
                backend.diagnostic = inventory_diagnostic;
                if (library_service)
                    if (auto root = library_service->root()) {
                        backend.library_owner = root->owner;
                        backend.library_content = [root](auto id) {
                            return library_folder_content(root, id);
                        };
                    }
                region_folders = std::make_unique<services::RegionInventoryDescendentsRuntime>(
                    config, region_runtime->capabilities()->shared_events(), std::move(backend));
            }
            if (!config.get("ClientStack.LindenCaps", "Cap_FetchLibDescendents2").empty()) {
                if (!library_service)
                    throw std::runtime_error("Library folder capabilities require native LibraryService");
                services::LibraryDescendentsBackend backend;
                backend.root_owner = [library = library_service]() -> std::optional<framework::Uuid> {
                    auto root = library->root();
                    return root ? std::optional(root->owner) : std::nullopt;
                };
                backend.folder_content = [library = library_service](auto id) {
                    return library_folder_content(library->root(), id);
                };
                backend.log = descendents_log;
                backend.diagnostic = inventory_diagnostic;
                region_library_folders = std::make_unique<services::RegionLibraryDescendentsRuntime>(
                    config, region_runtime->capabilities()->shared_events(), std::move(backend));
            }
            const auto previous_terrain = region.terrain_image;
            const auto previous_parcel = region.parcel_image;
            region_runtime->generate_startup_map_tiles();
            const auto map_images = region_runtime->descriptor();
            region.terrain_image = map_images.terrain_image;
            region.parcel_image = map_images.parcel_image;
            register_local_region(region, nullptr);
            retire_map_tile(previous_terrain, region.terrain_image);
            retire_map_tile(previous_parcel, region.parcel_image);
            publish_map_images = [&, runtime = region_runtime.get(), published = region]() mutable {
                auto current = runtime->descriptor();
                current.http_port = published.http_port;
                current.set_server_uri(published.server_uri());
                register_local_region(current, &published);
                retire_map_tile(published.terrain_image, current.terrain_image);
                retire_map_tile(published.parcel_image, current.parcel_image);
                published = std::move(current);
            };
            local_regions.add(region_runtime);
            capability_polls.start();
            std::cout << "REGION_UDP " << udp.address << ':' << udp.port << '\n' << std::flush;
        }
        using ConsoleCallback =
            std::function<void(const std::optional<std::string>&, std::vector<std::string>&)>;
        auto quit_command =
            std::make_shared<ConsoleCallback>([&](const auto&, auto&) { console_shutdown = true; });
        auto help_command = std::make_shared<ConsoleCallback>([&](const auto&, auto& args) {
            for (const auto& line : console_commands.help(args))
                console_output(line.value_or(""));
        });
        auto login_command = std::make_shared<ConsoleCallback>([&](const auto&, auto& args) {
            if (const auto output = login.console_command(args))
                console_output(*output);
        });
        auto cache_command = std::make_shared<ConsoleCallback>([&](const auto&, auto& args) {
            try {
                if (!region_assets ||
                    !region_assets->cache_console(args, console_output))
                    console_output("Flotsam cache command unavailable");
            } catch (const std::exception& e) {
                log_message(framework::console_log_error,
                            std::string("Flotsam cache command failed: ") + e.what());
            }
        });
        console_commands.add({"Help", "help", "help [<item>]",
                              "Display help on a particular command or on a list of commands in a category",
                              "", false},
                             help_command);
        for (const auto* name : {"quit", "shutdown"})
            console_commands.add({"General", name, name, "Quit the application", "", false}, quit_command);
        console_commands.add({"Users", "login level", "login level <level>",
                              "Set the minimum user level to log in", "", false},
                             login_command);
        console_commands.add(
            {"Users", "login reset", "login reset", "Reset the login level to allow all users", "", false},
            login_command);
        console_commands.add(
            {"Users", "login text", "login text <text>", "Set the text users will see on login", "", false},
            login_command);
        services::register_fs_asset_console(filesystem_assets, console_commands, console_output);
        if (!filesystem_assets && region_assets)
            region_assets->register_filesystem_console(console_commands, console_output);
        if (asset_endpoint)
            asset_endpoint->register_console_commands(console_commands, console_output);
        if (hg_asset_endpoint)
            hg_asset_endpoint->register_console_commands(console_commands, console_output);
        if (bakes_enabled) {
            auto delete_bakes = std::make_shared<ConsoleCallback>([&](const auto&, auto& args) {
                framework::Uuid id;
                if (args.size() < 3 || !framework::try_parse_sdk_uuid(args[2], id)) {
                    console_output("Syntax: delete bakes <ID>");
                    return;
                }
                std::error_code error;
                const bool removed = std::filesystem::remove(bake_file(id), error);
                if (error)
                    console_output("Could not delete bakes: " + error.message());
                else
                    console_output(removed ? "Bakes deleted" : "Bakes not found");
            });
            console_commands.add({"fs", "delete bakes", "delete bakes <ID>",
                                  "Delete agent's baked textures from server", "", false},
                                 delete_bakes);
        }
        if (advanced_profiles) {
            auto profile_status = std::make_shared<ConsoleCallback>([&](const auto&, auto&) {
                const auto name = region_runtime->descriptor().name.value_or("");
                console_output("Profile requests in '" + name +
                               "' Local: " + std::to_string(advanced_profiles->queue_count(false)) +
                               " HG: " + std::to_string(advanced_profiles->queue_count(true)));
            });
            console_commands.add(
                {"Debug", "profiles status", "profiles status", "Show user profile Queues count", "", false},
                profile_status);
        }
        if (region_assets)
            for (auto command : region_assets->cache_command_descriptions())
                console_commands.add(std::move(command), cache_command);
        if (region_runtime) {
            if (region_runtime->world_map_enabled()) {
                auto generate_map = std::make_shared<ConsoleCallback>([&](const auto&, auto&) {
                    try {
                        region_runtime->generate_map_from_console();
                    } catch (const std::exception& failure) {
                        log_message(framework::console_log_error, failure.what());
                    }
                });
                console_commands.add({"Regions", "generate map", "generate map",
                                      "Generates and stores a new maptile.", "", false}, generate_map);
                auto export_map = std::make_shared<ConsoleCallback>([&](const auto&, auto& args) {
                    const auto path = args.size() > 1 ? args[1] : "exportmap.jpg";
                    try {
                        region_runtime->export_world_map(framework::utf8_path(path));
                        console_output("Successfully exported world map to " + path);
                    } catch (const std::exception& failure) {
                        log_message(framework::console_log_error,
                                    std::string("Failed to export world map: ") + failure.what());
                    }
                });
                console_commands.add({"Regions", "export-map", "export-map [<path>]",
                                      "Save an image of the world map", "", false}, export_map);
            }
            auto restart_command = std::make_shared<ConsoleCallback>([&](const auto&, auto& args) {
                if (args.size() >= 3 && args[2] == "abort" && args.size() < 5) {
                    region_runtime->abort_restart(args.size() == 4 ? args[3] : std::string{});
                    console_output("Region restart aborted");
                    return;
                }
                constexpr auto usage = "Error: restart region <mode> <name> <delta seconds>+";
                if (args.size() < 5 || (args[2] != "notice" && args[2] != "bluebox")) {
                    console_output(usage);
                    return;
                }
                std::vector<int> alerts;
                std::int64_t total = 0;
                for (std::size_t index = 4; index < args.size(); ++index) {
                    std::int32_t seconds;
                    if (!services::parse_account_integer(args[index], seconds) || seconds < 0) {
                        console_output(usage);
                        return;
                    }
                    alerts.push_back(seconds);
                    total += seconds;
                }
                console_output("Region " + region_runtime->descriptor().name.value_or("") +
                               " scheduled for restart in " + std::to_string(total) + " seconds");
                region_runtime->schedule_restart(std::move(alerts), args[3], args[2] == "notice");
            });
            console_commands.add({"Regions", "region restart bluebox",
                                  "region restart bluebox <message> <delta seconds>+",
                                  "Schedule a restart with dismissable notices", "", false}, restart_command);
            console_commands.add({"Regions", "region restart notice",
                                  "region restart notice <message> <delta seconds>+",
                                  "Schedule a restart with transient notices", "", false}, restart_command);
            console_commands.add({"Regions", "region restart abort", "region restart abort [<message>]",
                                  "Abort a region restart", "", false}, restart_command);
            if (config.native_section("Wind") && config.boolean("Wind", "enabled", true)) {
                const auto callback = std::make_shared<ConsoleCallback>([&](const auto&, auto& args) {
                    const auto output = region_runtime->wind_console_command(args);
                    if (!output.empty())
                        console_output(output);
                });
                constexpr std::pair<std::string_view, std::string_view> wind_commands[]{
                    {"wind base wind_update_rate", "Get or set the wind update rate."},
                    {"wind SimpleRandomWind strength", "wind strength"},
                    {"wind ConfigurableWind avgStrength", "average wind strength"},
                    {"wind ConfigurableWind avgDirection", "average wind direction in degrees"},
                    {"wind ConfigurableWind varStrength", "allowable variance in wind strength"},
                    {"wind ConfigurableWind varDirection", "allowable variance in wind direction in +/- degrees"},
                    {"wind ConfigurableWind rateChange", "rate of change"},
                };
                for (const auto& [name, description] : wind_commands) {
                    const auto command = std::string(name);
                    console_commands.add({"Regions", command, command + " [<value>]",
                                          std::string(description), "", false}, callback);
                }
            }
            auto terrain_command = std::make_shared<ConsoleCallback>([&](const auto&, auto& args) {
                if (args.at(1) == "bake")
                    region_runtime->bake_terrain();
                else
                    region_runtime->revert_terrain();
            });
            console_commands.add({"Debug", "debug scene set", "debug scene set scripting <true|false>",
                                  "Enable or disable script execution in the region", "", false},
                                 std::make_shared<ConsoleCallback>([&](const auto&, auto& args) {
                                     if (args.size() != 5 || args[3] != "scripting" ||
                                         (args[4] != "true" && args[4] != "false")) {
                                         console_output("debug scene set scripting <true|false>");
                                         return;
                                     }
                                     region_runtime->set_scripts_enabled(args[4] == "true");
                                     console_output(args[4] == "true" ? "Scene scripting enabled"
                                                                      : "Scene scripting disabled");
                                 }));
            console_commands.add({"Terrain", "terrain bake", "terrain bake",
                                  "Saves the current terrain into the region's baked map", "", false},
                                 terrain_command);
            console_commands.add({"Terrain", "terrain revert", "terrain revert",
                                  "Loads the baked map into the region's heightmap", "", false},
                                 terrain_command);
            auto flip_terrain = std::make_shared<ConsoleCallback>([&](const auto&, auto& args) {
                if (args.size() < 3 || args[2].empty()) {
                    console_output("terrain flip <x|y>");
                    return;
                }
                const char axis = args[2].front();
                if (axis == 'x' || axis == 'X')
                    region_runtime->flip_terrain(true);
                else if (axis == 'y' || axis == 'Y')
                    region_runtime->flip_terrain(false);
            });
            console_commands.add({"Terrain", "terrain flip", "terrain flip <x|y>",
                                  "Flips the terrain along the selected axis", "", false},
                                 flip_terrain);
            const auto add_terrain_adjustment = [&](std::string name, std::string description,
                                                    float (*adjust)(float, float)) {
                const auto usage = name + " <height>";
                auto command = std::make_shared<ConsoleCallback>([&, usage, adjust](const auto&, auto& args) {
                    if (args.size() != 3) {
                        console_output(usage);
                        return;
                    }
                    float value;
                    if (!framework::parse_invariant_float(args[2], value)) {
                        console_output(usage);
                        return;
                    }
                    region_runtime->adjust_terrain([&](float height) { return adjust(height, value); });
                });
                console_commands.add({"Terrain", std::move(name), usage, std::move(description), "", false},
                                     command);
            };
            add_terrain_adjustment("terrain fill", "Fills the heightmap with the specified height",
                                   [](float, float v) { return v; });
            add_terrain_adjustment("terrain elevate", "Raises the heightmap",
                                   [](float h, float v) { return h + v; });
            add_terrain_adjustment("terrain lower", "Lowers the heightmap",
                                   [](float h, float v) { return h - v; });
            add_terrain_adjustment("terrain multiply", "Multiplies terrain heights",
                                   [](float h, float v) { return h * v; });
            add_terrain_adjustment("terrain min", "Sets the minimum terrain height",
                                   [](float h, float v) { return std::isnan(h) ? h : std::max(v, h); });
            add_terrain_adjustment("terrain max", "Sets the maximum terrain height",
                                   [](float h, float v) { return std::isnan(h) ? h : std::min(v, h); });
            auto rescale_terrain = std::make_shared<ConsoleCallback>([&](const auto&, auto& args) {
                float minimum, maximum;
                if (args.size() != 4 || !framework::parse_invariant_float(args[2], minimum) ||
                    !framework::parse_invariant_float(args[3], maximum)) {
                    console_output("terrain rescale <min> <max>");
                    return;
                }
                region_runtime->rescale_terrain(minimum, maximum);
            });
            console_commands.add({"Terrain", "terrain rescale", "terrain rescale <min> <max>",
                                  "Rescales terrain heights into the specified range", "", false},
                                 rescale_terrain);
            const auto format_height = [](auto value) {
                char buffer[64];
                const auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
                return std::string(buffer, result.ptr);
            };
            auto show_terrain =
                std::make_shared<ConsoleCallback>([&, format_height](const auto&, auto& args) {
                    if (args.size() != 3) {
                        console_output("terrain show <x,y>");
                        return;
                    }
                    const std::string_view point = args[2];
                    const auto comma = point.find(',');
                    const auto parse_coordinate = [](std::string_view text, float& value) {
                        if (text == "~")
                            value = std::numeric_limits<float>::max();
                        else if (text == "-~")
                            value = std::numeric_limits<float>::lowest();
                        else
                            return framework::parse_invariant_float(text, value);
                        return true;
                    };
                    float x, y;
                    if (comma == point.npos || !parse_coordinate(point.substr(0, comma), x) ||
                        !parse_coordinate(point.substr(comma + 1), y)) {
                        console_output("ERROR: " + args[2] + " is not a valid vector");
                        return;
                    }
                    const auto& terrain = region_runtime->world().terrain();
                    double height = 0;
                    if (x > -1.f && x < terrain.width() && y > -1.f && y < terrain.height())
                        height = terrain.get(static_cast<int>(x), static_cast<int>(y));
                    console_output("Terrain height at <" + format_height(x) + ", " + format_height(y) +
                                   "> is " + format_height(height));
                });
            console_commands.add({"Terrain", "terrain show", "terrain show <x,y>",
                                  "Shows terrain height at a point", "", false},
                                 show_terrain);
            auto terrain_stats = std::make_shared<ConsoleCallback>([&, format_height](const auto&, auto&) {
                const auto& terrain = region_runtime->world().terrain();
                const auto samples = terrain.compressed_map();
                float low = std::numeric_limits<float>::max(), high = std::numeric_limits<float>::lowest();
                double sum = 0;
                for (float sample : samples) {
                    sum += sample;
                    low = std::min(low, sample);
                    high = std::max(high, sample);
                }
                console_output("Channel " + std::to_string(terrain.width()) + "x" +
                               std::to_string(terrain.height()));
                console_output("max/min/avg/sum: " + format_height(high) + "/" + format_height(low) + "/" +
                               format_height(sum / samples.size()) + "/" + format_height(sum));
            });
            console_commands.add(
                {"Terrain", "terrain stats", "terrain stats", "Shows terrain height statistics", "", false},
                terrain_stats);
            auto save_terrain_file = std::make_shared<ConsoleCallback>([&](const auto&, auto& args) {
                if (args.size() != 3) {
                    console_output("terrain save <filename>");
                    return;
                }
                const auto& filename = args[2];
                const bool llraw = filename.ends_with(".raw");
                const bool terragen = filename.ends_with(".ter");
                const bool bitmap = filename.ends_with(".bmp");
                const bool png = filename.ends_with(".png");
                if (!llraw && !terragen && !bitmap && !png && !filename.ends_with(".r32") &&
                    !filename.ends_with(".f32")) {
                    console_output("Unsupported terrain export format");
                    return;
                }
                const scene::Terrain snapshot(region_runtime->world().terrain());
                const auto samples = snapshot.serialized_floats();
                // The source MaxHeight returns the truncated peak plus one.
                const float limit = bitmap || png ? 255.f : 4095.f;
                if (std::any_of(samples.begin(), samples.end(), [limit](float h) { return h >= limit; })) {
                    console_output("Terrain exceeds the export format's height limit");
                    return;
                }
                try {
                    const auto raw_bytes = llraw ? snapshot.llraw() : std::vector<std::uint8_t>{};
                    const auto image_bytes = terragen ? snapshot.terragen()
                                             : bitmap ? snapshot.bitmap()
                                             : png    ? snapshot.png()
                                                      : std::string{};
                    std::ofstream output(framework::utf8_path(filename), std::ios::binary | std::ios::trunc);
                    output.exceptions(std::ios::badbit | std::ios::failbit);
                    if (llraw) {
                        output.write(reinterpret_cast<const char*>(raw_bytes.data()), raw_bytes.size());
                    } else if (terragen || bitmap || png) {
                        output.write(image_bytes.data(), image_bytes.size());
                    } else {
                        for (float sample : samples) {
                            const auto bits = std::bit_cast<std::uint32_t>(sample);
                            const char bytes[]{static_cast<char>(bits), static_cast<char>(bits >> 8),
                                               static_cast<char>(bits >> 16), static_cast<char>(bits >> 24)};
                            output.write(bytes, sizeof bytes);
                        }
                    }
                    output.close();
                    console_output("Saved terrain to " + filename);
                } catch (const std::exception& error) {
                    console_output("Unable to save terrain to " + filename + ": " + error.what());
                }
            });
            console_commands.add({"Terrain", "terrain save", "terrain save <filename>",
                                  "Exports the active heightmap", "", false},
                                 save_terrain_file);
            auto load_terrain_file = std::make_shared<ConsoleCallback>([&](const auto&, auto& args) {
                if (args.size() != 3) {
                    console_output("terrain load <filename>");
                    return;
                }
                try {
                    std::ifstream input(framework::utf8_path(args[2]), std::ios::binary);
                    if (!input)
                        throw std::ios_base::failure("Cannot open terrain file " + args[2]);
                    input.exceptions(std::ios::badbit);
                    const std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(input)), {});
                    region_runtime->load_terrain_file(args[2], data);
                    console_output("Loaded terrain from " + args[2]);
                } catch (const std::exception& error) {
                    console_output(std::string("Unable to load terrain: ") + error.what());
                }
            });
            console_commands.add({"Terrain", "terrain load", "terrain load <filename>",
                                  "Imports and bakes a region-sized heightmap", "", false},
                                 load_terrain_file);
        }
        if (remote_console_mode) {
            const auto configured_console_port = config.integer("Network", "ConsolePort", 0);
            if (configured_console_port < 0 || configured_console_port > 65535)
                throw std::invalid_argument("ConsolePort out of range");
            remote_on_main = configured_console_port == 0 || configured_console_port == actual_port;
            if (!capability_listener)
                capability_listener = std::make_shared<server::RegistryCapabilityListener>(
                    server::CapabilityListenerInfo{scheme == "https",
                                                   static_cast<std::uint32_t>(actual_port),
                                                   static_cast<std::uint32_t>(actual_port),
                                                   {}});
            console_listener =
                remote_on_main
                    ? capability_listener
                    : std::make_shared<server::RegistryCapabilityListener>(server::CapabilityListenerInfo{
                          false, static_cast<std::uint32_t>(configured_console_port), 0, {}});
            *console_listener_binding = console_listener;
            remote_console_routes = std::make_unique<server::RemoteConsoleHttpRoutes>(*remote_console);
            if (!local_region)
                capability_polls.start();
            if (!remote_on_main) {
                auto console_options = options;
                console_options.port = configured_console_port;
                console_options.certificate.clear();
                console_options.private_key.clear();
                console_options.pkcs12_bundle.clear();
                console_options.pkcs12_password.clear();
                console_http = std::make_unique<network::AsyncHttpServer>(
                    console_options, [&](const auto& request, auto channel) {
                        capability_polls.dispatch(
                            *console_listener, request, std::move(channel), [&](const auto& incoming) {
                                if (auto response = remote_console_routes->handle(incoming))
                                    return *response;
                                network::HttpResponse response;
                                response.status = 404;
                                return response;
                            });
                    });
                console_http->start();
            }
        }
    };
    const auto bound = region_http ? region_http->start(on_bound) : http->start(on_bound);
    std::unique_ptr<network::HttpServer> remote_data_http;
    if (remote_data && remote_data_port != options.port && remote_data_port != bound) {
        network::HttpServerOptions remote_data_options;
        remote_data_options.port = remote_data_port;
        remote_data_http = std::make_unique<network::HttpServer>(remote_data_options, [remote_data](const auto& request) {
            if (auto response = remote_data_rpc(request, *remote_data))
                return *response;
            network::HttpResponse response;
            response.status = 404;
            return response;
        });
        remote_data_http->start();
    }
    std::cout << "LISTENING " << scheme << "://"
              << (options.host.find(':') != std::string::npos ? "[" + options.host + "]" : options.host)
              << ':' << bound << "/\n"
              << std::flush;
    notify_snapshot_services("online", bound);
    std::cout << (local_region
                      ? "Experimental local region enabled; full scene features remain incomplete.\n"
                      : "Authentication endpoints are running; simulator sessions are not available yet.\n")
              << std::flush;
    if (presence_endpoint)
        std::cout << "Presence endpoint enabled on port " << (presence_port == options.port ? bound : presence_port)
                  << ".\n" << std::flush;
    if (grid_endpoint)
        std::cout << "Grid endpoint enabled with " << (durable_regions ? "SQLite" : "volatile Null")
                  << " region storage.\n"
                  << std::flush;
    RemoteConsoleReader remote_reader;
    remote_reader.console = remote_console.get();
    if (remote_console)
        remote_reader.worker = std::thread([&] {
            while (!console_shutdown && !remote_reader.stopping) {
                try {
                    if (!remote_console->read_line(u"Region# ", true, true).empty())
                        console_output("Invalid command");
                } catch (const std::exception& error) {
                    console_output(error.what());
                }
            }
        });
    std::string pending;
    double host_sleep_ms = 0.;
    while (!shutdown_requested && !console_shutdown &&
           (region_http ? region_http->is_running() : http->is_running()) &&
           (!script_https || script_https->is_running()) && (!script_http || script_http->is_running())) {
        if (console && !remote_console_mode && console_quit(pending, [&](const std::string& command) {
                auto args = framework::parse_console_command_utf8(command);
                try {
                    console_commands.invoke(args);
                } catch (const std::invalid_argument&) {
                    return false;
                }
                return console_shutdown.load();
            }))
            break;
        // CLR can collect the provider's unreachable, unclosed readers after a
        // request returns. Retire their native handles on the host collection tick.
        if (profiles_storage)
            profiles_storage->database()->collect_readers();
        if (region_runtime)
            region_runtime->poll(host_sleep_ms);
        const bool region_save_enabled =
            region_persistence.save_terrain || region_persistence.save_prim_metadata;
        if (region_save_enabled &&
            std::chrono::steady_clock::now() >= region_persistence.next_terrain_save) {
            region_persistence.next_terrain_save = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            try {
                if (region_persistence.save_terrain)
                    region_persistence.save_terrain();
            } catch (const std::exception& error) {
                error_log(error.what());
            }
            try {
                if (region_persistence.save_prim_metadata)
                    region_persistence.save_prim_metadata();
            } catch (const std::exception& error) {
                error_log(error.what());
            }
        }
        if (region_inventory)
            region_inventory->poll();
        const auto sleep_start = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(region_poll_interval);
        host_sleep_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - sleep_start).count();
    }
    console_shutdown = true;
    remote_reader.stop();
    notify_snapshot_services("offline", bound);
    if (local_profiles_connector) {
        local_profiles_connector->remove_region(local_profiles_scene);
        local_profiles_connector->close();
    }
    if (region_runtime)
        region_runtime->quiesce_script_updates();
    if (bakes_workers)
        bakes_workers->stop();
    if (asset_transfer_workers)
        asset_transfer_workers->stop();
    if (region_lookup_workers)
        region_lookup_workers->stop();
    if (bakes_upload_worker)
        bakes_upload_worker->stop();
    if (console_http)
        console_http->stop();
    if (offline_http)
        offline_http->stop();
    if (bakes_http)
        bakes_http->stop();
    if (preferences_http)
        preferences_http->stop();
    for (auto& listener : private_service_listeners)
        listener->stop();
    if (helo_http)
        helo_http->stop();
    if (gatekeeper_http)
        gatekeeper_http->stop();
    capability_polls.stop();
    if (script_https)
        script_https->stop();
    if (script_http)
        script_http->stop();
    if (region_http)
        region_http->stop();
    if (http)
        http->stop();
    if (region_library_folders)
        region_library_folders->stop();
    if (region_folders)
        region_folders->stop();
    if (region_inventory)
        region_inventory->stop();
    if (region_scripts)
        region_scripts->stop();
    if (region_inventory_uploads)
        region_inventory_uploads->stop();
    if (region_bakes)
        region_bakes->stop();
    if (profile_workers)
        profile_workers->stop();
    if (estate_workers)
        estate_workers->stop();
    if (region_assets)
        region_assets->quiesce_cache_cleanup();
    if (region_runtime) {
        const auto id = region_runtime->descriptor().id;
        local_regions.remove(id);
        region_runtime->stop(region_persistence.save_prim_metadata);
        if (region_persistence.save_terrain)
            region_persistence.save_terrain();
        if (grid->deregister_region(id) && remote_grid_request) {
            try {
                const auto reply = remote_grid_request({{"REGIONID", id.str()}, {"METHOD", "deregister"}});
                const auto succeeded =
                    std::any_of(reply.fields.begin(), reply.fields.end(), [](const auto& field) {
                        auto result = field.second.text;
                        for (auto& c : result)
                            if (c >= 'A' && c <= 'Z')
                                c += 'a' - 'A';
                        return field.first == "Result" && !field.second.list && result == "success";
                    });
                if (!succeeded)
                    error_log("[GRID CONNECTOR]: Remote region deregistration failed");
            } catch (const std::exception& error) {
                error_log(std::string("[GRID CONNECTOR]: Remote region deregistration failed: ") +
                          error.what());
            }
        }
    }
    if (region_assets)
        region_assets->stop();
    std::cout << "STOPPED\n" << std::flush;
    return restart_region ? restart_region_result : 0;
}

int run_with_region_restart(const std::vector<std::string>& args) {
    RegionListenerPorts ports;
    while (!shutdown_requested) {
        const int result = run(args, ports);
        if (result != restart_region_result)
            return result;
    }
    return 0;
}
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        const auto size = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string text(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, text.data(), size, nullptr, nullptr);
        text.pop_back();
        args.push_back(std::move(text));
    }
    try {
        return run_with_region_restart(args);
    } catch (const opensim::server::ConfigurationStartupExit& error) {
        return error.code;
    } catch (const std::exception& error) {
        std::cerr << "OpenSimNative: " << error.what() << '\n';
        return 1;
    }
}
#else
int main(int argc, char** argv) {
    try {
        return run_with_region_restart({argv, argv + argc});
    } catch (const opensim::server::ConfigurationStartupExit& error) {
        return error.code;
    } catch (const std::exception& error) {
        std::cerr << "OpenSimNative: " << error.what() << '\n';
        return 1;
    }
}
#endif
