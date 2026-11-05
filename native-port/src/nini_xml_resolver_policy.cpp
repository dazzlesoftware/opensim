// Source System.LocalAppContextSwitches.GetCachedSwitchValueInternal,8.0.31.
// Copyright .NET Foundation and Contributors; MIT .NET license.
#include "opensim/framework/nini_xml_resolver_policy.hpp"
#include <stdexcept>
namespace opensim::framework {
NiniXmlResolverPolicy::NiniXmlResolverPolicy(std::function<std::optional<bool>(std::u16string_view)> getter)
    : switch_(std::move(getter)) {
    if (!switch_)
        throw std::invalid_argument("XML resolver policy requires AppContext.TryGetSwitch");
}
bool NiniXmlResolverPolicy::networking_enabled_by_default() {
    int cached = networking_.load(std::memory_order_relaxed);
    if (cached < 0)
        return false;
    if (cached > 0)
        return true;
    auto getter = switch_;
    bool enabled = getter(u"System.Xml.XmlResolver.IsNetworkingEnabledByDefault").value_or(true);
    if (!getter(u"TestSwitch.LocalAppContext.DisableCaching").value_or(false))
        networking_.store(enabled ? 1 : -1, std::memory_order_relaxed);
    return enabled;
}
}
