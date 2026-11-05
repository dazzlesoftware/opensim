// Explicit, provenance-checked bridge between the typed URI and download APIs.
#pragma once
#include "opensim/framework/config_uri_object.hpp"
#include "opensim/framework/config_xml_transport.hpp"
namespace opensim::framework {
class ConfigXmlUriObjects {
    struct State;
    std::shared_ptr<State> state_;

  public:
    ConfigXmlUriObjects();
    std::shared_ptr<ConfigXmlUri> wrap(ConfigUriObjectPtr) const;
    // Only descriptors produced by this adapter (or a copy of it) are accepted.
    // No reinterpretation of arbitrary opaque native_uri pointers is performed.
    ConfigUriObjectPtr get(const std::shared_ptr<ConfigXmlUri>&) const;
};
}
