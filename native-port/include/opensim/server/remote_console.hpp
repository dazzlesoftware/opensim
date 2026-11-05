// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/console_command_resolver.hpp"
#include "opensim/framework/uuid.hpp"
#include "opensim/framework/console_prompts.hpp"
#include "opensim/server/poll_types.hpp"
#include "opensim/server/config.hpp"
#include "opensim/network/http_server.hpp"
#include <map>
namespace opensim::server {
struct RemoteConsoleReply {
    std::int32_t status = 200;
    std::u16string content_type, body;
    std::optional<bool> keep_alive;
    std::optional<std::u16string> allowed_origin;
};
struct RemoteConsoleOptions {
    std::shared_ptr<framework::ConsoleCommandResolver> commands;
    std::u16string prompt, user, password, allowed_origin;
    std::function<std::int32_t()> tick_count;
    std::function<framework::Uuid()> random_uuid;
    std::function<void(std::shared_ptr<services::CapabilityPollHandler>)> add_poll;
    std::function<void(const std::string&, const std::string&)> remove_poll;
    std::function<void(const std::u16string&)> on_output, debug;
    bool expiration_timer = true;
};
struct RemoteConsoleState {
    std::int64_t line_number = 0;
    std::size_t scrollback_count = 0, input_count = 0, connection_count = 0;
    bool expecting_input = false, expecting_command = true;
    std::u16string last_prompt;
};
// Original RemoteConsole state and handlers. The host supplies the actual poll
// registration/removal operations; no successful session is fabricated without them.
class RemoteConsole {
    struct Impl;
    std::shared_ptr<Impl> impl_;

public:
    explicit RemoteConsole(RemoteConsoleOptions);
    ~RemoteConsole();
    RemoteConsole(const RemoteConsole&) = delete;
    RemoteConsole& operator=(const RemoteConsole&) = delete;
    // The host registers generic HTTP routes, as IHttpServer.AddHTTPHandler does.
    using HttpRegistrar = std::function<void(const std::string&, network::HttpServer::Handler)>;
    void set_server(const HttpRegistrar&);
    void read_config(const IniConfig&);
    static std::map<std::u16string, std::u16string> decode_post(std::u16string_view);
    RemoteConsoleReply start_session(std::u16string_view), close_session(std::u16string_view),
        session_command(std::u16string_view);
    void output(std::optional<std::u16string> text, std::optional<std::u16string> level = {},
                bool prompt = false, bool command = false, bool input = false);
    std::u16string read_line(std::u16string prompt, bool command, bool echo);
    framework::ConsolePrompts prompts(framework::ConsolePrompts::Diagnostic diagnostic = {}) const;
    bool has_events(framework::Uuid);
    RemoteConsoleReply get_events(framework::Uuid), no_events();
    void expire();
    void close_connection(framework::Uuid);
    void stop();
    RemoteConsoleState state() const;
};
// Fixed RemoteConsole generic-HTTP registrations, using the original path-prefix
// selection. Poll routes remain owned by the host's RegistryCapabilityListener.
class RemoteConsoleHttpRoutes {
    std::vector<std::pair<std::string, network::HttpServer::Handler>> routes_;

public:
    explicit RemoteConsoleHttpRoutes(RemoteConsole&);
    std::optional<network::HttpResponse> handle(const network::HttpRequest&) const;
};

}
