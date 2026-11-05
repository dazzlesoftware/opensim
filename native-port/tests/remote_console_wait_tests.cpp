// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/remote_console.hpp"
#include "opensim/framework/text.hpp"
#include <future>
#include <atomic>
#include <stdexcept>
#include <iostream>
#include <cstdlib>
using namespace opensim;
using namespace framework;
using namespace server;
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::shared_ptr<ConsoleCommandResolver> commands() {
    auto value = std::make_shared<ConsoleCommandResolver>();
    auto callback =
        std::make_shared<ConsoleCommandResolver::Callback::element_type>([](const auto&, auto&) {});
    value->add({"Help", "help", "help", "Help", "", false}, callback);
    value->add({"General", "quit", "quit", "Quit", "", false}, callback);
    return value;
}
int main() {
    try {
        RemoteConsoleOptions options;
        options.commands = commands();
        options.user = u"user";
        options.password = u"pass";
        options.prompt = u"Region";
        options.expiration_timer = false;
        options.debug = [](const auto&) {};
        Uuid session;
        options.add_poll = [&](auto handler) { session = handler->id; };
        options.remove_poll = [](const auto&, const auto&) {};
        std::promise<void> prompt;
        auto prompted = prompt.get_future();
        options.on_output = [&](const auto& text) {
            if (text == u"NewPrompt")
                prompt.set_value();
        };
        RemoteConsole console(options);
        std::future<std::u16string> reader;
        struct Stop {
            RemoteConsole& console;
            ~Stop() {
                console.stop();
            }
        } stop{console};
        check(console.start_session(u"USER=user&PASS=pass").status == 200, "start read session");
        reader =
            std::async(std::launch::async, [&] { return console.read_line(u"NewPrompt", false, false); });
        check(prompted.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
              "blocked reader publishes prompt");
        auto initial = console.get_events(session);
        check(initial.body.find(u"Prompt=\"true\" Command=\"true\" Input=\"false\"") != std::u16string::npos,
              "initial non-command prompt retains source expecting-command state");
        check(console.session_command(u"ID=" + utf16(session.str()) + u"&COMMAND=hello").status == 200,
              "post wakes read");
        check(reader.wait_for(std::chrono::seconds(5)) == std::future_status::ready &&
                  reader.get() == u"hello",
              "blocked read wakes and returns raw input");
        check(console.state().last_prompt == u"Region" && !console.state().expecting_input &&
                  !console.state().expecting_command,
              "non-command prompt does not replace default prompt");
        auto echo = console.get_events(session);
        check(echo.body.find(u"Input=\"true\">hello</Line>") != std::u16string::npos,
              "echo=false still produces source input echo");
        console.session_command(u"ID=" + utf16(session.str()) + u"&COMMAND=help");
        console.session_command(u"ID=" + utf16(session.str()) + u"&COMMAND=raw+input");
        check(console.read_line(u"Command>", true, false).empty() && console.state().input_count == 1,
              "command dispatch consumes only first queued input");
        check(console.read_line(u"Again>", false, true) == u"raw input" &&
                  console.state().last_prompt == u"Command>",
              "remaining buffered input does not lose its signal");
        std::vector<std::u16string> diagnostics;
        auto interactive = console.prompts([&](const auto& text) { diagnostics.push_back(text); });
        console.session_command(u"ID=" + utf16(session.str()) + u"&COMMAND=bad%21");
        console.session_command(u"ID=" + utf16(session.str()) + u"&COMMAND=");
        auto password = interactive.prompt_default_excluding(u"Password", u"default!",
                                                             std::vector<char16_t>{u'!'}, false);
        check(password == u"default!" &&
                  diagnostics == std::vector<std::u16string>{u"The character \"!\" is not permitted."},
              "remote inherited prompt accepts default without rechecking exclusions");
        check(console.state().last_prompt == u"Command>" && console.state().input_count == 0,
              "inherited prompts preserve command prompt and consume shared queue");
        console.session_command(u"ID=" + utf16(session.str()) + u"&COMMAND=YES");
        console.session_command(u"ID=" + utf16(session.str()) + u"&COMMAND=yes");
        check(interactive.prompt_options(u"Proceed", u"no",
                                         std::vector<ConsolePrompts::Text>{u"yes", u"no"}) == u"yes" &&
                  diagnostics.back() == u"Valid options are yes no",
              "remote option prompts preserve ordinal validation and diagnostics");
        // Session setup exports the command tree while command callbacks emit output.
        // Exercise that lock ordering with actual concurrent service operations.
        RemoteConsoleOptions parallel;
        parallel.commands = commands();
        parallel.user = u"user";
        parallel.password = u"pass";
        parallel.expiration_timer = false;
        parallel.debug = [](const auto&) {};
        RemoteConsole* active = nullptr;
        auto emit = std::make_shared<ConsoleCommandResolver::Callback::element_type>(
            [&](const auto&, auto&) { active->output(u"work"); });
        parallel.commands->add({"Test", "emit", "emit", "Emit", "", false}, emit);
        std::atomic<int> registrations = 0;
        Uuid first_session;
        parallel.add_poll = [&](auto handler) {
            if (registrations.fetch_add(1) == 0)
                first_session = handler->id;
        };
        parallel.remove_poll = [](const auto&, const auto&) {};
        RemoteConsole concurrent(parallel);
        active = &concurrent;
        concurrent.start_session(u"USER=user&PASS=pass");
        for (int i = 0; i < 100; ++i)
            concurrent.session_command(u"ID=" + utf16(first_session.str()) + u"&COMMAND=emit");
        auto consumer = std::async(std::launch::async, [&] {
            for (int i = 0; i < 100; ++i)
                check(concurrent.read_line(u"C>", true, false).empty(), "concurrent command resolves");
        });
        std::vector<std::future<void>> clients;
        for (int i = 0; i < 4; ++i)
            clients.push_back(std::async(std::launch::async, [&] {
                for (int j = 0; j < 20; ++j)
                    check(concurrent.start_session(u"USER=user&PASS=pass").status == 200,
                          "concurrent session setup");
            }));
        auto complete = [](auto& future) {
            if (future.wait_for(std::chrono::seconds(10)) != std::future_status::ready) {
                std::cerr << "Concurrent remote console deadlock\n";
                std::_Exit(1);
            }
            future.get();
        };
        complete(consumer);
        for (auto& client : clients)
            complete(client);
        check(concurrent.state().connection_count == 81 && concurrent.state().line_number == 300 &&
                  concurrent.state().input_count == 0,
              "concurrent sessions retain all command output and input");
        std::promise<void> expired;
        auto expiration = expired.get_future();
        std::atomic<std::int32_t> ticks = 0;
        RemoteConsoleOptions timed;
        timed.commands = commands();
        timed.user = u"user";
        timed.password = u"pass";
        timed.tick_count = [&] { return ticks.load(); };
        timed.debug = [](const auto&) {};
        timed.add_poll = [](auto) {};
        timed.remove_poll = [&](const auto& method, const auto& path) {
            if (method.empty() && path.ends_with('/'))
                expired.set_value();
        };
        RemoteConsole automatic(timed);
        automatic.start_session(u"USER=user&PASS=pass");
        ticks = 500001;
        check(expiration.wait_for(std::chrono::seconds(12)) == std::future_status::ready &&
                  automatic.state().connection_count == 0,
              "default five-second timer expires idle session");
        automatic.stop();
        std::cout
            << "PASS: remote console blocking wake, queued input, prompt flags, concurrent sessions and expiration timer\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
