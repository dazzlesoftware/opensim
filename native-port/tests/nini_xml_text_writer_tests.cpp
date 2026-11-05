#include "opensim/framework/nini_xml_text_writer.hpp"
#include <iostream>
#include <sstream>
using namespace opensim::framework;
std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::istringstream in(text);
    std::string s;
    while (std::getline(in, s, separator))
        parts.push_back(s);
    return parts;
}
std::string hex(const XmlWriterText& s) {
    if (!s)
        return "~";
    if (s->empty())
        return "-";
    std::string out;
    for (auto c : *s)
        for (int shift = 12; shift >= 0; shift -= 4)
            out += "0123456789abcdef"[(c >> shift) & 15];
    return out;
}
XmlWriterText decode(const std::string& s) {
    if (s == "~")
        return {};
    std::u16string out;
    if (s != "-")
        for (std::size_t i = 0; i < s.size(); i += 4)
            out += static_cast<char16_t>(std::stoul(s.substr(i, 4), nullptr, 16));
    return out;
}
std::string error(const std::string& type, unsigned code, const std::u16string& message,
                  const XmlWriterText& parameter = {}, std::optional<int> actual = {}) {
    std::ostringstream h;
    h << std::hex << code;
    return type + ":" + h.str() + ":" + hex(message) + ":" + hex(parameter) + ":" +
           (actual ? std::to_string(*actual) : "~");
}
std::string current_error() {
    try {
        throw;
    } catch (const XmlTextWriterError& e) {
        return error(e.source_type, e.hresult, e.message, e.parameter);
    } catch (const XmlEncoderError& e) {
        return error(e.source_type, e.hresult, e.message);
    } catch (const XmlEncoderArrayError& e) {
        return error(e.source_type, e.hresult, e.message, e.parameter, e.actual_value);
    } catch (const std::runtime_error&) {
        return error("System.IO.IOException", 0x80131620, u"injected writer failure");
    }
}
std::string execute(NativeXmlTextWriter& w, XmlWriterText& encoding_name,
                    XmlWriterCharArrayHandle& last_array, const std::string& command) {
    auto p = split(command, ',');
    auto s = [&](int i) { return decode(p[i]); };
    auto n = [&](int i) { return std::stoi(p[i]); };
    auto op = p[0];
    if (op == "encname")
        encoding_name = s(1);
    else if (op == "mutate") {
        if (!last_array)
            throw XmlTextWriterError("System.NullReferenceException", 0x80004003,
                                     u"Object reference not set to an instance of an object.");
        (*last_array)[n(1)] = s(2)->at(0);
    } else if (op == "peek") {
        if (!last_array)
            throw XmlTextWriterError("System.NullReferenceException", 0x80004003,
                                     u"Object reference not set to an instance of an object.");
        return hex(std::u16string(1, (*last_array)[n(1)])) + ":" + std::to_string(last_array->size());
    } else if (op == "doc")
        w.write_start_document(n(1));
    else if (op == "enddoc")
        w.write_end_document();
    else if (op == "doctype")
        w.write_doctype(s(1), s(2), s(3), s(4));
    else if (op == "elem")
        w.write_start_element(s(1), s(2), s(3));
    else if (op == "end")
        w.write_end_element();
    else if (op == "full")
        w.write_full_end_element();
    else if (op == "attr")
        w.write_start_attribute(s(1), s(2), s(3));
    else if (op == "endattr")
        w.write_end_attribute();
    else if (op == "comment")
        w.write_comment(s(1));
    else if (op == "cdata")
        w.write_cdata(s(1));
    else if (op == "pi")
        w.write_processing_instruction(s(1), s(2));
    else if (op == "entity")
        w.write_entity_ref(s(1));
    else if (op == "char")
        w.write_char_entity(s(1)->at(0));
    else if (op == "surrogate") {
        auto value = s(1);
        w.write_surrogate_entity(value->at(0), value->at(1));
    } else if (op == "ws")
        w.write_whitespace(s(1));
    else if (op == "str")
        w.write_string(s(1));
    else if (op == "raw")
        w.write_raw(s(1));
    else if (op == "chars" || op == "rawchars") {
        auto value = s(1);
        std::optional<std::u16string_view> view;
        if (value)
            view = *value;
        w.write_chars(view, n(2), n(3), op == "rawchars");
    } else if (op == "base64" || op == "binhex") {
        std::vector<std::uint8_t> buffer;
        std::optional<std::span<const std::uint8_t>> view;
        if (p[1] != "~") {
            if (p[1] != "-")
                for (std::size_t i = 0; i < p[1].size(); i += 2)
                    buffer.push_back(static_cast<std::uint8_t>(std::stoul(p[1].substr(i, 2), nullptr, 16)));
            view = buffer;
        }
        if (op == "base64")
            w.write_base64(view, n(2), n(3));
        else
            w.write_binhex(view, n(2), n(3));
    } else if (op == "name")
        w.write_name(s(1));
    else if (op == "nmtoken")
        w.write_nmtoken(s(1));
    else if (op == "qualified")
        w.write_qualified_name(s(1), s(2));
    else if (op == "lookup")
        return hex(w.lookup_prefix(s(1)));
    else if (op == "ns")
        w.namespaces(n(1) != 0);
    else if (op == "fmt")
        w.formatting(n(1));
    else if (op == "indent")
        w.indentation(n(1));
    else if (op == "ic")
        w.indent_char(s(1)->at(0));
    else if (op == "quote")
        w.quote_char(s(1)->at(0));
    else if (op == "flush")
        w.flush();
    else if (op == "close")
        w.close();
    return "~";
}
std::string state(const NativeXmlTextWriter& w) {
    return std::to_string(w.write_state()) + "," + std::to_string(w.xml_space()) + "," + hex(w.xml_lang()) +
           "," + (w.namespaces() ? "1" : "0") + "," + std::to_string(w.formatting()) + "," +
           std::to_string(w.indentation()) + "," + std::to_string(w.indent_char()) + "," +
           std::to_string(w.quote_char());
}
int main(int argc, char**) {
    if (argc == 1) {
        std::u16string output;
        int flushes = 0, closes = 0;
        XmlTextWriterIo io;
        io.encoding = [] {
            auto encoding = std::make_shared<NativeXmlTextEncoding>();
            encoding->web_name = [] { return XmlWriterText(u"utf-16"); };
            return encoding;
        };
        io.write = [&](XmlEncoderWriteKind, std::optional<std::u16string_view> value) {
            if (value)
                output += *value;
        };
        io.write_array = [&](XmlWriterCharArrayHandle value, int index, int count) {
            output += value->view().substr(index, count);
        };
        io.write_whole_array = [&](XmlWriterCharArrayHandle value) { output += value->view(); };
        io.write_line = [&] { output += u"\n"; };
        io.flush = [&] { ++flushes; };
        io.dispose = [&] { ++closes; };
        NativeXmlTextWriter writer(io, true);
        writer.write_start_document();
        writer.write_start_element({}, u"root", {});
        writer.write_start_attribute({}, u"a", {});
        writer.write_string(u"<&\"");
        writer.write_end_attribute();
        writer.write_end_element();
        writer.flush();
        if (output != u"<?xml version=\"1.0\" encoding=\"utf-16\"?><root a=\"&lt;&amp;&quot;\" />" ||
            flushes != 1 || closes != 0 || writer.write_state() != 4)
            throw std::runtime_error("XML writer escaping/state/flush ownership unit failed");
        writer.close();
        if (closes != 1 || writer.write_state() != 5)
            throw std::runtime_error("XML writer close ownership unit failed");
        return 0;
    }
    std::string line;
    while (std::getline(std::cin, line)) {
        auto commands = split(line, ';'), header = split(commands[0], ',');
        int fail = std::stoi(header[0]), encoding_mode = header.size() > 2 ? std::stoi(header[2]) : 0,
            reenter = header.size() > 3 ? std::stoi(header[3]) : 0, start = header.size() > 3 ? 2 : 1,
            array_mode = header.size() > 4 ? std::stoi(header[4]) : 0;
        XmlWriterText encoding_name = u"native-x";
        XmlWriterCharArrayHandle last_array;
        std::vector<XmlWriterCharArrayHandle> arrays;
        std::unique_ptr<NativeXmlTextWriter> w;
        std::vector<std::string> calls, snapshots;
        auto emit = [&](const std::string& kind, const XmlWriterText& value = std::nullopt) {
            calls.push_back(kind + ":" + hex(value));
            int index = static_cast<int>(calls.size());
            if (index == reenter) {
                if (w)
                    execute(*w, encoding_name, last_array, commands[1]);
                else if (commands[1].starts_with("encname,"))
                    encoding_name = decode(split(commands[1], ',')[1]);
                else
                    throw XmlTextWriterError("System.NullReferenceException", 0x80004003,
                                             u"Object reference not set to an instance of an object.");
            }
            if (index == fail)
                throw std::runtime_error("injected");
        };
        auto remember = [&](XmlWriterCharArrayHandle value) {
            last_array = value;
            if (!value)
                return 0;
            for (std::size_t i = 0; i < arrays.size(); ++i)
                if (arrays[i] == value)
                    return static_cast<int>(i) + 1;
            arrays.push_back(value);
            return static_cast<int>(arrays.size());
        };
        auto range = [&](XmlWriterCharArrayHandle value, int index, int count) {
            int id = remember(value);
            if (array_mode == 2) {
                emit("Z" + std::to_string(id) + ":" + std::to_string(value->size()) + ":" +
                     std::to_string(index) + ":" + std::to_string(count));
                return;
            }
            if (count < 0) {
                auto v = std::to_string(count);
                std::u16string number(v.begin(), v.end());
#ifdef _WIN32
                constexpr auto newline = u"\r\n";
#else
                constexpr auto newline = u"\n";
#endif
                throw XmlEncoderArrayError("System.ArgumentOutOfRangeException", 0x80131502,
                                           u"length ('" + number +
                                               u"') must be a non-negative value. (Parameter 'length')" +
                                               newline + u"Actual value was " + number + u".",
                                           u"length", count);
            }
            auto contents = std::u16string(value->view().substr(index, count));
            emit(array_mode == 1 ? "A" + std::to_string(id) + ":" + std::to_string(value->size()) + ":" +
                                       std::to_string(index) + ":" + std::to_string(count)
                                 : "A",
                 contents);
        };
        XmlTextWriterIo io;
        io.write = [&](XmlEncoderWriteKind kind, std::optional<std::u16string_view> value) {
            XmlWriterText s;
            if (value)
                s = std::u16string(*value);
            emit(std::string(1, "CTSA"[static_cast<int>(kind)]), s);
        };
        io.write_line = [&] { emit("L"); };
        io.flush = [&] { emit("F"); };
        io.dispose = [&] { emit("X"); };
        io.encoding = [&]() -> std::shared_ptr<NativeXmlTextEncoding> {
            emit("G");
            if (encoding_mode == 1)
                return {};
            auto value = std::make_shared<NativeXmlTextEncoding>();
            value->web_name = [&]() -> XmlWriterText {
                if (encoding_mode == 2) {
                    emit("B");
                    return encoding_name;
                }
                return u"utf-16";
            };
            return value;
        };
        io.write_array = range;
        io.write_whole_array = [&](XmlWriterCharArrayHandle value) {
            if (array_mode != 1) {
                range(value, 0, static_cast<int>(value->size()));
                return;
            }
            int id = remember(value);
            emit("H" + std::to_string(id) + ":" + std::to_string(value->size()),
                 std::u16string(value->view()));
        };
        try {
            w = std::make_unique<NativeXmlTextWriter>(io, header[1] == "1");
        } catch (...) {
            snapshots.push_back("NEW@" + current_error());
        }
        if (w)
            for (std::size_t i = start; i < commands.size(); ++i) {
                std::string result = "~", exception = "-";
                try {
                    result = execute(*w, encoding_name, last_array, commands[i]);
                } catch (...) {
                    exception = current_error();
                }
                snapshots.push_back(state(*w) + "@" + result + "@" + exception);
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
