// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/service_protocol.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include <pugixml.hpp>
#include <set>
#include <map>
#include <algorithm>
#include <utility>
#include "opensim/network/service_xml_names.hpp"

namespace opensim::network {
std::string build_service_form(const std::vector<std::pair<std::string, std::optional<std::string>>> &fields) {
    std::string out;
    for (const auto &[key, value] : fields) {
        if (!out.empty())
            out += '&';
        out += service_url_encode(key);
        if (!value)
            throw std::invalid_argument("Null service form value");
        if (!value->empty())
            out += '=' + service_url_encode(*value);
    }
    return out;
}

namespace {
std::string decoded_name(std::string_view input) {
    auto text = framework::utf16_replacing(input);
    std::u16string out;
    auto hex = [](char16_t c) {
        return c >= '0' && c <= '9'   ? c - '0'
               : c >= 'a' && c <= 'f' ? c - 'a' + 10
               : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                      : -1;
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        bool found = false;
        if (text[i] == '_' && i + 6 < text.size() && (text[i + 1] == 'x' || text[i + 1] == 'X'))
            for (int n : {4, 8}) {
                if (i + n + 2 >= text.size() || text[i + n + 2] != '_')
                    continue;
                std::uint32_t value = 0;
                bool valid = true;
                for (int j = 0; j < n; ++j) {
                    auto h = hex(text[i + 2 + j]);
                    if (h < 0) {
                        valid = false;
                        break;
                    }
                    value = (value << 4) | h;
                }
                if (valid && (value <= 0x10ffff || value >= 0x80000000u)) {
                    if (value <= 0xffff || value >= 0x80000000u)
                        out += static_cast<char16_t>(value);
                    else {
                        value -= 0x10000;
                        out += static_cast<char16_t>(0xd800 + (value >> 10));
                        out += static_cast<char16_t>(0xdc00 + (value & 1023));
                    }
                    i += n + 2;
                    found = true;
                    break;
                }
            }
        if (!found)
            out += text[i];
    }
    return framework::utf8(out);
}

bool xml_char(std::uint32_t c) {
    return c == 9 || c == 10 || c == 13 || (c >= 32 && c <= 0xd7ff) || (c >= 0xe000 && c <= 0xfffd) ||
           (c >= 0x10000 && c <= 0x10ffff);
}

void validate(std::string_view text) {
    for (auto c : framework::utf16_replacing(text))
        if ((c < 32 && !xml_char(c)) || c == 0xfffe || c == 0xffff)
            throw std::invalid_argument("Invalid XML character");
    bool tag = false;
    char quote = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text.substr(i).starts_with("<![CDATA[")) {
            auto end = text.find("]]>", i + 9);
            if (end == std::string_view::npos)
                throw std::invalid_argument("Unterminated CDATA");
            i = end + 2;
            continue;
        }
        if (text.substr(i).starts_with("<!--")) {
            auto end = text.find("-->", i + 4);
            if (end == std::string_view::npos)
                throw std::invalid_argument("Unterminated comment");
            i = end + 2;
            continue;
        }
        if (text.substr(i).starts_with("<?")) {
            auto end = text.find("?>", i + 2);
            if (end == std::string_view::npos)
                throw std::invalid_argument("Unterminated PI");
            const auto target = text.substr(i + 2, 3);
            const auto delimiter = i + 5 < text.size() ? text[i + 5] : '\0';
            if (framework::ordinal_equals_ignore_case(target, "xml") &&
                (delimiter == ' ' || delimiter == '\t' || delimiter == '\r' || delimiter == '\n' || delimiter == '?') &&
                (i != 0 || target != "xml"))
                throw std::invalid_argument("Misplaced or reserved XML declaration target");
            i = end + 1;
            continue;
        }
        if (!tag && text.substr(i).starts_with("]]>"))
            throw std::invalid_argument("CDATA terminator in text");
        if (tag) {
            if (quote) {
                if (text[i] == quote)
                    quote = 0;
            } else if (text[i] == '\"' || text[i] == '\'')
                quote = text[i];
            else if (text[i] == '>')
                tag = false;
        } else if (text[i] == '<')
            tag = true;
        if (text[i] != '&')
            continue;
        auto end = text.find(';', i + 1);
        if (end == std::string_view::npos)
            throw std::invalid_argument("Unterminated entity");
        auto value = text.substr(i + 1, end - i - 1);
        if (value.starts_with('#')) {
            value.remove_prefix(1);
            int base = 10;
            if (value.starts_with('x')) {
                base = 16;
                value.remove_prefix(1);
            }
            std::uint32_t n = 0;
            if (value.empty())
                throw std::invalid_argument("Empty numeric entity");
            for (char c : value) {
                int digit = c >= '0' && c <= '9'   ? c - '0'
                            : c >= 'a' && c <= 'f' ? c - 'a' + 10
                            : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                   : -1;
                if (digit < 0 || digit >= base || n > (0x10ffffu - digit) / base)
                    throw std::invalid_argument("Invalid numeric entity");
                n = n * base + digit;
            }
            if (!xml_char(n))
                throw std::invalid_argument("Invalid XML entity character");
        } else if (value != "amp" && value != "lt" && value != "gt" && value != "apos" && value != "quot")
            throw std::invalid_argument("Unknown XML entity");
        i = end;
    }
}

std::string response_prefix(std::string_view text) {
    std::vector<std::string> stack;
    std::size_t target = 0;
    for (std::size_t pos = 0; pos < text.size();) {
        auto begin = text.find('<', pos);
        if (begin == std::string_view::npos)
            break;
        if (text.substr(begin).starts_with("<!--") || text.substr(begin).starts_with("<![CDATA[") ||
            text.substr(begin).starts_with("<?")) {
            auto tail = text.substr(begin).starts_with("<!--") ? "-->"
                        : text.substr(begin).starts_with("<?") ? "?>"
                                                               : "]]>";
            auto end = text.find(tail, begin + 2);
            if (end == std::string_view::npos)
                break;
            pos = end + std::char_traits<char>::length(tail);
            continue;
        }
        char quote = 0;
        std::size_t end = begin + 1;
        for (; end < text.size(); ++end) {
            char c = text[end];
            if (quote) {
                if (c == quote)
                    quote = 0;
            } else if (c == '\"' || c == '\'')
                quote = c;
            else if (c == '>')
                break;
        }
        if (end == text.size())
            break;
        auto tag = text.substr(begin + 1, end - begin - 1);
        bool close = tag.starts_with('/'), empty = tag.ends_with('/');
        if (close)
            tag.remove_prefix(1);
        auto n = tag.find_first_of(" \t\r\n/");
        auto name = std::string(tag.substr(0, n));
        if (!name.empty() && name[0] != '!') {
            if (close) {
                if (stack.empty() || stack.back() != name)
                    break;
                if (target == stack.size() && name == "ServerResponse") {
                    stack.pop_back();
                    std::string prefix(text.substr(0, end + 1));
                    while (!stack.empty()) {
                        prefix += "</" + stack.back() + ">";
                        stack.pop_back();
                    }
                    return prefix;
                }
                stack.pop_back();
            } else if (!empty) {
                stack.push_back(name);
                if (!target && name == "ServerResponse")
                    target = stack.size();
            } else if (!target && name == "ServerResponse")
                return std::string(text);
        }
        pos = end + 1;
    }
    return std::string(text);
}

using Namespaces = std::map<std::string, std::string>;

std::pair<std::string, std::string> qualified(std::string name) {
    auto colon = name.find(':');
    if (colon != std::string::npos && name.find(':', colon + 1) != std::string::npos)
        throw std::invalid_argument("Invalid XML qualified name");
    std::pair<std::string, std::string> parts = colon == std::string::npos
                                                    ? std::pair{std::string{}, name}
                                                    : std::pair{name.substr(0, colon), name.substr(colon + 1)};
    auto check = [](const std::string &value) {
        auto text = framework::utf16_replacing(value);
        if (text.empty())
            throw std::invalid_argument("Empty XML name");
        for (std::size_t i = 0; i < text.size(); ++i) {
            auto c = text[i];
            auto valid = [&](const auto &ranges) {
                auto at = std::lower_bound(ranges.begin(), ranges.end(), c,
                                           [](auto range, char16_t n) { return range.second < n; });
                return at != ranges.end() && at->first <= c;
            };
            if (!(i == 0 ? valid(detail::xml_name_start) : valid(detail::xml_name_char)))
                throw std::invalid_argument("Invalid XML name character");
        }
    };
    if (!parts.first.empty())
        check(parts.first);
    check(parts.second);
    return parts;
}

Namespaces namespaces(pugi::xml_node node, Namespaces values) {
    for (auto attr : node.attributes()) {
        std::string name = attr.name(), uri = attr.value();
        if (name == "xmlns")
            values[""] = uri;
        else if (name.starts_with("xmlns:")) {
            auto prefix = name.substr(6);
            qualified(prefix);
            if (prefix.empty() || uri.empty() || prefix == "xmlns" || uri == "http://www.w3.org/2000/xmlns/" ||
                (prefix == "xml" && uri != "http://www.w3.org/XML/1998/namespace") ||
                (prefix != "xml" && uri == "http://www.w3.org/XML/1998/namespace"))
                throw std::invalid_argument("Invalid namespace declaration");
            values[prefix] = uri;
        }
    }
    auto resolve = [&](const std::string &name, bool attribute) {
        auto [prefix, local] = qualified(name);
        if (!prefix.empty() && !values.contains(prefix))
            throw std::invalid_argument("Undeclared XML prefix");
        return std::pair{prefix.empty() && attribute ? std::string{} : values[prefix], local};
    };
    resolve(node.name(), false);
    std::set<std::pair<std::string, std::string>> names;
    for (auto attr : node.attributes()) {
        std::string name = attr.name();
        if (name == "xmlns" || name.starts_with("xmlns:"))
            continue;
        if (!names.insert(resolve(name, true)).second)
            throw std::invalid_argument("Duplicate expanded XML attribute");
    }
    return values;
}

struct Event {
    pugi::xml_node node;
    bool end = false, empty = false;
};

bool self_closing(pugi::xml_node node, std::string_view text) {
    auto pos = static_cast<std::size_t>(node.offset_debug());
    char quote = 0;
    for (; pos < text.size(); ++pos) {
        char c = text[pos];
        if (quote) {
            if (c == quote)
                quote = 0;
        } else if (c == '\"' || c == '\'')
            quote = c;
        else if (c == '>')
            return pos > 0 && text[pos - 1] == '/';
    }
    throw std::invalid_argument("Unterminated XML element");
}

void events(pugi::xml_node parent, std::string_view text, std::vector<Event> &out, bool preserve = false,
            unsigned depth = 0, Namespaces ns = {{"xml", "http://www.w3.org/XML/1998/namespace"}}) {
    if (depth > 512)
        throw std::invalid_argument("Service XML nesting limit");
    for (auto node : parent.children()) {
        if (node.type() == pugi::node_element) {
            auto bindings = namespaces(node, ns);
            auto space = node.attribute("xml:space");
            if (space && std::string_view(space.value()) != "preserve" && std::string_view(space.value()) != "default")
                throw std::invalid_argument("Invalid XML space value");
            bool keep = space ? std::string_view(space.value()) == "preserve" : preserve;
            std::set<std::string> names;
            for (auto attr : node.attributes())
                if (!names.insert(attr.name()).second)
                    throw std::invalid_argument("Duplicate XML attribute");
            bool empty = self_closing(node, text);
            out.push_back({node, false, empty});
            events(node, text, out, keep, depth + 1, bindings);
            if (!empty)
                out.push_back({node, true});
        } else if (node.type() == pugi::node_pcdata) {
            auto characters = framework::utf16_replacing(node.value());
            bool white = true;
            for (auto c : characters)
                if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
                    white = false;
                    break;
                }
            if (preserve || !white)
                out.push_back({node});
        } else if (node.type() == pugi::node_cdata)
            out.push_back({node});
        else if (node.type() == pugi::node_doctype)
            throw std::invalid_argument("DTD prohibited");
    }
}

void set(ServiceXmlValue &target, std::string key, ServiceXmlValue value) {
    for (auto &field : target.fields)
        if (field.first == key) {
            field.second = std::move(value);
            return;
        }
    target.fields.push_back({std::move(key), std::move(value)});
}

ServiceXmlValue scan(const std::vector<Event> &values, std::size_t &pos, unsigned depth = 0) {
    if (depth > 512)
        throw std::invalid_argument("Service XML nesting limit");
    auto result = ServiceXmlValue::object({});
    ++pos;
    while (pos < values.size() && !values[pos].end) {
        auto node = values[pos].node;
        if (node.type() != pugi::node_element) {
            ++pos;
            continue;
        }
        auto name = decoded_name(node.name());
        if (std::string_view(node.attribute("type").value()) == "List") {
            auto child = !values[pos].empty ? scan(values, pos, depth + 1) : ServiceXmlValue::object({});
            set(result, std::move(name), std::move(child));
            ++pos;
        } else {
            std::string text;
            if (!values[pos].empty) {
                ++pos;
                while (pos < values.size() && !values[pos].end) {
                    if (values[pos].node.type() == pugi::node_element)
                        throw std::invalid_argument("Nested scalar XML content");
                    text += values[pos++].node.value();
                }
                if (pos == values.size() || values[pos].node != node)
                    throw std::invalid_argument("Invalid XML content");
            }
            set(result, std::move(name), ServiceXmlValue(text));
            ++pos;
        }
    }
    return result;
}
} // namespace

static std::pair<std::size_t, std::string> xml_declaration(std::string_view bytes) {
    // The original XmlParserContext supplies UTF8NoBomEncoding. Unlike a
    // StreamReader, it neither detects UTF16 BOMs nor consumes a UTF8 BOM.
    const auto declaration_start = [](std::string_view text) {
        return text.starts_with("<?xml") && text.size() > 5 &&
               (text[5] == ' ' || text[5] == '\t' || text[5] == '\r' || text[5] == '\n' ||
                (text[5] == '?' && text.size() > 6 && text[6] == '>'));
    };
    if (bytes.starts_with("\xef\xbb\xbf") && declaration_start(bytes.substr(3)))
        throw std::invalid_argument("XML declaration follows a BOM character");
    std::string encoding = "utf-8";
    std::size_t body = 0;
    if (declaration_start(bytes)) {
        const auto end = bytes.find("?>");
        if (end == std::string_view::npos)
            throw std::invalid_argument("Unterminated XML declaration");
        body = end + 2;
        const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
        bool version = false, encoded = false, standalone = false;
        std::size_t cursor = 5;
        while (cursor < end) {
            const auto before_space = cursor;
            while (cursor < end && space(bytes[cursor]))
                ++cursor;
            if (cursor == end)
                break;
            if (cursor == before_space)
                throw std::invalid_argument("XML declaration attributes require whitespace");
            const auto name_start = cursor;
            while (cursor < end && !space(bytes[cursor]) && bytes[cursor] != '=')
                ++cursor;
            const auto name = bytes.substr(name_start, cursor - name_start);
            while (cursor < end && space(bytes[cursor]))
                ++cursor;
            if (cursor == end || bytes[cursor++] != '=')
                throw std::invalid_argument("Invalid XML declaration attribute");
            while (cursor < end && space(bytes[cursor]))
                ++cursor;
            if (cursor == end || (bytes[cursor] != '\'' && bytes[cursor] != '"'))
                throw std::invalid_argument("Unquoted XML declaration attribute");
            const auto quote = bytes[cursor++];
            const auto value_start = cursor;
            while (cursor < end && bytes[cursor] != quote)
                ++cursor;
            if (cursor == end)
                throw std::invalid_argument("Unterminated XML declaration attribute");
            const auto value = bytes.substr(value_start, cursor++ - value_start);
            if (!version && name == "version" && value == "1.0")
                version = true;
            else if (version && !encoded && !standalone && name == "encoding") {
                const auto letter = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
                if (value.empty() || !letter(value.front()) || !std::all_of(value.begin(), value.end(), [&](char c) {
                        return letter(c) || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
                    }))
                    throw std::invalid_argument("Invalid XML encoding name");
                encoding = value;
                encoded = true;
            } else if (version && !standalone && name == "standalone" && (value == "yes" || value == "no"))
                standalone = true;
            else
                throw std::invalid_argument("Invalid XML declaration attribute or order");
        }
        if (!version)
            throw std::invalid_argument("XML declaration requires version 1.0");
        std::transform(encoding.begin(), encoding.end(), encoding.begin(),
                       [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; });
    }
    return {body, std::move(encoding)};
}

static ServiceXmlValue parse_service_xml_value(std::string_view text) {
    (void)xml_declaration(text);
    if (text.size() > 50000000)
        throw std::invalid_argument("Service XML length limit");
    auto prefix = response_prefix(text);
    text = prefix;
    validate(text);
    pugi::xml_document doc;
    auto parsed = doc.load_buffer(
        text.data(), text.size(),
        pugi::parse_default | pugi::parse_fragment | pugi::parse_ws_pcdata | pugi::parse_doctype, pugi::encoding_utf8);
    if (!parsed)
        throw std::invalid_argument("Invalid service XML stream");
    std::vector<Event> values;
    events(doc, text, values);
    for (std::size_t pos = 0; pos < values.size(); ++pos)
        if (!values[pos].end && values[pos].node.type() == pugi::node_element &&
            std::string_view(values[pos].node.name()) == "ServerResponse")
            return scan(values, pos);
    return ServiceXmlValue::object({});
}

ServiceXmlValue parse_service_xml_stream(std::string_view bytes) {
    const auto [body, encoding] = xml_declaration(bytes);
    const auto input = bytes.substr(body);
    std::string decoded;
    if (encoding == "utf-8") {
        decoded = framework::utf8(framework::utf16_replacing(input));
    } else if (encoding == "iso-8859-1" || encoding == "latin1" || encoding == "us-ascii" || encoding == "ascii") {
        std::u16string characters;
        const bool ascii = encoding == "us-ascii" || encoding == "ascii";
        for (const unsigned char c : input)
            characters.push_back(ascii && c > 127 ? u'?' : static_cast<char16_t>(c));
        decoded = framework::utf8(characters);
    } else if (encoding == "utf-16be" || encoding == "utf-32") {
        // These explicit encodings can switch after an ASCII declaration.
        const auto bom = encoding == "utf-16be" ? std::string("\xfe\xff", 2) : std::string("\xff\xfe\0\0", 4);
        decoded = read_service_body(bom + std::string(input));
    } else {
        // Switching from the context's UTF8 decoder to UTF16LE requires a
        // byte-order mark; unsupported code pages likewise fail in XmlReader.
        throw std::invalid_argument("Unsupported XML stream encoding");
    }
    // Parsing is already decoded UTF8; do not let the XML library select a
    // different decoder from the original declaration a second time.
    return parse_service_xml_value(body ? std::string("<?xml version=\"1.0\" encoding=\"utf-8\"?>") + decoded
                                        : decoded);
}

ServiceXmlValue parse_service_xml(std::string_view text) {
    try {
        return parse_service_xml_value(text);
    } catch (const std::exception &) {
        return ServiceXmlValue::object({});
    }
}
} // namespace opensim::network
