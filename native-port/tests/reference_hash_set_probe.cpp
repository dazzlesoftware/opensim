// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/reference_hash_set.hpp"
#include <iostream>
#include <sstream>
#include <string>
#include <array>
struct Actor {
    int id, serial;
};
using Set = opensim::framework::ReferenceHashSet<Actor>;
std::unordered_map<int, std::shared_ptr<Actor>> actors;
std::unordered_map<int, int> serials;
std::shared_ptr<Set> set;
std::array<std::optional<Set::Enumerator>, 2> enumerators;
std::string trace;
std::shared_ptr<Actor> Get(int id, bool replace = false) {
    if (id == 0)
        return {};
    auto i = actors.find(id);
    if (!replace && i != actors.end())
        return i->second;
    auto value = std::make_shared<Actor>(Actor{id, ++serials[id]});
    actors[id] = value;
    return value;
}
std::string Name(const std::shared_ptr<Actor>& value) {
    return value ? std::to_string(value->id) + "." + std::to_string(value->serial) : "0";
}
std::string H(std::string_view s) {
    std::string out;
    for (unsigned char c : s) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
Set& Required() {
    if (!set)
        throw std::runtime_error("Object reference not set to an instance of an object.");
    return *set;
}
Set::Enumerator& Enum(int n) {
    if (!enumerators[n])
        throw std::runtime_error("Nullable object must have a value.");
    return *enumerators[n];
}
std::string State() {
    std::string rows;
    if (set)
        for (auto& a : *set) {
            if (!rows.empty())
                rows += ',';
            rows += Name(a);
        }
    return (set ? std::to_string(set->size()) + ":" + rows : "N") + ";" +
           (enumerators[0] ? Name(enumerators[0]->current()) : "N") + ";" +
           (enumerators[1] ? Name(enumerators[1]->current()) : "N");
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        set = std::make_shared<Set>();
        actors.clear();
        serials.clear();
        enumerators = {};
        trace.clear();
        std::istringstream in(line);
        std::string output;
        for (std::string token; in >> token;) {
            std::string result = "OK";
            try {
                auto colon = token.find(':');
                std::string op = token.substr(0, colon);
                int n = (colon != std::string::npos && op != "F") ? std::stoi(token.substr(colon + 1)) : 0;
                if (op == "A" || op == "R" || op == "H") {
                    auto value = Get(n);
                    result = (op == "A"   ? Required().insert(value)
                              : op == "R" ? Required().erase(value)
                                          : Required().contains(value))
                                 ? "1"
                                 : "0";
                } else if (op == "X")
                    Get(n, true);
                else if (op == "C")
                    Required().clear();
                else if (op == "N")
                    set.reset();
                else if (op == "S")
                    set = std::make_shared<Set>();
                else if (op == "E")
                    enumerators[n] = Required().get_enumerator();
                else if (op == "M")
                    result = Enum(n).move_next() ? "1" : "0";
                else if (op == "G")
                    result = Name(Enum(n).current());
                else if (op == "B")
                    result = Name(Enum(n).boxed_current());
                else if (op == "Z")
                    Enum(n).reset();
                else if (op == "D")
                    Enum(n).dispose();
                else if (op == "K")
                    enumerators[n] = enumerators[1 - n];
                else if (op == "F") {
                    auto last = token.rfind(':');
                    std::string action = token.substr(colon + 1, last - colon - 1);
                    int id = std::stoi(token.substr(last + 1));
                    for (auto& actor : Required()) {
                        trace += "walk:" + Name(actor) + ";";
                        if (action == "A") {
                            auto v = Get(id);
                            Required().insert(v);
                        } else if (action == "R") {
                            auto v = Get(id);
                            Required().erase(v);
                        } else if (action == "V")
                            Required().erase(actor);
                        else if (action == "C")
                            Required().clear();
                        else if (action == "M") {
                            Required().clear();
                            auto v = Get(id);
                            Required().insert(v);
                        } else if (action == "S")
                            set = std::make_shared<Set>();
                    }
                }
            } catch (const std::exception& error) {
                result = "EX:" + H(error.what());
            }
            if (!output.empty())
                output += '/';
            output += result + ":" + State();
        }
        std::cout << output << "|" << H(trace) << "\n";
    }
}
