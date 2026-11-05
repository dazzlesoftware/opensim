// Source .NET8.0.31 System.Xml.LocalAppContextSwitches; MIT .NET license.
#pragma once
#include <atomic>
#include <functional>
#include <optional>
#include <memory>
#include <string_view>
namespace opensim::framework {
class NiniXmlResolverPolicy final {
    std::function<std::optional<bool>(std::u16string_view)> switch_;
    std::atomic<int> networking_{0};

public:
    explicit NiniXmlResolverPolicy(std::function<std::optional<bool>(std::u16string_view)>);
    bool networking_enabled_by_default();
    int cache_snapshot() const noexcept {
        return networking_.load(std::memory_order_relaxed);
    }
};
}
