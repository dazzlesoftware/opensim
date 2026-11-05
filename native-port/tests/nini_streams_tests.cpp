#include "opensim/framework/nini_streams.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <chrono>
using namespace opensim::framework;
namespace {
std::string hexbytes(std::span<const std::uint8_t> b) {
    if (b.empty())
        return ".";
    std::string o;
    for (auto c : b) {
        o += "0123456789abcdef"[c >> 4];
        o += "0123456789abcdef"[c & 15];
    }
    return o;
}
std::vector<std::uint8_t> bytes(const std::string& s) {
    std::vector<std::uint8_t> b;
    if (s != ".")
        for (std::size_t i = 0; i < s.size(); i += 2)
            b.push_back(static_cast<std::uint8_t>(std::stoul(s.substr(i, 2), nullptr, 16)));
    return b;
}
NiniWriterText dec(const std::string& s) {
    if (s == "-")
        return {};
    auto b = bytes(s);
    std::u16string t;
    for (std::size_t i = 0; i < b.size(); i += 2)
        t += static_cast<char16_t>(b[i] | (std::uint32_t(b[i + 1]) << 8));
    return nini_writer_text(t);
}
std::string hex(const std::u16string& t) {
    std::vector<std::uint8_t> b;
    for (auto c : t) {
        b.push_back(static_cast<std::uint8_t>(c));
        b.push_back(static_cast<std::uint8_t>(c >> 8));
    }
    return hexbytes(b);
}
std::vector<std::string> split(const std::string& s, char c) {
    std::vector<std::string> out;
    std::size_t p = 0;
    for (;;) {
        auto e = s.find(c, p);
        out.push_back(s.substr(p, e == s.npos ? s.npos : e - p));
        if (e == s.npos)
            return out;
        p = e + 1;
    }
}
struct Stream : NiniByteStream {
    int chunk = 1024, point = 0, nth = 1, hit = 0;
    bool seek = true, readable = true, writable = true, closed = false;
    std::size_t pos = 0;
    std::vector<std::uint8_t> data;
    std::vector<std::string> trace;
    void event(int p, std::string s) {
        trace.push_back(std::move(s));
        if (point == p && ++hit == nth)
            throw NiniStreamError("System.Exception", std::bit_cast<std::int32_t>(0x80131500u), u"fault");
    }
    bool can_read() override {
        event(5, "CR");
        return readable && !closed;
    }
    bool can_write() override {
        event(6, "CW");
        return writable && !closed;
    }
    bool can_seek() override {
        event(7, "CS");
        return seek && !closed;
    }
    std::int64_t position() override {
        event(8, "G");
        return static_cast<std::int64_t>(pos);
    }
    std::size_t read(std::span<std::uint8_t> b, std::size_t offset, std::size_t count) override {
        event(1, "R:" + std::to_string(offset) + ":" + std::to_string(count));
        auto n =
            std::min({count, static_cast<std::size_t>(chunk), pos < data.size() ? data.size() - pos : 0});
        std::copy_n(data.begin() + std::min(pos, data.size()), n, b.begin() + offset);
        pos += n;
        return n;
    }
    void write(std::span<const std::uint8_t> b) override {
        event(2, "W:" + hexbytes(b));
        auto n = b.size();
        bool partial = point == 9 && ++hit == nth;
        if (partial)
            n /= 2;
        if (pos + n > data.size())
            data.resize(pos + n);
        std::copy_n(b.begin(), n, data.begin() + pos);
        pos += n;
        if (partial)
            throw NiniStreamError("System.Exception", std::bit_cast<std::int32_t>(0x80131500u), u"partial");
    }
    void flush() override {
        event(3, "F");
    }
    void close() override {
        event(4, "C");
        closed = true;
    }
};
std::string error(const NiniStreamError& e) {
    return e.source_type + ":" + std::to_string(std::bit_cast<std::uint32_t>(e.hresult)) + ":" +
           hex(e.message) + ":" + (e.parameter ? hex(*e.parameter) : "-") +
           (e.character_index
                ? ":" + std::to_string(*e.character_index) + ":" + std::to_string(*e.unknown_character)
                : "") +
           (e.source_type == "System.IO.FileNotFoundException"
                ? ":FN:" + (e.file_name ? hex(*e.file_name) : "-")
                : "") +
           (e.object_name ? ":ON:" + hex(*e.object_name) : "");
}
std::string run(const std::string& line) {
    std::istringstream in(line);
    int kind, flags;
    std::string input, ops;
    auto stream = std::make_shared<Stream>();
    in >> kind >> input >> stream->chunk >> stream->point >> stream->nth >> flags >> stream->pos >> ops;
    stream->seek = flags & 1;
    stream->readable = flags & 2;
    stream->writable = flags & 4;
    stream->data = bytes(input);
    std::shared_ptr<NiniByteTextReader> reader;
    std::shared_ptr<NiniByteTextWriter> writer;
    std::string out;
    auto snap = [&] {
        return std::to_string(stream->pos) + ":" + (stream->closed ? "1" : "0") + ":" +
               hexbytes(stream->data) + ":" +
               (reader ? std::to_string(static_cast<int>(reader->current_encoding())) : "-");
    };
    try {
        if (kind == 0)
            reader = std::make_shared<NiniByteTextReader>(stream);
        else
            writer = std::make_shared<NiniByteTextWriter>(stream);
    } catch (const NiniStreamError& e) {
        out = "CTOR:" + error(e) + "/" + snap();
    }
    if (out.empty())
        for (auto& token : split(ops, ',')) {
            auto a = split(token, ':');
            std::string r = a[0];
            try {
                if (a[0] == "p")
                    r = "P:" + std::to_string(reader->peek());
                else if (a[0] == "r")
                    r = "R:" + std::to_string(reader->read());
                else if (a[0] == "all") {
                    std::u16string text;
                    for (int c; (c = reader->read()) != -1;)
                        text += static_cast<char16_t>(c);
                    r = "A:" + hex(text);
                } else if (a[0] == "w")
                    writer->write(dec(a.at(1)));
                else if (a[0] == "f")
                    writer->flush();
                else if (a[0] == "c") {
                    if (reader)
                        reader->close();
                    else
                        writer->close();
                } else if (a[0] == "t")
                    r = "T:" + hex(*writer->to_string());
                else if (a[0] == "pos")
                    stream->pos = std::stoull(a.at(1));
                else if (a[0] == "append") {
                    auto b = bytes(a.at(1));
                    stream->data.insert(stream->data.end(), b.begin(), b.end());
                } else
                    throw std::runtime_error("operation");
            } catch (const NiniStreamError& e) {
                r = "EX:" + error(e);
            }
            if (!out.empty())
                out += '|';
            out += r + "/" + snap();
        }
    out += "/IO:";
    for (std::size_t i = 0; i < stream->trace.size(); ++i) {
        if (i)
            out += ';';
        out += stream->trace[i];
    }
    return out;
}
std::string filerun(const std::string& row) {
    std::istringstream in(row);
    int kind;
    std::string encoded, ops;
    in >> kind >> encoded >> ops;
    auto path = dec(encoded);
    auto provider = native_nini_file_stream_provider();
    std::shared_ptr<NiniByteStream> stream;
    std::shared_ptr<NiniTextReader> reader;
    std::shared_ptr<NiniTextWriter> writer;
    std::vector<std::shared_ptr<NiniByteStream>> held;
    std::string output;
    auto state = [&] {
        std::string s;
        try {
            s = stream ? std::to_string(stream->position()) : "-";
        } catch (const NiniStreamError&) {
            s = "unavailable";
        }
        return s;
    };
    try {
        if (kind == 0) {
            stream = provider(path, 3, 1, 1, 4096, 0);
            reader = std::make_shared<NiniByteTextReader>(stream);
        } else if (kind == 1) {
            stream = provider(path, 2, 2, 0, 4096, 0);
            writer = std::make_shared<NiniByteTextWriter>(stream);
        } else if (kind == 2) {
            stream = provider(path, 2, 2, 1, 4096, 0);
            writer = std::make_shared<NiniByteTextWriter>(stream);
        } else
            throw std::runtime_error("filekind");
    } catch (const NiniStreamError& e) {
        output = "CTOR:" + error(e);
    }
    if (output.empty())
        for (auto& token : split(ops, ',')) {
            auto a = split(token, ':');
            std::string r = a[0];
            try {
                if (a[0] == "w")
                    writer->write(dec(a.at(1)));
                else if (a[0] == "f")
                    writer->flush();
                else if (a[0] == "c") {
                    if (reader)
                        reader->close();
                    else
                        writer->close();
                } else if (a[0] == "p")
                    r = "P:" + std::to_string(reader->peek());
                else if (a[0] == "r")
                    r = "R:" + std::to_string(reader->read());
                else if (a[0] == "all") {
                    std::u16string text;
                    for (int c; (c = reader->read()) != -1;)
                        text += static_cast<char16_t>(c);
                    r = "A:" + hex(text);
                } else if (a[0] == "openr")
                    held.push_back(provider(path, 3, 1, 1, 4096, 0));
                else if (a[0] == "openw")
                    held.push_back(provider(path, 2, 2, 1, 4096, 0));
                else if (a[0] == "inspect") {
                    auto inspector = provider(path, 3, 1, 3, 4096, 0);
                    std::array<std::uint8_t, 4096> b{};
                    std::vector<std::uint8_t> data;
                    for (;;) {
                        auto n = inspector->read(b, 0, b.size());
                        if (!n)
                            break;
                        data.insert(data.end(), b.begin(), b.begin() + n);
                    }
                    inspector->close();
                    r = "B:" + hexbytes(data);
                } else if (a[0] == "raw") {
                    auto data = bytes(a.at(1));
                    stream->write(data);
                } else if (a[0] == "sf")
                    stream->flush();
                else if (a[0] == "sc")
                    stream->close();
                else if (a[0] == "t")
                    r = "T:" + hex(*writer->to_string());
                else
                    throw std::runtime_error("fileop");
            } catch (const NiniStreamError& e) {
                r = "EX:" + error(e);
            }
            if (!output.empty())
                output += '|';
            output += r + "/" + state();
        }
    if (writer)
        try {
            writer->close();
        } catch (...) {
        }
    if (reader)
        try {
            reader->close();
        } catch (...) {
        }
    return output;
}

std::string inifile(const std::string& row) {
    std::istringstream in(row);
    int kind, ft;
    std::string encoded, payload;
    in >> kind >> encoded >> ft >> payload;
    auto path = dec(encoded), value = dec(payload);
    try {
        if (kind == 0) {
            NiniWriter w(path, native_nini_writer_factory());
            w.write_section(nini_writer_text(u"配置😀"));
            w.write_key(nini_writer_text(u"key"), value, nini_writer_text(u"comment"));
            w.close();
            return "OK:" + std::to_string(static_cast<int>(w.state()));
        }
        if (kind == 1) {
            NiniDocument d;
            d.file_type(static_cast<NiniFileType>(ft));
            auto section = std::make_shared<NiniSection>(nini_writer_text(u"配置😀"),
                                                         nini_writer_text(u"section comment"));
            section->set(nini_writer_text(u"key"), value, nini_writer_text(u"comment"));
            section->set(nini_writer_text(u"empty comment"));
            d.sections().add(section);
            d.save(path, native_nini_document_factory());
            d.finalize_retired_writers();
            return "OK";
        }
        NiniDocument d(path, native_nini_document_factory(), static_cast<NiniFileType>(ft));
        auto output = std::make_shared<NiniStringWriter>();
        d.save(output);
        return "TEXT:" + hex(*output->to_string());
    } catch (const NiniStreamError& e) {
        return "EX:" + error(e);
    } catch (const NiniDocumentError& e) {
        return "EX:" + std::string(e.source_type()) + ":" +
               std::to_string(std::bit_cast<std::uint32_t>(e.hresult())) + ":" + hex(e.message) + ":" +
               (e.parameter ? hex(*e.parameter) : "-");
    } catch (const NiniWriterError& e) {
        return "EX:" + std::string(e.source_type()) + ":" +
               std::to_string(std::bit_cast<std::uint32_t>(e.hresult())) + ":" + hex(e.message) + ":" +
               (e.parameter ? hex(*e.parameter) : "-");
    } catch (const NiniReaderError& e) {
        return "EX:" + std::string(e.source_type()) + ":" +
               std::to_string(std::bit_cast<std::uint32_t>(e.hresult())) + ":" + hex(e.message_utf16()) +
               ":-";
    }
}

void native_units() {
    auto path = nini_writer_text(u"immutable-path");
    std::vector<std::array<std::int32_t, 5>> recipes;
    NiniFileStreamProvider file = [&](const NiniWriterText& received, std::int32_t mode, std::int32_t access,
                                      std::int32_t share, std::int32_t buffer, std::int32_t options) {
        if (received != path)
            throw std::runtime_error("Factory changed immutable filename identity");
        recipes.push_back({mode, access, share, buffer, options});
        return std::make_shared<Stream>();
    };
    auto wf = nini_byte_writer_factory(file);
    {
        NiniWriter writer(path, wf);
        writer.write_empty();
        writer.close();
    }
    auto df = nini_byte_document_factory(file);
    {
        auto reader = df.file_reader(path);
        reader->close();
        auto writer = df.file_writer(path);
        writer->close();
    }
    if (recipes !=
        std::vector<std::array<std::int32_t, 5>>{{2, 2, 0, 4096, 0}, {3, 1, 1, 4096, 0}, {2, 2, 1, 4096, 0}})
        throw std::runtime_error("Default FileStream recipes differ from consumed Nini/BCL constructors");
    auto count = recipes.size();
    for (auto invalid : {NiniWriterText{}, nini_writer_text(u"")})
        try {
            df.file_writer(invalid);
            throw std::runtime_error("Missing invalid path error");
        } catch (const NiniStreamError&) {
            if (recipes.size() != count)
                throw std::runtime_error("Path validation must precede file provider");
        }
    auto sink = std::make_shared<Stream>();
    NiniByteTextWriter strict(sink);
    strict.write(nini_writer_text(std::u16string(1, static_cast<char16_t>(0xd800))));
    bool caught = false;
    try {
        strict.flush();
    } catch (const NiniStreamError& e) {
        caught = true;
        if (e.source_type != "System.Text.EncoderFallbackException" || e.character_index != 0 ||
            e.unknown_character != static_cast<char16_t>(0xd800) ||
            std::string(e.what()).find("D800") == std::string::npos)
            throw std::runtime_error("Strict UTF8 error metadata/what");
    }
    if (!caught)
        throw std::runtime_error("Default UTF8 must reject lone surrogate");
    try {
        strict.close();
    } catch (const NiniStreamError&) {
    }
    auto directory = std::filesystem::temp_directory_path() /
                     ("nini-streams-native-" +
                      std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    if (!std::filesystem::create_directory(directory))
        throw std::runtime_error("Cannot create isolated stream fixture directory");
    auto file_path = directory / std::filesystem::path(u"config-配置😀.ini");
    struct Cleanup {
        std::filesystem::path file, directory;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove(file, e);
            std::filesystem::remove(directory, e);
        }
    } cleanup{file_path, directory};
    auto filename = nini_writer_text(file_path.u16string());
    NiniDocument document;
    auto section = std::make_shared<NiniSection>(nini_writer_text(u"配置😀"));
    section->set(nini_writer_text(u"key"), nini_writer_text(u"value😀"));
    document.sections().add(section);
    document.save(filename, native_nini_document_factory());
    std::ifstream binary(file_path, std::ios::binary);
    std::string actual((std::istreambuf_iterator<char>(binary)), {});
    binary.close();
    const std::string expected =
        "[\xe9\x85\x8d\xe7\xbd\xae\xf0\x9f\x98\x80]\r\nkey = value\xf0\x9f\x98\x80\r\n";
    if (actual != expected)
        throw std::runtime_error("Default document file output bytes/BOM");
    NiniDocument loaded(filename, native_nini_document_factory());
    if (*loaded.sections().at(0)->get_value(nini_writer_text(u"key")) != u"value😀")
        throw std::runtime_error("Default file reader UTF8/BOM roundtrip");
    {
        NiniWriter writer(filename, native_nini_writer_factory());
        writer.write_empty(nini_writer_text(u"comment"));
        writer.close();
    }
    std::ifstream rewritten(file_path, std::ios::binary);
    actual.assign(std::istreambuf_iterator<char>(rewritten), {});
    rewritten.close();
    if (actual != "; comment\r\n")
        throw std::runtime_error("Default IniWriter file Create truncation");
}

}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--ini-file-probe") {
            std::string row;
            while (std::getline(std::cin, row))
                std::cout << inifile(row) << '\n';
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--file-probe") {
            std::string row;
            while (std::getline(std::cin, row))
                std::cout << filerun(row) << '\n';
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--probe") {
            std::string row;
            while (std::getline(std::cin, row))
                std::cout << run(row) << '\n';
            return 0;
        }
        native_units();
        auto s = std::make_shared<Stream>();
        NiniByteTextWriter w(s);
        w.write(nini_writer_text(u"actual"));
        if (!s->data.empty())
            throw std::runtime_error("Writer must buffer");
        w.flush();
        if (hexbytes(s->data) != "61637475616c")
            throw std::runtime_error("Writer UTF8");
        w.close();
        std::cout << "PASS Nini byte text streams\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
