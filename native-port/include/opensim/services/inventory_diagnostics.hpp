// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/sdk_llsd_xml.hpp"
#include <functional>
#include <stdexcept>
namespace opensim::services {
enum class InventoryLogLevel { Debug, Warn, Error };
using InventoryDiagnostic = std::function<void(InventoryLogLevel, const std::string &)>;
inline std::string inventory_cast_message(network::Value::Kind kind) {
    const char *type = "OSD";
    using K = network::Value::Kind;
    switch (kind) {
    case K::String:
        type = "OSDString";
        break;
    case K::Boolean:
        type = "OSDBoolean";
        break;
    case K::Integer:
        type = "OSDInteger";
        break;
    case K::Real:
        type = "OSDReal";
        break;
    case K::Uuid:
        type = "OSDUUID";
        break;
    case K::Date:
        type = "OSDDate";
        break;
    case K::Uri:
        type = "OSDUri";
        break;
    case K::Binary:
        type = "OSDBinary";
        break;
    case K::Array:
        type = "OSDArray";
        break;
    default:
        break;
    }
    return std::string("Unable to cast object of type 'OpenMetaverse.StructuredData.") + type +
           "' to type 'OpenMetaverse.StructuredData.OSDMap'.";
}
inline std::string inventory_exception_message() {
    try {
        throw;
    } catch (const std::exception &e) {
        return e.what();
    } catch (...) {
        return "Unknown native exception";
    }
}
}
