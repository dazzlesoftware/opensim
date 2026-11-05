#include "opensim/framework/config_xml_uri_object.hpp"
#include <mutex>
#include <unordered_map>
#include <stdexcept>
namespace opensim::framework {
struct ConfigXmlUriObjects::State {
    struct Entry {
        std::weak_ptr<ConfigXmlUri> descriptor;
        std::weak_ptr<const ConfigUriObject> object;
    };
    std::mutex mutex;
    std::unordered_map<const ConfigXmlUri*, Entry> entries;
};
ConfigXmlUriObjects::ConfigXmlUriObjects() : state_(std::make_shared<State>()) {}
std::shared_ptr<ConfigXmlUri> ConfigXmlUriObjects::wrap(ConfigUriObjectPtr object) const {
    if (!object)
        throw std::invalid_argument("XML URI bridge requires a non-null typed URI object");
    auto descriptor = std::make_shared<ConfigXmlUri>();
    descriptor->native_uri = object;
    descriptor->scheme = [object]() { return object->scheme(); };
    descriptor->local_path = [object]() { return object->local_path(); };
    std::lock_guard lock(state_->mutex);
    for (auto it = state_->entries.begin(); it != state_->entries.end();)
        if (it->second.descriptor.expired())
            it = state_->entries.erase(it);
        else
            ++it;
    state_->entries[descriptor.get()] = {descriptor, object};
    return descriptor;
}
ConfigUriObjectPtr ConfigXmlUriObjects::get(const std::shared_ptr<ConfigXmlUri>& descriptor) const {
    std::lock_guard lock(state_->mutex);
    auto found = state_->entries.find(descriptor.get());
    if (found == state_->entries.end() || found->second.descriptor.lock() != descriptor)
        throw std::invalid_argument("XML URI descriptor belongs to a different typed URI bridge");
    auto object = found->second.object.lock();
    if (!object)
        throw std::logic_error("XML URI bridge lost its retained URI object");
    return object;
}
}
