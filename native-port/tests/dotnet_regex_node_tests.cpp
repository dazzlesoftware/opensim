// Copyright (c) Contributors, http://opensimulator.org/; BSD native-port/LICENSE.txt.
#include "opensim/framework/dotnet_regex_node.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/dotnet_regex_prefix.hpp"
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
using namespace opensim::framework;
DotnetRegexNodeText dec(const std::string& s) {
    if (s == "-")
        return {};
    std::u16string v;
    if (s != ".")
        for (std::size_t i = 0; i < s.size(); i += 4)
            v.push_back(static_cast<char16_t>(std::stoul(s.substr(i, 4), nullptr, 16)));
    return v.empty() ? runtime_empty_text() : std::make_shared<const std::u16string>(v);
}
std::string hex(const DotnetRegexNodeText& s) {
    if (!s)
        return "-";
    if (s->empty())
        return ".";
    std::ostringstream out;
    for (auto c : *s)
        out << std::hex << std::setfill('0') << std::setw(4) << unsigned(c);
    return out.str();
}
std::string bit(bool b) {
    return b ? "1" : "0";
}
std::string capture(const std::function<std::string()>& f) {
    try {
        return f();
    } catch (const DotnetRegexClassError& e) {
        std::ostringstream out;
        out << "E:" << e.source_type << ':' << std::hex << e.hresult << ':'
            << hex(std::make_shared<const std::u16string>(utf16(e.what())));
        return out.str();
    }
}
using Nodes = std::map<int, DotnetRegexNode*>;
std::string id(Nodes& nodes, DotnetRegexNode* node) {
    if (!node)
        return "-";
    for (auto entry : nodes)
        if (entry.second == node)
            return std::to_string(entry.first);
    return "?";
}
namespace opensim::framework {
struct DotnetRegexNodeProbe {
    static void children(DotnetRegexNode& n, int shape, const std::vector<DotnetRegexNode*>& values) {
        if (shape == 0 || (shape == 1 && !values[0]))
            n.children_ = std::monostate{};
        else if (shape == 1)
            n.children_ = values[0];
        else
            n.children_ = values;
    }
    static void field(DotnetRegexNode& n, const std::string& name, const std::string& value, Nodes& nodes) {
        if (name == "Str")
            n.str_ = dec(value);
        else if (name == "Parent")
            n.parent_ = value == "-" ? nullptr : nodes.at(std::stoi(value));
        else {
            auto v = std::stoll(value);
            if (name == "M")
                n.m_ = static_cast<std::int32_t>(v);
            else if (name == "N")
                n.n_ = static_cast<std::int32_t>(v);
            else if (name == "Ch")
                n.ch_ = static_cast<char16_t>(v);
            else if (name == "Kind")
                n.kind_ = static_cast<DotnetRegexNodeKind>(v);
            else if (name == "Options")
                n.options_ = static_cast<std::int32_t>(v);
        }
    }
    static std::string snapshot(DotnetRegexNode& n, Nodes& nodes) {
        std::string shape = "-";
        if (auto one = std::get_if<DotnetRegexNode*>(&n.children_))
            shape = "o:" + id(nodes, *one);
        else if (auto list = std::get_if<std::vector<DotnetRegexNode*>>(&n.children_)) {
            shape = "l:";
            for (std::size_t i = 0; i < list->size(); ++i) {
                if (i)
                    shape += ',';
                shape += id(nodes, (*list)[i]);
            }
        }
        return std::to_string(unsigned(n.kind_)) + ":" + std::to_string(n.options_) + ":" +
               std::to_string(unsigned(n.ch_)) + ":" + std::to_string(n.m_) + ":" + std::to_string(n.n_) +
               ":" + hex(n.str_) + ":" + id(nodes, n.parent_) + ":" + shape + ":" + bit(n.is_one_family()) +
               bit(n.is_notone_family()) + bit(n.is_set_family());
    }
};
}
std::string literal(DotnetRegexNode& node, int capacity) {
    auto result = node.find_starting_literal(capacity);
    if (!result)
        return "-";
    DotnetRegexNodeText ascii;
    if (result->ascii_chars)
        ascii =
            std::make_shared<const std::u16string>(result->ascii_chars->begin(), result->ascii_chars->end());
    return std::to_string(unsigned(result->range.first)) + "," +
           std::to_string(unsigned(result->range.last)) + ":" + hex(result->string) + ":" +
           hex(result->set_chars) + ":" + hex(ascii) + ":" + bit(result->negated);
}
std::string graph(Nodes nodes) {
    Nodes objects;
    std::map<DotnetRegexNode*, int> ids;
    auto add = [&](DotnetRegexNode* node) {
        if (!node)
            return std::string("-");
        auto found = ids.find(node);
        if (found == ids.end()) {
            int key = static_cast<int>(ids.size());
            ids[node] = key;
            objects[key] = node;
            return std::to_string(key);
        }
        return std::to_string(found->second);
    };
    std::string result;
    for (auto entry : nodes)
        result += std::to_string(entry.first) + "=" + add(entry.second) + ",";
    for (int i = 0; i < static_cast<int>(objects.size()); ++i) {
        auto node = objects.at(i);
        for (int j = 0; j < node->child_count(); ++j)
            add(node->child(j));
        add(node->parent());
    }
    for (auto entry : objects) {
        result +=
            "/" + std::to_string(entry.first) + "=" + DotnetRegexNodeProbe::snapshot(*entry.second, objects);
        int first = 0;
        for (auto other : objects) {
            if (other.second->text() == entry.second->text()) {
                first = other.first;
                break;
            }
        }
        result += "@" + std::to_string(first);
    }
    return result;
}
int main(int argc, char**) {
    if (argc > 1) {
        DotnetRegexNodeArena arena;
        auto root = arena.create(DotnetRegexNodeKind::Capture);
        int calls = 0;
        DotnetRegexExecutionStack no = [&]() {
            ++calls;
            return false;
        };
        if (root->compute_min_length(no) != 0 || root->compute_max_length(no).has_value() || calls != 2)
            return 1;
        try {
            root->compute_min_length({});
            return 2;
        } catch (const std::invalid_argument&) {
        }
        if (DotnetRegexNode::can_be_made_atomic(nullptr, nullptr, true, true, no))
            return 3;
        root->find_and_make_loops_atomic(no);
        try {
            DotnetRegexNode::can_be_made_atomic(nullptr, nullptr, true, true, {});
            return 4;
        } catch (const std::invalid_argument&) {
        }
        int before = calls;
        if (*dotnet_regex_find_prefix(nullptr, no) != u"" ||
            dotnet_regex_find_first_char_class(nullptr, no) ||
            dotnet_regex_find_fixed_distance_sets(nullptr, true, no) ||
            dotnet_regex_find_leading_anchor(nullptr, no) != DotnetRegexNodeKind::Unknown ||
            dotnet_regex_find_trailing_anchor(nullptr, no) != DotnetRegexNodeKind::Unknown ||
            calls - before != 6)
            return 5;
        try {
            dotnet_regex_find_prefix(nullptr, {});
            return 6;
        } catch (const std::invalid_argument&) {
        }
        std::cout << "PASS required stack policy\n";
        return 0;
    }
    for (std::string row; std::getline(std::cin, row);) {
        DotnetRegexNodeArena arena;
        Nodes nodes;
        std::istringstream commands(row);
        bool first = true;
        for (std::string command; std::getline(commands, command, ';');) {
            if (!first)
                std::cout << '|';
            first = false;
            std::cout << capture([&]() {
                std::istringstream in(command);
                std::string op, a, b;
                int key;
                in >> op >> key;
                if (op == "N") {
                    int kind, options;
                    in >> kind >> options >> a;
                    auto k = static_cast<DotnetRegexNodeKind>(kind);
                    if (a == "z")
                        nodes[key] = arena.create(k, options);
                    else if (a == "c") {
                        int c;
                        in >> c;
                        nodes[key] = arena.create(k, options, static_cast<char16_t>(c));
                    } else if (a == "s") {
                        in >> b;
                        nodes[key] = arena.create(k, options, dec(b));
                    } else {
                        int m, n = 0;
                        in >> m;
                        if (a == "q")
                            in >> n;
                        nodes[key] = arena.create(k, options, m, n);
                    }
                    return op;
                }
                if (op == "C") {
                    int code, options, initial;
                    in >> code >> options >> a >> initial;
                    auto name = dec(a);
                    int reads = 0;
                    auto behavior = static_cast<DotnetRegexCaseBehavior>(initial);
                    nodes[key] = arena.create_one_with_case_conversion(
                        static_cast<char16_t>(code), options,
                        [&]() {
                            ++reads;
                            return name ? *name : std::u16string{};
                        },
                        behavior);
                    return op + ":" + std::to_string(static_cast<int>(behavior)) + ":" +
                           std::to_string(reads);
                }
                if (op == "U") {
                    int source;
                    in >> source >> a;
                    auto input = nodes.at(source);
                    nodes[key] = a == "g"   ? input->reduce_group()
                                 : a == "l" ? input->reduce_loops()
                                 : a == "c" ? input->reduce_concatenation()
                                            : input->replace_node_if_unnecessary();
                    return op;
                }
                auto node = nodes.at(key);
                if (op == "S") {
                    int start, bound, zero, consumed = 123;
                    in >> start >> bound >> zero;
                    auto result = std::make_shared<const std::u16string>(u"sentinel");
                    bool success = node->try_get_ordinal_case_insensitive_string(start, bound, consumed,
                                                                                 result, zero != 0);
                    return bit(success) + ":" + std::to_string(consumed) + ":" + hex(result);
                }
                if (op == "D")
                    return graph(nodes);
                if (op == "K") {
                    int lazy, minimum, maximum;
                    in >> lazy >> minimum >> maximum;
                    nodes[999] = node->make_quantifier(lazy != 0, minimum, maximum, [] { return true; });
                    return op;
                }
                if (op == "B") {
                    nodes[999] = node->final_optimize([] { return true; });
                    return op;
                }
                if (op == "P") {
                    int other, index;
                    in >> a >> other >> index;
                    auto adopted = other < 0 ? nullptr : nodes.at(other);
                    if (a == "a")
                        node->add_child(adopted, [] { return true; });
                    else if (a == "i")
                        node->insert_child(index, adopted, [] { return true; });
                    else
                        node->replace_child(index, adopted, [] { return true; });
                    return op;
                }
                if (op == "Y") {
                    in >> a;
                    DotnetRegexNode* result = node;
                    if (a == "r")
                        result = node->reduce([] { return true; });
                    else if (a == "a")
                        result = node->reduce_alternation([] { return true; });
                    else if (a == "t")
                        result = node->reduce_atomic([] { return true; });
                    else if (a == "l")
                        result = node->reduce_lookaround([] { return true; });
                    else if (a == "e")
                        result = node->reduce_expression_conditional([] { return true; });
                    else if (a == "b")
                        result = node->reduce_backreference_conditional([] { return true; });
                    else
                        node->eliminate_ending_backtracking([] { return true; });
                    nodes[999] = result;
                    return op;
                }
                if (op == "W") {
                    int other, iterate, lazy;
                    in >> other >> iterate >> lazy;
                    return bit(DotnetRegexNode::can_be_made_atomic(node, nodes.at(other), iterate != 0,
                                                                   lazy != 0, [] { return true; }));
                }
                if (op == "E") {
                    in >> a;
                    if (a == "f")
                        return id(nodes,
                                  node->find_last_expression_in_loop_for_auto_atomic([] { return true; }));
                    node->find_and_make_loops_atomic([] { return true; });
                    return op;
                }
                if (op == "O") {
                    in >> a;
                    if (a == "s") {
                        node->reduce_single_letter_and_nested_alternations();
                        return op;
                    }
                    return id(nodes, node->remove_redundant_empties_and_nothings());
                }
                if (op == "F") {
                    in >> a >> b;
                    DotnetRegexNodeProbe::field(*node, a, b, nodes);
                    return op;
                }
                if (op == "H") {
                    int shape;
                    in >> shape;
                    std::vector<DotnetRegexNode*> values;
                    while (in >> a)
                        values.push_back(a == "-" ? nullptr : nodes.at(std::stoi(a)));
                    DotnetRegexNodeProbe::children(*node, shape, values);
                    return op;
                }
                if (op == "Q")
                    return DotnetRegexNodeProbe::snapshot(*node, nodes);
                if (op == "V") {
                    in >> a;
                    if (a == "s")
                        node->reduce_adjacent_strings();
                    else
                        node->reduce_adjacent_loops();
                    return op;
                }
                if (op == "I") {
                    int other;
                    in >> other;
                    return bit(node->text() == nodes.at(other)->text());
                }
                if (op == "J") {
                    int index;
                    in >> index;
                    return id(nodes, node->child(index));
                }
                if (op == "Z") {
                    node->reduce_set();
                    return op;
                }
                if (op == "T") {
                    node->make_loop_atomic();
                    return op;
                }
                if (op == "R") {
                    node->reverse_concatenation_if_right_to_left();
                    return op;
                }
                if (op == "L") {
                    int capacity;
                    in >> capacity;
                    return literal(*node, capacity);
                }
                if (op == "A") {
                    std::vector<std::string> fields;
                    fields.push_back(capture(
                        [&]() { return std::to_string(node->compute_min_length([]() { return true; })); }));
                    fields.push_back(capture([&]() {
                        auto value = node->compute_max_length([]() { return true; });
                        return value ? std::to_string(*value) : "-";
                    }));
                    std::optional<std::u16string> reason = u"sentinel";
                    auto support = capture([&]() { return bit(node->supports_compilation(reason)); });
                    fields.push_back(support + ":" +
                                     hex(reason ? std::make_shared<const std::u16string>(*reason) : nullptr));
                    fields.push_back(
                        capture([&]() { return id(nodes, node->find_branch_one_or_multi_start()); }));
                    fields.push_back(capture(
                        [&]() { return std::to_string(unsigned(node->first_char_of_one_or_multi())); }));
                    fields.push_back(
                        capture([&]() { return id(nodes, node->find_starting_literal_node()); }));
                    std::string result;
                    for (std::size_t i = 0; i < fields.size(); ++i) {
                        if (i)
                            result += ",";
                        result += fields[i];
                    }
                    return result;
                }
                throw std::logic_error("Unknown node probe operation");
            });
        }
        std::cout << '\n';
    }
}
