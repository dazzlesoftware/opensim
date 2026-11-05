// Native XmlDocument load orchestration follows runtime v8.0.31 XmlDocument.
// Copyright (c) .NET Foundation and Contributors. MIT license:
// native-port/third_party/dotnet_collation/LICENSE.
#include "opensim/framework/nini_xml_native_document.hpp"
namespace opensim::framework {
namespace {
void require_io(const std::shared_ptr<NiniXmlNativeDocumentIo>& io) {
    if (!io || !io->open_path || !io->save_path || !io->save_writer || !io->save_stream)
        throw std::invalid_argument(
            "Native XML document requires explicit complete path-reader and writer operations");
}
}
// A public node owns its concrete document/provider. The document caches only
// weak proxies, so no document -> node -> document ownership cycle is formed.
class NativeNiniXmlDocumentNode final : public NiniXmlNode {
    std::shared_ptr<NativeNiniXmlDocument> owner_;
    std::shared_ptr<NiniXmlDomNode> node_;
    static std::shared_ptr<NiniXmlNode> unwrap(const std::shared_ptr<NiniXmlNode>& value) {
        if (auto proxy = std::dynamic_pointer_cast<NativeNiniXmlDocumentNode>(value))
            return proxy->node_;
        return value;
    }

  public:
    NativeNiniXmlDocumentNode(std::shared_ptr<NativeNiniXmlDocument> owner,
                              std::shared_ptr<NiniXmlDomNode> node)
        : owner_(std::move(owner)), node_(std::move(node)) {
    }
    std::int32_t node_type() override {
        return node_->node_type();
    }
    NiniWriterText name() override {
        return node_->name();
    }
    NiniWriterText value() override {
        return node_->value();
    }
    void value(NiniWriterText value) override {
        node_->value(std::move(value));
    }
    std::shared_ptr<NiniXmlNode> attribute(NiniWriterText name) override {
        return owner_->wrap(std::dynamic_pointer_cast<NiniXmlDomNode>(node_->attribute(std::move(name))));
    }
    std::unique_ptr<NiniXmlChildEnumerator> children() override;
    void append_child(std::shared_ptr<NiniXmlNode> value) override {
        node_->append_child(unwrap(value));
    }
    void remove_child(std::shared_ptr<NiniXmlNode> value) override {
        node_->remove_child(unwrap(value));
    }
    void append_attribute(std::shared_ptr<NiniXmlNode> value) override {
        node_->append_attribute(unwrap(value));
    }
};
class NativeNiniXmlDocumentEnumerator final : public NiniXmlChildEnumerator {
    std::shared_ptr<NativeNiniXmlDocument> owner_;
    std::unique_ptr<NiniXmlChildEnumerator> enumeration_;

  public:
    NativeNiniXmlDocumentEnumerator(std::shared_ptr<NativeNiniXmlDocument> owner,
                                    std::unique_ptr<NiniXmlChildEnumerator> enumeration)
        : owner_(std::move(owner)), enumeration_(std::move(enumeration)) {
    }
    bool move_next() override {
        return enumeration_->move_next();
    }
    std::shared_ptr<NiniXmlNode> current() override {
        auto node = enumeration_->current();
        return owner_ ? owner_->wrap(std::dynamic_pointer_cast<NiniXmlDomNode>(node)) : node;
    }
};
std::unique_ptr<NiniXmlChildEnumerator> NativeNiniXmlDocumentNode::children() {
    auto probe = node_->children();
    bool populated = probe->move_next();
    return std::make_unique<NativeNiniXmlDocumentEnumerator>(populated ? owner_ : nullptr, node_->children());
}
std::shared_ptr<NiniXmlNode> NativeNiniXmlDocument::wrap(std::shared_ptr<NiniXmlDomNode> node) {
    if (!node)
        return {};
    auto& weak = node_handles_[node->identity()];
    if (auto existing = weak.lock())
        return existing;
    auto result = std::make_shared<NativeNiniXmlDocumentNode>(shared_from_this(), std::move(node));
    weak = result;
    return result;
}
NativeNiniXmlDocument::NativeNiniXmlDocument(std::shared_ptr<NiniXmlNativeDocumentIo> io)
    : io_(std::move(io)) {
}
std::shared_ptr<NativeNiniXmlDocument>
NativeNiniXmlDocument::create(const std::shared_ptr<NiniXmlNativeDocumentIo>& io) {
    require_io(io);
    return std::shared_ptr<NativeNiniXmlDocument>(new NativeNiniXmlDocument(io));
}
std::shared_ptr<NiniXmlNode> NativeNiniXmlDocument::document_element() {
    return wrap(dom_.document_element());
}
void NativeNiniXmlDocument::load_owned(const std::shared_ptr<NativeNiniXmlReader>& reader) {
    try {
        native_nini_xml_load(dom_, reader);
    } catch (...) {
        if (reader)
            reader->close();
        throw;
    }
    if (reader)
        reader->close();
}
void NativeNiniXmlDocument::load_xml(NiniWriterText value) {
    const auto lifetime = shared_from_this();
    auto reader = NativeNiniXmlReader::from_text(std::move(value), NiniXmlReaderSettings::legacy_document());
    load_owned(reader);
}
void NativeNiniXmlDocument::load_path(NiniWriterText path) {
    const auto lifetime = shared_from_this();
    auto open = io_->open_path;
    auto reader = open(std::move(path));
    load_owned(reader);
}
void NativeNiniXmlDocument::load_reader(std::shared_ptr<NiniXmlReader> reader) {
    const auto lifetime = shared_from_this();
    auto native = std::dynamic_pointer_cast<NativeNiniXmlReader>(reader);
    if (reader && !native)
        throw std::invalid_argument(
            "Native XML document requires its native reader; select the reader's own XML provider for foreign reader implementations");
    native_nini_xml_load(dom_, native);
}
void NativeNiniXmlDocument::save_path(NiniWriterText path) {
    const auto lifetime = shared_from_this();
    auto save = io_->save_path;
    save(dom_, std::move(path));
}
void NativeNiniXmlDocument::save_writer(std::shared_ptr<NiniTextWriter> writer) {
    const auto lifetime = shared_from_this();
    auto save = io_->save_writer;
    save(dom_, std::move(writer));
}
void NativeNiniXmlDocument::save_stream(std::shared_ptr<NiniWriterStream> stream) {
    const auto lifetime = shared_from_this();
    auto save = io_->save_stream;
    save(dom_, std::move(stream));
}
std::shared_ptr<NiniXmlNode> NativeNiniXmlDocument::create_element(NiniWriterText name) {
    return wrap(dom_.create_element(std::move(name)));
}
std::shared_ptr<NiniXmlNode> NativeNiniXmlDocument::create_attribute(NiniWriterText name) {
    return wrap(dom_.create_attribute(std::move(name)));
}
std::shared_ptr<NiniXmlConfigBackend>
native_nini_xml_config_backend(const std::shared_ptr<NiniXmlNativeDocumentIo>& io) {
    require_io(io);
    auto backend = std::make_shared<NiniXmlConfigBackend>();
    backend->create_document = [io] { return NativeNiniXmlDocument::create(io); };
    backend->string_writer = [] { return std::make_shared<NiniStringWriter>(); };
    return backend;
}
}
