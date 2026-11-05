// Concrete retained DOM/reader binding for bundled Nini XmlConfigSource.
// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/nini_xml_reader.hpp"
#include <unordered_map>
namespace opensim::framework {
// Required remaining System.Xml file/writer closures. These callbacks are
// operations, not permissive parser or no-op Save substitutes. open_path must
// construct the original document-load legacy reader, including its path/URI,
// encoding, DTD, ownership and constructor-failure policies.
struct NiniXmlNativeDocumentIo {
    std::function<std::shared_ptr<NativeNiniXmlReader>(NiniWriterText)> open_path;
    std::function<void(NiniXmlDom&, NiniWriterText)> save_path;
    std::function<void(NiniXmlDom&, std::shared_ptr<NiniTextWriter>)> save_writer;
    std::function<void(NiniXmlDom&, std::shared_ptr<NiniWriterStream>)> save_stream;
};
class NativeNiniXmlDocument final : public NiniXmlDocument,
                                    public std::enable_shared_from_this<NativeNiniXmlDocument> {
    friend class NativeNiniXmlDocumentNode;
    friend class NativeNiniXmlDocumentEnumerator;
    std::shared_ptr<NiniXmlNativeDocumentIo> io_;
    NiniXmlDom dom_;
    std::unordered_map<std::size_t, std::weak_ptr<NiniXmlNode>> node_handles_;
    std::shared_ptr<NiniXmlNode> wrap(std::shared_ptr<NiniXmlDomNode>);
    explicit NativeNiniXmlDocument(std::shared_ptr<NiniXmlNativeDocumentIo>);
    void load_owned(const std::shared_ptr<NativeNiniXmlReader>&);

public:
    static std::shared_ptr<NativeNiniXmlDocument> create(const std::shared_ptr<NiniXmlNativeDocumentIo>&);
    std::shared_ptr<NiniXmlNode> document_element() override;
    void load_xml(NiniWriterText) override;
    void load_path(NiniWriterText) override;
    void load_reader(std::shared_ptr<NiniXmlReader>) override;
    void save_path(NiniWriterText) override;
    void save_writer(std::shared_ptr<NiniTextWriter>) override;
    void save_stream(std::shared_ptr<NiniWriterStream>) override;
    std::shared_ptr<NiniXmlNode> create_element(NiniWriterText) override;
    std::shared_ptr<NiniXmlNode> create_attribute(NiniWriterText) override;
    NiniXmlDom& native_dom() noexcept {
        return dom_;
    }
    const NiniXmlDom& native_dom() const noexcept {
        return dom_;
    }
};
std::shared_ptr<NiniXmlConfigBackend>
native_nini_xml_config_backend(const std::shared_ptr<NiniXmlNativeDocumentIo>&);
}
