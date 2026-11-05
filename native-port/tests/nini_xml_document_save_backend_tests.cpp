#define main unused_save_probe_main
#include "nini_xml_document_save_tests.cpp"
#undef main
#include <filesystem>
#include <fstream>
struct Memory : NiniByteStream {
    std::vector<std::uint8_t> data;
    std::size_t pos = 0;
    bool closed = false;
    bool can_read() override {
        return !closed;
    }
    bool can_write() override {
        return !closed;
    }
    bool can_seek() override {
        return !closed;
    }
    std::int64_t position() override {
        return pos;
    }
    std::size_t read(std::span<std::uint8_t>, std::size_t, std::size_t) override {
        throw std::logic_error("unused read");
    }
    void write(std::span<const std::uint8_t> bytes) override {
        if (pos + bytes.size() > data.size())
            data.resize(pos + bytes.size());
        std::copy(bytes.begin(), bytes.end(), data.begin() + pos);
        pos += bytes.size();
    }
    void flush() override {}
    void close() override {
        closed = true;
    }
};
std::string bytehex(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty())
        return "-";
    std::string out;
    for (auto c : bytes) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    auto path = std::filesystem::absolute(argv[1]) / "output.xml";
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream input(line);
        int mode, event;
        std::string xml;
        input >> mode >> event >> xml;
        auto uri = std::make_shared<NiniXmlUriReaderBackend>();
        uri->get_temp_resolver = []() -> std::shared_ptr<NiniXmlUriReaderResolver> {
            throw std::logic_error("No filename load in this focused Save flow");
        };
        auto backend = native_nini_xml_config_backend(native_xml_document_io(uri));
        auto arena = std::make_shared<NiniConfigArena>(native_nini_invariant_config_backend());
        std::shared_ptr<NiniXmlConfigSource> source;
        auto memory = std::make_shared<Memory>();
        auto text = std::make_shared<NiniStringWriter>();
        int saved = 0;
        std::string failure = "-", payload = "~";
        {
            std::ofstream seed(path, std::ios::binary);
            seed << "previous";
        }
        try {
            NiniXmlReaderSettings settings;
            settings.dtd_processing = NiniXmlDtdProcessing::Parse;
            source = NiniXmlConfigSource::create(arena, backend,
                                                 NativeNiniXmlReader::from_text(decode(xml), settings));
            source->saved.add(1, [&](auto&, auto) {
                ++saved;
                if (event == 1)
                    throw NiniXmlProviderError("System.InvalidOperationException", 0x80131509,
                                               u"saved event fault");
                if (event == 2)
                    source->add_config(nini_writer_text(u"Event"))
                        ->set_text(nini_writer_text(u"key"), nini_writer_text(u"value"));
            });
            source->add_config(nini_writer_text(u"Added"))
                ->set_text(nini_writer_text(u"Escaped"), nini_writer_text(u"<&>\"'\r\n"));
            switch (mode) {
            case 0:
                payload = hex(source->to_string());
                break;
            case 1:
                source->save(std::static_pointer_cast<NiniTextWriter>(text));
                break;
            case 2:
                source->save(std::static_pointer_cast<NiniWriterStream>(memory));
                break;
            case 3:
                source->save(
                    std::static_pointer_cast<NiniTextWriter>(std::make_shared<NiniByteTextWriter>(memory)));
                break;
            case 4:
                source->save(nini_writer_text(path.u16string()));
                break;
            case 5:
                source->save(std::shared_ptr<NiniTextWriter>{});
                break;
            case 6:
                source->save(std::shared_ptr<NiniWriterStream>{});
                break;
            case 7:
                source->save(nini_writer_text(std::filesystem::absolute(argv[1]).u16string()));
                break;
            }
        } catch (...) {
            failure = current_error();
        }
        if (mode == 1)
            payload = hex(text->to_string());
        if (mode == 2 || mode == 3)
            payload = bytehex(memory->data);
        if (mode == 4 || mode == 7) {
            std::ifstream f(path, std::ios::binary);
            std::string b(std::istreambuf_iterator<char>(f), {});
            payload = bytehex(std::vector<std::uint8_t>(b.begin(), b.end()));
        }
        NiniWriterText post;
        std::string posterror = "-";
        try {
            if (source)
                post = source->to_string();
        } catch (...) {
            posterror = current_error();
        }
        std::cout << failure << ';' << payload << ';' << saved << ';'
                  << hex(source ? source->save_path() : nullptr) << ';' << memory->closed << ';' << hex(post)
                  << ';' << posterror << '\n';
    }
}
