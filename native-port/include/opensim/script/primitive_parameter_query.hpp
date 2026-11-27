// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/script/lsl_runtime.hpp"

namespace opensim::script {
enum class PrimitiveQueryApi { Primitive, Link, OtherObject, OsslLink };
enum class PrimitiveQuerySelection { Parts, PartsAndAvatars, Entities };

struct PrimitiveParameterQueryBackend {
    // OtherObject performs the source target-group/root-owner/Modify gate.
    std::function<bool(framework::Uuid)> other_object_permitted;
    // PartsAndAvatars must return parts first, followed by sitting avatars.
    std::function<std::vector<framework::Uuid>(framework::Uuid, std::int32_t, PrimitiveQuerySelection)> select;
    // Append results in place and return the unconsumed PRIM_LINK_TARGET tail.
    std::function<LslList(framework::Uuid, const LslList &, LslList &)> read;
    std::function<void(const std::string &)> error;
};

// API UUID parsing and OSSL permissions are applied by the caller before this
// routing core. The backend supplies live selection, ownership and value reads.
LslList query_primitive_parameters(const PrimitiveParameterQueryBackend &backend, PrimitiveQueryApi api,
                                   framework::Uuid target, std::int32_t link, const LslList &rules);

// GetIntegerItem conversion for the native value representation and invariant
// culture; differs from llList2Integer on strings and nonnumeric list entries.
std::int32_t primitive_rule_integer(const LslValue &value);
} // namespace opensim::script
