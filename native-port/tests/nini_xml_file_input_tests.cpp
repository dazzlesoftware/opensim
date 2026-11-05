#include "opensim/framework/nini_xml_file_input.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <vector>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <array>
using namespace opensim::framework;
std::string hex(std::u16string_view s) {
    std::string r;
    for (auto c : s)
        for (int b = 12; b >= 0; b -= 4)
            r += "0123456789abcdef"[(c >> b) & 15];
    return r;
}
int file_unit() {
    const auto path = std::filesystem::temp_directory_path() /
                      ("opensim-native-xml-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".xml");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } cleanup{path};
    {
        std::ofstream out(path, std::ios::binary);
        out << "<!DOCTYPE root [<!ATTLIST root x CDATA 'default'>]><root>text</root>";
        if (!out)
            throw std::runtime_error("Cannot create XML input fixture");
    }
    auto file = native_nini_xml_file_input(nini_writer_text(path.u16string()));
    if (!file->length_hint() || *file->length_hint() <= 0)
        throw std::runtime_error("Real XML file must expose length");
    auto settings = NiniXmlReaderSettings::legacy_document();
    settings.close_input = true;
    auto reader = NativeNiniXmlReader::from_stream(file, settings);
    NiniXmlDom dom;
    native_nini_xml_load(dom, reader);
    auto root = dom.document_element();
    if (!root || *root->name() != u"root" ||
        (!root->first_child() || *root->first_child()->value() != u"text") ||
        *root->attribute(nini_writer_text(u"x"))->value() != u"default")
        throw std::runtime_error("Real XML file must parse DTD defaults and content");
    reader->close();
    bool closed = false;
    try {
        std::array<std::uint8_t, 1> byte{};
        file->read(byte);
    } catch (const NiniXmlProviderError& e) {
        closed = e.source_type == "System.ObjectDisposedException";
    }
    if (!closed)
        throw std::runtime_error("Owned XML reader must close its actual file");
    bool null = false;
    try {
        native_nini_xml_file_input({});
    } catch (const NiniXmlProviderError& e) {
        null = e.source_type == "System.ArgumentNullException" &&
               e.parameter == std::optional<std::u16string>(u"path");
    }
    if (!null)
        throw std::runtime_error("Null XML file path must retain typed error");
    return 0;
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    if (argc == 1)
        return file_unit();
    if (argc != 2)
        return 2;
    auto path = nini_writer_text(std::u16string(reinterpret_cast<char16_t*>(argv[1])));
#else
int main(int argc, char** argv) {
    if (argc == 1)
        return file_unit();
    if (argc != 2)
        return 2;
    auto path = nini_writer_text(utf16_replacing(argv[1]));
#endif
    for (int size : {0, 1, 2, 3, 4, 7, 4096, 4097, 65536}) {
        std::shared_ptr<NativeNiniXmlByteInput> f;
        try {
            f = native_nini_xml_file_input(path);
            std::cout << "L" << f->length_hint().value_or(-1) << ":";
            std::vector<uint8_t> b(size);
            for (int i = 0; i < 4; ++i) {
                auto n = f->read(b);
                uint64_t sum = 0;
                for (int j = 0; j < n; ++j)
                    sum = sum * 131 + b[j];
                std::cout << n << "," << sum << ";";
            }
            f->close();
            std::cout << "CL" << f->length_hint().value_or(-1) << ":";
            try {
                f->read(b);
            } catch (const NiniXmlProviderError& e) {
                std::cout << e.source_type << ":" << e.hresult << ":" << hex(e.message);
            }
            std::cout << "\n";
        } catch (const NiniXmlProviderError& e) {
            std::cout << "E:" << e.source_type << ":" << e.hresult << ":" << hex(e.message) << ":"
                      << (e.file_name ? hex(*e.file_name) : "-") << "\n";
        }
    }
}
