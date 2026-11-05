#include "opensim/framework/nini_xml_encoder.hpp"
#include <iostream>
#include <sstream>
#include <vector>
using namespace opensim::framework;
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
int main(int argc, char**) {
    if (argc == 1) {
        std::u16string output;
        NativeXmlTextEncoder encoder([&](XmlEncoderWriteKind, std::optional<std::u16string_view> value) {
            if (value)
                output += *value;
        });
        encoder.start_attribute(true);
        encoder.write(u"<&\"\n");
        if (output != u"&lt;&amp;&quot;&#xA;" || encoder.attribute_value() != u"<&\"\n")
            throw std::runtime_error("XML encoder escaping/cache unit failed");
        encoder.end_attribute();
        if (!encoder.attribute_value().empty())
            throw std::runtime_error("XML encoder end attribute unit failed");
        output.clear();
        encoder.write_surrogate_entity(0xdc00, 0xd800);
        if (output != u"&#x10000;")
            throw std::runtime_error("XML encoder scalar unit failed");
        try {
            encoder.write_array(std::u16string_view(u"a"), -1, -1, true);
            throw std::runtime_error("XML encoder missing argument error");
        } catch (const XmlEncoderArrayError& e) {
            if (e.parameter != u"count" || e.actual_value != -1 || e.hresult != 0x80131502)
                throw;
        }
        // A writer callback can change the borrowed array after the next code unit
        // has been inspected. The source retains that inspected character.
        std::u16string array = u"a<b", written;
        bool changed = false;
        NativeXmlTextEncoder array_encoder(
            [&](XmlEncoderWriteKind kind, std::optional<std::u16string_view> value) {
                if (value)
                    written += *value;
                if (kind == XmlEncoderWriteKind::Array && !changed) {
                    array[1] = u'&';
                    changed = true;
                }
            });
        array_encoder.write_array(array, 0, 3);
        if (!changed || array != u"a&b" || written != u"a&lt;b")
            throw std::runtime_error("XML encoder cached array character unit failed");
        std::u16string borrowed = u"a\xd800";
        bool span_changed = false;
        NativeXmlTextEncoder span_encoder([&](XmlEncoderWriteKind, std::optional<std::u16string_view>) {
            borrowed[1] = u'&';
            span_changed = true;
        });
        try {
            span_encoder.write(borrowed);
            throw std::runtime_error("XML encoder missing borrowed-span error");
        } catch (const XmlEncoderError& e) {
            if (!span_changed || e.kind != XmlEncoderError::Kind::InvalidPair || e.high != 0xd800 ||
                e.low != u'&')
                throw;
        }
        return 0;
    }
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        int mode, attr, quote, fail;
        std::string payload;
        in >> mode >> attr >> quote >> fail >> payload;
        std::u16string s;
        if (payload != "-" && payload != "null")
            for (std::size_t i = 0; i < payload.size(); i += 4)
                s += static_cast<char16_t>(std::stoul(payload.substr(i, 4), nullptr, 16));
        int offset = 0, count = static_cast<int>(s.size());
        if (in >> offset)
            in >> count;
        int mutation_at = 0, mutation_index = 0, mutation_unit = 0;
        in >> mutation_at >> mutation_index >> mutation_unit;
        std::vector<std::string> calls;
        NativeXmlTextEncoder encoder([&](XmlEncoderWriteKind kind, std::optional<std::u16string_view> value) {
            calls.push_back(std::string(1, "CTSA"[static_cast<int>(kind)]) + ":" +
                            (value ? bytes(*value) : "null"));
            int number = static_cast<int>(calls.size());
            if (number == mutation_at)
                s[mutation_index] = static_cast<char16_t>(mutation_unit);
            if (number == fail)
                throw std::runtime_error("injected");
        });
        encoder.quote_char(static_cast<char16_t>(quote));
        if (attr)
            encoder.start_attribute(attr == 2);
        std::string error = "-";
        try {
            switch (mode) {
            case 0:
                encoder.write(s);
                break;
            case 1:
                encoder.write_raw_checked(s);
                break;
            case 2:
                encoder.write_char_entity(s.at(0));
                break;
            case 3:
                encoder.write_entity_ref(s);
                break;
            case 4:
                encoder.write_surrogate_entity(s.at(0), s.at(1));
                break;
            case 5:
                encoder.write_surrogate_char(s.at(0), s.at(1));
                break;
            case 8:
                encoder.write(s);
                break;
            case 6:
            case 7:
                encoder.write_array(payload == "null" ? std::nullopt : std::optional<std::u16string_view>(s),
                                    offset, count, mode == 7);
                break;
            }
        } catch (const XmlEncoderError& e) {
            error = std::string(e.source_type) + ":80070057:" + hex(e.message);
        } catch (const XmlEncoderArrayError& e) {
            std::ostringstream code;
            code << std::hex << e.hresult;
            error = e.source_type + ":" + code.str() + ":" + hex(e.message);
        } catch (const std::runtime_error&) {
            error = "System.IO.IOException:80131620:" + hex(u"injected writer failure");
        }
        for (std::size_t i = 0; i < calls.size(); ++i) {
            if (i)
                std::cout << '|';
            std::cout << calls[i];
        }
        std::cout << ';' << hex(encoder.attribute_value()) << ';' << error << '\n';
    }
}
