// Test-only required-DOM adapter backed by real System.Xml through the oracle.
// Production XmlConfigSource never starts a CLR process or supplies this adapter.
#include "opensim/framework/nini_xml_config.hpp"
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
std::optional<std::u16string> optional(const std::string& text) {
    auto value = dec(text);
    return value ? std::optional<std::u16string>(*value) : std::nullopt;
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
struct Session : std::enable_shared_from_this<Session> {
    std::string rpc(const std::string& command) {
        std::cout << "@DOM " << command << '\n' << std::flush;
        std::string result;
        if (!std::getline(std::cin, result))
            throw std::runtime_error("DOM oracle closed");
        if (result.starts_with("E ")) {
            std::istringstream row(result);
            std::string marker, type, parameter, message, uri, line, position, file, object;
            std::uint32_t code;
            row >> marker >> type >> code >> parameter >> message >> uri >> line >> position >> file >>
                object;
            NiniXmlProviderError e(type, code, *dec(message), optional(parameter));
            e.source_uri = optional(uri);
            if (line != "~")
                e.line_number = std::stoi(line);
            if (position != "~")
                e.line_position = std::stoi(position);
            e.file_name = optional(file);
            e.object_name = optional(object);
            throw e;
        }
        return result;
    }
    Text text(const std::string& command) {
        auto result = rpc(command);
        if (!result.starts_with("T "))
            throw std::runtime_error("Expected DOM text reply");
        return dec(result.substr(2));
    }
};
class Node;
class Enumerator : public NiniXmlChildEnumerator {
    std::shared_ptr<Session> s_;
    int id_;

  public:
    Enumerator(std::shared_ptr<Session> s, int id) : s_(std::move(s)), id_(id) {
    }
    bool move_next() override {
        return s_->rpc("MOVE " + std::to_string(id_)) == "1";
    }
    std::shared_ptr<NiniXmlNode> current() override;
};
class Node : public NiniXmlNode {
  public:
    std::shared_ptr<Session> s;
    int id;
    Node(std::shared_ptr<Session> session, int value) : s(std::move(session)), id(value) {
    }
    std::string arg() const {
        return std::to_string(id);
    }
    std::int32_t node_type() override {
        return std::stoi(s->rpc("TYPE " + arg()));
    }
    Text name() override {
        return s->text("NAME " + arg());
    }
    Text value() override {
        return s->text("VALUE " + arg());
    }
    void value(Text v) override {
        s->rpc("SETVAL " + arg() + " " + enc(v));
    }
    std::shared_ptr<NiniXmlNode> attribute(Text) override;
    std::unique_ptr<NiniXmlChildEnumerator> children() override {
        return std::make_unique<Enumerator>(s, std::stoi(s->rpc("ENUM " + arg())));
    }
    void append_child(std::shared_ptr<NiniXmlNode> n) override {
        s->rpc("APP " + arg() + " " + std::dynamic_pointer_cast<Node>(n)->arg());
    }
    void remove_child(std::shared_ptr<NiniXmlNode> n) override {
        s->rpc("REMOVE " + arg() + " " + std::dynamic_pointer_cast<Node>(n)->arg());
    }
    void append_attribute(std::shared_ptr<NiniXmlNode> n) override {
        s->rpc("APPATTR " + arg() + " " + std::dynamic_pointer_cast<Node>(n)->arg());
    }
};
std::shared_ptr<NiniXmlNode> node(std::shared_ptr<Session> s, int id) {
    return id ? std::make_shared<Node>(std::move(s), id) : nullptr;
}
std::shared_ptr<NiniXmlNode> Node::attribute(Text name) {
    return node(s, std::stoi(s->rpc("ATTR " + arg() + " " + enc(name))));
}
std::shared_ptr<NiniXmlNode> Enumerator::current() {
    return node(s_, std::stoi(s_->rpc("CURRENT " + std::to_string(id_))));
}
struct Reader : NiniXmlReader {
    int id;
    explicit Reader(int v) : id(v) {
    }
};
struct Writer : NiniTextWriter {
    std::shared_ptr<Session> s;
    int id;
    Writer(std::shared_ptr<Session> context, int value) : s(std::move(context)), id(value) {
    }
    void write(const Text& t) override {
        s->rpc("WW " + std::to_string(id) + " " + enc(t));
    }
    void flush() override {
        s->rpc("WFLUSH " + std::to_string(id));
    }
    void close() override {
        s->rpc("WCLOSE " + std::to_string(id));
    }
    Text to_string() override {
        return s->text("WTOS " + std::to_string(id));
    }
};
struct Stream : NiniWriterStream {
    std::shared_ptr<Session> s;
    int id;
    Stream(std::shared_ptr<Session> context, int value) : s(std::move(context)), id(value) {
    }
    void close() override {
        s->rpc("SCLOSE " + std::to_string(id));
    }
};
struct Document : NiniXmlDocument {
    std::shared_ptr<Session> s;
    int id;
    Document(std::shared_ptr<Session> context, int value) : s(std::move(context)), id(value) {
    }
    std::string arg() const {
        return std::to_string(id);
    }
    std::shared_ptr<NiniXmlNode> document_element() override {
        return node(s, std::stoi(s->rpc("DROOT " + arg())));
    }
    void load_xml(Text text) override {
        s->rpc("LOADXML " + arg() + " " + enc(text));
    }
    void load_path(Text text) override {
        s->rpc("LOADFILE " + arg() + " " + enc(text));
    }
    void load_reader(std::shared_ptr<NiniXmlReader> r) override {
        s->rpc("LOADREADER " + arg() + " " +
               std::to_string(r ? std::dynamic_pointer_cast<Reader>(r)->id : 0));
    }
    void save_path(Text text) override {
        s->rpc("SAVEFILE " + arg() + " " + enc(text));
    }
    void save_writer(std::shared_ptr<NiniTextWriter> w) override {
        s->rpc("SAVEWRITER " + arg() + " " +
               std::to_string(w ? std::dynamic_pointer_cast<Writer>(w)->id : 0));
    }
    void save_stream(std::shared_ptr<NiniWriterStream> stream) override {
        s->rpc("SAVESTREAM " + arg() + " " +
               std::to_string(stream ? std::dynamic_pointer_cast<Stream>(stream)->id : 0));
    }
    std::shared_ptr<NiniXmlNode> create_element(Text text) override {
        return node(s, std::stoi(s->rpc("DELEM " + arg() + " " + enc(text))));
    }
    std::shared_ptr<NiniXmlNode> create_attribute(Text text) override {
        return node(s, std::stoi(s->rpc("DATTR " + arg() + " " + enc(text))));
    }
};
std::string flow(const std::string& input) {
    auto session = std::make_shared<Session>();
    session->rpc("RESET");
    auto arena = std::make_shared<NiniConfigArena>(native_nini_invariant_config_backend());
    auto backend = std::make_shared<NiniXmlConfigBackend>();
    backend->create_document = [session] {
        return std::make_shared<Document>(session, std::stoi(session->rpc("DNEW")));
    };
    backend->string_writer = [session] {
        return std::make_shared<Writer>(session, std::stoi(session->rpc("WPLAIN")));
    };
    std::array<std::shared_ptr<NiniXmlConfigSource>, 3> sources;
    std::shared_ptr<Writer> last_writer;
    std::shared_ptr<Stream> last_stream;
    std::shared_ptr<Reader> last_reader;
    std::string trace, fault;
    int nth = 1, seen = 0, mutation = 0;
    std::uint64_t identity = 0;
    auto reader = [session](Text text, int mode) -> std::shared_ptr<Reader> {
        auto id = std::stoi(session->rpc("RNEW " + enc(text) + " " + std::to_string(mode)));
        return id ? std::make_shared<Reader>(id) : nullptr;
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
            state +=
                ";" +
                (doc ? enc(session->text("OUTER " + std::dynamic_pointer_cast<Document>(doc)->arg())) : "~");
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
            if (!source && op != "NEW" && op != "PATH" && op != "READER" && op != "FAIL" && op != "IOSTATE")
                throw NiniConfigError(NiniConfigError::Kind::NullReference,
                                      u"Object reference not set to an instance of an object.");
            if (op == "NEW") {
                sources[i] = NiniXmlConfigSource::create(arena, backend);
                attach(sources[i], i);
            } else if (op == "PATH") {
                auto path = text();
                sources[i] = NiniXmlConfigSource::create(arena, backend, path);
                attach(sources[i], i);
            } else if (op == "READER" || op == "LOADR") {
                auto xml = text();
                int mode;
                row >> mode;
                last_reader = reader(xml, mode);
                if (op == "READER") {
                    sources[i] = NiniXmlConfigSource::create(arena, backend, last_reader);
                    attach(sources[i], i);
                } else
                    source->load(last_reader);
            } else if (op == "LOADLAST") {
                int count;
                row >> count;
                if (last_reader)
                    session->rpc("RSTEP " + std::to_string(last_reader->id) + " " + std::to_string(count));
                source->load(last_reader);
            } else if (op == "LOADP")
                source->load(text());
            else if (op == "STRING")
                result = "T " + enc(source->to_string());
            else if (op == "SAVE")
                source->save();
            else if (op == "SAVEP")
                source->save(text());
            else if (op == "SAVEW") {
                int fail;
                row >> fail;
                last_writer =
                    fail == -2
                        ? nullptr
                        : std::make_shared<Writer>(
                              session, std::stoi(session->rpc("WNEW " + std::to_string(std::max(0, fail)))));
                if (fail == -1)
                    last_writer->close();
                source->save(std::static_pointer_cast<NiniTextWriter>(last_writer));
            } else if (op == "SAVES") {
                int fail;
                row >> fail;
                last_stream =
                    fail == -2
                        ? nullptr
                        : std::make_shared<Stream>(
                              session, std::stoi(session->rpc("SNEW " + std::to_string(std::max(0, fail)))));
                if (fail == -1)
                    last_stream->close();
                source->save(std::static_pointer_cast<NiniWriterStream>(last_stream));
            } else if (op == "RELOAD")
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
            } else if (op == "FILE") {
                auto path = text(), xml = text();
                session->rpc("FILEWRITE " + enc(path) + " " + enc(xml));
            } else if (op == "FAIL") {
                auto name = text();
                fault = name ? utf8(*name) : std::string{};
                row >> nth >> mutation;
                seen = 0;
            } else if (op == "IOSTATE")
                result =
                    (last_writer ? session->rpc("WSTATE " + std::to_string(last_writer->id)) : "~") + ";" +
                    (last_stream ? session->rpc("SSTATE " + std::to_string(last_stream->id)) : "~") + ";" +
                    (last_reader ? session->rpc("RSTATE " + std::to_string(last_reader->id)) : "~");
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
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--probe") {
            for (std::string line; std::getline(std::cin, line);)
                std::cout << flow(line) << '\n' << std::flush;
            return 0;
        }
        bool failed = false;
        try {
            auto arena = std::make_shared<NiniConfigArena>(native_nini_invariant_config_backend());
            NiniXmlConfigSource::create(arena, {});
        } catch (const std::invalid_argument&) {
            failed = true;
        }
        if (!failed)
            throw std::runtime_error("Missing XML provider accepted");
        std::cout << "PASS: XmlConfigSource requires explicit complete XML provider\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
