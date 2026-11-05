// Actual source parity fixture for bundled Nini document classes.
#include "opensim/framework/nini_document.hpp"
#include <algorithm>
#include <bit>
#include <iostream>
#include <sstream>
using namespace opensim::framework;
namespace {
std::string hex(const NiniWriterText& t) {
    if (!t)
        return "-";
    if (t->empty())
        return ".";
    std::string o;
    for (auto c : *t)
        for (unsigned char b : {static_cast<unsigned char>(c), static_cast<unsigned char>(c >> 8)}) {
            o += "0123456789abcdef"[b >> 4];
            o += "0123456789abcdef"[b & 15];
        }
    return o;
}
NiniWriterText dec(const std::string& s) {
    if (s == "-")
        return {};
    std::u16string t;
    if (s != ".")
        for (std::size_t i = 0; i < s.size(); i += 4)
            t += static_cast<char16_t>(std::stoul(s.substr(i, 2), nullptr, 16) |
                                       (std::stoul(s.substr(i + 2, 2), nullptr, 16) << 8));
    return nini_writer_text(t);
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
std::string err(const char* type, std::int32_t hr, const std::u16string& msg,
                std::optional<std::u16string> param = {}) {
    return std::string(type) + ":" + std::to_string(std::bit_cast<std::uint32_t>(hr)) + ":" +
           hex(nini_writer_text(msg)) + ":" + hex(nini_writer_text(param));
}
struct Probe;
struct Reader : NiniTextReader {
    Probe& p;
    NiniWriterText text;
    std::size_t pos = 0;
    bool closed = false;
    Reader(Probe& p, NiniWriterText text) : p(p), text(std::move(text)) {
    }
    int read() override;
    int peek() override;
    void close() override;
};
struct Writer : NiniTextWriter {
    Probe& p;
    bool closed = false;
    int mutation;
    Writer(Probe& p, int m) : p(p), mutation(m) {
    }
    void write(const NiniWriterText&) override;
    void flush() override;
    void close() override;
    NiniWriterText to_string() override {
        return {};
    }
};
struct Array : NiniSectionArray {
    int r, b, n, type;
    std::vector<NiniSectionEntry> values;
    Array(int t, int r, int b, int n) : r(r), b(b), n(n), type(t) {
    }
    int rank() const override {
        return r;
    }
    int lower_bound() const override {
        return b;
    }
    int length() const override {
        return n;
    }
    void set(int index, const NiniSectionEntry& v) override {
        if (index < b || index >= b + n)
            throw NiniDocumentError(NiniDocumentError::Kind::IndexOutOfRange,
                                    u"Index was outside the bounds of the array.");
        if (type > 1)
            throw NiniDocumentError(NiniDocumentError::Kind::InvalidCast,
                                    u"Object cannot be stored in an array of this type.");
        values.push_back(v);
    }
};
struct Probe {
    NiniDocument doc;
    NiniSectionCollection collection = doc.sections();
    std::shared_ptr<NiniSection> section;
    std::shared_ptr<NiniItem> item;
    std::vector<std::shared_ptr<NiniSection>> sections;
    std::vector<std::shared_ptr<Reader>> readers;
    std::vector<std::shared_ptr<NiniReader>> ini_readers;
    std::vector<std::shared_ptr<Writer>> writers;
    std::optional<NiniSectionCollection::Enumerator> enumerator;
    int point = 0, nth = 1, hit = 0;
    bool recording = true;
    std::vector<std::string> trace;
    ~Probe() {
        recording = false;
        while (doc.retired_writer_count())
            try {
                doc.finalize_retired_writers();
            } catch (...) {
            }
        while (doc.retired_reader_count())
            try {
                doc.finalize_retired_readers();
            } catch (...) {
            }
    }
    void event(int p, std::string text) {
        if (!recording)
            return;
        trace.push_back(std::move(text));
        if (point == p && ++hit == nth)
            throw NiniWriterError(NiniWriterError::Kind::Other, u"fault");
    }
    void load(NiniWriterText t, bool explicit_reader = false) {
        if (!t) {
            if (explicit_reader)
                doc.load(std::shared_ptr<NiniReader>{});
            else
                doc.load(std::shared_ptr<NiniTextReader>{});
            return;
        }
        auto r = std::make_shared<Reader>(*this, t);
        readers.push_back(r);
        if (explicit_reader) {
            auto ini = std::make_shared<NiniReader>(r);
            ini->ignore_comments(true);
            ini->accept_no_assignment(true);
            ini_readers.push_back(ini);
            doc.load(ini);
        } else
            doc.load(r);
    }
    void mutate(int m) {
        if (m == 1)
            collection.add(std::make_shared<NiniSection>(nini_writer_text(u"later")));
        else if (m == 2) {
            if (collection.count())
                collection.at(0)->set(nini_writer_text(u"added"), nini_writer_text(u"value"));
        } else if (m == 3) {
            if (collection.count())
                collection.remove(collection.at(0)->name());
        } else if (m == 4)
            load(nini_writer_text(u";added\n"));
        else if (m == 5) {
            if (collection.count() && collection.at(0)->item_count())
                collection.at(0)->get_item(0)->type(NiniType::Empty);
        } else if (m == 6)
            load(nini_writer_text(u"[replacement]\nx=y\n"));
        else if (m == 7)
            doc.file_type(NiniFileType::PythonStyle);
        else if (m == 8) {
            if (collection.count() && collection.at(0)->item_count())
                collection.at(0)->remove(collection.at(0)->get_item(0)->name());
        } else if (m == 9) {
            auto w = std::make_shared<Writer>(*this, 0);
            writers.push_back(w);
            doc.save(w);
        }
    }
    std::string snapshot() {
        std::string o = std::to_string(static_cast<int>(doc.file_type())) + "{";
        for (int i = 0; i < collection.count(); ++i) {
            auto s = collection.at(i);
            o += hex(s->name()) + ":" + hex(s->comment()) + "[";
            for (int j = 0; j < s->item_count(); ++j) {
                auto t = s->get_item(j);
                o += std::to_string(static_cast<int>(t->type())) + ":" + hex(t->name()) + ":" +
                     hex(t->value()) + ":" + hex(t->comment()) + ";";
            }
            o += "]";
        }
        o += "}/S:";
        if (section) {
            o += hex(section->name()) + ":" + hex(section->comment()) + "[";
            for (int j = 0; j < section->item_count(); ++j) {
                auto t = section->get_item(j);
                o += std::to_string(static_cast<int>(t->type())) + ":" + hex(t->name()) + ":" +
                     hex(t->value()) + ":" + hex(t->comment()) + ";";
            }
            o += "]";
        } else
            o += "-";
        o += "/I:";
        if (item)
            o += std::to_string(static_cast<int>(item->type())) + ":" + hex(item->name()) + ":" +
                 hex(item->value()) + ":" + hex(item->comment());
        else
            o += "-";
        return o;
    }
    std::string run(const std::string& row) {
        std::istringstream in(row);
        int ft;
        std::string ops;
        in >> ft >> point >> nth >> ops;
        doc.file_type(static_cast<NiniFileType>(ft));
        std::string output;
        for (auto& token : split(ops, ',')) {
            auto a = split(token, ':');
            std::string op = a[0], r = op;
            try {
                if (op == "load" || op == "ir")
                    load(dec(a.at(1)), op == "ir");
                else if (op == "ft")
                    doc.file_type(static_cast<NiniFileType>(std::stoi(a.at(1))));
                else if (op == "new") {
                    section = std::make_shared<NiniSection>(dec(a.at(1)), dec(a.at(2)));
                    sections.push_back(section);
                } else if (op == "add")
                    collection.add(a.at(1) == "-" ? nullptr : sections.at(std::stoul(a.at(1))));
                else if (op == "sel")
                    section = collection.at(std::stoi(a.at(1)));
                else if (op == "find") {
                    auto s = collection.at(dec(a.at(1)));
                    r = s ? "found" : "missing";
                } else if (op == "remove")
                    collection.remove(dec(a.at(1)));
                else if (op == "set") {
                    if (!section)
                        throw NiniDocumentError(NiniDocumentError::Kind::NullReference,
                                                u"Object reference not set to an instance of an object.");
                    section->set(dec(a.at(1)), dec(a.at(2)), dec(a.at(3)));
                } else if (op == "empty")
                    section->set(dec(a.at(1)));
                else if (op == "rm")
                    section->remove(dec(a.at(1)));
                else if (op == "get")
                    r = "V:" + hex(section->get_value(dec(a.at(1))));
                else if (op == "contains")
                    r = section->contains(dec(a.at(1))) ? "true" : "false";
                else if (op == "keys") {
                    r = "K";
                    for (auto& k : section->get_keys())
                        r += ":" + hex(k);
                } else if (op == "item")
                    item = section->get_item(std::stoi(a.at(1)));
                else if (op == "makeitem")
                    item = std::make_shared<NiniItem>(
                        dec(a.at(1)), dec(a.at(2)), static_cast<NiniType>(std::stoi(a.at(3))), dec(a.at(4)));
                else if (op == "identity") {
                    bool same = true;
                    auto keys = section->get_keys();
                    std::size_t k = 0;
                    for (int j = 0; j < section->item_count(); ++j) {
                        auto t = section->get_item(j);
                        if (t->type() == NiniType::Key)
                            same = same && keys.at(k++) == t->name();
                        same = same && section->get_value(t->name()) == t->value();
                    }
                    r = same ? "same" : "bad";
                } else if (op == "iv")
                    item->value(dec(a.at(1)));
                else if (op == "ic")
                    item->comment(dec(a.at(1)));
                else if (op == "it")
                    item->type(static_cast<NiniType>(std::stoi(a.at(1))));
                else if (op == "save") {
                    auto w = std::make_shared<Writer>(*this, std::stoi(a.at(1)));
                    writers.push_back(w);
                    doc.save(w);
                } else if (op == "save-null")
                    doc.save(std::shared_ptr<NiniTextWriter>{});
                else if (op == "enum")
                    enumerator = collection.get_enumerator();
                else if (op == "next")
                    r = enumerator->move_next() ? "true" : "false";
                else if (op == "reset")
                    enumerator->reset();
                else if (op == "current") {
                    auto e = enumerator->current();
                    r = "E:" + hex(e.key) + ":" + hex(e.value->name());
                } else if (op == "copy") {
                    std::shared_ptr<Array> ar;
                    if (a.at(1) != "-")
                        ar = std::make_shared<Array>(std::stoi(a.at(1)), std::stoi(a.at(2)),
                                                     std::stoi(a.at(3)), std::stoi(a.at(4)));
                    collection.copy_to(ar, std::stoi(a.at(5)));
                    std::vector<std::string> entries;
                    for (auto& e : ar->values)
                        entries.push_back(hex(e.key) + ":" + hex(e.value->name()));
                    std::sort(entries.begin(), entries.end());
                    r = "A";
                    for (auto& e : entries)
                        r += ":" + e;
                } else if (op == "sync")
                    r = collection.sync_root() == doc.sections().sync_root() && !collection.is_synchronized()
                            ? "same"
                            : "bad";
                else
                    throw std::runtime_error("operation");
            } catch (const NiniDocumentError& e) {
                r = "EX:" + err(e.source_type(), e.hresult(), e.message, e.parameter);
            } catch (const NiniReaderError& e) {
                r = "EX:" + err(e.source_type(), e.hresult(), e.message_utf16());
            } catch (const NiniWriterError& e) {
                r = "EX:" + err(e.source_type(), e.hresult(), e.message, e.parameter);
            }
            if (!output.empty())
                output += '|';
            output += r + "/" + snapshot();
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
int Reader::peek() {
    p.event(3, "P");
    if (closed)
        throw NiniReaderError(NiniReaderError::Kind::DisposedReader,
                              u"Cannot read from a closed TextReader.");
    return pos < text->size() ? (*text)[pos] : -1;
}
int Reader::read() {
    p.event(2, "R");
    if (closed)
        throw NiniReaderError(NiniReaderError::Kind::DisposedReader,
                              u"Cannot read from a closed TextReader.");
    return pos < text->size() ? (*text)[pos++] : -1;
}
void Reader::close() {
    p.event(4, "RC");
    closed = true;
}
void Writer::write(const NiniWriterText& t) {
    p.event(1, "W:" + hex(t));
    int m = mutation;
    mutation = 0;
    p.mutate(m);
    if (closed)
        throw NiniWriterError(NiniWriterError::Kind::DisposedWriter, u"Cannot write to a closed TextWriter.");
}
void Writer::flush() {
    p.event(6, "F");
}
void Writer::close() {
    p.event(5, "WC");
    closed = true;
}
struct FinalWriter : NiniTextWriter {
    int closes = 0;
    bool fail = false;
    void write(const NiniWriterText&) override {
        if (fail)
            throw std::runtime_error("write");
    }
    void flush() override {
    }
    void close() override {
        ++closes;
    }
    NiniWriterText to_string() override {
        return {};
    }
};
struct ProviderStream : NiniWriterStream {
    int closes = 0;
    void close() override {
        ++closes;
    }
};
struct ProviderReader : NiniTextReader {
    std::shared_ptr<NiniTextReader> inner = nini_string_reader(u"[a]\nx:python\n");
    int closes = 0;
    int read() override {
        return inner->read();
    }
    int peek() override {
        return inner->peek();
    }
    void close() override {
        ++closes;
        inner->close();
    }
};
void generic_error_messages() {
    bool caught = false;
    try {
        NiniDocument document;
        document.load(nini_string_reader(u"key=value\n"));
    } catch (const std::exception& error) {
        caught = true;
        if (std::string(error.what()) != "Object reference not set to an instance of an object.")
            throw std::runtime_error("Before-section error lost through std::exception::what()");
    }
    if (!caught)
        throw std::runtime_error("Missing before-section exception");
    std::u16string name = u"\u914d\u7f6e \U0001f600";
    name.push_back(static_cast<char16_t>(0xd800));
    name.push_back(u'\u00e9');
    const std::string rendered = "\xe9\x85\x8d\xe7\xbd\xae \xf0\x9f\x98\x80\xef\xbf\xbd\xc3\xa9";
    const auto expected = "Item has already been added. Key in dictionary: '" + rendered +
                          "'  Key being added: '" + rendered + "'";
    NiniSectionCollection collection;
    auto section = std::make_shared<NiniSection>(nini_writer_text(name));
    collection.add(section);
    caught = false;
    try {
        collection.add(std::make_shared<NiniSection>(nini_writer_text(name)));
    } catch (const std::exception& error) {
        caught = true;
        if (std::string(error.what()) != expected)
            throw std::runtime_error(
                "Duplicate Unicode section error lost UTF8 through std::exception::what()");
    }
    if (!caught)
        throw std::runtime_error("Missing duplicate section exception");
}

void provider_units() {
    auto path = nini_writer_text(u"borrowed-path");
    auto stream = std::make_shared<ProviderStream>();
    auto reader = std::make_shared<ProviderReader>();
    auto writer = std::make_shared<FinalWriter>();
    std::vector<int> calls;
    NiniDocumentFactory factory;
    factory.file_reader = [&](const NiniWriterText& received) {
        if (received != path)
            throw std::runtime_error("File reader path identity");
        calls.push_back(1);
        return reader;
    };
    factory.stream_reader = [&](std::shared_ptr<NiniWriterStream> received) {
        if (received != stream)
            throw std::runtime_error("Stream reader identity");
        calls.push_back(2);
        reader = std::make_shared<ProviderReader>();
        return reader;
    };
    factory.file_writer = [&](const NiniWriterText& received) {
        if (received != path)
            throw std::runtime_error("File writer path identity");
        calls.push_back(3);
        return writer;
    };
    factory.stream_writer = [&](std::shared_ptr<NiniWriterStream> received) {
        if (received != stream)
            throw std::runtime_error("Stream writer identity");
        calls.push_back(4);
        return writer;
    };
    NiniDocument document(path, factory, NiniFileType::PythonStyle);
    if (*document.sections().at(0)->get_value(nini_writer_text(u"x")) != u"python" || reader->closes != 1 ||
        document.retired_reader_count() != 1)
        throw std::runtime_error("File reader constructor/type/Close");
    document.finalize_retired_readers();
    if (reader->closes != 2)
        throw std::runtime_error("Reader controlled finalizer");
    document.save(path, factory);
    if (writer->closes != 2)
        throw std::runtime_error("File Save double Close");
    document.finalize_retired_writers();
    if (writer->closes != 3)
        throw std::runtime_error("File Save finalizer");
    NiniDocument from_stream(stream, factory, NiniFileType::PythonStyle);
    from_stream.save(stream, factory);
    if (calls != std::vector<int>{1, 3, 2, 4} || writer->closes != 4 || stream->closes)
        throw std::runtime_error("Stream provider constructor/Save delegation");
    auto explicit_reader = std::make_shared<NiniReader>(u"[b]\nx=y\n");
    NiniDocument borrowed(explicit_reader);
    if (borrowed.retired_reader_count() || explicit_reader->state() != NiniReadState::Closed)
        throw std::runtime_error("Caller-owned IniReader retained or unclosed");
    factory.file_reader = [&](const NiniWriterText&) -> std::shared_ptr<NiniTextReader> {
        throw NiniWriterError(NiniWriterError::Kind::Other, u"reader constructor fault");
    };
    try {
        document.load(path, factory);
        throw std::runtime_error("Missing provider fault");
    } catch (const NiniWriterError& e) {
        if (e.message != u"reader constructor fault")
            throw;
    }
    auto typed = std::make_shared<std::vector<std::shared_ptr<NiniSection>>>(3);
    try {
        document.sections().copy_to(typed, 0);
        throw std::runtime_error("Typed CopyTo must copy DictionaryEntry");
    } catch (const NiniDocumentError& e) {
        if (e.kind != NiniDocumentError::Kind::InvalidCast)
            throw;
    }
    NiniSectionCollection empty;
    empty.copy_to(typed, 0);
    NiniDocument alias = document;
    alias.file_type(NiniFileType::MysqlStyle);
    if (document.file_type() != NiniFileType::MysqlStyle ||
        alias.sections().sync_root() != document.sections().sync_root())
        throw std::runtime_error("Document managed aliases");
}

void finalizer_proof() {
    NiniDocument d;
    d.sections().add(std::make_shared<NiniSection>(nini_writer_text(u"a")));
    auto ok = std::make_shared<FinalWriter>();
    d.save(ok);
    if (ok->closes != 1 || d.retired_writer_count() != 1)
        throw std::runtime_error("Successful Save closes synchronously once");
    d.finalize_retired_writers();
    auto bad = std::make_shared<FinalWriter>();
    bad->fail = true;
    try {
        d.save(bad);
        throw std::logic_error("Missing Save failure");
    } catch (const std::runtime_error& e) {
        if (std::string(e.what()) != "write")
            throw;
    }
    if (bad->closes || d.retired_writer_count() != 1)
        throw std::runtime_error("Failed Save must not synchronously Close");
    d.finalize_retired_writers();
    std::cout << ok->closes << ":" << bad->closes << '\n';
}

}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--finalizer") {
            finalizer_proof();
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--probe") {
            std::string row;
            while (std::getline(std::cin, row)) {
                Probe p;
                std::cout << p.run(row) << '\n';
            }
            return 0;
        }
        generic_error_messages();
        provider_units();
        NiniDocument d;
        d.load(nini_string_reader(u";before\n[a]\nx=first\nx=second\n"));
        auto s = d.sections().at(0);
        if (*s->get_value(nini_writer_text(u"x")) != u"first")
            throw std::runtime_error("First-key document semantics");
        auto w = std::make_shared<NiniStringWriter>();
        d.save(w);
        if (*w->to_string() != u"; before\r\n[a]\r\nx = first\r\n")
            throw std::runtime_error("Document Save");
        std::cout << "PASS Nini document\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
