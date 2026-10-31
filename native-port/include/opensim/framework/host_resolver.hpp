// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/ip_address.hpp"
#include <functional>
#include <vector>
namespace opensim::framework {
class HostResolver {
public:
    using Address=std::shared_ptr<IpAddress>;
    using Lookup=std::function<std::optional<std::vector<Address>>(const std::string&)>;
    using Clock=std::function<double()>;
    explicit HostResolver(Lookup lookup={},Clock clock={},bool automatic_purge=true);
    ~HostResolver();
    static std::optional<std::vector<Address>> system_lookup(const std::string& host);
    Address host_from_dns(const std::optional<std::string>& host);
    Address host_from_url(const std::optional<std::string>& url);
    std::shared_ptr<IpEndpoint> endpoint(const std::optional<std::string>& host,std::int32_t port);
    static std::shared_ptr<IpEndpoint> endpoint(const Address& address,std::int32_t port);
    void purge();
    void clear();
    std::size_t count()const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
HostResolver& host_resolver();
}
