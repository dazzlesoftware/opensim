// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/console_command_resolver.hpp"
#include "opensim/framework/console_parser.hpp"
#include "opensim/framework/nini_xml_dom.hpp"
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
int main() {
    std::string row;
    while (std::getline(std::cin, row)) {
        std::vector<std::string> fields;
        std::size_t start = 0;
        while (true) {
            auto end = row.find('\t', start);
            fields.push_back(row.substr(start, end == std::string::npos ? end : end - start));
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
        int flags = std::stoi(fields[0]);
        ConsoleCommandResolver commands;
        for (std::size_t i = 1; i < fields.size(); ++i) {
            std::u16string units;
            for (std::size_t j = 0; j < fields[i].size(); j += 4)
                units.push_back(static_cast<char16_t>(std::stoi(fields[i].substr(j, 4), nullptr, 16)));
            auto command = utf8(units);
            commands.add({"Module " + std::to_string(i % 3), command, command + " <&>",
                          "L<&>é\n" + std::to_string(i), i % 2 ? "Details '\"\r\n" : "", i % 2 == 0});
        }
        for (int attempt = 0; attempt < 3; ++attempt) {
            NiniXmlDom document;
            if (flags & 4)
                document.document_node()->append_child(
                    document.create_element(nini_writer_text(u"Existing")));
            std::string out;
            try {
                auto root = commands.get_xml((flags & 3) == attempt ? nullptr : &document);
                out = "OK:" + std::to_string(root->same_owner(*document.document_node())) + ":" +
                      std::to_string(root->parent() != nullptr);
                tree(root, out);
            } catch (const std::exception&) {
                out = "EX";
            }
            out += ";D:" +
                   (document.document_element() ? optional_text(document.document_element()->name()) : "-");
            try {
                out += ";N" + list(commands.next_options({}, false));
            } catch (const std::exception&) {
                out += ";NEX";
            }
            try {
                out += ";H" + list(commands.help({"help", "all"}));
            } catch (const std::exception&) {
                out += ";HEX";
            }
            try {
                out += ";C:" + std::to_string(commands.has_command("help")) + ":" +
                       std::to_string(commands.has_command("quit"));
            } catch (const std::exception&) {
                out += ";CEX";
            }
            std::cout << out << (attempt < 2 ? '~' : '\n');
        }
    }
}
