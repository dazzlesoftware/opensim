#include "opensim/framework/nini_xml_stream_writer.hpp"
#include "opensim/framework/nini_streams.hpp"
#include <iostream>
#include <sstream>
using namespace opensim::framework;
std::vector<std::string> split(const std::string& s, char c) {
    std::vector<std::string> r;
    std::istringstream in(s);
    std::string x;
    while (std::getline(in, x, c))
        r.push_back(x);
    return r;
}
std::string hex(std::u16string_view s) {
    if (s.empty())
        return "-";
    std::string r;
    for (auto c : s)
        for (int n = 12; n >= 0; n -= 4)
            r += "0123456789abcdef"[(c >> n) & 15];
    return r;
}
NiniWriterText decode(const std::string& s) {
    if (s == "~")
        return {};
    std::u16string r;
    if (s != "-")
        for (std::size_t i = 0; i < s.size(); i += 4)
            r += static_cast<char16_t>(std::stoul(s.substr(i, 4), nullptr, 16));
    return nini_writer_text(r);
}
std::string error() {
    try {
        throw;
    } catch (const NiniStreamError& e) {
        std::ostringstream out;
        out << std::hex << static_cast<unsigned>(e.hresult);
        auto typed = dynamic_cast<const NiniStreamWriterError*>(&e);
        return e.source_type + ":" + out.str() + ":" + hex(e.message) + ":" +
               (e.parameter ? hex(*e.parameter) : "~") + ":" +
               (typed && typed->actual_value ? std::to_string(*typed->actual_value) : "~");
    }
}
struct Sink : NiniByteStream {
    bool writable = true, seek = true, closed = false;
    long long offset = 0;
    int fail = 0;
    std::function<void()> hook;
    std::vector<std::string> calls;
    std::vector<std::uint8_t> bytes;
    void emit(std::string s) {
        calls.push_back(s);
        if (hook)
            hook();
        if (static_cast<int>(calls.size()) == fail)
            throw NiniStreamError("System.IO.IOException", static_cast<int>(0x80131620u),
                                  u"injected failure");
    }
    bool can_read() override {
        return false;
    }
    bool can_write() override {
        emit("CW");
        return writable;
    }
    bool can_seek() override {
        emit("CS");
        return seek;
    }
    std::int64_t position() override {
        emit("P");
        return offset;
    }
    std::size_t read(std::span<std::uint8_t>, std::size_t, std::size_t) override {
        throw std::logic_error("notreadable");
    }
    void write(std::span<const std::uint8_t> b) override {
        std::string text = "W:";
        for (auto c : b) {
            text += "0123456789abcdef"[c >> 4];
            text += "0123456789abcdef"[c & 15];
        }
        emit(text);
        if (closed)
            throw NiniStreamError("System.IO.IOException", static_cast<int>(0x80131620u), u"closed stream");
        bytes.insert(bytes.end(), b.begin(), b.end());
        offset += b.size();
    }
    void flush() override {
        emit("F");
        if (closed)
            throw NiniStreamError("System.IO.IOException", static_cast<int>(0x80131620u), u"closed stream");
    }
    void close() override {
        emit("X");
        closed = true;
    }
};

int main(int argc, char**) {
    if (argc == 1) {
        auto sink = std::make_shared<Sink>();
        NativeXmlStreamWriter writer(sink);
        writer.write(nini_writer_text(u"\u00e9"));
        if (!sink->bytes.empty())
            throw std::runtime_error("StreamWriter must buffer before flush");
        writer.flush();
        if (sink->bytes != std::vector<std::uint8_t>{0xc3, 0xa9} || sink->closed)
            throw std::runtime_error("UTF8 bytes/flush ownership unit");
        writer.close();
        try {
            writer.write_char(u'x');
            throw std::runtime_error("Missing disposed writer error");
        } catch (const NiniStreamError& e) {
            if (e.source_type != "System.ObjectDisposedException" || e.object_name != u"StreamWriter")
                throw;
        }
        try {
            writer.write_array(std::make_shared<XmlWriterCharArray>(u"a"), -1, 0);
            throw std::runtime_error("Missing array index validation");
        } catch (const NiniStreamWriterError& e) {
            if (e.actual_value != -1 || e.parameter != u"index" ||
                e.source_type != "System.ArgumentOutOfRangeException")
                throw;
        }
        auto sink2 = std::make_shared<Sink>();
        auto alias = nini_byte_writer_factory({}).stream_writer(sink2);
        auto shared = std::dynamic_pointer_cast<NativeXmlStreamWriter>(alias);
        if (!shared || shared->encoding() != native_xml_utf8_no_bom())
            throw std::runtime_error("INI factory must share source writer kernel");
        auto text = alias->to_string();
        if (text != alias->to_string() || *text != u"System.IO.StreamWriter")
            throw std::runtime_error("StreamWriter type-name identity unit");
        alias->close();
        if (native_xml_intrinsic_encoding(nini_writer_text(u"UTF-8")) !=
            native_xml_intrinsic_encoding(nini_writer_text(u"unicode-1-1-utf-8")))
            throw std::runtime_error("Encoding alias object identity unit");
        return 0;
    }
    std::string line;
    while (std::getline(std::cin, line)) {
        auto parts = split(line, ';'), h = split(parts[0], ',');
        auto sink = std::make_shared<Sink>();
        sink->writable = std::stoi(h[0]) != 0;
        sink->seek = std::stoi(h[1]) != 0;
        sink->offset = std::stoll(h[2]);
        sink->fail = std::stoi(h[3]);
        std::unique_ptr<NativeXmlStreamWriter> w;
        std::vector<std::string> results;
        if (h.size() > 7) {
            int trigger = std::stoi(h[6]);
            auto action = h[7];
            sink->hook = [&, trigger, action] {
                if (static_cast<int>(sink->calls.size()) != trigger)
                    return;
                if (!w)
                    throw NiniStreamError("System.NullReferenceException", static_cast<int>(0x80004003u),
                                          u"Object reference not set to an instance of an object.");
                if (action == "X")
                    w->close();
                else if (action == "F")
                    w->flush();
                else if (action == "AF0")
                    w->auto_flush(false);
                else if (action == "AF1")
                    w->auto_flush(true);
                else if (action == "S1")
                    w->write(nini_writer_text(u"z"));
                else if (action == "S128")
                    w->write(nini_writer_text(std::u16string(128, u'z')));
                else if (action == "S1024")
                    w->write(nini_writer_text(std::u16string(1024, u'z')));
                else if (action == "NL")
                    w->new_line(nini_writer_text(u"changed"));
            };
        }
        try {
            if (h.size() > 9 && h[9] == "1")
                w = std::make_unique<NiniByteTextWriter>(std::stoi(h[0]) < 0 ? nullptr : sink);
            else {
                auto encoding =
                    h.size() > 8 && h[8] != "#" ? native_xml_intrinsic_encoding(decode(h[8])) : nullptr;
                w = std::make_unique<NativeXmlStreamWriter>(std::stoi(h[0]) < 0 ? nullptr : sink, encoding,
                                                            std::stoi(h[4]), std::stoi(h[5]) != 0);
            }
        } catch (...) {
            results.push_back("NEW@" + error());
        }
        if (w)
            for (std::size_t i = 1; i < parts.size(); ++i) {
                auto c = split(parts[i], ',');
                std::string exception = "-", value = "~";
                try {
                    if (c[0] == "S")
                        w->write(decode(c[1]));
                    else if (c[0] == "C")
                        w->write_char((*decode(c[1]))[0]);
                    else if (c[0] == "A" || c[0] == "H") {
                        auto value = decode(c[1]);
                        XmlWriterCharArrayHandle a;
                        if (value)
                            a = std::make_shared<XmlWriterCharArray>(*value);
                        if (c[0] == "A")
                            w->write_array(a, std::stoi(c[2]), std::stoi(c[3]));
                        else
                            w->write_array(a);
                    } else if (c[0] == "P") {
                        auto value = decode(c[1]);
                        w->write_span(value ? std::u16string_view(*value) : std::u16string_view{});
                    } else if (c[0] == "L")
                        w->write_line();
                    else if (c[0] == "N")
                        w->new_line(decode(c[1]));
                    else if (c[0] == "AF")
                        w->auto_flush(std::stoi(c[1]) != 0);
                    else if (c[0] == "F")
                        w->flush();
                    else if (c[0] == "X")
                        w->close();
                    else if (c[0] == "M")
                        value = std::to_string(w->encoding()->max_byte_count(std::stoi(c[1])));
                    else if (c[0] == "W")
                        value = hex(*w->encoding()->xml_encoding->web_name());
                    else if (c[0] == "B") {
                        auto b = w->encoding()->preamble();
                        value = b.empty() ? "-" : "";
                        for (auto c : b) {
                            value += "0123456789abcdef"[c >> 4];
                            value += "0123456789abcdef"[c & 15];
                        }
                    } else if (c[0] == "R")
                        value = w->encoding() == native_xml_intrinsic_encoding(decode(c[1])) ? "1" : "0";
                } catch (...) {
                    exception = error();
                }
                results.push_back(exception + "@" + (w->auto_flush() ? "1" : "0") + "@" +
                                  hex(*w->new_line()) + "@" + value);
            }
        for (std::size_t i = 0; i < sink->calls.size(); ++i) {
            if (i)
                std::cout << '|';
            std::cout << sink->calls[i];
        }
        std::cout << ';';
        for (std::size_t i = 0; i < results.size(); ++i) {
            if (i)
                std::cout << ';';
            std::cout << results[i];
        }
        std::cout << ';' << sink->closed << '\n';
    }
    return 0;
}
