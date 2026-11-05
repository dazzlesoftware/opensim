// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/nini_writer.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <iostream>
#include <sstream>
#include <vector>
using namespace opensim::framework;
namespace {
NiniWriterText decode(const std::string& token) {
    if (token == "-")
        return {};
    std::u16string value;
    if (token != ".")
        for (std::size_t i = 0; i < token.size(); i += 4) {
            auto lo = std::stoul(token.substr(i, 2), nullptr, 16),
                 hi = std::stoul(token.substr(i + 2, 2), nullptr, 16);
            value.push_back(static_cast<char16_t>(lo | (hi << 8)));
        }
    return nini_writer_text(value);
}
std::string hex(const std::u16string& value) {
    if (value.empty())
        return ".";
    std::string out;
    for (auto ch : value)
        for (unsigned char c : {static_cast<unsigned char>(ch & 255), static_cast<unsigned char>(ch >> 8)}) {
            out += "0123456789abcdef"[c >> 4];
            out += "0123456789abcdef"[c & 15];
        }
    return out;
}
std::string hex(const NiniWriterText& value) {
    return value ? hex(*value) : "-";
}
std::u16string value(const NiniWriterText& text) {
    return text ? *text : std::u16string{};
}
std::vector<std::string> split(const std::string& text, char delimiter) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (;;) {
        auto end = text.find(delimiter, start);
        out.push_back(text.substr(start, end == text.npos ? text.npos : end - start));
        if (end == text.npos)
            return out;
        start = end + 1;
    }
}
std::string error(const NiniWriterError& e) {
    return std::string(e.source_type()) + ":" + std::to_string(std::bit_cast<std::uint32_t>(e.hresult())) +
           ":" + hex(e.message) + ":" + hex(nini_writer_text(e.parameter));
}
struct Probe;
struct Stream final : NiniWriterStream {
    Probe& p;
    bool closed = false;
    explicit Stream(Probe& owner) : p(owner) {
    }
    void close() override;
};
struct Sink {
    Probe& p;
    bool closed = false;
    NiniWriterText returned = nini_writer_text(u"writer"), newline;
    std::u16string buffer;
    std::vector<NiniWriterText> seen;
    explicit Sink(Probe&);
    void write(const NiniWriterText&);
    void flush();
    void close();
    NiniWriterText to_string();
    void line(NiniWriterText);
    NiniWriterText line();
};
struct Writer final : NiniTextWriter {
    Sink sink;
    explicit Writer(Probe& p) : sink(p) {
    }
    void write(const NiniWriterText& t) override {
        sink.write(t);
    }
    void flush() override {
        sink.flush();
    }
    void close() override {
        sink.close();
    }
    NiniWriterText to_string() override {
        return sink.to_string();
    }
};
struct StreamWriter final : NiniStreamWriter {
    Sink sink;
    StreamWriter(Probe& p, std::shared_ptr<Stream> stream) : NiniStreamWriter(stream), sink(p) {
    }
    void write(const NiniWriterText& t) override {
        sink.write(t);
    }
    void flush() override {
        sink.flush();
    }
    void close() override {
        sink.close();
    }
    NiniWriterText to_string() override {
        return sink.to_string();
    }
};
struct Exposed final : NiniWriter {
    using NiniWriter::dispose;
    Probe& p;
    Exposed(Probe& owner, std::shared_ptr<NiniTextWriter> writer) : NiniWriter(std::move(writer)), p(owner) {
    }

  protected:
    void dispose(bool) override;
};
struct Probe {
    int point = 0, nth = 1, hit = 0, override_mode = 0;
    std::vector<std::string> trace;
    std::shared_ptr<Stream> stream;
    std::shared_ptr<NiniTextWriter> backend;
    std::shared_ptr<NiniStringWriter> real;
    std::shared_ptr<std::u16string> external;
    Sink* sink = nullptr;
    std::unique_ptr<Exposed> writer;
    std::string saved = "-";
    void event(int p, const std::string& text) {
        trace.push_back(text);
        if (point == p && ++hit == nth)
            throw NiniWriterError(NiniWriterError::Kind::Other, u"fault");
    }
    std::string snapshot() {
        auto buffer = real ? *real->buffer() : sink ? sink->buffer : std::u16string{};
        return std::to_string(static_cast<int>(writer->state())) + "," +
               std::to_string(writer->indentation()) + "," + (writer->use_value_quotes() ? "1" : "0") + "," +
               std::to_string(writer->comment_delimiter()) + "," +
               std::to_string(writer->assign_delimiter()) + "," + (writer->disposed() ? "1" : "0") + "," +
               (writer->base_stream() == stream && stream ? "1" : "0") + "," +
               (sink ? (sink->closed ? "1" : "0") : "-") + "," +
               (stream ? (stream->closed ? "1" : "0") : "-") + "," + hex(buffer) + "," +
               (real ? (real->buffer() == external ? "1" : "0") : "-") + "," +
               (sink   ? hex(sink->newline)
                : real ? hex(real->new_line())
                       : "-");
    }
    std::string run(const std::string& line) {
        std::istringstream in(line);
        int kind;
        std::string ops;
        in >> kind >> point >> nth >> override_mode >> ops;
        if (kind == 0) {
            auto w = std::make_shared<Writer>(*this);
            sink = &w->sink;
            backend = w;
        } else if (kind == 1) {
            stream = std::make_shared<Stream>(*this);
            auto w = std::make_shared<StreamWriter>(*this, stream);
            sink = &w->sink;
            backend = w;
        } else if (kind == 2) {
            external = std::make_shared<std::u16string>(u"seed");
            real = std::make_shared<NiniStringWriter>(external);
            backend = real;
        } else if (kind == 4) {
            real = std::make_shared<NiniStringWriter>();
            external = real->buffer();
            backend = real;
        }
        writer = std::make_unique<Exposed>(*this, backend);
        std::string output;
        for (auto& token : split(ops, ',')) {
            auto parts = split(token, ':');
            auto op = parts[0];
            std::string result = op;
            try {
                if (op == "s")
                    writer->write_section(decode(parts.at(1)));
                else if (op == "sc")
                    writer->write_section(decode(parts.at(1)), decode(parts.at(2)));
                else if (op == "k")
                    writer->write_key(decode(parts.at(1)), decode(parts.at(2)));
                else if (op == "kc")
                    writer->write_key(decode(parts.at(1)), decode(parts.at(2)), decode(parts.at(3)));
                else if (op == "e")
                    writer->write_empty();
                else if (op == "ec")
                    writer->write_empty(decode(parts.at(1)));
                else if (op == "i")
                    writer->indentation(std::stoi(parts.at(1)));
                else if (op == "v")
                    writer->use_value_quotes(parts.at(1) == "1");
                else if (op == "a")
                    writer->assign_delimiter(static_cast<char16_t>(std::stoul(parts.at(1))));
                else if (op == "m")
                    writer->comment_delimiter(static_cast<char16_t>(std::stoul(parts.at(1))));
                else if (op == "c")
                    writer->close();
                else if (op == "f")
                    writer->flush();
                else if (op == "d")
                    writer->dispose();
                else if (op == "z")
                    writer->finalize();
                else if (op == "h")
                    override_mode = std::stoi(parts.at(1));
                else if (op == "t") {
                    auto text = writer->to_string();
                    result = "T:" + hex(text) + "@" + (sink ? (text == sink->returned ? "R" : "N") : "N");
                } else if (op == "ret") {
                    if (sink)
                        sink->returned = decode(parts.at(1));
                } else if (op == "nl") {
                    auto text = decode(parts.at(1));
                    if (sink)
                        sink->line(text);
                    else if (real)
                        real->new_line(text);
                } else if (op == "ng") {
                    result = "N:" + (sink ? hex(sink->line()) : real ? hex(real->new_line()) : "-");
                } else if (op == "b") {
                    if (external)
                        external->append(value(decode(parts.at(1))));
                } else if (op == "bx") {
                    if (external)
                        external->clear();
                } else if (op == "bs") {
                    external = std::make_shared<std::u16string>(value(decode(parts.at(1))));
                } else if (op == "bc") {
                    if (backend)
                        backend->close();
                } else if (op == "bw") {
                    if (backend)
                        backend->write(decode(parts.at(1)));
                } else if (op == "sf") {
                    if (stream)
                        stream->close();
                } else if (op == "q")
                    result = saved;
                else
                    throw std::runtime_error("operation");
            } catch (const NiniWriterError& e) {
                saved = error(e);
                result = "EX:" + saved;
            }
            if (!output.empty())
                output += '|';
            output += result + "/" + snapshot() + "/" + saved;
        }
        output += "/IO:";
        for (std::size_t i = 0; i < trace.size(); ++i) {
            if (i)
                output += ';';
            output += trace[i];
        }
        return output;
    }
};
void Stream::close() {
    p.event(5, "S");
    closed = true;
}
Sink::Sink(Probe& owner) : p(owner) {
#ifdef _WIN32
    newline = nini_writer_text(u"\r\n");
#else
    newline = nini_writer_text(u"\n");
#endif
}
void Sink::write(const NiniWriterText& text) {
    auto pos = std::find(seen.begin(), seen.end(), text);
    std::size_t id;
    if (pos == seen.end()) {
        seen.push_back(text);
        id = seen.size();
    } else
        id = static_cast<std::size_t>(pos - seen.begin()) + 1;
    p.event(1, "W" + std::to_string(id) + ":" + hex(text));
    if (closed)
        throw NiniWriterError(NiniWriterError::Kind::DisposedWriter, u"Cannot write to a closed TextWriter.");
    if (text)
        buffer += *text;
}
void Sink::flush() {
    p.event(2, "F");
}
void Sink::close() {
    p.event(3, "C");
    closed = true;
}
NiniWriterText Sink::to_string() {
    p.event(4, "T");
    return returned;
}
void Sink::line(NiniWriterText text) {
    p.event(8, "NS:" + hex(text));
#ifdef _WIN32
    newline = text ? text : nini_writer_text(u"\r\n");
#else
    newline = text ? text : nini_writer_text(u"\n");
#endif
}
NiniWriterText Sink::line() {
    p.event(7, "NG");
    return newline;
}
void Exposed::dispose(bool disposing) {
    p.event(6, disposing ? "H1" : "H0");
    if (p.override_mode != 1)
        NiniWriter::dispose(disposing);
    if (p.override_mode == 2)
        throw NiniWriterError(NiniWriterError::Kind::Other, u"after");
}
void constructors() {
    Probe p;
    auto stream = std::make_shared<Stream>(p);
    auto path = nini_writer_text(u"borrowed-path");
    std::vector<int> calls;
    NiniWriterFactory factory;
    factory.open_file = [&](const NiniWriterText& received, std::int32_t mode, std::int32_t access,
                            std::int32_t share) {
        if (received != path || mode != 2 || access != 2 || share != 0)
            throw std::runtime_error("FileStream argument identity/options");
        calls.push_back(1);
        return stream;
    };
    factory.stream_writer = [&](std::shared_ptr<NiniWriterStream> received) {
        if (received != stream)
            throw std::runtime_error("Stream constructor identity");
        calls.push_back(2);
        return std::make_shared<StreamWriter>(p, stream);
    };
    {
        NiniWriter w(path, factory);
        if (calls != std::vector<int>{1, 2} || w.base_stream() != stream)
            throw std::runtime_error("File/stream constructor delegation");
        w.dispose();
        if (p.trace != std::vector<std::string>{"C", "S"})
            throw std::runtime_error("Captured stream disposal order");
    }
    calls.clear();
    {
        NiniWriter w(stream, factory);
        if (calls != std::vector<int>{2} || w.base_stream() != stream)
            throw std::runtime_error("Direct stream constructor");
        w.dispose();
    }
    factory.open_file = [&](const NiniWriterText&, std::int32_t, std::int32_t,
                            std::int32_t) -> std::shared_ptr<NiniWriterStream> {
        calls.push_back(3);
        throw NiniWriterError(NiniWriterError::Kind::Other, u"open fault");
    };
    calls.clear();
    try {
        NiniWriter w(path, factory);
        throw std::runtime_error("Missing file fault");
    } catch (const NiniWriterError& e) {
        if (e.message != u"open fault" || calls != std::vector<int>{3})
            throw;
    }
    factory.stream_writer =
        [&](std::shared_ptr<NiniWriterStream> received) -> std::shared_ptr<NiniStreamWriter> {
        if (received)
            throw std::runtime_error("Null stream changed");
        throw NiniWriterError(NiniWriterError::Kind::Other, u"stream fault");
    };
    try {
        NiniWriter w(std::shared_ptr<NiniWriterStream>{}, factory);
        throw std::runtime_error("Missing stream fault");
    } catch (const NiniWriterError& e) {
        if (e.message != u"stream fault")
            throw;
    }
    try {
        NiniStringWriter w(std::shared_ptr<std::u16string>{});
        throw std::runtime_error("Missing null StringBuilder fault");
    } catch (const NiniWriterError& e) {
        if (e.kind != NiniWriterError::Kind::ArgumentNull || e.parameter != u"sb")
            throw;
    }
    auto backend = std::make_shared<NiniStringWriter>();
    {
        NiniWriter first(backend);
        NiniWriter alias = first;
        alias.write_section(path);
        if (first.state() != NiniWriteState::Section)
            throw std::runtime_error("Managed alias state");
        first.dispose();
        if (!alias.disposed())
            throw std::runtime_error("Managed alias disposal");
    }
}

}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--probe") {
            std::string line;
            while (std::getline(std::cin, line)) {
                Probe p;
                std::cout << p.run(line) << '\n';
            }
            return 0;
        }
        constructors();
        auto buffer = std::make_shared<std::u16string>();
        auto sink = std::make_shared<NiniStringWriter>(buffer);
        NiniWriter writer(sink);
        writer.write_section(nini_writer_text(u"section"));
        writer.write_key(nini_writer_text(u"key"), nini_writer_text(u"a\nb\rc"));
        writer.dispose();
        if (*buffer != u"[section]\r\nkey = ab\rc\r\n" || writer.state() != NiniWriteState::Section)
            throw std::runtime_error("Nini writer output/state");
        buffer->append(u"tail");
        if (*writer.to_string() != *buffer)
            throw std::runtime_error("Borrowed StringBuilder mutation");
        std::cout << "PASS: Nini writer raw UTF16/state/borrowed string buffer\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
