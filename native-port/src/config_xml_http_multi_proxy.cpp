#include "opensim/framework/config_xml_http_multi_proxy.hpp"
#include "opensim/framework/config_http_proxy_resolution.hpp"
#include "opensim/framework/config_http_connect_error.hpp"
#include "opensim/framework/config_http_headers.hpp"
#include "opensim/framework/text.hpp"
namespace opensim::framework {
namespace {
class Plan final : public ConfigXmlHttpProxyRoutePlan {
    ConfigHttpMultiProxy proxy_;
    ConfigHttpMultiProxyCandidate candidate_;
    bool multiple_;

public:
    explicit Plan(ConfigHttpMultiProxy proxy)
        : proxy_(std::move(proxy)), candidate_(proxy_.read_next()),
          multiple_(candidate_.found && !candidate_.is_final) {}
    std::optional<ConfigXmlHttpRoute> current() override {
        if (!candidate_.found)
            return {};
        if (*candidate_.uri->scheme() != u"http")
            throw ConfigHttpProxyProviderError("MultiProxy HTTP route requires an HTTP proxy URI");
        auto endpoint = config_http_proxy_endpoint(candidate_.uri);
        return ConfigXmlHttpRoute{
            utf8(*endpoint.host), static_cast<std::uint16_t>(endpoint.port), {}, candidate_.uri};
    }
    bool advance() override {
        if (!multiple_)
            return false;
        candidate_ = proxy_.read_next();
        return candidate_.found;
    }
};
}
std::shared_ptr<ConfigXmlHttpProxyRoutePlan> config_xml_http_multi_proxy_plan(ConfigHttpMultiProxy proxy) {
    return std::make_shared<Plan>(std::move(proxy));
}
bool config_xml_http_proxy_retryable(std::exception_ptr error) {
    if (!error)
        return false;
    try {
        std::rethrow_exception(error);
    } catch (const ConfigHttpConnectionError& e) {
        return e.retry_on_next_proxy;
    } catch (const ConfigHttpHeaderError& e) {
        return e.source_type == "System.Net.Http.HttpRequestException" && e.retry_on_connection_failure;
    } catch (...) {
        return false;
    }
}
}
