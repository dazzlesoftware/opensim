// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/remote_console.hpp"
#include "opensim/framework/nini_xml_document_save.hpp"
#include "opensim/framework/console_parser.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/culture_sort.hpp"
#include "opensim/framework/stream_reader_text.hpp"
#include <bit>
#include <mutex>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <thread>
#include <unordered_map>
namespace opensim::server {
using namespace framework;
namespace {
int hex(char16_t c) {
    return c >= u'0' && c <= u'9'   ? c - u'0'
           : c >= u'a' && c <= u'f' ? c - u'a' + 10
           : c >= u'A' && c <= u'F' ? c - u'A' + 10
                                    : -1;
}
std::u16string decode(std::u16string_view text) {
    std::u16string result;
    std::string bytes;
    auto flush = [&] {
        result += utf16_replacing(bytes);
        bytes.clear();
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        auto c = text[i];
        if (c == u'+')
            c = u' ';
        else if (c == u'%') {
            if (i + 5 < text.size() && text[i + 1] == u'u' && hex(text[i + 2]) >= 0 &&
                hex(text[i + 3]) >= 0 && hex(text[i + 4]) >= 0 && hex(text[i + 5]) >= 0) {
                flush();
                result.push_back(static_cast<char16_t>((hex(text[i + 2]) << 12) | (hex(text[i + 3]) << 8) |
                                                       (hex(text[i + 4]) << 4) | hex(text[i + 5])));
                i += 5;
                continue;
            }
            if (i + 2 < text.size() && hex(text[i + 1]) >= 0 && hex(text[i + 2]) >= 0) {
                bytes.push_back(static_cast<char>((hex(text[i + 1]) << 4) | hex(text[i + 2])));
                i += 2;
                continue;
            }
        }
        if (c < 128)
            bytes.push_back(static_cast<char>(c));
        else {
            flush();
            result.push_back(c);
        }
    }
    flush();
    return utf16(utf8(result));
}
std::int32_t difference(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) - std::uint32_t(b));
}
std::u16string number(std::int64_t n) {
    return utf16(std::to_string(n));
}
struct Document {
    NiniXmlDom dom;
    NiniXmlDomCollectionScope lifetime;
    std::shared_ptr<NiniXmlDomNode> root;
    Document() : lifetime(dom.defer_collection()) {
        dom.document_node()->append_child(
            dom.create_declaration(nini_writer_text(u"1.0"), nini_writer_text(u""), nini_writer_text(u"")));
        root = dom.create_element(nini_writer_text(u"ConsoleSession"));
    }
    std::shared_ptr<NiniXmlDomNode> element(const std::u16string& name,
                                            const std::optional<std::u16string>& text) {
        auto element = dom.create_element(nini_writer_text(name));
        element->append_child(dom.create_text(nini_writer_text(text)));
        return element;
    }
    void attribute(const std::shared_ptr<NiniXmlDomNode>& node, const std::u16string& name,
                   const std::u16string& value) {
        auto attribute = dom.create_attribute(nini_writer_text(name));
        attribute->value(nini_writer_text(value));
        node->append_attribute(attribute);
    }
    std::u16string finish() {
        dom.document_node()->append_child(root);
        auto sink = std::make_shared<NiniStringWriter>();
        NativeXmlTextWriter writer(native_xml_string_writer_io(sink), true);
        native_xml_document_write_to(dom, writer);
        writer.flush();
        return *sink->to_string();
    }
};
PollTablePtr table(RemoteConsoleReply reply) {
    auto value = std::make_shared<PollTable>();
    value->set("str_response_string", utf8(reply.body));
    value->set("int_response_code", reply.status);
    value->set("content_type", utf8(reply.content_type));
    if (reply.keep_alive)
        value->set("keepalive", *reply.keep_alive);
    if (reply.allowed_origin)
        value->set("access_control_allow_origin", utf8(*reply.allowed_origin));
    return value;
}
}
std::map<std::u16string, std::u16string> RemoteConsole::decode_post(std::u16string_view data) {
    std::map<std::u16string, std::u16string> result;
    std::size_t start = 0;
    while (true) {
        auto end = data.find(u'&', start);
        auto term = data.substr(start, end == data.npos ? end : end - start);
        auto equal = term.find(u'=');
        auto name = decode(term.substr(0, equal));
        std::u16string value;
        if (equal != term.npos) {
            auto next = term.find(u'=', equal + 1);
            value = decode(term.substr(equal + 1, next == term.npos ? next : next - equal - 1));
        }
        result[std::move(name)] = std::move(value);
        if (end == data.npos)
            break;
        start = end + 1;
    }
    return result;
}
struct RemoteConsole::Impl : std::enable_shared_from_this<Impl> {
    struct Connection {
        Uuid id;
        std::int32_t last;
        std::int64_t seen = 0;
        bool fresh = true;
    };
    struct Line {
        std::int64_t number;
        std::optional<std::u16string> text;
        std::u16string level;
        bool prompt, command, input;
    };
    RemoteConsoleOptions options;
    mutable std::recursive_mutex mutex;
    std::recursive_mutex commands_mutex;
    std::condition_variable_any data_event, timer_event;
    std::thread timer;
    bool stopped = false;
    std::vector<std::shared_ptr<Connection>> slots;
    std::vector<std::size_t> free;
    std::unordered_map<Uuid, std::size_t, UuidHash> sessions;
    std::deque<Line> scrollback;
    std::deque<std::u16string> input;
    std::int64_t line = 0;
    bool expecting_input = false, expecting_command = true;
    std::u16string last_prompt;
    explicit Impl(RemoteConsoleOptions value) : options(std::move(value)), last_prompt(options.prompt) {
        if (!options.tick_count)
            options.tick_count = []() {
                return std::bit_cast<std::int32_t>(
                    static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                   std::chrono::steady_clock::now().time_since_epoch())
                                                   .count()));
            };
        if (!options.random_uuid)
            options.random_uuid = Uuid::random;
        if (!options.debug)
            options.debug = [](const auto& text) { std::cout << utf8(text) << '\n'; };
    }
    void launch_timer() {
        if (options.expiration_timer)
            timer = std::thread([this] {
                std::unique_lock lock(mutex);
                while (!stopped) {
                    if (timer_event.wait_for(lock, std::chrono::seconds(5), [&] { return stopped; }))
                        break;
                    expire();
                }
            });
    }
    void stop() {
        std::unique_lock lock(mutex);
        stopped = true;
        data_event.notify_all();
        timer_event.notify_all();
        lock.unlock();
        if (timer.joinable())
            timer.join();
    }
    std::shared_ptr<Connection> find(Uuid id) {
        auto it = sessions.find(id);
        return it == sessions.end() ? nullptr : slots[it->second];
    }
    void insert(std::shared_ptr<Connection> value) {
        auto old = sessions.find(value->id);
        if (old != sessions.end()) {
            slots[old->second] = std::move(value);
            return;
        }
        auto index = slots.size();
        if (!free.empty()) {
            index = free.back();
            free.pop_back();
            slots[index] = value;
        } else
            slots.push_back(value);
        sessions[value->id] = index;
    }
    void erase(Uuid id) {
        auto it = sessions.find(id);
        if (it == sessions.end())
            return;
        slots[it->second].reset();
        free.push_back(it->second);
        sessions.erase(it);
    }
    RemoteConsoleReply origin(RemoteConsoleReply result) {
        std::lock_guard lock(mutex);
        if (!options.allowed_origin.empty())
            result.allowed_origin = options.allowed_origin;
        return result;
    }
    RemoteConsoleReply ok() {
        Document doc;
        doc.root->append_child(doc.element(u"Result", u"OK"));
        return origin({200, u"text/xml", doc.finish(), {}, {}});
    }
    void close_connection(Uuid id) {
        try {
            if (!options.remove_poll)
                throw std::runtime_error("Remote console poll remover unavailable");
            options.remove_poll("", "/ReadResponses/" + id.str() + "/");
        } catch (...) {
        }
    }
    void expire() {
        std::lock_guard lock(mutex);
        std::vector<Uuid> expired;
        for (const auto& connection : slots)
            if (connection && difference(options.tick_count(), connection->last) > 500000)
                expired.push_back(connection->id);
        for (auto id : expired) {
            erase(id);
            close_connection(id);
        }
    }
    RemoteConsoleReply start(std::u16string_view body) {
        auto post = decode_post(body);
        std::shared_ptr<Connection> connection;
        {
            std::lock_guard lock(mutex);
            RemoteConsoleReply failure{401, u"text/plain", u"", {}, {}};
            if (options.user.empty() || !post.contains(u"USER") || !post.contains(u"PASS") ||
                options.user != post[u"USER"] || options.password != post[u"PASS"])
                return failure;
            connection = std::make_shared<Connection>();
            connection->last = options.tick_count();
            connection->id = options.random_uuid();
            insert(connection);
        }
        auto weak = weak_from_this();
        auto require = [weak]() {
            auto state = weak.lock();
            if (!state)
                throw std::runtime_error("Remote console stopped");
            return state;
        };
        auto handler = std::make_shared<services::CapabilityPollHandler>(
            nullptr, "/ReadResponses/" + connection->id.str(),
            [require](auto, auto id) { return require()->has_events(id); },
            [require](auto, auto id) { return table(require()->get_events(id)); },
            [require](auto, auto) { return table(require()->no_events()); }, nullptr, connection->id, 25000);
        if (!options.add_poll)
            throw std::runtime_error("Remote console poll registrar unavailable");
        options.add_poll(handler);
        Document doc;
        doc.root->append_child(doc.element(u"SessionID", utf16(connection->id.str())));
        std::u16string prompt;
        {
            std::lock_guard lock(mutex);
            prompt = last_prompt;
        }
        doc.root->append_child(doc.element(u"Prompt", prompt));
        if (!options.commands)
            throw std::runtime_error("Remote console command registry unavailable");
        {
            std::lock_guard lock(commands_mutex);
            doc.root->append_child(options.commands->get_xml(&doc.dom));
        }
        return origin({200, u"text/xml", doc.finish(), {}, {}});
    }
    RemoteConsoleReply close(std::u16string_view body) {
        std::lock_guard lock(mutex);
        auto post = decode_post(body);
        Uuid id;
        if (!post.contains(u"ID") || !try_parse_sdk_uuid(utf8(post[u"ID"]), id))
            return {404, u"text/plain", u"", {}, {}};
        if (find(id)) {
            erase(id);
            close_connection(id);
        }
        return ok();
    }
    RemoteConsoleReply command(std::u16string_view body) {
        std::lock_guard lock(mutex);
        auto post = decode_post(body);
        Uuid id;
        if (!post.contains(u"ID") || !try_parse_sdk_uuid(utf8(post[u"ID"]), id) || !find(id) ||
            !post.contains(u"COMMAND"))
            return {404, u"text/plain", u"", {}, {}};
        input.push_back(post[u"COMMAND"]);
        data_event.notify_all();
        return ok();
    }
    void output(std::optional<std::u16string> text, std::optional<std::u16string> level, bool prompt,
                bool command, bool is_input) {
        {
            std::lock_guard lock(mutex);
            line = std::bit_cast<std::int64_t>(std::uint64_t(line) + 1);
            while (scrollback.size() >= 1000)
                scrollback.pop_front();
            scrollback.push_back({line, text, level.value_or(u""), prompt, command, is_input});
        }
        if (!text)
            throw std::invalid_argument("Null console output");
        std::u16string_view trimmed = *text;
        while (!trimmed.empty() && unicode_whitespace(trimmed.front()))
            trimmed.remove_prefix(1);
        while (!trimmed.empty() && unicode_whitespace(trimmed.back()))
            trimmed.remove_suffix(1);
        std::u16string value(trimmed);
        if (options.on_output)
            options.on_output(value);
        options.debug(value);
    }
    bool has_events(Uuid id) {
        std::lock_guard lock(mutex);
        auto connection = find(id);
        if (!connection)
            return false;
        connection->last = options.tick_count();
        return connection->seen < line;
    }
    RemoteConsoleReply no_events() {
        std::lock_guard lock(mutex);
        Document doc;
        return origin({200, u"text/xml", doc.finish(), false, {}});
    }
    RemoteConsoleReply get_events(Uuid id) {
        std::lock_guard lock(mutex);
        auto connection = find(id);
        if (!connection)
            return no_events();
        connection->last = options.tick_count();
        if (connection->seen >= line)
            return no_events();
        Document doc;
        auto start = line - static_cast<std::int64_t>(scrollback.size());
        auto first = std::max(start, connection->seen);
        for (auto i = first; i < line; ++i) {
            auto& entry = scrollback.at(static_cast<std::size_t>(i - start));
            auto element = doc.dom.create_element(nini_writer_text(u"Line"));
            doc.attribute(element, u"Number", number(entry.number));
            doc.attribute(element, u"Level", entry.level);
            auto flag = [&](const std::u16string& name, bool value) {
                doc.attribute(element, name, value ? u"true" : u"false");
            };
            if (!connection->fresh) {
                flag(u"Prompt", entry.prompt);
                flag(u"Command", entry.command);
                flag(u"Input", entry.input);
            } else if (i == line - 1) {
                flag(u"Prompt", expecting_input);
                flag(u"Command", expecting_command);
                flag(u"Input", !expecting_input);
            } else
                flag(u"Input", entry.input);
            element->append_child(doc.dom.create_text(nini_writer_text(entry.text)));
            doc.root->append_child(element);
        }
        connection->seen = line;
        connection->fresh = false;
        return origin({200, u"application/xml", doc.finish(), false, {}});
    }
    std::u16string read_line(std::u16string prompt, bool is_command, bool) {
        {
            std::lock_guard lock(mutex);
            expecting_input = true;
            if (is_command)
                expecting_command = true;
        }
        output(prompt, u"", true, is_command, false);
        if (is_command) {
            std::lock_guard lock(mutex);
            last_prompt = prompt;
        }
        std::u16string value;
        {
            std::unique_lock lock(mutex);
            data_event.wait(lock, [&] { return !input.empty() || stopped; });
            if (input.empty()) {
                expecting_input = expecting_command = false;
                return u"";
            }
            value = std::move(input.front());
            input.pop_front();
            expecting_input = expecting_command = false;
        }
        output(value, u"", false, false, true);
        if (is_command) {
            if (!options.commands)
                throw std::runtime_error("Remote console command registry unavailable");
            auto words = parse_console_command_utf8(utf8(value));
            std::lock_guard lock(commands_mutex);
            auto result = options.commands->invoke(words);
            if (result && !words.empty()) {
                for (auto& word : words)
                    if (word.find(' ') != std::string::npos)
                        word = '"' + word + '"';
                return u"";
            }
        }
        return value;
    }
};
RemoteConsole::RemoteConsole(RemoteConsoleOptions options)
    : impl_(std::make_shared<Impl>(std::move(options))) {
    impl_->launch_timer();
}
RemoteConsole::~RemoteConsole() {
    impl_->stop();
}
void RemoteConsole::stop() {
    impl_->stop();
}
void RemoteConsole::set_server(const HttpRegistrar& add_http) {
    if (!add_http)
        throw std::invalid_argument("Remote console HTTP registrar unavailable");
    const char* paths[] = {"/StartSession", "/CloseSession", "/SessionCommand"};
    for (int index = 0; index < 3; ++index) {
        std::weak_ptr<Impl> weak = impl_;
        add_http(paths[index], [weak, index](const network::HttpRequest& request) {
            network::HttpResponse response;
            auto core = weak.lock();
            if (!core) {
                response.status = 503;
                return response;
            }
            auto body = decode_stream_reader_text(
                            {reinterpret_cast<const std::uint8_t*>(request.body.data()), request.body.size()})
                            .text;
            auto reply = index == 0   ? core->start(body)
                         : index == 1 ? core->close(body)
                                      : core->command(body);
            response.status = reply.status;
            response.content_type = utf8(reply.content_type);
            response.body = utf8(reply.body);
            bool keep_alive = true;
            for (const auto& [name, value] : request.headers)
                if (ordinal_equals_ignore_case(name, "Connection") &&
                    invariant_equals_ignore_case(value, "close"))
                    keep_alive = false;
            response.keep_alive = reply.keep_alive.value_or(keep_alive);
            if (reply.allowed_origin)
                response.headers["Access-Control-Allow-Origin"] = utf8(*reply.allowed_origin);
            return response;
        });
    }
}

void RemoteConsole::read_config(const IniConfig& config) {
    std::lock_guard lock(impl_->mutex);
    if (!config.has_section("Network"))
        return;
    impl_->options.user = utf16(config.get("Network", "ConsoleUser"));
    impl_->options.password = utf16(config.get("Network", "ConsolePass"));
    impl_->options.allowed_origin = utf16(config.get("Network", "ConsoleAllowedOrigin"));
}
RemoteConsoleReply RemoteConsole::start_session(std::u16string_view body) {
    return impl_->start(body);
}
RemoteConsoleReply RemoteConsole::close_session(std::u16string_view body) {
    return impl_->close(body);
}
RemoteConsoleReply RemoteConsole::session_command(std::u16string_view body) {
    return impl_->command(body);
}
void RemoteConsole::output(std::optional<std::u16string> text, std::optional<std::u16string> level,
                           bool prompt, bool command, bool input) {
    impl_->output(std::move(text), std::move(level), prompt, command, input);
}
std::u16string RemoteConsole::read_line(std::u16string prompt, bool command, bool echo) {
    return impl_->read_line(std::move(prompt), command, echo);
}
ConsolePrompts RemoteConsole::prompts(ConsolePrompts::Diagnostic diagnostic) const {
    std::weak_ptr<Impl> weak = impl_;
    return ConsolePrompts(
        [weak](const auto& prompt, bool command, bool echo) -> ConsolePrompts::Text {
            auto core = weak.lock();
            if (!core)
                throw std::runtime_error("Remote console unavailable");
            return core->read_line(prompt, command, echo);
        },
        std::move(diagnostic));
}
bool RemoteConsole::has_events(Uuid id) {
    return impl_->has_events(id);
}
RemoteConsoleReply RemoteConsole::get_events(Uuid id) {
    return impl_->get_events(id);
}
RemoteConsoleReply RemoteConsole::no_events() {
    return impl_->no_events();
}
void RemoteConsole::expire() {
    impl_->expire();
}
void RemoteConsole::close_connection(Uuid id) {
    impl_->close_connection(id);
}
RemoteConsoleState RemoteConsole::state() const {
    std::lock_guard lock(impl_->mutex);
    return {impl_->line,
            impl_->scrollback.size(),
            impl_->input.size(),
            impl_->sessions.size(),
            impl_->expecting_input,
            impl_->expecting_command,
            impl_->last_prompt};
}
RemoteConsoleHttpRoutes::RemoteConsoleHttpRoutes(RemoteConsole& console) {
    console.set_server(
        [&](const auto& path, auto handler) { routes_.emplace_back(path, std::move(handler)); });
}
std::optional<network::HttpResponse>
RemoteConsoleHttpRoutes::handle(const network::HttpRequest& request) const {
    auto path = trim_utf8(request.raw_target.empty() ? request.path : request.raw_target);
    while (!path.empty() && path.back() == '/')
        path.pop_back();
    if (path.empty())
        return {};
    if (path.front() != '/')
        path = '/' + path;
    for (const auto& [prefix, handler] : routes_)
        if (invariant_starts_with_ignore_case(path, prefix))
            return handler(request);
    return {};
}

}
