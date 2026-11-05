#include "opensim/framework/config_xml_uri_object.hpp"
#include "opensim/framework/config_uri_relative.hpp"
#include <stdexcept>
#include "opensim/framework/config_uri_components.hpp"
using namespace opensim::framework;
int main() {
    ConfigXmlUriObjects first, copy = first, other;
    auto text = std::make_shared<const std::u16string>(u"relative");
    auto object = config_uri_relative_object(text, text);
    std::weak_ptr<const ConfigUriObject> lifetime = object;
    auto descriptor = first.wrap(object);
    if (first.get(descriptor) != object || copy.get(descriptor) != object)
        return 1;
    bool rejected = false;
    try {
        other.get(descriptor);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    if (!rejected)
        return 2;
    rejected = false;
    try {
        first.get(std::make_shared<ConfigXmlUri>());
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    if (!rejected)
        return 3;
    rejected = false;
    try {
        first.get({});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    if (!rejected)
        return 4;
    object.reset();
    if (lifetime.expired())
        return 5;
    bool relative_failure = false;
    try {
        descriptor->scheme();
    } catch (const ConfigUriComponentsError&) {
        relative_failure = true;
    }
    if (!relative_failure)
        return 6;
    descriptor.reset();
    if (!lifetime.expired())
        return 7;
    rejected = false;
    try {
        first.wrap({});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    if (!rejected)
        return 8;
    return 0;
}
