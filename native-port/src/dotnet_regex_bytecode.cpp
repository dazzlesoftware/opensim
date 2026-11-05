// Copyright (c) .NET Foundation and Contributors.
// .NET8.0.31 RegexWriter; MIT third_party/dotnet_collation/LICENSE.
#include "opensim/framework/dotnet_regex_bytecode.hpp"
#include <bit>
namespace opensim::framework {
namespace {
using K = DotnetRegexNodeKind;
using O = DotnetRegexOpcode;
[[noreturn]] void null_reference() {
    throw DotnetRegexClassError("System.NullReferenceException", 0x80004003u,
                                "Object reference not set to an instance of an object.");
}
[[noreturn]] void index_error() {
    throw DotnetRegexClassError("System.IndexOutOfRangeException", 0x80131508u,
                                "Index was outside the bounds of the array.");
}
DotnetRegexNode* required(DotnetRegexNode* node) {
    if (!node)
        null_reference();
    return node;
}
int wrap(std::int64_t value) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value));
}
class Writer {
    const DotnetRegexParsedPattern& tree_;
    DotnetRegexBytecode output_;
    std::map<std::u16string, int> strings_;
    std::vector<int> stack_;
    int position() const {
        return static_cast<int>(output_.codes.size());
    }
    int pop() {
        if (stack_.empty())
            index_error();
        int result = stack_.back();
        stack_.pop_back();
        return result;
    }
    void patch(int offset, int target) {
        if (offset < -1 ||
            static_cast<std::size_t>(static_cast<std::int64_t>(offset) + 1) >= output_.codes.size())
            index_error();
        output_.codes[offset + 1] = target;
    }
    void emit(int opcode) {
        if (dotnet_regex_opcode_backtracks(opcode))
            ++output_.track_count;
        output_.codes.push_back(opcode);
    }
    void emit(O opcode) {
        emit(static_cast<int>(opcode));
    }
    void emit(int opcode, int a) {
        emit(opcode);
        output_.codes.push_back(a);
    }
    void emit(O opcode, int a) {
        emit(static_cast<int>(opcode), a);
    }
    void emit(int opcode, int a, int b) {
        emit(opcode, a);
        output_.codes.push_back(b);
    }
    void emit(O opcode, int a, int b) {
        emit(static_cast<int>(opcode), a, b);
    }
    int string_code(const DotnetRegexNodeText& value) {
        if (!value)
            throw DotnetRegexClassError("System.ArgumentNullException", 0x80004003u,
                                        "Value cannot be null. (Parameter 'key')");
        auto found = strings_.find(*value);
        if (found != strings_.end())
            return found->second;
        int index = static_cast<int>(strings_.size());
        strings_.emplace(*value, index);
        output_.strings.push_back(value);
        return index;
    }
    int capture(int number) const {
        return dotnet_regex_map_capture_number(number, tree_.capture_number_sparse_mapping);
    }
    void fragment(int type, DotnetRegexNode* node, int child) {
        int flags = ((node->options() & 64) ? 64 : 0) | ((node->options() & 1) ? 512 : 0);
        switch (type) {
        case 88:
            if (child < node->child_count() - 1) {
                stack_.push_back(position());
                emit(O::Lazybranch, 0);
            }
            break;
        case 152:
            if (child < node->child_count() - 1) {
                int offset = pop();
                stack_.push_back(position());
                emit(O::Goto, 0);
                patch(offset, position());
            } else
                for (int i = 0; i < child; ++i)
                    patch(pop(), position());
            break;
        case 97:
            if (child == 0) {
                emit(O::Setjump);
                stack_.push_back(position());
                emit(O::Lazybranch, 0);
                emit(O::TestBackreference, capture(node->minimum()));
                emit(O::Forejump);
            }
            break;
        case 161:
            if (child == 0) {
                int offset = pop();
                stack_.push_back(position());
                emit(O::Goto, 0);
                patch(offset, position());
                emit(O::Forejump);
            } else if (child == 1)
                patch(pop(), position());
            break;
        case 98:
            if (child == 0) {
                emit(O::Setjump);
                emit(O::Setmark);
                stack_.push_back(position());
                emit(O::Lazybranch, 0);
            }
            break;
        case 162:
            switch (child) {
            case 0:
                emit(O::Getmark);
                emit(O::Forejump);
                break;
            case 1: {
                int offset = pop();
                stack_.push_back(position());
                emit(O::Goto, 0);
                patch(offset, position());
                emit(O::Getmark);
                emit(O::Forejump);
                break;
            }
            case 2:
                patch(pop(), position());
                break;
            }
            break;
        case 90:
        case 91:
            if (node->maximum() < 2147483647 || node->minimum() > 1)
                emit(node->minimum() == 0 ? O::Nullcount : O::Setcount,
                     node->minimum() != 0 ? wrap(1LL - node->minimum()) : 0);
            else
                emit(node->minimum() == 0 ? O::Nullmark : O::Setmark);
            if (node->minimum() == 0) {
                stack_.push_back(position());
                emit(O::Goto, 0);
            }
            stack_.push_back(position());
            break;
        case 154:
        case 155: {
            int before = position(), lazy = type - 154;
            int body = pop();
            if (node->maximum() < 2147483647 || node->minimum() > 1)
                emit(28 + lazy, body,
                     node->maximum() == 2147483647
                         ? 2147483647
                         : wrap(static_cast<std::int64_t>(node->maximum()) - node->minimum()));
            else
                emit(24 + lazy, body);
            if (node->minimum() == 0)
                patch(pop(), before);
            break;
        }
        case 92:
            emit(O::Setmark);
            break;
        case 156: {
            int first = capture(node->minimum());
            int second = capture(node->maximum());
            emit(O::Capturemark, first, second);
            break;
        }
        case 94:
            emit(O::Setjump);
            emit(O::Setmark);
            break;
        case 158:
            emit(O::Getmark);
            emit(O::Forejump);
            break;
        case 95:
            emit(O::Setjump);
            stack_.push_back(position());
            emit(O::Lazybranch, 0);
            break;
        case 159:
            emit(O::Backjump);
            patch(pop(), position());
            emit(O::Forejump);
            break;
        case 96:
            emit(O::Setjump);
            break;
        case 160:
            emit(O::Forejump);
            break;
        case 9:
        case 10:
            emit(static_cast<int>(node->kind()) | flags, node->character());
            break;
        case 3:
        case 4:
        case 6:
        case 7:
        case 43:
        case 44: {
            int kind = static_cast<int>(node->kind());
            if (node->minimum() > 0)
                emit(((kind == 3 || kind == 6 || kind == 43) ? 0 : 1) | flags, node->character(),
                     node->minimum());
            if (node->maximum() > node->minimum())
                emit(kind | flags, node->character(),
                     node->maximum() == 2147483647
                         ? 2147483647
                         : wrap(static_cast<std::int64_t>(node->maximum()) - node->minimum()));
            break;
        }
        case 5:
        case 8:
        case 45: {
            int index = string_code(node->text());
            if (node->minimum() > 0)
                emit(2 | flags, index, node->minimum());
            if (node->maximum() > node->minimum())
                emit(static_cast<int>(node->kind()) | flags, index,
                     node->maximum() == 2147483647
                         ? 2147483647
                         : wrap(static_cast<std::int64_t>(node->maximum()) - node->minimum()));
            break;
        }
        case 11:
        case 12:
            emit(static_cast<int>(node->kind()) | flags, string_code(node->text()));
            break;
        case 13:
            emit(static_cast<int>(node->kind()) | flags, capture(node->minimum()));
            break;
        case 14:
        case 15:
        case 16:
        case 17:
        case 18:
        case 19:
        case 20:
        case 21:
        case 22:
        case 41:
        case 42:
        case 46:
            emit(static_cast<int>(node->kind()));
            break;
        default:
            break;
        }
    }

  public:
    explicit Writer(const DotnetRegexParsedPattern& tree) : tree_(tree) {
        output_.options = tree.options;
    }
    DotnetRegexBytecode write() {
        emit(O::Lazybranch, 0);
        auto node = required(tree_.root);
        int child = 0;
        for (;;) {
            int count = node->child_count();
            if (count == 0)
                fragment(static_cast<int>(node->kind()), node, 0);
            else if (child < count) {
                fragment(static_cast<int>(node->kind()) | 64, node, child);
                node = required(node->child(child));
                stack_.push_back(child);
                child = 0;
                continue;
            }
            if (stack_.empty())
                break;
            child = pop();
            node = required(node->parent());
            fragment(static_cast<int>(node->kind()) | 128, node, child);
            ++child;
        }
        patch(0, position());
        emit(O::Stop);
        output_.strings_ascii_lookup = std::make_unique<DotnetRegexAsciiCacheSlot[]>(output_.strings.size());
        return std::move(output_);
    }
};
}
bool dotnet_regex_opcode_backtracks(std::int32_t opcode) {
    switch (opcode & 63) {
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
    case 28:
    case 29:
    case 31:
    case 32:
    case 33:
    case 34:
    case 35:
    case 36:
    case 38:
        return true;
    default:
        return false;
    }
}
DotnetRegexBytecode dotnet_regex_write(const DotnetRegexParsedPattern& tree) {
    return Writer(tree).write();
}
}
