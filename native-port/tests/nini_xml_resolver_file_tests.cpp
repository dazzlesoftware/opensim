#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/nini_xml_resolver.hpp"
#include "opensim/framework/nini_xml_reader.hpp"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iostream>
using namespace opensim::framework;
int main() {
    try {
        auto folder = std::filesystem::temp_directory_path() /
                      ("opensim-uri-load-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(folder);
        auto path = folder / std::filesystem::path(u8"config space é.xml");
        struct Cleanup {
            std::filesystem::path file, folder;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove(file, ec);
                std::filesystem::remove(folder, ec);
            }
        } cleanup{path, folder};
        {
            std::ofstream out(path, std::ios::binary);
            out << "<!DOCTYPE root [<!ATTLIST root x CDATA 'default'>]><root>text</root>";
            if (!out)
                throw std::runtime_error("Cannot create real resolver file fixture");
        }
        auto operations = native_config_uri_object_backend();
        int nonfile_calls = 0;
        auto backend = native_nini_xml_url_reader_backend(
            operations, [&](ConfigUriObjectPtr) -> std::shared_ptr<NativeNiniXmlByteInput> {
                ++nonfile_calls;
                throw std::runtime_error("File URI unexpectedly requested non-file transport");
            });
        auto input = nini_writer_text(path.u16string());
        auto escaped = operations->create(input, ConfigUriObjectKind::Absolute)->absolute_uri();
        for (auto name : {input, escaped}) {
            auto reader = native_nini_xml_uri_reader(name, backend);
            NiniXmlDom dom;
            native_nini_xml_load(dom, reader);
            auto root = dom.document_element();
            if (!root || *root->name() != u"root" || !root->first_child() ||
                *root->first_child()->value() != u"text" || !root->attribute(nini_writer_text(u"x")) ||
                *root->attribute(nini_writer_text(u"x"))->value() != u"default")
                throw std::runtime_error("Composed resolver did not retain XML content/DTD defaults");
            reader->close();
        }
        auto missing =
            native_nini_xml_uri_reader(nini_writer_text((folder / "missing.xml").u16string()), backend);
        bool failed = false;
        try {
            missing->read();
        } catch (const NiniXmlProviderError& e) {
            failed = e.source_type == "System.IO.FileNotFoundException";
        }
        missing->close();
        if (!failed || nonfile_calls)
            throw std::runtime_error("Deferred file acquisition or URI dispatch differs");
        std::cout << "real path + escaped file URI + DTD defaults + deferred missing file: passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
