// Native consumed System.Xml reader/loader port, pinned runtime v8.0.31.
// Copyright (c) .NET Foundation and Contributors. MIT license:
// native-port/third_party/dotnet_collation/LICENSE.
#pragma once
#include "opensim/framework/nini_xml_dom.hpp"
#include <span>
namespace opensim::framework {
class NiniXmlDtdAdapter;
struct NiniXmlDtdInfo;
class NativeNiniXmlReaderDtdAdapter;
enum class NiniXmlDtdProcessing : std::int32_t { Prohibit = 0, Ignore = 1, Parse = 2 };
enum class NiniXmlEntityHandling : std::int32_t { ExpandEntities = 1, ExpandCharEntities = 2 };
enum class NiniXmlReadState : std::int32_t {
    Initial = 0,
    Interactive = 1,
    Error = 2,
    EndOfFile = 3,
    Closed = 4
};
struct NiniXmlReaderSettings {
    NiniWriterText base_uri = nini_writer_text(u"");
    NiniXmlDtdProcessing dtd_processing = NiniXmlDtdProcessing::Prohibit;
    NiniXmlEntityHandling entity_handling = NiniXmlEntityHandling::ExpandEntities;
    bool normalize = true, legacy_compatibility = false, close_input = false;
    bool validating_reader_compatibility = false;
    bool ignore_whitespace = false, ignore_comments = false, ignore_processing_instructions = false;
    std::int64_t max_characters_in_document = 0, max_characters_from_entities = 10000000;
    static NiniXmlReaderSettings legacy_document();
};
class NativeNiniXmlByteInput {
public:
    virtual ~NativeNiniXmlByteInput() = default;
    virtual std::int32_t read(std::span<std::uint8_t> destination) = 0;
    virtual void close() = 0;
    // A value supplies the original CanSeek/Length buffer-size path; nullopt is
    // the original non-seekable path. The reader does not seek the resource.
    virtual std::optional<std::int64_t> length_hint() const {
        return {};
    }
};
// Consumed TextReader.Read(char[], index, count) data boundary. Unlike byte
// input, declarations do not select an encoding for already decoded text.
class NativeNiniXmlTextInput {
public:
    virtual ~NativeNiniXmlTextInput() = default;
    virtual std::int32_t read(std::span<char16_t> destination) = 0;
    virtual void close() = 0;
};
struct NiniXmlReadAttribute {
    std::u16string name, prefix, local_name, namespace_uri, value;
    std::int32_t line_number = 0, line_position = 0;
    std::int32_t value_line_number = 0, value_line_position = 0;
    bool is_default = false;
};
struct NiniXmlReadToken {
    std::int32_t node_type = 0, depth = 0, line_number = 0, line_position = 0;
    std::u16string name, prefix, local_name, namespace_uri, value, base_uri;
    bool is_empty_element = false;
    std::vector<NiniXmlReadAttribute> attributes;
};
class NativeNiniXmlReader final : public NiniXmlReader,
                                  public std::enable_shared_from_this<NativeNiniXmlReader> {
    friend class NativeNiniXmlReaderDtdAdapter;
    struct State;
    std::unique_ptr<State> state_;
    explicit NativeNiniXmlReader(std::unique_ptr<State>);

public:
    ~NativeNiniXmlReader() override;
    NativeNiniXmlReader(const NativeNiniXmlReader&) = delete;
    NativeNiniXmlReader& operator=(const NativeNiniXmlReader&) = delete;
    static std::shared_ptr<NativeNiniXmlReader> from_text(NiniWriterText, NiniXmlReaderSettings = {});
    static std::shared_ptr<NativeNiniXmlReader> from_text_input(std::shared_ptr<NativeNiniXmlTextInput>,
                                                                NiniXmlReaderSettings = {});
    // XML declaration/BOM decoding belongs to this reader, not URL transport.
    static std::shared_ptr<NativeNiniXmlReader> from_bytes(std::span<const std::uint8_t>,
                                                           NiniXmlReaderSettings = {});
    static std::shared_ptr<NativeNiniXmlReader> from_stream(std::shared_ptr<NativeNiniXmlByteInput>,
                                                            NiniXmlReaderSettings = {});
    static std::shared_ptr<NativeNiniXmlReader>
        from_deferred_uri(NiniWriterText, std::function<std::shared_ptr<NativeNiniXmlByteInput>()>,
                          NiniXmlReaderSettings);
    bool read();
    const NiniXmlReadToken& token() const;
    bool is_default() const;
    std::shared_ptr<NiniXmlDtdInfo> dtd_info() const;
    bool move_to_first_attribute();
    bool move_to_next_attribute();
    bool move_to_element();
    bool read_attribute_value();
    void resolve_entity();
    NiniXmlReadState read_state() const;
    bool eof() const;
    void close();
};
// Source XmlLoader publication: preserve document nodes and completed subtrees;
// errors leave precisely those nodes that the original already appended.
void native_nini_xml_load(NiniXmlDom&, const std::shared_ptr<NativeNiniXmlReader>&,
                          bool preserve_whitespace = false);
std::vector<NiniXmlReadAttribute> native_nini_xml_declaration_attributes(NiniWriterText);
std::unique_ptr<NiniXmlDtdAdapter> native_nini_xml_dtd_adapter(const std::shared_ptr<NativeNiniXmlReader>&);
}
