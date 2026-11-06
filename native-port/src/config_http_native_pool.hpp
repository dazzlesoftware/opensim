// Handler-owned direct HTTP/1.1 transport pool. No process-global sockets.
#pragma once
#include "opensim/framework/config_xml_transport_http.hpp"
#include "opensim/framework/config_http_headers.hpp"
#include "config_http_source_receive_buffer.hpp"
#include <httplib.h>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
namespace opensim::framework {
class ConfigHttpNativePool {
    using Clock = std::chrono::steady_clock;
    struct State;
    struct Bucket;

  public:
    class Lease {
        friend class ConfigHttpNativePool;
        std::shared_ptr<State> state_;
        std::shared_ptr<Bucket> bucket_;
        httplib::ClientImpl::StreamHandle idle_;
        std::shared_ptr<ConfigHttpSourceReceiveBuffer> receive_;
        Clock::time_point created_;
        ConfigHttpKeepAliveState keep_alive_;
        bool returned_ = true;
        Lease(std::shared_ptr<State>, std::shared_ptr<Bucket>);

      public:
        ~Lease();
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        void adopt(httplib::Client&);
        void connected(const httplib::ClientImpl::StreamHandle&);
        std::shared_ptr<ConfigHttpSourceReceiveBuffer> source_receive_buffer();
        std::optional<std::u16string> tunnel_user_agent() const;
        bool response_headers(const ConfigHttpResponseHead&);
        void finish(httplib::ClientImpl::StreamHandle&&, bool reusable) noexcept;
    };
    explicit ConfigHttpNativePool(ConfigXmlHttpPoolSettings);
    ~ConfigHttpNativePool();
    std::shared_ptr<Lease> acquire(const std::string& host, std::uint16_t port, Clock::time_point deadline,
                                   std::function<bool()> interrupted = {},
                                   std::optional<std::u16string> tunnel_user_agent = {});

  private:
    std::shared_ptr<State> state_;
    std::jthread scavenger_;
};
}
