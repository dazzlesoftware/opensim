#include "opensim/framework/nini_xml_uri_reader.hpp"
#include "opensim/framework/nini_xml_reader.hpp"
#include "opensim/framework/nini_xml_native_document.hpp"
#include <iostream>
#include <algorithm>
#include "opensim/framework/nini_xml_dom.hpp"
using namespace opensim::framework;
std::string err(const NiniXmlProviderError& e) {
    std::string h;
    for (auto c : e.message)
        for (int b = 12; b >= 0; b -= 4)
            h += "0123456789abcdef"[(c >> b) & 15];
    return "E[" + e.source_type + ":" + std::to_string(e.hresult) + ":" + h + "]";
}
struct Input : NativeNiniXmlByteInput {
    std::string* trace;
    std::string body = "<a/>";
    size_t pos = 0;
    int fault = 0;
    int reads = 0;
    std::optional<int64_t> length_hint() const override {
        *trace += "L,";
        if (fault == 2)
            throw NiniXmlProviderError("System.IO.IOException", 0x80131620, u"length");
        return body.size();
    }
    int32_t read(std::span<uint8_t> d) override {
        *trace += "R" + std::to_string(d.size()) + ",";
        if (fault == 3 || (fault == 7 && ++reads == 2))
            throw NiniXmlProviderError("System.IO.IOException", 0x80131620, u"read");
        auto n = std::min(d.size(), body.size() - pos);
        if (fault == 7)
            n = std::min<size_t>(n, 2);
        std::copy_n(body.begin() + pos, n, d.begin());
        pos += n;
        return (int32_t)n;
    }
    void close() override {
        *trace += "C,";
    }
};
struct Resolver : NiniXmlUriReaderResolver {
    std::string trace;
    int mode = 0;
    int entities = 0;
    std::function<void()> on_entity;
    std::string body = "<a/>";
    std::shared_ptr<NiniXmlReaderUri> resolve_uri(NiniWriterText) override {
        trace += "U,";
        auto u = std::make_shared<NiniXmlReaderUri>();
        u->to_string = [this] {
            trace += "S,";
            return nini_writer_text(u"file:///probe.xml");
        };
        return u;
    }
    std::shared_ptr<NativeNiniXmlByteInput> get_entity(std::shared_ptr<NiniXmlReaderUri>) override {
        trace += "G,";
        if (on_entity)
            on_entity();
        ++entities;
        if (mode == 4)
            return {};
        if (mode == 1)
            throw NiniXmlProviderError("System.IO.IOException", 0x80131620, u"entity");
        auto i = std::make_shared<Input>();
        i->trace = &trace;
        i->body = body;
        i->fault = (mode == 5 ? (entities == 1 ? 2 : 0) : mode == 6 ? (entities == 1 ? 3 : 0) : mode);
        return i;
    }
};
int main(int argc, char**) {
    if (argc == 1) {
        auto resolver = std::make_shared<Resolver>();
        auto backend = std::make_shared<NiniXmlUriReaderBackend>();
        backend->get_temp_resolver = [resolver] { return resolver; };
        auto reader = native_nini_xml_uri_reader(nini_writer_text(u"probe.xml"), backend);
        if (resolver->trace != "U,S,")
            throw std::runtime_error("URI constructor must resolve without opening input");
        if (!reader->read() || reader->token().name != u"a")
            throw std::runtime_error("Deferred URI read must parse actual element");
        reader->close();
        if (resolver->trace.find("G,") == std::string::npos ||
            resolver->trace.find("C,") == std::string::npos)
            throw std::runtime_error("URI stream open/close ownership unit");
        return 0;
    }
    for (int mode = 0; mode < 8; ++mode)
        for (int action = 0; action < 5; ++action) {
            auto r = std::make_shared<Resolver>();
            r->mode = mode;
            auto b = std::make_shared<NiniXmlUriReaderBackend>();
            b->get_temp_resolver = [r] { return r; };
            auto reader = native_nini_xml_uri_reader(nini_writer_text(u"probe.xml"), b);
            std::string states;
            for (int j = 0; j < action; ++j) {
                try {
                    states += reader->read() ? "T" : "F";
                } catch (const NiniXmlProviderError& e) {
                    states += err(e);
                }
                states += std::to_string((int)reader->read_state()) + ",";
            }
            reader->close();
            std::cout << mode << ":" << action << ":" << states << ":" << r->trace << "\n";
        }
    for (int mode = 0; mode < 8; ++mode)
        for (int body = 0; body < 7; ++body) {
            auto resolver = std::make_shared<Resolver>();
            resolver->mode = mode;
            resolver->body =
                body == 0   ? "<a/>"
                : body == 1 ? "<a/><b/>"
                : body == 2 ? "<a><b/></a><"
                : body == 3 ? "<a><b/>"
                : body == 4 ? "<!DOCTYPE a [<!ENTITY e 'value'><!ATTLIST a x CDATA 'default'>]><a>&e;</a>"
                : body == 5
                    ? "<!DOCTYPE a [<!ENTITY % p '<!ELEMENT a ANY>'>%p;<!ATTLIST a x NMTOKENS ' a  b '>]><a/>"
                    : "<!DOCTYPE a [<!ELEMENT a (b|)>]><a/>";
            auto backend = std::make_shared<NiniXmlUriReaderBackend>();
            backend->get_temp_resolver = [resolver] { return resolver; };
            auto io = std::make_shared<NiniXmlNativeDocumentIo>();
            io->open_path = native_nini_xml_uri_reader_factory(backend);
            io->save_path = [](auto&, auto) { throw std::logic_error("unused"); };
            io->save_stream = [](auto&, auto) { throw std::logic_error("unused"); };
            io->save_writer = [](auto&, auto) { throw std::logic_error("unused"); };
            auto doc = NativeNiniXmlDocument::create(io);
            doc->load_xml(nini_writer_text(u"<old/>"));
            resolver->on_entity = [&] { resolver->trace += doc->document_element() ? "OLD," : "EMPTY,"; };
            std::string status = "OK";
            try {
                doc->load_path(nini_writer_text(u"probe.xml"));
            } catch (const NiniXmlProviderError& e) {
                status = err(e);
            }
            auto root = doc->native_dom().document_element();
            std::string name = root ? std::string(root->name()->begin(), root->name()->end()) : "-";
            std::string meta = "[";
            if (root)
                for (auto& a : root->attributes()) {
                    meta += std::string(a->name()->begin(), a->name()->end()) + "=";
                    auto attribute_value = a->value();
                    for (auto c : *attribute_value)
                        for (int bit = 12; bit >= 0; bit -= 4)
                            meta += "0123456789abcdef"[(c >> bit) & 15];
                    meta += a->specified() ? "/True;" : "/False;";
                }
            meta += "]";
            std::cout << "D" << mode << ":" << body << ":" << status << ":" << name << meta << ":"
                      << resolver->trace << "\n";
        }
}
