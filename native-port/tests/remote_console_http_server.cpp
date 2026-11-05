// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/remote_console.hpp"
#include "opensim/server/poll_http_service.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/console_appender.hpp"
#include "opensim/services/inventory_descendents.hpp"
#include "opensim/services/library_descendents.hpp"
#include <iostream>
#include <thread>
#include <atomic>
using namespace opensim;
using namespace server;
using namespace network;
using namespace framework;
int main() {
    try {
        auto registry = std::make_shared<RegistryCapabilityListener>(CapabilityListenerInfo{});
        PollHttpService polls(2);
        auto commands = std::make_shared<ConsoleCommandResolver>();
        RemoteConsole* active = nullptr;
        std::atomic<bool> done = false;
        auto callback =
            std::make_shared<ConsoleCommandResolver::Callback::element_type>([&](const auto&, auto& args) {
                active->output(utf16(args.size() > 1 ? args[1] : "empty"), u"info");
            });
        auto noop =
            std::make_shared<ConsoleCommandResolver::Callback::element_type>([](const auto&, auto&) {});
        for (const auto* name : {"help", "quit"})
            commands->add({"General", name, name, name, "", false}, noop);
        commands->add({"Test", "echo", "echo <text>", "Echo text", "", false}, callback);
        RemoteConsoleOptions options;
        options.commands = commands;
        options.prompt = u"Region";
        options.user = u"user";
        options.password = u"pass";
        options.allowed_origin = u"https://console.example";
        options.debug = [](const auto&) {};
        options.add_poll = [&](auto handler) { registry->add_poll(handler); };
        options.remove_poll = [&](const auto& method, const auto& path) {
            registry->routes.remove_poll(method, path);
        };
        RemoteConsole console(options);
        active = &console;
        RemoteConsoleHttpRoutes routes(console);
        ConsoleAppender logs;
        logs.console(std::make_shared<ConsoleAppender::Output>(
            [&](const auto& text, const auto& level) { console.output(text, level); }));
        services::InventoryDescendentsBackend inventory_backend;
        inventory_backend.multiple_content =
            [](auto, const auto& ids) -> std::optional<services::InventoryDescendentsBackend::Collections> {
            return services::InventoryDescendentsBackend::Collections(ids.size(), std::nullopt);
        };
        inventory_backend.folder = [](auto, auto) -> std::optional<services::InventoryFolder> { return {}; };
        inventory_backend.multiple_items =
            [](auto, const auto&) -> std::optional<services::FetchInventory2Core::Items> {
            return services::FetchInventory2Core::Items{};
        };
        auto diagnostic = [&](services::InventoryLogLevel level, const auto& message) {
            logs.append(level == services::InventoryLogLevel::Error  ? console_log_error
                        : level == services::InventoryLogLevel::Warn ? console_log_warn
                                                                     : console_log_debug,
                        utf16(message));
        };
        inventory_backend.diagnostic = diagnostic;
        services::FetchInventoryDescendentsCore inventory(inventory_backend);
        services::InventoryBadRequests inventory_bad([] { return 0; });
        services::LibraryDescendentsBackend library_backend;
        library_backend.root_owner = []() -> std::optional<Uuid> {
            return Uuid("00000000-0000-0000-0000-000000000200");
        };
        library_backend.folder_content = [](auto) -> std::optional<services::InventoryCollection> {
            return {};
        };
        library_backend.diagnostic = diagnostic;
        services::FetchLibraryDescendentsCore library(library_backend);
        services::InventoryBadRequests library_bad([] { return 0; });
        HttpServerOptions http_options;
        http_options.port = 0;
        AsyncHttpServer http(http_options, [&](const HttpRequest& request, auto channel) {
            polls.dispatch(*registry, request, channel, [&](const HttpRequest& incoming) {
                if (incoming.path == "/FixtureInventory")
                    return inventory.handle(incoming, &inventory_bad);
                if (incoming.path == "/FixtureLibrary")
                    return library.handle(incoming, &library_bad, Uuid{});
                if (incoming.path == "/FixtureLog") {
                    logs.append(incoming.body == "error"   ? console_log_error
                                : incoming.body == "warn"  ? console_log_warn
                                : incoming.body == "fatal" ? 110000
                                                           : console_log_debug,
                                utf16("LOG {0}: " + incoming.body + "\n"));
                    return HttpResponse{};
                }
                if (auto response = routes.handle(incoming))
                    return *response;
                HttpResponse response;
                response.status = 404;
                return response;
            });
        });
        polls.start();
        int port = http.start();
        std::thread reader([&] {
            while (!done)
                console.read_line(u"Region# ", true, false);
        });
        std::cout << "LISTENING " << port << std::endl;
        std::string line;
        std::getline(std::cin, line);
        done = true;
        console.stop();
        reader.join();
        polls.stop();
        http.stop();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
