#include "opensim/framework/nini_xml_reader.hpp"
#include <iostream>
#include <algorithm>
#include <sstream>
using namespace opensim::framework;
std::u16string decode(std::string s) {
    std::u16string v;
    if (s != "-")
        for (std::size_t i = 0; i < s.size(); i += 4)
            v += static_cast<char16_t>(std::stoul(s.substr(i, 4), nullptr, 16));
    return v;
}
std::string encode(std::u16string_view s) {
    if (s.empty())
        return "-";
    constexpr char h[] = "0123456789abcdef";
    std::string out;
    for (auto c : s)
        for (int b = 12; b >= 0; b -= 4)
            out += h[(c >> b) & 15];
    return out;
}
std::string shape(std::shared_ptr<NiniXmlDomNode> node) {
    std::string out = std::to_string(node->node_type()) + ":" + encode(*node->name()) + ":" +
                      encode(*node->prefix()) + ":" + encode(*node->local_name()) + ":" +
                      encode(*node->namespace_uri()) + ":" + (node->value() ? encode(*node->value()) : "~") +
                      ":" + (node->is_empty_element() ? "1" : "0") + "[";
    auto e = node->children();
    while (e->move_next())
        out += shape(std::dynamic_pointer_cast<NiniXmlDomNode>(e->current())) + ",";
    out += "]{";
    for (auto a : node->attributes())
        out += shape(a) + ",";
    return out + "}";
}
std::string summary(NiniXmlDom& document) {
    auto collection = document.defer_collection();
    std::vector<std::pair<std::shared_ptr<NiniXmlDomNode>, std::size_t>> pending{
        {document.document_node(), 0}};
    std::uint64_t nodes = 0, attrs = 0, maximum = 0, units = 0, hash = 14695981039346656037ull;
    auto number = [&](std::uint64_t value) { hash = (hash ^ value) * 1099511628211ull; };
    auto text = [&](NiniWriterText value) {
        number(value ? 1 : 0);
        if (value) {
            number(value->size());
            for (auto c : *value)
                number(c);
        }
    };
    while (!pending.empty()) {
        auto [node, depth] = std::move(pending.back());
        pending.pop_back();
        ++nodes;
        maximum = std::max<std::uint64_t>(maximum, depth);
        number(node->node_type());
        number(depth);
        number(node->is_empty_element() ? 1 : 0);
        text(node->name());
        text(node->prefix());
        text(node->local_name());
        text(node->namespace_uri());
        auto value = node->value();
        text(value);
        if (value)
            units += value->size();
        auto attributes = node->attributes();
        attrs += attributes.size();
        std::vector<std::shared_ptr<NiniXmlDomNode>> children;
        auto e = node->children();
        while (e->move_next())
            children.push_back(std::dynamic_pointer_cast<NiniXmlDomNode>(e->current()));
        number(children.size());
        number(attributes.size());
        for (auto it = children.rbegin(); it != children.rend(); ++it)
            pending.emplace_back(*it, depth + 1);
        for (auto it = attributes.rbegin(); it != attributes.rend(); ++it)
            pending.emplace_back(*it, depth + 1);
    }
    return std::to_string(nodes) + "," + std::to_string(attrs) + "," + std::to_string(maximum) + "," +
           std::to_string(units) + "," + std::to_string(hash);
}
void navigation(std::ostringstream& out, const std::shared_ptr<NativeNiniXmlReader>& r,
                bool resolve = false) {
    auto snapshot = [&]() {
        auto& t = r->token();
        out << t.node_type << ':' << t.depth << ':' << t.line_number << ':' << t.line_position << ':'
            << encode(t.name) << ':' << encode(t.value) << ',';
    };
    out << "NAV[";
    if (r->move_to_first_attribute()) {
        do {
            snapshot();
            for (int j = 0; j < (resolve ? 1024 : 2); ++j) {
                bool moved = r->read_attribute_value();
                out << (moved ? 1 : 0) << ':';
                snapshot();
                if (resolve && !moved)
                    break;
                if (resolve && r->token().node_type == 5)
                    r->resolve_entity();
            }
        } while (r->move_to_next_attribute());
        out << (r->move_to_element() ? 1 : 0) << ':';
        snapshot();
    }
    out << "]";
}
class TracingInput final : public NativeNiniXmlByteInput {
  public:
    std::vector<std::uint8_t> bytes;
    std::size_t position = 0;
    bool seekable = true;
    int chunk = 3;
    std::string trace;
    std::optional<std::int64_t> length_hint() const override {
        return seekable ? std::optional<std::int64_t>(bytes.size()) : std::nullopt;
    }
    std::int32_t read(std::span<std::uint8_t> target) override {
        trace += "R" + std::to_string(target.size()) + ",";
        auto count = std::min({target.size(), bytes.size() - position, static_cast<std::size_t>(chunk)});
        std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(position), count, target.begin());
        position += count;
        return static_cast<std::int32_t>(count);
    }
    void close() override {
        trace += "C,";
    }
};
int main(int argc, char** argv) {
    if (argc == 1) {
        NiniXmlReaderSettings options;
        options.dtd_processing = NiniXmlDtdProcessing::Ignore;
        auto r = NativeNiniXmlReader::from_text(nini_writer_text(u"<Nini/>"), options);
        if (!r->read() || r->token().node_type != 1 || r->token().name != u"Nini" ||
            !r->token().is_empty_element || r->read() || !r->eof())
            throw std::runtime_error("Native XML reader unit failed");
        auto attributes = NativeNiniXmlReader::from_text(
            nini_writer_text(u"<?xml version='1.0'?><a x='v'>text</a>"), options);
        if (!attributes->read() || !attributes->move_to_first_attribute() ||
            attributes->token().name != u"version" || !attributes->read_attribute_value() ||
            attributes->token().line_position != 16 || attributes->read_attribute_value() ||
            !attributes->move_to_element() || !attributes->read() || !attributes->move_to_first_attribute() ||
            attributes->token().value != u"v" || !attributes->read() || attributes->token().node_type != 3 ||
            attributes->move_to_first_attribute())
            throw std::runtime_error("Native XML attribute navigation/lifetime unit failed");
        return 0;
    }
    int stream_chunk = 3;
    std::int64_t doc_limit = 0, entity_limit = -1;
    bool normalize = false, resolve = false, load = false, byte_mode = false, legacy = false,
         standalone = false, nav = false, prohibit = false, stream_mode = false, nonseek = false,
         close_stream = false, capture_state = false, start_attribute = false, summarize = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--normalize")
            normalize = true;
        if (std::string(argv[i]).starts_with("--doc-limit="))
            doc_limit = std::stoll(std::string(argv[i]).substr(12));
        if (std::string(argv[i]).starts_with("--entity-limit="))
            entity_limit = std::stoll(std::string(argv[i]).substr(15));
        if (std::string(argv[i]) == "--resolve")
            resolve = true;
        if (std::string(argv[i]) == "--summary")
            summarize = true;
        if (std::string(argv[i]) == "--state")
            capture_state = true;
        if (std::string(argv[i]) == "--start-attribute")
            start_attribute = true;
        if (std::string(argv[i]).starts_with("--chunk="))
            stream_chunk = std::stoi(std::string(argv[i]).substr(8));
        if (std::string(argv[i]) == "--stream")
            stream_mode = true;
        if (std::string(argv[i]) == "--nonseek")
            nonseek = true;
        if (std::string(argv[i]) == "--close-stream")
            close_stream = true;
        if (std::string(argv[i]) == "--prohibit")
            prohibit = true;
        if (std::string(argv[i]) == "--navigation")
            nav = true;
        if (std::string(argv[i]) == "--load")
            load = true;
        if (std::string(argv[i]) == "--bytes")
            byte_mode = true;
        if (std::string(argv[i]) == "--legacy")
            legacy = true;
        if (std::string(argv[i]) == "--standalone")
            standalone = true;
    }
    std::string line;
    while (std::getline(std::cin, line)) {
        std::ostringstream out;
        NiniXmlReaderSettings settings =
            legacy ? NiniXmlReaderSettings::legacy_document() : NiniXmlReaderSettings{};
        if (standalone)
            settings.validating_reader_compatibility = false;
        if (!legacy)
            settings.dtd_processing = prohibit ? NiniXmlDtdProcessing::Prohibit : NiniXmlDtdProcessing::Parse;
        settings.close_input = close_stream || legacy;
        if (normalize)
            settings.normalize = true;
        settings.max_characters_in_document = doc_limit;
        if (entity_limit >= 0)
            settings.max_characters_from_entities = entity_limit;
        std::shared_ptr<TracingInput> stream;
        std::shared_ptr<NativeNiniXmlReader> r;
        NiniXmlDom document;
        try {
            if (byte_mode) {
                std::vector<std::uint8_t> bytes;
                if (line != "-")
                    for (std::size_t i = 0; i < line.size(); i += 2)
                        bytes.push_back(
                            static_cast<std::uint8_t>(std::stoul(line.substr(i, 2), nullptr, 16)));
                if (stream_mode) {
                    stream = std::make_shared<TracingInput>();
                    stream->bytes = std::move(bytes);
                    stream->seekable = !nonseek;
                    stream->chunk = stream_chunk;
                    r = NativeNiniXmlReader::from_stream(stream, settings);
                } else
                    r = NativeNiniXmlReader::from_bytes(bytes, settings);
            } else
                r = NativeNiniXmlReader::from_text(nini_writer_text(decode(line)), settings);
            if (start_attribute) {
                r->read();
                r->move_to_first_attribute();
            }
            if (load) {
                native_nini_xml_load(document, r);
                out << "OK";
            } else {
                while (r->read()) {
                    auto& t = r->token();
                    out << t.node_type << ':' << t.depth << ':' << t.line_number << ':' << t.line_position
                        << ':' << (t.is_empty_element ? 1 : 0) << ':' << encode(t.name) << ':'
                        << encode(t.prefix) << ':' << encode(t.local_name) << ':' << encode(t.namespace_uri)
                        << ':' << encode(t.value) << '[';
                    for (auto& a : t.attributes)
                        out << encode(a.name) << ':' << encode(a.prefix) << ':' << encode(a.local_name) << ':'
                            << encode(a.namespace_uri) << ':' << encode(a.value) << ',';
                    out << "]|";
                    if (nav)
                        navigation(out, r, resolve);
                    if (resolve && r->token().node_type == 5)
                        r->resolve_entity();
                }
                out << "END";
            }
        } catch (const NiniXmlProviderError& e) {
            out << "E:" << e.source_type << ':' << e.hresult << ':' << encode(e.message) << ':'
                << (e.line_number ? std::to_string(*e.line_number) : "~") << ':'
                << (e.line_position ? std::to_string(*e.line_position) : "~");
        }
        if (capture_state && r)
            out << "/STATE:" << static_cast<int>(r->read_state()) << "," << (r->eof() ? 1 : 0) << ","
                << r->token().node_type << "," << r->token().depth;
        if (r)
            r->close();
        if (stream_mode && stream)
            out << "/IO:" << stream->trace;
        if (load)
            out << "/" << (summarize ? summary(document) : shape(document.document_node()));
        std::cout << out.str() << '\n';
        std::cout.flush();
    }
}
