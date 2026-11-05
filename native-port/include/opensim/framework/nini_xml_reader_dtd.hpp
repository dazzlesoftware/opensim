// Synchronous consumed IDtdParserAdapter/IDtdInfo boundary, runtime v8.0.31.
// Copyright (c) .NET Foundation and Contributors. MIT license:
// native-port/third_party/dotnet_collation/LICENSE.
#pragma once
#include "opensim/framework/nini_xml_config.hpp"
#include <span>
#include <variant>
namespace opensim::framework {
struct NiniXmlDtdError : NiniXmlProviderError {
    using NiniXmlProviderError::NiniXmlProviderError;
    // Pinned resource identity supports the original parser's resource-based
    // catches; it does not replace the fully formatted public message.
    std::string resource_name;
};
struct NiniXmlDtdLine {
    std::int32_t number = 0, position = 0;
};
// DTD XmlQualifiedName.Namespace stores a lexical prefix, not a resolved URI.
struct NiniXmlDtdName {
    std::u16string local_name, prefix;
    bool operator==(const NiniXmlDtdName&) const = default;
    std::u16string qualified() const {
        return prefix.empty() ? local_name : prefix + u":" + local_name;
    }
};
enum class NiniXmlDtdTokenizedType : std::int32_t {
    CData = 0,
    Id = 1,
    IdRef = 2,
    IdRefs = 3,
    Entity = 4,
    Entities = 5,
    NmToken = 6,
    NmTokens = 7,
    Notation = 8,
    Enumeration = 9,
    None = 10
};
enum class NiniXmlDtdUse : std::int32_t { Default = 0, Required = 1, Implied = 2, Fixed = 3, RequiredFixed = 4 };
using NiniXmlDtdTypedValue =
    std::variant<std::monostate, std::u16string, std::vector<std::u16string>, NiniXmlDtdName>;
struct NiniXmlDtdAttribute {
    NiniXmlDtdName name;
    NiniXmlDtdLine line, value_line;
    NiniXmlDtdTokenizedType tokenized_type = NiniXmlDtdTokenizedType::CData;
    NiniXmlDtdUse presence = NiniXmlDtdUse::Default;
    bool declared_in_external = false, is_xml_attribute = false;
    std::u16string default_value_expanded;
    NiniXmlDtdTypedValue default_value_typed;
    std::vector<std::u16string> values;
    bool non_cdata() const {
        return tokenized_type != NiniXmlDtdTokenizedType::CData;
    }
    bool has_default() const {
        return presence == NiniXmlDtdUse::Default || presence == NiniXmlDtdUse::Fixed;
    }
};
struct NiniXmlDtdAttributeList {
    NiniXmlDtdName name;
    bool has_non_cdata_attributes = false;
    std::vector<std::shared_ptr<NiniXmlDtdAttribute>> attributes, default_attributes;
    std::shared_ptr<NiniXmlDtdAttribute> id_attribute;
};
struct NiniXmlDtdEntity {
    NiniXmlDtdName name, ndata;
    NiniXmlDtdLine line;
    bool external = false, declared_in_external = false, parameter = false, parsing_in_progress = false;
    NiniWriterText base_uri, declared_uri, system_id, public_id, text;
    bool unparsed() const {
        return !ndata.local_name.empty();
    }
};
struct NiniXmlDtdNotation {
    NiniXmlDtdName name;
    NiniWriterText system_id, public_id;
};
struct NiniXmlDtdInfo {
    NiniXmlDtdName name;
    std::u16string internal_subset;
    NiniWriterText system_id, public_id;
    bool has_default_attributes = false, has_non_cdata_attributes = false;
    // Preserve both stores: LookupAttributeList searches declared then undeclared;
    // IDtdInfo.GetAttributeLists enumerates declared element lists only.
    std::vector<std::shared_ptr<NiniXmlDtdAttributeList>> element_declarations, undeclared_element_declarations;
    std::vector<std::shared_ptr<NiniXmlDtdEntity>> general_entities, parameter_entities;
    std::vector<NiniXmlDtdNotation> notations;
};
class NiniXmlDtdAdapter {
public:
    virtual ~NiniXmlDtdAdapter() = default;
    virtual NiniWriterText intern(std::u16string_view) = 0;
    virtual NiniWriterText lookup_namespace(std::u16string_view prefix) = 0;
    virtual NiniWriterText base_uri() const = 0;
    // Buffer includes the original NUL sentinel after parsing_buffer_length().
    // Views are invalidated by ReadData/entity transitions, exactly as source.
    virtual std::span<char16_t> parsing_buffer() = 0;
    virtual std::int32_t parsing_buffer_length() const = 0;
    virtual std::int32_t current_position() const = 0;
    virtual void current_position(std::int32_t) = 0;
    virtual std::int32_t line_number() const = 0;
    virtual std::int32_t line_start_position() const = 0;
    virtual bool is_eof() const = 0;
    virtual std::int32_t entity_stack_length() const = 0;
    virtual bool entity_eol_normalized() const = 0;
    virtual bool v1_compatibility() const = 0;
    virtual bool normalization() const = 0;
    virtual bool namespaces() const = 0;
    virtual bool validation_enabled() const = 0;
    virtual bool has_validation_handler() const = 0;
    virtual std::int32_t read_data() = 0;
    virtual void on_new_line(std::int32_t next_position) = 0;
    // Return source buffer-relative positions; numeric/expanded named references
    // rewrite their result near the original semicolon and reset CurrentPosition.
    virtual std::int32_t parse_numeric_reference(std::u16string* internal_subset_builder) = 0;
    virtual std::int32_t parse_named_reference(bool expand, std::u16string* internal_subset_builder) = 0;
    virtual void parse_processing_instruction(std::u16string* builder) = 0;
    virtual void parse_comment(std::u16string* builder) = 0;
    virtual bool push_entity(const std::shared_ptr<NiniXmlDtdEntity>&, std::int32_t& entity_id) = 0;
    virtual bool pop_entity(std::shared_ptr<NiniXmlDtdEntity>& old_entity, std::int32_t& new_entity_id) = 0;
    virtual bool push_external_subset(NiniWriterText system_id, NiniWriterText public_id) = 0;
    virtual void push_internal_dtd(NiniWriterText base_uri, NiniWriterText subset) = 0;
    virtual void on_system_id(NiniWriterText, NiniXmlDtdLine keyword, NiniXmlDtdLine literal) = 0;
    virtual void on_public_id(NiniWriterText, NiniXmlDtdLine keyword, NiniXmlDtdLine literal) = 0;
    virtual void validation_event(const NiniXmlProviderError&, std::int32_t severity) = 0;
    virtual void throw_error(NiniXmlDtdError) = 0;
};
// Native DtdParser implementation supplies these synchronous entry points.
std::shared_ptr<NiniXmlDtdInfo> native_nini_xml_parse_dtd(NiniXmlDtdAdapter&, bool save_internal_subset = true);
std::shared_ptr<NiniXmlDtdInfo>
native_nini_xml_parse_free_floating_dtd(NiniXmlDtdAdapter&, NiniWriterText base_uri,
                                        NiniWriterText document_type_name, NiniWriterText public_id,
                                        NiniWriterText system_id, NiniWriterText subset);
}
