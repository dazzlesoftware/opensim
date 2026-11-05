#include "opensim/framework/nini_xml_encoder.hpp"
#include <iostream>
#include <sstream>
#include <vector>
using namespace opensim::framework;
std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::istringstream in(text);
    std::string s;
    while (std::getline(in, s, separator))
        parts.push_back(s);
    return parts;
}
std::string hex(std::u16string_view s) {
    if (s.empty())
        return "-";
    std::string out;
    for (auto c : s)
        for (int shift = 12; shift >= 0; shift -= 4)
            out += "0123456789abcdef"[(c >> shift) & 15];
    return out;
}
std::string bytes(std::u16string_view s) {
    std::string out;
    for (auto c : s)
        for (int n : {0, 8}) {
            auto b = (c >> n) & 255;
            out += "0123456789ABCDEF"[b >> 4];
            out += "0123456789ABCDEF"[b & 15];
        }
    return out;
}
std::optional<std::u16string> decode(const std::string& s) {
    if (s == "~")
        return {};
    std::u16string out;
    if (s != "-")
        for (std::size_t i = 0; i < s.size(); i += 4)
            out += static_cast<char16_t>(std::stoul(s.substr(i, 4), nullptr, 16));
    return out;
}
void execute(NativeXmlTextEncoder& encoder, const std::string& command) {
    auto p = split(command, ',');
    auto s = p.size() > 1 ? decode(p[1]) : std::nullopt;
    std::optional<std::u16string_view> view;
    if (s)
        view = *s;
    switch (p[0][0]) {
    case 'A':
        encoder.start_attribute(true);
        break;
    case 'B':
        encoder.start_attribute(false);
        break;
    case 'E':
        encoder.end_attribute();
        break;
    case 'Q':
        encoder.quote_char(s->at(0));
        break;
    case 'W':
        encoder.write(view.value_or(u""));
        break;
    case 'R':
        encoder.write_raw_checked(view);
        break;
    case 'C':
        encoder.write_char_entity(s->at(0));
        break;
    case 'N':
        encoder.write_entity_ref(view);
        break;
    case 'S':
        encoder.write_surrogate_entity(s->at(0), s->at(1));
        break;
    case 'D':
        encoder.write_surrogate_char(s->at(0), s->at(1));
        break;
    case 'V':
    case 'T':
        encoder.write_array(view, std::stoi(p[2]), std::stoi(p[3]), p[0][0] == 'T');
        break;
    }
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        auto commands = split(line, ';');
        auto header = split(commands[0], ',');
        int fail = std::stoi(header[0]), reenter = std::stoi(header[1]);
        std::vector<std::string> calls, snapshots;
        NativeXmlTextEncoder* encoder_ptr = nullptr;
        NativeXmlTextEncoder encoder([&](XmlEncoderWriteKind kind, std::optional<std::u16string_view> value) {
            calls.push_back(std::string(1, "CTSA"[static_cast<int>(kind)]) + ":" +
                            (value ? bytes(*value) : "null"));
            int index = static_cast<int>(calls.size());
            if (index == reenter)
                execute(*encoder_ptr, commands[1]);
            if (index == fail)
                throw std::runtime_error("injected");
        });
        encoder_ptr = &encoder;
        for (std::size_t i = 2; i < commands.size(); ++i) {
            std::string error = "-";
            try {
                execute(encoder, commands[i]);
            } catch (const XmlEncoderError& e) {
                error = std::string(e.source_type) + ":80070057:" + hex(e.message) + ":~:~";
            } catch (const XmlEncoderArrayError& e) {
                std::ostringstream code;
                code << std::hex << e.hresult;
                error = e.source_type + ":" + code.str() + ":" + hex(e.message) + ":" + hex(e.parameter) +
                        ":" + (e.actual_value ? std::to_string(*e.actual_value) : "~");
            } catch (const std::runtime_error&) {
                error = "System.IO.IOException:80131620:" + hex(u"injected writer failure") + ":~:~";
            }
            snapshots.push_back(hex(encoder.attribute_value()) + "@" + error);
        }
        for (std::size_t i = 0; i < calls.size(); ++i) {
            if (i)
                std::cout << '|';
            std::cout << calls[i];
        }
        std::cout << ';';
        for (std::size_t i = 0; i < snapshots.size(); ++i) {
            if (i)
                std::cout << ';';
            std::cout << snapshots[i];
        }
        std::cout << '\n';
    }
}
