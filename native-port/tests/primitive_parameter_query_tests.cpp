// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/primitive_parameter_query.hpp"
#include "opensim/framework/invariant_number_parse.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace opensim;
using namespace script;
using framework::Uuid;

namespace {
Uuid id(int value) {
    auto bytes = Uuid{}.bytes();
    bytes.back() = static_cast<std::uint8_t>(value);
    return Uuid::from_bytes(bytes);
}

LslValue link_value(int kind) {
    switch (kind) {
    case 1:
        return 2.9;
    case 2:
        return std::string(" 2 ");
    case 3:
        return std::string("2.5");
    case 4:
        return framework::Vector3d{};
    case 5:
        return LslRotation{};
    case 6:
        return std::make_shared<const LslList>();
    case 7:
        return 1.0e100;
    case 8:
        return std::string("2147483648");
    default:
        return std::int32_t{2};
    }
}

std::string observe(int api, int initial_link, int denied, int scenario, int kind, int empty) {
    std::string trace;
    PrimitiveParameterQueryBackend backend;
    backend.other_object_permitted = [&](Uuid) { return denied == 0; };
    backend.select = [&](Uuid, int link, PrimitiveQuerySelection selection) {
        std::vector<Uuid> result;
        if (selection == PrimitiveQuerySelection::Entities) {
            trace += "E" + std::to_string(link) + ";";
            if (link == 1)
                result.push_back(id(1));
            if (link == 2)
                result.insert(result.end(), {id(2), id(3), id(101)});
            if (link == 3)
                result.insert(result.end(), {id(3), id(102)});
        } else {
            trace += "P" + std::to_string(link) + ";";
            if (link == 1)
                result.push_back(id(1));
            if (link == 2)
                result.insert(result.end(), {id(2), id(3)});
            if (link == 3)
                result.push_back(id(3));
            if (selection == PrimitiveQuerySelection::PartsAndAvatars) {
                trace += "A" + std::to_string(link) + ";";
                if (link == 2 || link == 3)
                    result.push_back(id(101 + link - 2));
            }
        }
        return result;
    };
    backend.read = [&](Uuid entity, const LslList &rules, LslList &result) {
        const int number = entity.bytes().back();
        const int stage = rules.items.empty() ? 0 : std::get<std::int32_t>(rules.items.front());
        trace += "R" + std::to_string(number) + "," + std::to_string(rules.items.size()) + ";";
        result.items.emplace_back(number * 10 + stage);
        if (stage == 1) {
            LslList remaining{{link_value(kind)}};
            if (scenario != 2)
                remaining.items.emplace_back(std::int32_t{2});
            if (scenario == 3)
                remaining.items.emplace_back(std::int32_t{99});
            return remaining;
        }
        if (stage == 2)
            return LslList{{std::int32_t{scenario == 1 && number == 3 ? 4 : 3}, std::int32_t{3}}};
        return LslList{};
    };
    backend.error = [&](const auto &message) { trace += "ERR:" + message + ";"; };
    try {
        LslList rules;
        if (!empty)
            rules.items.emplace_back(std::int32_t{1});
        const auto result =
            query_primitive_parameters(backend, static_cast<PrimitiveQueryApi>(api), id(1), initial_link, rules);
        trace += "|";
        for (const auto &value : result.items)
            trace += std::to_string(std::get<std::int32_t>(value)) + ",";
    } catch (const framework::InvariantNumberError &error) {
        trace +=
            error.kind == framework::InvariantNumberError::Kind::Format ? "|!FormatException" : "|!OverflowException";
    } catch (const std::invalid_argument &) {
        trace += "|!InvalidCastException";
    }
    return trace;
}

void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main(int argc, char **) {
    try {
        if (argc > 1) {
            int api, link, denied, scenario, kind, empty;
            while (std::cin >> api >> link >> denied >> scenario >> kind >> empty)
                std::cout << observe(api, link, denied, scenario, kind, empty) << "\n";
            return 0;
        }
        check(observe(2, 1, 1, 0, 0, 0) == "|", "Other-object permission denial precedes reads and selection");
        check(observe(0, 1, 0, 0, 4, 0) == "R1,1;|!InvalidCastException",
              "The ordinary getter propagates invalid link-target types");
        check(observe(2, 1, 0, 0, 4, 0) == "R1,1;ERR:Error PRIM_LINK_TARGET: parameter must be integer;|11,",
              "The other-object getter reports invalid link types and retains partial results");
        check(observe(1, 1, 0, 0, 0, 1) == "|", "The link getter avoids reads and selection for empty input");
        check(observe(3, 1, 0, 0, 0, 1) == "P1;R1,0;|10,", "The OSSL link getter retains its initial empty-rule read");
        check(observe(1, 2, 0, 0, 0, 0).starts_with("P2;A2;R2,1;R3,1;R101,1;"),
              "The link getter enumerates parts before avatars");
        check(primitive_rule_integer(std::string(" +12 ")) == 12 && primitive_rule_integer(2.9) == 2 &&
                  primitive_rule_integer(1.0e100) == std::numeric_limits<std::int32_t>::min(),
              "Primitive rule integers use the source strict list conversion");
        std::cout << "PASS: primitive parameter query routing and strict list conversions\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
