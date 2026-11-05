// Port of bundled Nini.Config.XmlConfigSource; copyright (c) 2006 Brent R. Matzelle.
// MIT license: native-port/third_party/nini/LICENSE.
#pragma once
#include "opensim/framework/nini_config.hpp"
namespace opensim::framework {
class NiniXmlNode;
// Concrete System.Xml child enumeration is live. Removing the current child
// must retain its handle and reproduce its subsequent NextSibling behavior.
class NiniXmlChildEnumerator {
public:
    virtual ~NiniXmlChildEnumerator() = default;
    virtual bool move_next() = 0;
    virtual std::shared_ptr<NiniXmlNode> current() = 0;
};
// Node handles retain their provider/OwnerDocument even while detached.
class NiniXmlNode {
public:
    virtual ~NiniXmlNode() = default;
    virtual std::int32_t node_type() = 0;
    virtual NiniWriterText name() = 0;
    virtual NiniWriterText value() = 0;
    virtual void value(NiniWriterText) = 0;
    virtual std::shared_ptr<NiniXmlNode> attribute(NiniWriterText qualified_name) = 0;
    virtual std::unique_ptr<NiniXmlChildEnumerator> children() = 0;
    virtual void append_child(std::shared_ptr<NiniXmlNode>) = 0;
    virtual void remove_child(std::shared_ptr<NiniXmlNode>) = 0;
    virtual void append_attribute(std::shared_ptr<NiniXmlNode>) = 0;
};
// Reader handles belong to the selected XML provider; it must preserve reader
// settings/state/lifetime, including separate DTD Parse/Ignore/Prohibit policies.
class NiniXmlReader {
public:
    virtual ~NiniXmlReader() = default;
};
class NiniXmlDocument {
public:
    virtual ~NiniXmlDocument() = default;
    virtual std::shared_ptr<NiniXmlNode> document_element() = 0;
    virtual void load_xml(NiniWriterText) = 0;
    virtual void load_path(NiniWriterText) = 0;
    virtual void load_reader(std::shared_ptr<NiniXmlReader>) = 0;
    virtual void save_path(NiniWriterText) = 0;
    virtual void save_writer(std::shared_ptr<NiniTextWriter>) = 0;
    virtual void save_stream(std::shared_ptr<NiniWriterStream>) = 0;
    virtual std::shared_ptr<NiniXmlNode> create_element(NiniWriterText qualified_name) = 0;
    virtual std::shared_ptr<NiniXmlNode> create_attribute(NiniWriterText qualified_name) = 0;
};
// Required System.Xml dependency. This class port does not silently substitute a
// permissive XML parser. Implementations own exact namespace/entity/encoding,
// DOM mutation, reader/writer/stream ownership and XmlDocument Load/Save behavior.
struct NiniXmlConfigBackend {
    std::function<std::shared_ptr<NiniXmlDocument>()> create_document;
    std::function<std::shared_ptr<NiniTextWriter>()> string_writer;
};
// XML providers preserve their original structured failures without conversion
// through a lossy UTF8 display string. Class-owned Nini failures use NiniConfigError.
class NiniXmlProviderError : public std::runtime_error {
public:
    NiniXmlProviderError(std::string source_type, std::uint32_t hresult, std::u16string message,
                         std::optional<std::u16string> parameter = {});
    std::string source_type;
    std::uint32_t hresult;
    std::u16string message;
    std::optional<std::u16string> parameter, source_uri, file_name, object_name;
    std::optional<std::int32_t> line_number, line_position;
};
class NiniXmlConfigSource final : public NiniConfigSourceBase {
    friend struct NiniXmlConfigProbe;
    std::shared_ptr<NiniXmlConfigBackend> backend_;
    std::shared_ptr<NiniXmlDocument> document_;
    NiniWriterText save_path_;
    std::shared_ptr<NiniXmlDocument> document_receiver() const {
        return document_;
    }
    NiniXmlConfigSource(NiniConfigArena*, std::shared_ptr<NiniXmlConfigBackend>);
    static std::shared_ptr<NiniXmlConfigSource> allocate(const std::shared_ptr<NiniConfigArena>&,
                                                         const std::shared_ptr<NiniXmlConfigBackend>&);
    void perform_load(std::shared_ptr<NiniXmlDocument>);
    void load_sections(std::shared_ptr<NiniXmlNode>);
    void load_keys(std::shared_ptr<NiniXmlNode>, std::shared_ptr<NiniConfig>);
    void merge_configs_into_document();
    void remove_sections();
    void remove_keys(NiniWriterText);
    std::shared_ptr<NiniXmlNode> get_section(NiniWriterText),
        get_key(std::shared_ptr<NiniXmlNode>, NiniWriterText), section_node(NiniWriterText);
    void set_key(std::shared_ptr<NiniXmlNode>, NiniWriterText, NiniWriterText);
    void create_key(std::shared_ptr<NiniXmlNode>, NiniWriterText, NiniWriterText);
    void merge_document_into_configs();
    void remove_configs();
    void remove_config_keys(std::shared_ptr<NiniConfig>);

public:
    static std::shared_ptr<NiniXmlConfigSource> create(const std::shared_ptr<NiniConfigArena>&,
                                                       const std::shared_ptr<NiniXmlConfigBackend>&);
    static std::shared_ptr<NiniXmlConfigSource> create(const std::shared_ptr<NiniConfigArena>&,
                                                       const std::shared_ptr<NiniXmlConfigBackend>&,
                                                       NiniWriterText path);
    static std::shared_ptr<NiniXmlConfigSource> create(const std::shared_ptr<NiniConfigArena>&,
                                                       const std::shared_ptr<NiniXmlConfigBackend>&,
                                                       std::shared_ptr<NiniXmlReader>);
    NiniWriterText save_path() const {
        return save_path_;
    }
    void load(NiniWriterText path);
    void load(std::shared_ptr<NiniXmlReader>);
    void save() override;
    void save(NiniWriterText path);
    void save(std::shared_ptr<NiniTextWriter>);
    void save(std::shared_ptr<NiniWriterStream>);
    void reload() override;
    NiniWriterText to_string() override;
};
}
