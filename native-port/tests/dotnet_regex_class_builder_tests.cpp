// Copyright (c) Contributors, http://opensimulator.org/; BSD native-port/LICENSE.txt.
#include "opensim/framework/dotnet_regex_class_builder.hpp"
#include "opensim/framework/text.hpp"
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
using namespace opensim::framework;
std::u16string dec(const std::string& s) {
    if (s == ".")
        return {};
    std::u16string result;
    for (std::size_t i = 0; i < s.size(); i += 4)
        result.push_back(static_cast<char16_t>(std::stoul(s.substr(i, 4), nullptr, 16)));
    return result;
}
std::string hex(std::u16string_view s) {
    if (s.empty())
        return ".";
    std::ostringstream out;
    for (auto c : s)
        out << std::hex << std::setfill('0') << std::setw(4) << unsigned(c);
    return out.str();
}
namespace opensim::framework {
struct DotnetRegexClassBuilderProbe {
    static std::string snapshot(DotnetRegexClassBuilder& v) {
        auto text = v.to_string_class();
        return "O:" + hex(text) + ":" + std::to_string(v.can_merge()) + ":" +
               std::to_string(v.ranges_.has_value()) + ":" + std::to_string(v.categories_.has_value()) + ":" +
               std::to_string(static_cast<int>(v.case_behavior_));
    }
};
}
int main() {
    for (std::string row; std::getline(std::cin, row);) {
        std::map<int, std::shared_ptr<DotnetRegexClassBuilder>> objects;
        std::istringstream commands(row);
        bool first = true;
        for (std::string command; std::getline(commands, command, ';');) {
            if (!first)
                std::cout << '|';
            first = false;
            try {
                std::istringstream in(command);
                std::string op, a, b;
                int id = 0, other = 0, flag = 0, extra = 0;
                in >> op;
                if (op == "C") {
                    in >> a >> b;
                    std::cout << "C:" << hex(dotnet_regex_convert_old_class(dec(a), dec(b)));
                    continue;
                }
                in >> id;
                if (op == "N")
                    objects[id] = std::make_shared<DotnetRegexClassBuilder>();
                else if (op == "P") {
                    in >> a;
                    objects[id] = DotnetRegexClassBuilder::parse(dec(a));
                } else if (op == "O") {
                    std::cout << DotnetRegexClassBuilderProbe::snapshot(*objects.at(id));
                    continue;
                } else if (op == "R") {
                    in >> flag >> extra;
                    objects.at(id)->add_range(static_cast<char16_t>(flag), static_cast<char16_t>(extra));
                } else if (op == "A") {
                    in >> flag;
                    objects.at(id)->add_char(static_cast<char16_t>(flag));
                } else if (op == "V") {
                    in >> flag;
                    objects.at(id)->negate(flag != 0);
                } else if (op == "X") {
                    in >> other;
                    objects.at(id)->add_subtraction(other < 0 ? nullptr : objects.at(other));
                } else if (op == "M") {
                    in >> other >> flag;
                    if (flag) {
                        std::cout << "M:" << objects.at(id)->try_add_class(*objects.at(other));
                        continue;
                    }
                    objects.at(id)->add_class(*objects.at(other));
                } else if (op == "G") {
                    int pos;
                    std::string pattern;
                    in >> a >> flag >> extra >> pattern >> pos;
                    objects.at(id)->add_category_from_name(dec(a), flag != 0, extra != 0, dec(pattern), pos);
                } else if (op == "B") {
                    in >> a >> flag;
                    for (int i = 0; i < flag; ++i)
                        objects.at(id)->add_category_from_name(dec(a), false, false, u"", 0);
                } else if (op == "U") {
                    for (unsigned i = 0; i < 65536; i += 2)
                        objects.at(id)->add_char(static_cast<char16_t>(i));
                } else if (op == "E") {
                    in >> a;
                    auto name = dec(a);
                    int reads = 0;
                    objects.at(id)->add_case_equivalences([&]() {
                        ++reads;
                        return name;
                    });
                    std::cout << "E:" << reads;
                    continue;
                } else {
                    in >> flag >> extra;
                    if (op == "W")
                        objects.at(id)->add_word(flag != 0, extra != 0);
                    else if (op == "S")
                        objects.at(id)->add_space(flag != 0, extra != 0);
                    else if (op == "D")
                        objects.at(id)->add_digit(flag != 0, extra != 0);
                    else
                        throw std::logic_error("Unknown builder operation");
                }
                std::cout << op;
            } catch (const DotnetRegexParseException& e) {
                std::cout << "E:" << e.source_type << ':' << std::hex << e.hresult << std::dec << ':'
                          << hex(e.message) << ':' << e.error << ':' << e.offset;
            } catch (const DotnetRegexClassError& e) {
                std::cout << "E:" << e.source_type << ':' << std::hex << e.hresult << std::dec << ':'
                          << hex(utf16(e.what())) << ":-:-";
            } catch (const std::exception& e) {
                std::cout << "NATIVE:" << e.what();
            }
        }
        std::cout << '\n';
    }
}
