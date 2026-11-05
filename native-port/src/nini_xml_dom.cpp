// Derived from .NET runtime v8.0.31 XmlNode/XmlElement/XmlAttribute/XmlChildEnumerator.
// Copyright (c) .NET Foundation and Contributors. MIT license:
// native-port/third_party/dotnet_collation/LICENSE.
#include "opensim/framework/nini_xml_dom.hpp"
#include "opensim/framework/nini_xml_character.hpp"
#include "opensim/framework/nini_xml_reader.hpp"
#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
namespace opensim::framework {
namespace {
using Text = NiniWriterText;
using Node = std::shared_ptr<NiniXmlDomNode>;
constexpr auto none = std::numeric_limits<std::size_t>::max();
[[noreturn]] void fault(const char* type, std::uint32_t code, std::u16string message) {
    throw NiniXmlProviderError(type, code, std::move(message));
}
[[noreturn]] void argument(std::u16string message) {
    fault("System.ArgumentException", 0x80070057, std::move(message));
}
[[noreturn]] void invalid(std::u16string message) {
    fault("System.InvalidOperationException", 0x80131509, std::move(message));
}
[[noreturn]] void nullref() {
    fault("System.NullReferenceException", 0x80004003,
          u"Object reference not set to an instance of an object.");
}
bool equal(const Text& a, const Text& b) {
    return (!a && !b) || (a && b && *a == *b);
}
Text empty_if_null(Text s) {
    return s ? s : nini_writer_text(u"");
}
void check_name(std::u16string_view s) {
    auto i = nini_xml_parse_nmtoken(s);
    if (i == s.size())
        return;
    std::u16string character;
    std::uint32_t number = s[i];
    if (s[i] >= 0xd800 && s[i] <= 0xdbff && i + 1 < s.size() && s[i + 1] != 0) {
        character = {s[i], s[i + 1]};
        number = (static_cast<std::uint32_t>(s[i + 1]) - 0xdc00) |
                 ((static_cast<std::uint32_t>(s[i]) - 0xd800) << 10) + 0x10000;
    } else
        character = s[i] == 0 ? u"." : std::u16string(1, s[i]);
    std::ostringstream hex;
    hex << "0x" << std::uppercase << std::hex << std::setfill('0') << std::setw(2) << number;
    std::u16string h;
    for (auto c : hex.str())
        h += static_cast<char16_t>(c);
    NiniXmlProviderError e("System.Xml.XmlException", 0x80131940,
                           u"The '" + character + u"' character, hexadecimal value " + h +
                               u", cannot be included in a name.");
    e.source_uri = u"";
    e.line_number = 0;
    e.line_position = 0;
    throw e;
}
} // namespace
struct NiniXmlDomState {
    struct Record {
        std::int32_t type;
        Text name, value;
        std::u16string local, uri;
        std::size_t parent = none, owner = none;
        std::vector<std::size_t> children, attributes;
        std::size_t handles = 0;
        bool empty = false;
        std::u16string version, encoding, standalone;
        bool specified = true;
        Text dtd_public, dtd_system, dtd_internal;
        std::shared_ptr<NiniXmlDtdInfo> dtd;
    };
    std::vector<std::unique_ptr<Record>> records;
    std::vector<std::size_t> free_slots;
    std::size_t collection_deferrals = 0;
    std::size_t add(std::int32_t type, Text name, Text value = {}) {
        auto record = std::make_unique<Record>(Record{
            type, std::move(name), std::move(value), {}, {}, none, none, {}, {}, 0, false, {}, {}, {}});
        record->empty = type == 1;
        if (!free_slots.empty()) {
            auto id = free_slots.back();
            free_slots.pop_back();
            records[id] = std::move(record);
            return id;
        }
        auto id = records.size();
        records.push_back(std::move(record));
        return id;
    }
    Record& at(std::size_t id) {
        return *records.at(id);
    }
    void collect() noexcept {
        if (collection_deferrals)
            return;
        try {
            std::vector<bool> marked(records.size());
            std::vector<std::size_t> pending{0};
            for (std::size_t i = 0; i < records.size(); ++i)
                if (records[i] && records[i]->handles)
                    pending.push_back(i);
            while (!pending.empty()) {
                auto id = pending.back();
                pending.pop_back();
                if (id == none || marked[id] || !records[id])
                    continue;
                marked[id] = true;
                auto& r = *records[id];
                pending.push_back(r.parent);
                pending.push_back(r.owner);
                pending.insert(pending.end(), r.children.begin(), r.children.end());
                pending.insert(pending.end(), r.attributes.begin(), r.attributes.end());
            }
            for (std::size_t i = 1; i < records.size(); ++i)
                if (records[i] && !marked[i]) {
                    free_slots.push_back(i);
                    records[i].reset();
                }
        } catch (...) { /* Retain records if collection allocation fails; destructors never throw. */
        }
    }
};
NiniXmlDomCollectionScope::NiniXmlDomCollectionScope(std::shared_ptr<NiniXmlDomState> state)
    : state_(std::move(state)) {
    ++state_->collection_deferrals;
}
NiniXmlDomCollectionScope::~NiniXmlDomCollectionScope() {
    if (state_ && --state_->collection_deferrals == 0)
        state_->collect();
}
NiniXmlDomCollectionScope::NiniXmlDomCollectionScope(NiniXmlDomCollectionScope&& other) noexcept
    : state_(std::move(other.state_)) {
}
NiniXmlDomCollectionScope NiniXmlDom::defer_collection() const {
    return NiniXmlDomCollectionScope(state_);
}
namespace {
Node handle(const std::shared_ptr<NiniXmlDomState>& state, std::size_t id) {
    return id == none ? Node{} : std::make_shared<NiniXmlDomNode>(state, id);
}
std::size_t next(const std::shared_ptr<NiniXmlDomState>& state, std::size_t id) {
    auto& r = state->at(id);
    if (r.parent == none)
        return none;
    auto& c = state->at(r.parent).children;
    auto p = std::find(c.begin(), c.end(), id);
    return p != c.end() && ++p != c.end() ? *p : none;
}
struct EmptyEnumerator final : NiniXmlChildEnumerator {
    bool move_next() override {
        return false;
    }
    std::shared_ptr<NiniXmlNode> current() override {
        invalid(u"Operation is not valid due to the current state of the object.");
    }
};
struct Enumerator final : NiniXmlChildEnumerator {
    std::shared_ptr<NiniXmlDomState> state;
    std::size_t container, current_id = none;
    Node container_handle, current_handle;
    bool first = true;
    Enumerator(std::shared_ptr<NiniXmlDomState> s, std::size_t c)
        : state(std::move(s)), container(c), container_handle(handle(state, c)) {
        auto& children = state->at(c).children;
        if (!children.empty())
            current_handle = handle(state, children.front());
    }
    bool move_next() override {
        if (first) {
            auto& c = state->at(container).children;
            current_id = c.empty() ? none : c.front();
            first = false;
        } else if (current_id != none)
            current_id = next(state, current_id);
        current_handle = handle(state, current_id);
        return current_id != none;
    }
    std::shared_ptr<NiniXmlNode> current() override {
        if (first || current_id == none)
            invalid(u"Operation is not valid due to the current state of the object.");
        return handle(state, current_id);
    }
};
Node named(const std::shared_ptr<NiniXmlDomState>& state, Text name, std::int32_t type) {
    if (!name)
        nullref();
    auto colon = name->find(u':');
    std::u16string prefix, local;
    if (colon == std::u16string::npos || colon == 0 || colon + 1 == name->size())
        local = *name;
    else {
        prefix = name->substr(0, colon);
        local = name->substr(colon + 1);
    }
    check_name(prefix);
    check_name(local);
    if (local.empty())
        argument(type == 2 ? u"The attribute local name cannot be empty."
                           : u"The local name for elements or attributes cannot be null or an empty string.");
    auto id = state->add(type, name);
    auto& r = state->at(id);
    r.local = std::move(local);
    if (type == 2 && (prefix == u"xmlns" || (prefix.empty() && r.local == u"xmlns")))
        r.uri = u"http://www.w3.org/2000/xmlns/";
    else if (type == 2 && prefix == u"xml")
        r.uri = u"http://www.w3.org/XML/1998/namespace";
    return handle(state, id);
}
Node named_parts(const std::shared_ptr<NiniXmlDomState>& state, Text prefix, Text local, Text uri,
                 std::int32_t type) {
    if (!local)
        throw NiniXmlProviderError("System.ArgumentNullException", 0x80004003,
                                   u"Value cannot be null. (Parameter 'key')", u"key");
    prefix = empty_if_null(prefix);
    uri = empty_if_null(uri);
    if (type == 2 && ((*prefix == u"xmlns" || (prefix->empty() && *local == u"xmlns")) !=
                      (*uri == u"http://www.w3.org/2000/xmlns/")))
        argument(u"The namespace declaration attribute has an incorrect 'namespaceURI': '" + *uri + u"'.");
    check_name(*prefix);
    check_name(*local);
    if (local->empty())
        argument(type == 2 ? u"The attribute local name cannot be empty."
                           : u"The local name for elements or attributes cannot be null or an empty string.");
    auto name = prefix->empty() ? local : nini_writer_text(*prefix + u":" + *local);
    auto id = state->add(type, name);
    auto& r = state->at(id);
    r.local = *local;
    r.uri = *uri;
    return handle(state, id);
}
bool readonly(const std::shared_ptr<NiniXmlDomState>& s, std::size_t id) {
    for (; id != none;) {
        auto& r = s->at(id);
        if (r.type == 5 || r.type == 10)
            return true;
        id = r.type == 2 ? r.owner : r.parent;
    }
    return false;
}
void writable(const std::shared_ptr<NiniXmlDomState>& s, std::size_t id) {
    if (readonly(s, id))
        invalid(u"This node is read-only. It cannot be modified.");
}
void append_text(const std::shared_ptr<NiniXmlDomState>& s, std::size_t id, std::u16string& out) {
    auto& r = s->at(id);
    for (auto c : r.children) {
        auto& child = s->at(c);
        if (child.type == 3 || child.type == 4 || child.type == 13 || child.type == 14) {
            if (child.value)
                out += *child.value;
        } else
            append_text(s, c, out);
    }
}
void detach(const std::shared_ptr<NiniXmlDomState>& s, std::size_t id) {
    auto& r = s->at(id);
    if (r.parent == none)
        return;
    writable(s, r.parent);
    auto& c = s->at(r.parent).children;
    c.erase(std::find(c.begin(), c.end(), id));
    r.parent = none;
}
} // namespace
NiniXmlDomNode::NiniXmlDomNode(std::shared_ptr<NiniXmlDomState> state, std::size_t id)
    : state_(std::move(state)), id_(id) {
    ++state_->at(id_).handles;
}
NiniXmlDomNode::~NiniXmlDomNode() {
    --state_->at(id_).handles;
    state_->collect();
}
std::int32_t NiniXmlDomNode::node_type() {
    return state_->at(id_).type;
}
Text NiniXmlDomNode::name() {
    return state_->at(id_).name;
}
Text NiniXmlDomNode::prefix() const {
    auto& r = state_->at(id_);
    if (r.type != 1 && r.type != 2)
        return nini_writer_text(u"");
    auto colon = r.name->find(u':');
    return nini_writer_text(colon == r.name->npos ? u"" : r.name->substr(0, colon));
}
Text NiniXmlDomNode::local_name() const {
    auto& r = state_->at(id_);
    return r.type == 1 || r.type == 2 ? nini_writer_text(r.local) : r.name;
}
Text NiniXmlDomNode::namespace_uri() const {
    return nini_writer_text(state_->at(id_).uri);
}
Text NiniXmlDomNode::value() {
    auto& r = state_->at(id_);
    if (r.type == 17) {
        std::u16string value = u"version=\"" + r.version + u"\"";
        if (!r.encoding.empty())
            value += u" encoding=\"" + r.encoding + u"\"";
        if (!r.standalone.empty())
            value += u" standalone=\"" + r.standalone + u"\"";
        return nini_writer_text(std::move(value));
    }
    if (r.type != 2)
        return r.value;
    std::u16string result;
    append_text(state_, id_, result);
    return nini_writer_text(std::move(result));
}
void NiniXmlDomNode::value(Text value) {
    auto& r = state_->at(id_);
    if (r.type == 17) {
        auto attrs = native_nini_xml_declaration_attributes(value);
        std::u16string version = r.version, encoding = r.encoding, standalone = r.standalone;
        for (auto& a : attrs) {
            if (a.name == u"version")
                version = a.value;
            else if (a.name == u"encoding")
                encoding = a.value;
            else if (a.name == u"standalone")
                standalone = a.value;
        }
        if (version.size() < 3 || !std::u16string_view(version).starts_with(u"1.") ||
            !std::all_of(version.begin() + 2, version.end(), [](auto c) { return c >= u'0' && c <= u'9'; }))
            argument(
                u"Wrong XML version information. The XML must match production \"VersionNum ::= '1.' [0-9]+\".");
        r.version = std::move(version);
        r.encoding = std::move(encoding);
        r.standalone = std::move(standalone);
        return;
    }
    if (r.type == 2) {
        writable(state_, id_);
        if (r.children.size() == 1 && state_->at(r.children[0]).type == 3) {
            state_->at(r.children[0]).value = empty_if_null(value);
            r.specified = true;
            return;
        }
        for (auto c : r.children)
            state_->at(c).parent = none;
        r.children.clear();
        auto id = state_->add(3, nini_writer_text(u"#text"), empty_if_null(value));
        state_->at(id).parent = id_;
        r.children.push_back(id);
        r.specified = true;
    } else if (r.type == 3 || r.type == 4 || r.type == 7 || r.type == 8 || r.type == 13 || r.type == 14) {
        if ((r.type == 13 || r.type == 14) && value && nini_xml_invalid_whitespace(*value) >= 0)
            argument(u"The string for whitespace contains an invalid character.");
        writable(state_, id_);
        r.value = empty_if_null(value);
        if (r.type == 3 && r.parent != none) {
            auto& parent = state_->at(r.parent);
            if (parent.type == 2 && !parent.specified)
                parent.specified = true;
        }
    } else if (r.type == 5)
        invalid(u"'EntityReference' nodes have no support for setting value.");
    else
        invalid(r.type == 1    ? u"Cannot set a value on node type 'Element'."
                : r.type == 10 ? u"Cannot set a value on node type 'DocumentType'."
                : r.type == 11 ? u"Cannot set a value on node type 'DocumentFragment'."
                               : u"Cannot set a value on node type 'Document'.");
}
std::shared_ptr<NiniXmlNode> NiniXmlDomNode::attribute(Text name) {
    auto& r = state_->at(id_);
    if (r.type != 1)
        return {};
    for (auto id : r.attributes)
        if (equal(state_->at(id).name, name))
            return handle(state_, id);
    return {};
}
std::unique_ptr<NiniXmlChildEnumerator> NiniXmlDomNode::children() {
    if (state_->at(id_).children.empty())
        return std::make_unique<EmptyEnumerator>();
    return std::make_unique<Enumerator>(state_, id_);
}
Node NiniXmlDomNode::parent() {
    return handle(state_, state_->at(id_).parent);
}
Node NiniXmlDomNode::next_sibling() {
    return handle(state_, next(state_, id_));
}
Node NiniXmlDomNode::owner_element() {
    return handle(state_, state_->at(id_).owner);
}
bool NiniXmlDomNode::same_owner(const NiniXmlDomNode& other) const noexcept {
    return state_ == other.state_;
}
std::vector<Node> NiniXmlDomNode::attributes() {
    std::vector<Node> out;
    for (auto id : state_->at(id_).attributes)
        out.push_back(handle(state_, id));
    return out;
}
void NiniXmlDomNode::append_child_for_load(std::shared_ptr<NiniXmlDomNode> child) {
    if (!child || state_ != child->state_ || id_ == child->id_) {
        append_child(std::move(child));
        return;
    }
    auto& parent = state_->at(id_);
    auto& node = state_->at(child->id_);
    const bool allowed = ((parent.type == 1 || parent.type == 5) &&
                          (node.type == 1 || node.type == 3 || node.type == 4 || node.type == 5 ||
                           node.type == 7 || node.type == 8 || node.type == 13 || node.type == 14)) ||
                         (parent.type == 2 && (node.type == 3 || node.type == 5));
    if (!allowed || node.parent != none || node.owner != none || !node.children.empty()) {
        append_child(std::move(child));
        return;
    }
    parent.children.push_back(child->id_);
    node.parent = id_;
    if (parent.type == 1)
        parent.empty = false;
}
void NiniXmlDomNode::append_child(std::shared_ptr<NiniXmlNode> child) {
    auto& r = state_->at(id_);
    if (r.type != 1 && r.type != 2 && r.type != 5 && r.type != 9 && r.type != 11)
        invalid(u"The current node cannot contain other nodes.");
    if (!child)
        nullref();
    auto node = std::dynamic_pointer_cast<NiniXmlDomNode>(child);
    if (!node)
        throw std::invalid_argument("Native XML DOM requires nodes from its provider");
    if (state_ == node->state_) {
        for (auto p = id_; p != none; p = state_->at(p).parent)
            if (p == node->id_)
                argument(u"Cannot insert a node or any ancestor of that node as a child of itself.");
    }
    detach(node->state_, node->id_);
    if (state_ != node->state_ && node->node_type() != 9)
        argument(u"The node to be inserted is from a different document context.");
    auto& c = node->state_->at(node->id_);
    if (c.type == 11) {
        auto children = c.children;
        for (auto id : children) {
            auto item = handle(state_, id);
            detach(state_, id);
            append_child(item);
        }
        if (r.type == 2)
            r.specified = true;
        return;
    }
    if (((r.type == 1 || r.type == 5 || r.type == 11) &&
         (c.type != 1 && c.type != 3 && c.type != 4 && c.type != 5 && c.type != 7 && c.type != 8 &&
          c.type != 13 && c.type != 14 && !(r.type == 11 && c.type == 17))) ||
        (r.type == 11 && c.type == 17 && !r.children.empty() && state_->at(r.children.front()).type == 17) ||
        (r.type == 2 && c.type != 3 && c.type != 5) ||
        (r.type == 9 && c.type != 1 && c.type != 7 && c.type != 8 && c.type != 13 && c.type != 14 &&
         c.type != 17 && c.type != 10))
        invalid(
            u"The specified node cannot be inserted as the valid child of this node, because the specified node is "
            u"the wrong type.");
    if (r.type == 9 && c.type == 1 &&
        std::any_of(r.children.begin(), r.children.end(), [&](auto id) { return state_->at(id).type == 1; }))
        invalid(u"This document already has a 'DocumentElement' node.");
    if (c.type == 10 && r.type == 9) {
        if (std::any_of(r.children.begin(), r.children.end(),
                        [&](auto id) { return state_->at(id).type == 10; }))
            invalid(u"This document already has a 'DocumentType' node.");
        if (std::any_of(r.children.begin(), r.children.end(),
                        [&](auto id) { return state_->at(id).type == 1; }))
            invalid(u"Cannot insert the node in the specified location.");
    }
    if (c.type == 17) {
        if (r.type == 9 && std::any_of(r.children.begin(), r.children.end(),
                                       [&](auto id) { return state_->at(id).type == 17; }))
            invalid(u"This document already has an 'XmlDeclaration' node.");
        if (!r.children.empty())
            invalid(u"Cannot insert the node in the specified location.");
    }
    writable(state_, id_);
    r.children.push_back(node->id_);
    if (r.type == 1)
        r.empty = false;
    c.parent = id_;
    if (c.type == 5 && c.children.empty()) {
        std::u16string replacement;
        if (*c.name == u"lt")
            replacement = u"<";
        else if (*c.name == u"gt")
            replacement = u">";
        else if (*c.name == u"amp")
            replacement = u"&";
        else if (*c.name == u"apos")
            replacement = u"'";
        else if (*c.name == u"quot")
            replacement = u"\"";
        auto text = state_->add(3, nini_writer_text(u"#text"), nini_writer_text(std::move(replacement)));
        state_->at(text).parent = node->id_;
        c.children.push_back(text);
    }
    if (r.type == 2)
        r.specified = true;
}
void NiniXmlDomNode::remove_child(std::shared_ptr<NiniXmlNode> child) {
    auto& r = state_->at(id_);
    if (r.type != 1 && r.type != 2 && r.type != 5 && r.type != 9 && r.type != 11)
        invalid(u"The current node cannot contain other nodes, so the node to be removed is not its child.");
    if (!child)
        nullref();
    auto node = std::dynamic_pointer_cast<NiniXmlDomNode>(child);
    if (!node || node->state_ != state_ || state_->at(node->id_).parent != id_)
        argument(u"The node to be removed is not a child of this node.");
    detach(state_, node->id_);
    if (state_->at(id_).type == 2)
        state_->at(id_).specified = true;
}
void NiniXmlDomNode::append_attribute(std::shared_ptr<NiniXmlNode> attribute) {
    if (state_->at(id_).type != 1)
        nullref();
    if (!attribute)
        nullref();
    auto node = std::dynamic_pointer_cast<NiniXmlDomNode>(attribute);
    if (!node || node->node_type() != 2)
        throw std::invalid_argument("Native XML DOM attribute requires an attribute handle");
    if (state_ != node->state_)
        argument(u"The named node is from a different document context.");
    writable(state_, id_);
    auto& a = state_->at(node->id_);
    if (a.owner != none) {
        auto& old = state_->at(a.owner).attributes;
        old.erase(std::find(old.begin(), old.end(), node->id_));
        a.owner = none;
    }
    auto& entries = state_->at(id_).attributes;
    auto p = std::find_if(entries.begin(), entries.end(), [&](auto id) {
        auto& x = state_->at(id);
        return x.local == a.local && x.uri == a.uri;
    });
    if (p != entries.end()) {
        state_->at(*p).owner = none;
        entries.erase(p);
    }
    entries.push_back(node->id_);
    a.owner = id_;
}
Node NiniXmlDomNode::first_child() {
    const auto& c = state_->at(id_).children;
    return c.empty() ? Node{} : handle(state_, c.front());
}
Node NiniXmlDomNode::last_child() {
    const auto& c = state_->at(id_).children;
    return c.empty() ? Node{} : handle(state_, c.back());
}
NiniWriterText NiniXmlDomNode::declaration_version() const {
    return nini_writer_text(state_->at(id_).version);
}
NiniWriterText NiniXmlDomNode::declaration_encoding() const {
    return nini_writer_text(state_->at(id_).encoding);
}
NiniWriterText NiniXmlDomNode::declaration_standalone() const {
    return nini_writer_text(state_->at(id_).standalone);
}
bool NiniXmlDomNode::is_empty_element() const {
    return state_->at(id_).type == 1 && state_->at(id_).empty;
}
void NiniXmlDomNode::is_empty_element(bool value) {
    auto& r = state_->at(id_);
    if (r.type != 1)
        throw std::invalid_argument("IsEmpty requires an element");
    if (value) {
        while (!r.children.empty())
            remove_child(handle(state_, r.children.back()));
        r.empty = true;
    } else
        r.empty = false;
}
void NiniXmlDom::clear() {
    auto root = document_node();
    while (!state_->at(0).children.empty())
        root->remove_child(handle(state_, state_->at(0).children.front()));
}
NiniXmlDom::NiniXmlDom() : state_(std::make_shared<NiniXmlDomState>()) {
    state_->add(9, nini_writer_text(u"#document"));
}
std::size_t NiniXmlDom::retained_node_count() const {
    return static_cast<std::size_t>(
        std::count_if(state_->records.begin(), state_->records.end(), [](const auto& p) { return bool(p); }));
}
Node NiniXmlDom::document_node() const {
    return handle(state_, 0);
}
Node NiniXmlDom::document_element() const {
    for (auto id : state_->at(0).children)
        if (state_->at(id).type == 1)
            return handle(state_, id);
    return {};
}
Node NiniXmlDom::create_element(Text name) {
    return named(state_, std::move(name), 1);
}
Node NiniXmlDom::create_attribute(Text name) {
    return named(state_, std::move(name), 2);
}
Node NiniXmlDom::create_element(Text prefix, Text local, Text uri) {
    return named_parts(state_, std::move(prefix), std::move(local), std::move(uri), 1);
}
Node NiniXmlDom::create_attribute(Text prefix, Text local, Text uri) {
    return named_parts(state_, std::move(prefix), std::move(local), std::move(uri), 2);
}
Node NiniXmlDom::create_text(Text value) {
    return handle(state_, state_->add(3, nini_writer_text(u"#text"), empty_if_null(value)));
}
Node NiniXmlDom::create_cdata(Text value) {
    return handle(state_, state_->add(4, nini_writer_text(u"#cdata-section"), empty_if_null(value)));
}
Node NiniXmlDom::create_comment(Text value) {
    return handle(state_, state_->add(8, nini_writer_text(u"#comment"), empty_if_null(value)));
}
Node NiniXmlDom::create_whitespace(Text value, bool significant) {
    if (value && nini_xml_invalid_whitespace(*value) >= 0)
        argument(u"The string for whitespace contains an invalid character.");
    return handle(state_,
                  state_->add(significant ? 14 : 13,
                              nini_writer_text(significant ? u"#significant-whitespace" : u"#whitespace"),
                              empty_if_null(value)));
}
Node NiniXmlDom::create_processing_instruction(Text target, Text data) {
    if (!target || target->empty()) {
        NiniXmlProviderError e(target ? "System.ArgumentException" : "System.ArgumentNullException",
                               target ? 0x80070057 : 0x80004003,
                               target ? u"The value cannot be an empty string. (Parameter 'target')"
                                      : u"Value cannot be null. (Parameter 'target')",
                               u"target");
        throw e;
    }
    return handle(state_, state_->add(7, std::move(target), empty_if_null(data)));
}
Node NiniXmlDom::create_fragment() {
    return handle(state_, state_->add(11, nini_writer_text(u"#document-fragment")));
}
bool NiniXmlDomNode::specified() const {
    return state_->at(id_).specified;
}
void NiniXmlDomNode::specified_for_load(bool value) {
    state_->at(id_).specified = value;
}
std::shared_ptr<NiniXmlDtdInfo> NiniXmlDomNode::dtd_info() const {
    return state_->at(id_).dtd;
}
Text NiniXmlDomNode::dtd_public_id() const {
    return state_->at(id_).dtd_public;
}
Text NiniXmlDomNode::dtd_system_id() const {
    return state_->at(id_).dtd_system;
}
Text NiniXmlDomNode::dtd_subset() const {
    return state_->at(id_).dtd_internal;
}
Node NiniXmlDom::create_document_type_for_load(Text name, Text pub, Text sys, Text subset,
                                               std::shared_ptr<NiniXmlDtdInfo> info) {
    auto id = state_->add(10, std::move(name));
    auto& record = state_->at(id);
    record.dtd_public = std::move(pub);
    record.dtd_system = std::move(sys);
    record.dtd_internal = std::move(subset);
    record.dtd = std::move(info);
    return handle(state_, id);
}
Node NiniXmlDom::create_entity_reference(Text name) {
    if (!name)
        nullref();
    if (!name->empty() && name->front() == u'#')
        argument(u"Cannot create an 'EntityReference' node with a name starting with '#'.");
    return handle(state_, state_->add(5, std::move(name)));
}
Node NiniXmlDom::create_declaration(Text version, Text encoding, Text standalone) {
    if (!version)
        nullref();
    if (version->size() < 3 || !std::u16string_view(*version).starts_with(u"1.") ||
        !std::all_of(version->begin() + 2, version->end(), [](auto c) { return c >= u'0' && c <= u'9'; }))
        argument(
            u"Wrong XML version information. The XML must match production \"VersionNum ::= '1.' [0-9]+\".");
    if (standalone && !standalone->empty() && *standalone != u"yes" && *standalone != u"no")
        argument(u"Wrong value for the XML declaration standalone attribute of '" + *standalone + u"'.");
    auto id = state_->add(17, nini_writer_text(u"xml"));
    auto& r = state_->at(id);
    r.version = *version;
    r.encoding = encoding ? *encoding : u"";
    r.standalone = standalone ? *standalone : u"";
    return handle(state_, id);
}
}
