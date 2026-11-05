// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/console_command_resolver.hpp"
#include "opensim/framework/console_parser.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include <stdexcept>
#include "opensim/framework/text.hpp"
#include "opensim/framework/culture_sort.hpp"
#include "dotnet_regex_sort.hpp"
#include "opensim/framework/nini_xml_dom.hpp"
namespace opensim::framework {
struct ConsoleCommandResolver::Node {
    struct Entry {
        std::string key;
        std::unique_ptr<Node> child;
        std::shared_ptr<ConsoleCommandDescription> info;
        bool live = true;
    };
    // Original Dictionary reuses removed entry slots in LIFO order. Export
    // removes/reinserts help and quit, so insertion-only ordering is insufficient.
    std::vector<Entry> entries;
    std::vector<std::size_t> free;
    std::size_t count = 0;
    Entry* find(const std::string& key) {
        for (auto& entry : entries)
            if (entry.live && entry.key == key)
                return &entry;
        return nullptr;
    }
    Entry* insert(std::string key, std::unique_ptr<Node> child,
                  std::shared_ptr<ConsoleCommandDescription> info = {}) {
        Entry entry{std::move(key), std::move(child), std::move(info), true};
        ++count;
        if (free.empty()) {
            entries.push_back(std::move(entry));
            return &entries.back();
        }
        auto index = free.back();
        free.pop_back();
        entries[index] = std::move(entry);
        return &entries[index];
    }
    Entry* set(const std::string& key, std::unique_ptr<Node> child,
               std::shared_ptr<ConsoleCommandDescription> info = {}) {
        if (auto* entry = find(key)) {
            entry->child = std::move(child);
            entry->info = std::move(info);
            return entry;
        }
        return insert(key, std::move(child), std::move(info));
    }
    void clear() {
        entries.clear();
        free.clear();
        count = 0;
    }
    void remove(const std::string& key) {
        for (std::size_t i = 0; i < entries.size(); ++i)
            if (entries[i].live && entries[i].key == key) {
                entries[i].live = false;
                entries[i].child.reset();
                entries[i].info.reset();
                free.push_back(i);
                --count;
                return;
            }
    }
};

ConsoleCommandResolver::ConsoleCommandResolver() : root_(std::make_unique<Node>()) {
}
ConsoleCommandResolver::~ConsoleCommandResolver() = default;
void ConsoleCommandResolver::add(const std::string& command) {
    add(ConsoleCommandDescription{"", command, command, "", "", false});
}
void ConsoleCommandResolver::add(ConsoleCommandDescription description) {
    add(std::move(description), {});
}
void ConsoleCommandResolver::add(ConsoleCommandDescription description, Callback callback) {
    const auto& command = description.command;
    auto* current = root_.get();
    for (const auto& part : parse_console_command_utf8(command)) {
        auto* entry = current->find(part);
        if (entry) {
            if (!entry->child)
                return;
            current = entry->child.get();
        } else
            current = current->insert(part, std::make_unique<Node>())->child.get();
    }
    if (auto* entry = current->find("")) {
        if (entry->child)
            throw std::invalid_argument("Command dictionary is not CommandInfo");
        auto& callbacks = callbacks_.at(entry->info.get());
        if (!entry->info->shared &&
            std::find(callbacks.values.begin(), callbacks.values.end(), callback) == callbacks.values.end()) {
            callbacks.values.push_back(std::move(callback));
            ++callbacks.version;
        }
        return;
    }
    auto info = std::make_shared<ConsoleCommandDescription>(std::move(description));
    callbacks_[info.get()] = Callbacks{{std::move(callback)}, 0};
    current->insert("", {}, info);
    if (!info->module)
        throw std::invalid_argument("Null command module");
    registrations_.push_back(std::move(info));
}
std::shared_ptr<ConsoleCommandDescription>
ConsoleCommandResolver::resolve_target(std::vector<std::string>& command) const {
    auto* current = root_.get();
    for (auto& word : command) {
        Node::Entry* match = nullptr;
        std::size_t count = 0;
        for (auto& entry : current->entries) {
            if (!entry.live)
                continue;
            if (entry.key == word) {
                match = &entry;
                count = 1;
                break;
            }
            if (invariant_starts_with(entry.key, word)) {
                match = &entry;
                ++count;
            }
        }
        if (count == 1) {
            word = match->key;
            if (!match->child)
                throw std::invalid_argument("CommandInfo is not command dictionary");
            current = match->child.get();
        } else if (count > 0)
            return {};
        else
            break;
    }
    const auto* terminal = current->find("");
    if (terminal && terminal->child)
        throw std::invalid_argument("Command dictionary is not CommandInfo");
    return terminal ? terminal->info : nullptr;
}
bool ConsoleCommandResolver::resolve(std::vector<std::string>& command) const {
    return bool(resolve_target(command));
}
bool ConsoleCommandResolver::has_command(const std::string& command) const {
    auto words = parse_console_command_utf8(command);
    return bool(resolve_target(words));
}
std::optional<std::reference_wrapper<std::vector<std::string>>>
ConsoleCommandResolver::invoke(std::vector<std::string>& command) const {
    const auto info = resolve_target(command);
    if (!info)
        return {};
    const auto& callbacks = callbacks_.at(info.get());
    if (callbacks.values.empty())
        return {};
    const auto version = callbacks.version;
    for (std::size_t i = 0;; ++i) {
        if (callbacks.version != version)
            throw std::runtime_error("Collection was modified; enumeration operation may not execute.");
        if (i == callbacks.values.size())
            break;
        auto callback = callbacks.values[i];
        if (!callback)
            return {};
        (*callback)(info->module, command);
    }
    return std::ref(command);
}
std::vector<std::string> ConsoleCommandResolver::next_options(const std::vector<std::string>& command,
                                                              bool terminated) const {
    auto* current = root_.get();
    auto remaining = command.size();
    for (const auto& word : command) {
        --remaining;
        std::vector<Node::Entry*> found;
        for (auto& entry : current->entries) {
            if (!entry.live)
                continue;
            if (remaining > 0 && entry.key == word) {
                found.clear();
                found.push_back(&entry);
                break;
            }
            if (invariant_starts_with(entry.key, word))
                found.push_back(&entry);
        }
        if (found.size() == 1 && (remaining != 0 || terminated)) {
            if (!found[0]->child)
                throw std::invalid_argument("CommandInfo is not command dictionary");
            current = found[0]->child.get();
        } else if (!found.empty()) {
            std::vector<std::string> result;
            for (auto* entry : found)
                result.push_back(entry->key);
            return result;
        } else
            break;
    }
    if (current->count > 1) {
        std::vector<std::string> result;
        bool add_return = false;
        for (const auto& entry : current->entries) {
            if (!entry.live)
                continue;
            if (entry.key.empty()) {
                if (!entry.info)
                    throw std::invalid_argument("Command dictionary is not CommandInfo");
                add_return = true;
            } else
                result.push_back(entry.key);
        }
        if (add_return)
            result.push_back("<cr>");
        return result;
    }
    if (const auto* entry = current->find("")) {
        if (!entry->info)
            throw std::invalid_argument("Command dictionary is not CommandInfo");
        return {"Command help: " + entry->info->usage.value_or("")};
    }
    if (current->count == 0)
        throw std::out_of_range("Empty command tree");
    for (const auto& entry : current->entries)
        if (entry.live)
            return {entry.key};
    throw std::logic_error("Command dictionary count mismatch");
}
std::vector<std::optional<std::string>>
ConsoleCommandResolver::help(const std::vector<std::string>& command) const {
    if (command.empty())
        throw std::out_of_range("Missing help keyword");
    const std::string item =
        "For more information, type 'help all' to get a list of all commands,\r\n              or type help <item>' where <item> is one of the following:";
    auto sort = [](auto& values) {
        regex_detail::sort(values, [](const auto& a, const auto& b) { return invariant_compare(a, b); });
    };
    std::vector<std::string> modules;
    for (const auto& info : registrations_)
        if (std::find(modules.begin(), modules.end(), info->module) == modules.end())
            modules.push_back(*info->module);
    std::vector<std::optional<std::string>> result{""};
    if (command.size() == 1) {
        result.push_back(
            "To enter an argument that contains spaces, surround the argument with double quotes.\nFor example, show object name \"My long object name\"\n");
        result.push_back(item);
        sort(modules);
        result.insert(result.end(), modules.begin(), modules.end());
    } else {
        std::string request;
        for (std::size_t i = 1; i < command.size(); ++i) {
            if (i > 1)
                request += ' ';
            request += command[i];
        }
        std::vector<std::string> lines;
        bool module_found = false;
        if (command.size() == 2 && command[1] == "all") {
            // Original collection groups registrations by module before List.Sort.
            for (const auto& module : modules)
                for (const auto& info : registrations_)
                    if (info->module == module)
                        lines.push_back(info->usage.value_or("") + " - " + info->description.value_or(""));
            sort(lines);
            result.insert(result.end(), lines.begin(), lines.end());
        } else {
            for (const auto& module : modules)
                if (invariant_lower_utf16(utf16(request)) == invariant_lower_utf16(utf16(module))) {
                    module_found = true;
                    for (const auto& info : registrations_)
                        if (info->module == module)
                            lines.push_back(info->usage.value_or("") + " - " +
                                            info->description.value_or(""));
                    sort(lines);
                    result.push_back(item);
                    result.insert(result.end(), lines.begin(), lines.end());
                    break;
                }
            if (!module_found) {
                auto* current = root_.get();
                for (std::size_t i = 1; i < command.size(); ++i) {
                    const auto* entry = current->find(command[i]);
                    if (!entry)
                        break;
                    if (entry->child)
                        current = entry->child.get();
                }
                const auto* terminal = current->find("");
                if (terminal) {
                    if (!terminal->info)
                        throw std::invalid_argument("Command dictionary is not CommandInfo");
                    const auto& info = *terminal->info;
                    result.push_back(info.usage);
                    result.push_back(info.description);
                    if (!info.descriptive_help || !info.descriptive_help->empty())
                        result.push_back("");
                    result.push_back(info.descriptive_help);
                } else
                    result.push_back("No help is available for " + request);
            }
        }
    }
    result.push_back("");
    return result;
}

std::shared_ptr<NiniXmlDomNode> ConsoleCommandResolver::get_xml(NiniXmlDom* document) {
    auto detach = [&](const std::string& name) {
        auto* level = root_->find(name);
        if (!level)
            throw std::out_of_range("Missing command key");
        if (!level->child)
            throw std::invalid_argument("CommandInfo is not command dictionary");
        auto* entry = level->child->find("");
        if (!entry)
            throw std::out_of_range("Missing terminal command key");
        if (!entry->info)
            throw std::invalid_argument("Command dictionary is not CommandInfo");
        auto info = entry->info;
        level->child->remove("");
        if (level->child->count == 0)
            root_->remove(name);
        return info;
    };
    auto help = detach("help");
    auto quit = detach("quit");
    // The source deliberately has no finally: any DOM failure leaves the
    // preceding removals visible to later command queries.
    if (!document)
        throw std::invalid_argument("Null XML document");
    auto root =
        document->create_element(nini_writer_text(u""), nini_writer_text(u"HelpTree"), nini_writer_text(u""));
    auto process = [&](auto&& self, const Node& level, const std::shared_ptr<NiniXmlDomNode>& xml) -> void {
        for (const auto& entry : level.entries) {
            if (!entry.live)
                continue;
            if (entry.child) {
                auto next = document->create_element(nini_writer_text(u""), nini_writer_text(u"Level"),
                                                     nini_writer_text(u""));
                auto name = document->create_attribute(nini_writer_text(u"Name"));
                name->value(nini_writer_text(utf16(entry.key)));
                next->append_attribute(name);
                xml->append_child(next);
                self(self, *entry.child, next);
            } else {
                const auto& info = *entry.info;
                auto command = document->create_element(nini_writer_text(u""), nini_writer_text(u"Command"),
                                                        nini_writer_text(u""));
                for (const auto& field : std::vector<std::pair<std::string, std::optional<std::string>>>{
                         {"Module", info.module},
                         {"Shared", info.shared ? "True" : "False"},
                         {"HelpText", info.usage},
                         {"LongHelp", info.description},
                         {"Description", info.descriptive_help}}) {
                    auto element = document->create_element(
                        nini_writer_text(u""), nini_writer_text(utf16(field.first)), nini_writer_text(u""));
                    command->append_child(element);
                    element->append_child(document->create_text(
                        field.second ? nini_writer_text(utf16(*field.second)) : NiniWriterText{}));
                }
                xml->append_child(command);
            }
        }
    };
    process(process, *root_, root);
    auto restore = [&](const std::string& name, std::shared_ptr<ConsoleCommandDescription> info) {
        auto* level = root_->find(name);
        if (!level)
            level = root_->insert(name, std::make_unique<Node>());
        level->child->insert("", {}, std::move(info));
    };
    restore("help", std::move(help));
    restore("quit", std::move(quit));
    return root;
}

void ConsoleCommandResolver::from_xml(std::shared_ptr<NiniXmlDomNode> root, Callback callback) {
    if (root && root->node_type() != 1)
        throw std::invalid_argument("Not an XML element");
    auto detach = [&](const std::string& name) {
        auto* level = root_->find(name);
        if (!level)
            throw std::out_of_range("Missing command key");
        if (!level->child)
            throw std::invalid_argument("CommandInfo is not command dictionary");
        auto* entry = level->child->find("");
        if (!entry)
            throw std::out_of_range("Missing terminal command key");
        if (!entry->info)
            throw std::invalid_argument("Command dictionary is not CommandInfo");
        auto info = entry->info;
        level->child->remove("");
        if (level->child->count == 0)
            root_->remove(name);
        return info;
    };
    auto help = detach("help");
    auto quit = detach("quit");
    root_->clear();
    // XmlNode.InnerText/AppendChildText and Boolean.Parse are derived from the
    // pinned .NET 8 runtime (MIT, third_party/dotnet_collation/LICENSE).
    auto inner_text = [](const std::shared_ptr<NiniXmlDomNode>& node) -> NiniWriterText {
        const auto type = node->node_type();
        if (type == 3 || type == 4 || type == 7 || type == 8 || type == 13 || type == 14)
            return node->value();
        auto first = node->first_child();
        if (!first)
            return nini_writer_text(u"");
        auto text_type = [](int t) { return t == 3 || t == 4 || t == 13 || t == 14; };
        if (!first->next_sibling() && text_type(first->node_type()))
            return first->value();
        std::u16string text;
        auto append = [&](auto&& self, const std::shared_ptr<NiniXmlDomNode>& parent) -> void {
            for (auto child = parent->first_child(); child; child = child->next_sibling()) {
                if (child->first_child())
                    self(self, child);
                else if (text_type(child->node_type())) {
                    auto value = child->value();
                    if (value)
                        text += *value;
                }
            }
        };
        append(append, node);
        return nini_writer_text(std::move(text));
    };
    auto boolean = [](const NiniWriterText& text) {
        if (!text)
            return false;
        std::u16string_view value = *text;
        while (!value.empty() && (value.front() == 0 || unicode_whitespace(value.front())))
            value.remove_prefix(1);
        while (!value.empty() && (value.back() == 0 || unicode_whitespace(value.back())))
            value.remove_suffix(1);
        auto equals = [&](std::u16string_view expected) {
            if (value.size() != expected.size())
                return false;
            for (std::size_t i = 0; i < value.size(); ++i) {
                auto c = value[i];
                if (c >= u'A' && c <= u'Z')
                    c += u'a' - u'A';
                if (c != expected[i])
                    return false;
            }
            return true;
        };
        if (equals(u"true"))
            return true;
        if (equals(u"false"))
            return false;
        throw std::invalid_argument("Invalid Boolean string");
    };
    auto read = [&](auto&& self, Node& level, const std::shared_ptr<NiniXmlDomNode>& node) -> void {
        if (!node)
            throw std::invalid_argument("Null command XML root");
        for (auto part = node->first_child(); part; part = part->next_sibling()) {
            const auto name = part->name();
            if (name && *name == u"Level") {
                if (part->node_type() != 1)
                    throw std::invalid_argument("XML node is not element");
                auto attribute = part->attribute(nini_writer_text(u"Name"));
                auto value = attribute ? attribute->value() : NiniWriterText{};
                auto* next = level.set(value ? utf8(*value) : "", std::make_unique<Node>());
                self(self, *next->child, part);
            } else if (name && *name == u"Command") {
                auto info = std::make_shared<ConsoleCommandDescription>();
                for (auto field = part->first_child(); field; field = field->next_sibling()) {
                    const auto key = field->name();
                    if (!key)
                        continue;
                    if (*key == u"Shared")
                        info->shared = boolean(inner_text(field));
                    else if (*key == u"Module" || *key == u"HelpText" || *key == u"LongHelp" ||
                             *key == u"Description") {
                        auto text = inner_text(field);
                        std::optional<std::string> value;
                        if (text)
                            value = utf8(*text);
                        if (*key == u"Module")
                            info->module = std::move(value);
                        else if (*key == u"HelpText")
                            info->usage = std::move(value);
                        else if (*key == u"LongHelp")
                            info->description = std::move(value);
                        else
                            info->descriptive_help = std::move(value);
                    }
                }
                callbacks_[info.get()] = Callbacks{{callback}, 0};
                level.set("", {}, std::move(info));
            }
        }
    };
    read(read, *root_, root);
    auto restore = [&](const std::string& name, std::shared_ptr<ConsoleCommandDescription> info) {
        auto* level = root_->find(name);
        if (!level)
            level = root_->insert(name, std::make_unique<Node>());
        if (!level->child)
            throw std::invalid_argument("CommandInfo is not command dictionary");
        level->child->set("", {}, std::move(info));
    };
    restore("help", std::move(help));
    restore("quit", std::move(quit));
    // m_modulesCommands is intentionally not rebuilt by the original FromXml.
}

}
