// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/console_command_resolver.hpp"
#include "opensim/framework/console_parser.hpp"
#include "opensim/framework/nini_xml_dom.hpp"
#include "opensim/framework/nini_xml_reader.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace opensim::framework;
std::string encode(std::u16string_view value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto c : value)
        out << std::setw(4) << static_cast<unsigned>(c);
    return out.str();
}
std::string encode(const std::string& value) {
    return encode(utf16(value));
}
std::string optional_text(const NiniWriterText& value) {
    return value ? encode(*value) : "-";
}
std::string list(const std::vector<std::string>& values) {
    std::string out;
    for (const auto& value : values)
        out += '|' + encode(value);
    return out;
}
std::string list(const std::vector<std::optional<std::string>>& values) {
    std::string out;
    for (const auto& value : values)
        out += '|' + (value ? encode(*value) : "-");
    return out;
}
void tree(const std::shared_ptr<NiniXmlDomNode>& node, std::string& out) {
    auto attributes = node->attributes();
    std::vector<std::shared_ptr<NiniXmlDomNode>> children;
    for (auto child = node->first_child(); child; child = child->next_sibling())
        children.push_back(child);
    out += '|' + std::to_string(node->node_type()) + ":" + optional_text(node->name()) + ":" +
           optional_text(node->value()) + ":" +
           (node->node_type() == 1 ? (node->is_empty_element() ? "1" : "0") : "-") + ":" +
           std::to_string(attributes.size()) + ":" + std::to_string(children.size());
    for (const auto& attribute : attributes)
        tree(attribute, out);
    for (const auto& child : children)
        tree(child, out);
}
std::u16string decode(const std::string& encoded) {
    std::u16string value;
    for (std::size_t i = 0; i < encoded.size(); i += 4)
        value.push_back(static_cast<char16_t>(std::stoi(encoded.substr(i, 4), nullptr, 16)));
    return value;
}
int main() {
    std::string row;
    while (std::getline(std::cin, row)) {
        auto tab = row.find('\t');
        int flags = std::stoi(row.substr(0, tab));
        auto input = row.substr(tab + 1);
        ConsoleCommandResolver commands;
        std::vector<std::string> events;
        using Function = ConsoleCommandResolver::Callback::element_type;
        auto old = std::make_shared<Function>([&](const auto& module, auto& words) {
            events.push_back("old:" + (module ? encode(*module) : "-") + list(words));
        });
        auto imported = std::make_shared<Function>([&](const auto& module, auto& words) {
            events.push_back("new:" + (module ? encode(*module) : "-") + list(words));
        });
        for (const auto* name : {"help", "quit", "old", "old nested", "help oldchild", "quit oldchild"}) {
            if (std::string(name) == "help" && (flags & 1))
                continue;
            if (std::string(name) == "quit" && (flags & 2))
                continue;
            commands.add({"Original", name, std::string(name) + " usage", "Original long", "", false}, old);
        }
        for (int attempt = 0; attempt < 2; ++attempt) {
            std::string out;
            NiniXmlDom document;
            std::shared_ptr<NiniXmlDomNode> root;
            try {
                if (input != "-") {
                    native_nini_xml_load(
                        document, NativeNiniXmlReader::from_text(nini_writer_text(decode(input)),
                                                                 NiniXmlReaderSettings::legacy_document()));
                    root = document.document_element();
                    auto rewrite = [&](auto&& self, const std::shared_ptr<NiniXmlDomNode>& node) -> void {
                        if (!node)
                            return;
                        auto value = node->value();
                        if (value && *value == u"~NULL~")
                            node->value({});
                        else if (value) {
                            auto text = *value;
                            for (std::size_t at = 0; (at = text.find(u"~NUL~", at)) != std::u16string::npos;
                                 ++at)
                                text.replace(at, 5, 1, char16_t{});
                            if (text != *value)
                                node->value(nini_writer_text(std::move(text)));
                        }
                        for (auto child = node->first_child(); child; child = child->next_sibling())
                            self(self, child);
                    };
                    rewrite(rewrite, root);
                }
                out = "LOADOK";
            } catch (const std::exception&) {
                out = "LOADEX";
            }
            if (out == "LOADOK")
                try {
                    commands.from_xml(root, (flags & 16) ? ConsoleCommandResolver::Callback{} : imported);
                    out = "OK";
                } catch (const std::exception&) {
                    out = "FROMEX";
                }
            for (const auto& query : std::vector<std::string>{"help", "quit", "old", "run", "run value",
                                                              "run nested", "a", "missing", ""}) {
                events.clear();
                auto words = parse_console_command_utf8(query);
                try {
                    bool has = commands.has_command(query);
                    auto result = commands.invoke(words);
                    out += ";Q:" + std::to_string(has) + ":" + std::to_string(bool(result)) + list(words) +
                           ":" + list(events);
                } catch (const std::exception&) {
                    out += ";QEX" + list(words) + ":" + list(events);
                }
            }
            for (const auto& query : std::vector<std::vector<std::string>>{{"help", "all"},
                                                                           {"help", "Original"},
                                                                           {"help", "New"},
                                                                           {"help", "run"},
                                                                           {"help", "run", "nested"}})
                try {
                    out += ";H" + list(commands.help(query));
                } catch (const std::exception&) {
                    out += ";HEX";
                }
            try {
                out += ";N" + list(commands.next_options({}, false));
            } catch (const std::exception&) {
                out += ";NEX";
            }
            NiniXmlDom exported;
            try {
                auto xml = commands.get_xml(&exported);
                out += ";X";
                tree(xml, out);
            } catch (const std::exception&) {
                out += ";XEX";
            }
            std::cout << out << (attempt == 0 ? '~' : '\n');
        }
    }
}
