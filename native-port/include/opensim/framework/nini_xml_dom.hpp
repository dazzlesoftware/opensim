// Native retained System.Xml DOM method layer, derived from runtime v8.0.31.
// Copyright (c) .NET Foundation and Contributors. MIT license:
// native-port/third_party/dotnet_collation/LICENSE.
#pragma once
#include "opensim/framework/nini_xml_config.hpp"
namespace opensim::framework
{
struct NiniXmlDomState;
struct NiniXmlDtdInfo;
// Internal native load/write transaction: defer cycle collection while an
// iterative operation owns an in-flight, not-yet-published subtree.
class NiniXmlDomCollectionScope {
 std::shared_ptr<NiniXmlDomState> state_;
 explicit NiniXmlDomCollectionScope(std::shared_ptr<NiniXmlDomState>);
 friend class NiniXmlDom;
public:
 ~NiniXmlDomCollectionScope();
 NiniXmlDomCollectionScope(const NiniXmlDomCollectionScope&)=delete;
 NiniXmlDomCollectionScope& operator=(const NiniXmlDomCollectionScope&)=delete;
 NiniXmlDomCollectionScope(NiniXmlDomCollectionScope&&)noexcept;
 NiniXmlDomCollectionScope& operator=(NiniXmlDomCollectionScope&&)=delete;
};
class NiniXmlDomNode final : public NiniXmlNode
{
    std::shared_ptr<NiniXmlDomState> state_;
    std::size_t id_;
    friend class NiniXmlDom;

  public:
    NiniXmlDomNode(std::shared_ptr<NiniXmlDomState>, std::size_t);
    ~NiniXmlDomNode() override;
    NiniXmlDomNode(const NiniXmlDomNode &) = delete;
    NiniXmlDomNode &operator=(const NiniXmlDomNode &) = delete;
    std::int32_t node_type() override;
    NiniWriterText name() override;
    NiniWriterText prefix()const;
    NiniWriterText local_name()const;
    NiniWriterText namespace_uri()const;
    NiniWriterText value() override;
    void value(NiniWriterText) override;
    std::shared_ptr<NiniXmlNode> attribute(NiniWriterText) override;
    std::unique_ptr<NiniXmlChildEnumerator> children() override;
    void append_child(std::shared_ptr<NiniXmlNode>) override;
    // XmlLoader-only fresh-node append; other inputs retain public validation.
    void append_child_for_load(std::shared_ptr<NiniXmlDomNode>);
    void remove_child(std::shared_ptr<NiniXmlNode>) override;
    void append_attribute(std::shared_ptr<NiniXmlNode>) override;
    std::shared_ptr<NiniXmlDomNode> parent(), next_sibling(), owner_element();
    std::vector<std::shared_ptr<NiniXmlDomNode>> attributes();
    std::shared_ptr<NiniXmlDomNode> first_child(),last_child();
    NiniWriterText declaration_version()const,declaration_encoding()const,declaration_standalone()const;
    bool specified()const;void specified_for_load(bool);
    std::shared_ptr<NiniXmlDtdInfo> dtd_info()const;
    NiniWriterText dtd_public_id()const,dtd_system_id()const,dtd_subset()const;
    bool is_empty_element()const;
    void is_empty_element(bool);
    std::size_t identity() const noexcept
    {
        return id_;
    }
    bool same_owner(const NiniXmlDomNode &) const noexcept;
};
// This closes native retained nodes/mutation/enumeration, not XML text parsing or
// serialization. No default reader/writer backend is supplied by this layer.
class NiniXmlDom
{
    std::shared_ptr<NiniXmlDomState> state_;

  public:
    NiniXmlDom();
    void clear();
    NiniXmlDomCollectionScope defer_collection()const;
    std::shared_ptr<NiniXmlDomNode> document_node() const;
    // Controlled lifetime diagnostic; this is not a System.Xml public member.
    std::size_t retained_node_count() const;
    std::shared_ptr<NiniXmlDomNode> document_element() const;
    std::shared_ptr<NiniXmlDomNode> create_element(NiniWriterText);
    std::shared_ptr<NiniXmlDomNode> create_attribute(NiniWriterText);
    std::shared_ptr<NiniXmlDomNode> create_element(NiniWriterText prefix, NiniWriterText local_name,
                                                   NiniWriterText namespace_uri);
    std::shared_ptr<NiniXmlDomNode> create_attribute(NiniWriterText prefix, NiniWriterText local_name,
                                                     NiniWriterText namespace_uri);
    std::shared_ptr<NiniXmlDomNode> create_text(NiniWriterText);
    std::shared_ptr<NiniXmlDomNode> create_cdata(NiniWriterText);
    std::shared_ptr<NiniXmlDomNode> create_comment(NiniWriterText);
    std::shared_ptr<NiniXmlDomNode> create_whitespace(NiniWriterText, bool significant = false);
    std::shared_ptr<NiniXmlDomNode> create_processing_instruction(NiniWriterText target, NiniWriterText data);
    std::shared_ptr<NiniXmlDomNode> create_fragment();
    std::shared_ptr<NiniXmlDomNode> create_document_type_for_load(NiniWriterText,NiniWriterText,NiniWriterText,NiniWriterText,std::shared_ptr<NiniXmlDtdInfo>);
    // Without a loaded DTD, source expansion resolves the five predefined names
    // and gives undeclared references an empty text child when attached.
    std::shared_ptr<NiniXmlDomNode> create_entity_reference(NiniWriterText name);
    std::shared_ptr<NiniXmlDomNode> create_declaration(NiniWriterText version,NiniWriterText encoding,NiniWriterText standalone);
};
} // namespace opensim::framework
