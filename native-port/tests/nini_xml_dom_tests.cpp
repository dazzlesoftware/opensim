#include "opensim/framework/nini_xml_dom.hpp"
#include <iostream>
#include <map>
#include <sstream>
using namespace opensim::framework;
using Text = NiniWriterText;
using Node = std::shared_ptr<NiniXmlDomNode>;
Text dec(const std::string &s)
{
    if (s == "~")
        return {};
    std::u16string v;
    if (s != "-")
        for (std::size_t i = 0; i < s.size(); i += 4)
            v += static_cast<char16_t>(std::stoul(s.substr(i, 4), nullptr, 16));
    return nini_writer_text(std::move(v));
}
std::string enc(Text s)
{
    if (!s)
        return "~";
    if (s->empty())
        return "-";
    constexpr char hex[] = "0123456789abcdef";
    std::string out;
    for (auto c : *s)
        for (int shift = 12; shift >= 0; shift -= 4)
            out += hex[(c >> shift) & 15];
    return out;
}
std::string shape(const Node &n)
{
    if (!n)
        return "~";
    std::string out = std::to_string(n->node_type()) + ":" + enc(n->name()) + ":" + enc(n->value()) + "[";
    auto e = n->children();
    while (e->move_next())
    {
        auto c = std::dynamic_pointer_cast<NiniXmlDomNode>(e->current());
        out += shape(c) + ",";
    }
    out += "]{";
    for (auto &a : n->attributes())
        out += shape(a) + ",";
    return out + "}";
}
int main(int argc, char **)
{
    if (argc == 1)
    {
        auto require = [](bool v) {
            if (!v)
                throw std::runtime_error("Native XML DOM unit failed");
        };
        Node retained;
        {
            NiniXmlDom collection;
            for (int i = 0; i < 1000; ++i)
            {
                auto orphan = collection.create_element(nini_writer_text(u"orphan"));
                auto child = collection.create_element(nini_writer_text(u"child"));
                orphan->append_child(child);
            }
            require(collection.retained_node_count() == 1);
        }
        {
            NiniXmlDom dom;
            auto root = dom.create_element(nini_writer_text(u"root"));
            dom.document_node()->append_child(root);
            retained = dom.create_element(nini_writer_text(u"child"));
            root->append_child(retained);
            auto later = dom.create_element(nini_writer_text(u"later"));
            root->append_child(later);
            auto e = root->children();
            require(e->move_next());
            root->remove_child(retained);
            require(!e->move_next());
            require(!retained->parent());
            require(root->children()->move_next());
            auto attribute = dom.create_attribute(nini_writer_text(u"x"));
            attribute->value(nini_writer_text(u"one"));
            auto ae = attribute->children();
            require(ae->move_next());
            auto text = std::dynamic_pointer_cast<NiniXmlDomNode>(ae->current());
            attribute->value(nini_writer_text(u"two"));
            require(text->parent()->identity() == attribute->identity());
            require(*text->value() == u"two");
            root->append_attribute(attribute);
            root->append_attribute(dom.create_attribute(nini_writer_text(u"x")));
            require(!attribute->owner_element());
        }
        require(*retained->name() == u"child");
        return 0;
    }
    std::string line;
    while (std::getline(std::cin, line))
    {
        std::map<int, NiniXmlDom> docs;
        std::map<int, Node> nodes;
        std::map<int, std::unique_ptr<NiniXmlChildEnumerator>> enums;
        std::stringstream commands(line);
        std::string cmd;
        std::string result;
        auto get = [&](int id) {
            auto p = nodes.find(id);
            return p == nodes.end() ? Node{} : p->second;
        };
        auto required = [&](int id) {
            auto n = get(id);
            if (!n)
                throw NiniXmlProviderError("System.NullReferenceException", 0x80004003,
                                           u"Object reference not set to an instance of an object.");
            return n;
        };
        while (std::getline(commands, cmd, '|'))
        {
            std::istringstream in(cmd);
            std::string op, value;
            int a = 0, b = 0;
            in >> op >> a;
            std::string out = "OK";
            try
            {
                if (op == "D")
                {
                    docs.insert_or_assign(a, NiniXmlDom{});
                    nodes[a] = docs.at(a).document_node();
                }
                else if (op == "E" || op == "A" || op == "T" || op == "C" || op == "W" || op == "S" || op == "PI" ||
                         op == "CD")
                {
                    in >> b >> value;
                    nodes[a] = op == "E"    ? docs.at(b).create_element(dec(value))
                               : op == "A"  ? docs.at(b).create_attribute(dec(value))
                               : op == "T"  ? docs.at(b).create_text(dec(value))
                               : op == "C"  ? docs.at(b).create_comment(dec(value))
                               : op == "W"  ? docs.at(b).create_whitespace(dec(value))
                               : op == "S"  ? docs.at(b).create_whitespace(dec(value), true)
                               : op == "CD" ? docs.at(b).create_cdata(dec(value))
                                            : docs.at(b).create_processing_instruction(dec(value), dec("0076"));
                }
                else if (op == "F")
                {
                    in >> b;
                    nodes[a] = docs.at(b).create_fragment();
                }
                else if (op == "ER")
                {
                    in >> b >> value;
                    nodes[a] = docs.at(b).create_entity_reference(dec(value));
                }
                else if (op == "EP" || op == "AP")
                {
                    std::string local, uri;
                    in >> b >> value >> local >> uri;
                    nodes[a] = op == "EP" ? docs.at(b).create_element(dec(value), dec(local), dec(uri))
                                          : docs.at(b).create_attribute(dec(value), dec(local), dec(uri));
                }
                else if (op == "APP" || op == "RM" || op == "AA")
                {
                    in >> b;
                    if (op == "APP")
                        required(a)->append_child(get(b));
                    else if (op == "RM")
                        required(a)->remove_child(get(b));
                    else
                        required(a)->append_attribute(get(b));
                }
                else if (op == "V")
                {
                    in >> value;
                    required(a)->value(dec(value));
                }
                else if (op == "EN")
                {
                    in >> b;
                    enums[a] = required(b)->children();
                }
                else if (op == "MV")
                    out = enums.at(a)->move_next() ? "1" : "0";
                else if (op == "CU")
                {
                    in >> b;
                    nodes[a] = std::dynamic_pointer_cast<NiniXmlDomNode>(enums.at(b)->current());
                }
                else if (op == "GET")
                {
                    in >> b >> value;
                    nodes[a] = std::dynamic_pointer_cast<NiniXmlDomNode>(required(b)->attribute(dec(value)));
                }
                else if (op == "ROOT")
                {
                    in >> b;
                    nodes[a] = docs.at(b).document_element();
                }
                else if (op == "P" || op == "N" || op == "OWN")
                {
                    in >> b;
                    nodes[a] = op == "P"   ? required(b)->parent()
                               : op == "N" ? required(b)->next_sibling()
                                           : required(b)->owner_element();
                }
                else if (op == "EQ")
                {
                    in >> b;
                    out = (!nodes[a] && !nodes[b]) || (nodes[a] && nodes[b] && required(a)->same_owner(*nodes[b]) &&
                                                       required(a)->identity() == required(b)->identity())
                              ? "1"
                              : "0";
                }
                else if (op == "DROP")
                {
                    docs.erase(a);
                    nodes.erase(a);
                }
                else
                    throw std::runtime_error("unknown DOM fixture operation");
            }
            catch (const NiniXmlProviderError &e)
            {
                out = "E:" + e.source_type + ":" + std::to_string(e.hresult) + ":" + enc(nini_writer_text(e.message)) +
                      ":" + (e.parameter ? enc(nini_writer_text(*e.parameter)) : "~");
            }
            result += out + "/";
            for (auto &[id, n] : nodes)
                result += std::to_string(id) + "=" + shape(n) + ";";
            result += "|";
        }
        std::cout << result << '\n';
    }
}
