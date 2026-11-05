// Actual native reader/retained DOM/Nini integration; no CLR calls in native code.
#include "opensim/framework/nini_xml_native_document.hpp"
#include "opensim/framework/nini_config_invariant.hpp"
#include "opensim/framework/text.hpp"
#include <array>
#include <iostream>
#include <sstream>
#include <unordered_map>
using namespace opensim::framework;
namespace opensim::framework {
struct NiniXmlConfigProbe {
    static auto document(const NiniXmlConfigSource& source) {
        return source.document_;
    }
};
}
namespace {
using Text = NiniWriterText;
std::string enc(const Text& text) {
    if (!text)
        return "~";
    std::string out;
    for (auto c : *text)
        for (int n : {12, 8, 4, 0})
            out += "0123456789abcdef"[(c >> n) & 15];
    return out.empty() ? "-" : out;
}
Text dec(const std::string& text) {
    if (text == "~")
        return {};
    std::u16string out;
    if (text != "-")
        for (std::size_t i = 0; i < text.size(); i += 4)
            out += static_cast<char16_t>(std::stoi(text.substr(i, 4), nullptr, 16));
    return nini_writer_text(std::move(out));
}
std::string error(const std::exception& e) {
    if (auto p = dynamic_cast<const NiniXmlProviderError*>(&e))
        return "E " + p->source_type + " " + std::to_string(p->hresult) + " " +
               enc(nini_writer_text(p->parameter)) + " " + enc(nini_writer_text(p->message)) + " " +
               enc(nini_writer_text(p->source_uri)) + " " +
               (p->line_number ? std::to_string(*p->line_number) : "~") + " " +
               (p->line_position ? std::to_string(*p->line_position) : "~") + " " +
               enc(nini_writer_text(p->file_name)) + " " + enc(nini_writer_text(p->object_name));
    if (auto p = dynamic_cast<const NiniConfigError*>(&e))
        return "E " + std::string(p->source_type()) + " " + std::to_string(p->hresult()) + " " +
               enc(nini_writer_text(p->parameter)) + " " + enc(nini_writer_text(p->message)) + " ~ ~ ~ ~ ~";
    return "X " + enc(nini_writer_text(utf16_replacing(e.what())));
}
std::string shape(const std::shared_ptr<NiniXmlDomNode>& n) {
    std::string out = std::to_string(n->node_type()) + ":" + enc(n->name()) + ":" + enc(n->prefix()) + ":" +
                      enc(n->local_name()) + ":" + enc(n->namespace_uri()) + ":" + enc(n->value()) + ":" +
                      (n->is_empty_element() ? "1" : "0") + "[";
    auto e = n->children();
    while (e->move_next())
        out += shape(std::dynamic_pointer_cast<NiniXmlDomNode>(e->current())) + ",";
    out += "]{";
    for (auto a : n->attributes())
        out += shape(a) + ",";
    return out + "}";
}
std::string flow(const std::string& input) {
    auto io = std::make_shared<NiniXmlNativeDocumentIo>();
    io->open_path = [](Text) -> std::shared_ptr<NativeNiniXmlReader> {
        throw std::logic_error(
            "Path reader is an explicit unused dependency in this remote-reader flow fixture");
    };
    io->save_path = [](NiniXmlDom&, Text) {
        throw std::logic_error("Path writer is an explicit unused dependency in this load/merge fixture");
    };
    io->save_writer = [](NiniXmlDom&, std::shared_ptr<NiniTextWriter>) {
        throw std::logic_error("Text writer is an explicit unused dependency in this load/merge fixture");
    };
    io->save_stream = [](NiniXmlDom&, std::shared_ptr<NiniWriterStream>) {
        throw std::logic_error("Stream writer is an explicit unused dependency in this load/merge fixture");
    };
    auto arena = std::make_shared<NiniConfigArena>(native_nini_invariant_config_backend());
    auto backend = native_nini_xml_config_backend(io);
    std::array<std::shared_ptr<NiniXmlConfigSource>, 3> sources;
    std::shared_ptr<NativeNiniXmlReader> last_reader;
    std::string trace, fault;
    int nth = 1, seen = 0, mutation = 0;
    std::uint64_t identity = 0;
    auto reader = [](Text text, int mode, Text uri = Text{}) -> std::shared_ptr<NativeNiniXmlReader> {
        if (!text)
            return {};
        NiniXmlReaderSettings settings;
        settings.dtd_processing = static_cast<NiniXmlDtdProcessing>(mode);
        if (uri)
            settings.base_uri = std::move(uri);
        return NativeNiniXmlReader::from_text(std::move(text), settings);
    };
    auto hit = [&](const std::string& name, NiniXmlConfigSource* source) {
        trace += name + ';';
        if (name == fault && ++seen == nth) {
            if (mutation == 1)
                source->configs()->clear();
            else if (mutation == 2)
                source->load(reader(
                    nini_writer_text(
                        u"<Nini><Section Name='Injected'><Key Name='new' Value='created'/></Section></Nini>"),
                    0));
            else
                throw NiniXmlProviderError("System.InvalidOperationException", 0x80131509u, u"event fault");
        }
    };
    auto attach_config = [&](const std::shared_ptr<NiniConfig>& config, int i, NiniXmlConfigSource* source) {
        if (!config)
            return;
        auto pointer = config.get();
        config->key_set.add(++identity, [&, pointer, i, source](auto&, auto e) {
            hit("k" + std::to_string(i) + ":" + enc(pointer->name()) + ":" + enc(e->key) + ":" +
                    enc(e->value),
                source);
        });
        config->key_removed.add(++identity, [&, pointer, i, source](auto&, auto e) {
            hit("d" + std::to_string(i) + ":" + enc(pointer->name()) + ":" + enc(e->key) + ":" +
                    enc(e->value),
                source);
        });
    };
    auto attach = [&](const std::shared_ptr<NiniXmlConfigSource>& source, int i) {
        auto pointer = source.get();
        source->saved.add(++identity,
                          [&, pointer, i](auto&, auto) { hit("s" + std::to_string(i), pointer); });
        source->reloaded.add(++identity,
                             [&, pointer, i](auto&, auto) { hit("r" + std::to_string(i), pointer); });
        source->configs()->config_added.add(++identity, [&, pointer, i](auto&, auto e) {
            attach_config(e->config, i, pointer);
            hit("a" + std::to_string(i) + ":" + enc(e->config ? e->config->name() : Text{}), pointer);
        });
        source->configs()->config_removed.add(++identity, [&, pointer, i](auto&, auto e) {
            hit("c" + std::to_string(i) + ":" + enc(e->config ? e->config->name() : Text{}), pointer);
        });
        for (int n = 0; n < source->configs()->count(); ++n)
            attach_config(source->configs()->at(n), i, pointer);
    };
    auto snapshot = [&](int i, const std::shared_ptr<NiniXmlDocument>& prior) {
        auto source = sources[i];
        if (!source)
            return std::string("~");
        auto doc = NiniXmlConfigProbe::document(*source);
        std::string state = enc(source->save_path()) + "," + (doc != prior ? "1" : "0") + ",";
        for (int n = 0; n < source->configs()->count(); ++n) {
            auto c = source->configs()->at(n);
            int owner = -1;
            for (int j = 0; j < 3; ++j)
                if (c->config_source() == sources[j]) {
                    owner = j;
                    break;
                }
            state += "[" + enc(c->name()) + ":" + std::to_string(owner);
            for (const auto& key : c->get_keys()) {
                std::string value;
                try {
                    value = enc(c->get(key));
                } catch (const std::exception& e) {
                    value = error(e);
                }
                state += ":" + enc(key) + "=" + value;
            }
            state += "]";
        }
        try {
            state += ";" + (doc ? shape(std::dynamic_pointer_cast<NativeNiniXmlDocument>(doc)
                                            ->native_dom()
                                            .document_node())
                                : "~");
        } catch (const std::exception& e) {
            state += ";" + error(e);
        }
        return state;
    };
    std::istringstream sequence(input);
    std::string output;
    for (std::string operation; std::getline(sequence, operation, '|');) {
        if (!output.empty())
            output += '/';
        std::istringstream row(operation);
        std::string op;
        int i;
        row >> op >> i;
        auto source = sources.at(i);
        auto prior = source ? NiniXmlConfigProbe::document(*source) : nullptr;
        std::string result = "OK";
        auto text = [&] {
            std::string token;
            row >> token;
            return dec(token);
        };
        try {
            if (!source && op != "NEW" && op != "PATH" && op != "READER" && op != "READERB" &&
                op != "READERU" && op != "FAIL" && op != "IOSTATE")
                throw NiniConfigError(NiniConfigError::Kind::NullReference,
                                      u"Object reference not set to an instance of an object.");
            if (op == "NEW") {
                sources[i] = NiniXmlConfigSource::create(arena, backend);
                attach(sources[i], i);
            } else if (op == "PATH") {
                auto path = text();
                sources[i] = NiniXmlConfigSource::create(arena, backend, path);
                attach(sources[i], i);
            } else if (op == "READER" || op == "LOADR" || op == "READERB" || op == "LOADB" ||
                       op == "READERU" || op == "LOADU") {
                auto xml = text();
                int mode;
                row >> mode;
                if (op == "READERB" || op == "LOADB") {
                    if (!xml)
                        last_reader.reset();
                    else {
                        std::vector<std::uint8_t> bytes;
                        for (std::size_t at = 0; at < xml->size(); at += 2) {
                            std::string hex;
                            hex += static_cast<char>((*xml)[at]);
                            hex += static_cast<char>((*xml)[at + 1]);
                            bytes.push_back(static_cast<std::uint8_t>(std::stoul(hex, nullptr, 16)));
                        }
                        NiniXmlReaderSettings settings;
                        settings.dtd_processing = static_cast<NiniXmlDtdProcessing>(mode);
                        last_reader = NativeNiniXmlReader::from_bytes(bytes, settings);
                    }
                } else
                    last_reader = reader(xml, mode, (op == "READERU" || op == "LOADU") ? text() : Text{});
                if (op == "READER" || op == "READERB" || op == "READERU") {
                    sources[i] = NiniXmlConfigSource::create(arena, backend, last_reader);
                    attach(sources[i], i);
                } else
                    source->load(last_reader);
            } else if (op == "LOADLAST") {
                int count;
                row >> count;
                if (last_reader) {
                    if (count < 0)
                        last_reader->close();
                    else
                        for (int step = 0; step < count; ++step)
                            last_reader->read();
                }
                source->load(last_reader);
            } else if (op == "LOADP")
                source->load(text());
            else if (op == "STRING")
                result = "T " + enc(source->to_string());
            else if (op == "SAVE")
                source->save();
            else if (op == "SAVEP")
                source->save(text());
            else if (op == "RELOAD")
                source->reload();
            else if (op == "AUTO") {
                bool value;
                row >> value;
                source->auto_save(value);
            } else if (op == "ADD")
                source->add_config(text());
            else if (op == "SET" || op == "RAWADD") {
                auto name = text(), key = text(), value = text();
                auto config = source->configs()->at(name);
                if (!config)
                    config = source->add_config(name);
                if (op == "SET")
                    config->set_text(key, value);
                else
                    config->add(key, value);
            } else if (op == "REM") {
                auto name = text(), key = text();
                auto config = source->configs()->at(name);
                if (!config)
                    throw NiniConfigError(NiniConfigError::Kind::NullReference,
                                          u"Object reference not set to an instance of an object.");
                config->remove(key);
            } else if (op == "DROPSEC")
                source->configs()->remove(source->configs()->at(text()));
            else if (op == "CLEAR")
                source->configs()->clear();
            else if (op == "MERGE") {
                int other;
                row >> other;
                source->merge(sources.at(other));
            } else if (op == "FAIL") {
                auto name = text();
                fault = name ? utf8(*name) : std::string{};
                row >> nth >> mutation;
                seen = 0;
            } else if (op == "IOSTATE")
                result = "~;~;" + (last_reader ? std::to_string(static_cast<int>(last_reader->read_state())) +
                                                     "," + (last_reader->eof() ? "1" : "0") + "," +
                                                     std::to_string(last_reader->token().node_type) + "," +
                                                     std::to_string(last_reader->token().depth)
                                               : "~");
            else
                throw std::runtime_error("Unknown source command");
        } catch (const std::exception& e) {
            result = error(e);
        }
        output += result + '#' + snapshot(i, prior);
    }
    return output + '!' + enc(nini_writer_text(utf16_replacing(trace)));
}
}
void ownership_units() {
    auto require = [](bool value, const char* message) {
        if (!value)
            throw std::runtime_error(message);
    };
    auto dependencies = []() {
        auto io = std::make_shared<NiniXmlNativeDocumentIo>();
        io->open_path = [](NiniWriterText) -> std::shared_ptr<NativeNiniXmlReader> {
            throw std::logic_error("No file-reader operation expected in this ownership test");
        };
        io->save_path = [](NiniXmlDom&, NiniWriterText) {
            throw std::logic_error("No file writer expected in this ownership test");
        };
        io->save_writer = [](NiniXmlDom&, std::shared_ptr<NiniTextWriter>) {
            throw std::logic_error("No text writer expected in this ownership test");
        };
        io->save_stream = [](NiniXmlDom&, std::shared_ptr<NiniWriterStream>) {
            throw std::logic_error("No stream writer expected in this ownership test");
        };
        return io;
    };
    {
        auto io = dependencies();
        auto document = NativeNiniXmlDocument::create(io);
        document->load_xml(nini_writer_text(u"<Nini/>"));
        auto root = document->document_element();
        require(root == document->document_element(), "Repeated native node handles lost reference identity");
        auto detached = document->create_element(nini_writer_text(u"detached"));
        std::weak_ptr<NativeNiniXmlDocument> weak = document;
        std::weak_ptr<NiniXmlNativeDocumentIo> provider = io;
        root.reset();
        document.reset();
        io.reset();
        require(!weak.expired() && !provider.expired() && *detached->name() == u"detached",
                "Detached native node released its document/provider");
        detached.reset();
        require(weak.expired() && provider.expired(),
                "Native document weak node cache formed an ownership cycle");
    }
    {
        auto io = dependencies();
        auto document = NativeNiniXmlDocument::create(io);
        document->load_xml(nini_writer_text(u"<Nini/>"));
        auto root = document->document_element();
        auto empty = root->children();
        std::weak_ptr<NativeNiniXmlDocument> weak = document;
        root.reset();
        document.reset();
        require(weak.expired() && !empty->move_next(), "Initially empty enumerator retained its document");
    }
    {
        auto io = dependencies();
        auto document = NativeNiniXmlDocument::create(io);
        document->load_xml(nini_writer_text(u"<Nini><child/></Nini>"));
        auto root = document->document_element();
        auto live = root->children();
        std::weak_ptr<NativeNiniXmlDocument> weak = document;
        root.reset();
        document.reset();
        require(!weak.expired() && live->move_next() && *live->current()->name() == u"child",
                "Populated live enumerator lost its owning provider");
        live.reset();
        require(weak.expired(), "Released native enumeration retained its document");
    }
    {
        auto io = dependencies();
        auto document = NativeNiniXmlDocument::create(io);
        auto supplied = NativeNiniXmlReader::from_text(nini_writer_text(u"<Nini/>"));
        document->load_reader(supplied);
        require(supplied->eof() && supplied->read_state() == NiniXmlReadState::EndOfFile,
                "Supplied reader was closed by document load");
        std::shared_ptr<NativeNiniXmlReader> owned;
        auto path = nini_writer_text(std::u16string(u"raw") + char16_t(0xd800) + u"path");
        io->open_path = [&](NiniWriterText received) {
            require(received == path, "Path input lost raw UTF16/reference identity");
            owned = NativeNiniXmlReader::from_text(nini_writer_text(u"<Nini/>"),
                                                   NiniXmlReaderSettings::legacy_document());
            return owned;
        };
        document->load_path(path);
        require(owned->read_state() == NiniXmlReadState::Closed, "Owned path reader was not closed");
    }
    {
        auto io = dependencies();
        auto document = NativeNiniXmlDocument::create(io);
        std::weak_ptr<NativeNiniXmlDocument> weak = document;
        io->save_writer = [&](NiniXmlDom&, std::shared_ptr<NiniTextWriter>) {
            document.reset();
            require(!weak.expired(), "Provider callback destroyed the active document receiver");
            io->save_writer = {};
            NiniXmlProviderError e("System.IO.IOException", 0x80131620,
                                   std::u16string(u"raw") + char16_t(0xd800) + u"fault");
            e.file_name = std::u16string(u"file") + char16_t(0xdfff);
            throw e;
        };
        bool caught = false;
        try {
            document->save_writer(std::make_shared<NiniStringWriter>());
        } catch (const NiniXmlProviderError& e) {
            caught = e.message == std::u16string(u"raw") + char16_t(0xd800) + u"fault" &&
                     e.file_name == std::optional<std::u16string>(std::u16string(u"file") + char16_t(0xdfff));
        }
        require(caught && weak.expired(),
                "Reentrant callback lost raw structured failure or retained its receiver");
    }
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--probe") {
            for (std::string line; std::getline(std::cin, line);)
                std::cout << flow(line) << '\n' << std::flush;
            return 0;
        }
        bool failed = false;
        try {
            native_nini_xml_config_backend({});
        } catch (const std::invalid_argument&) {
            failed = true;
        }
        if (!failed)
            throw std::runtime_error("Missing native XML document dependencies accepted");
        ownership_units();
        std::cout << "PASS: native document dependencies, ownership, identity and raw errors\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
