#pragma once

#include "opensim/framework/uuid.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace opensim::scene {
class RemoteDataChannels {
  public:
    struct Receiver {
        std::uint32_t local_id;
        framework::Uuid item;
        framework::Uuid region;
    };

    framework::Uuid open(std::uint32_t local_id, framework::Uuid item,
                         framework::Uuid requested = {}, framework::Uuid region = {});
    void close(framework::Uuid channel);
    std::optional<Receiver> find(framework::Uuid channel) const;
    struct Data {
        std::int32_t integer;
        std::string text;
    };
    struct Request {
        Receiver receiver;
        framework::Uuid channel;
        framework::Uuid message;
        Data data;
    };
    enum class Status { Replied, InvalidChannel, TimedOut };
    struct Result {
        Status status;
        std::optional<Data> data;
    };
    Result request(framework::Uuid channel, Data data,
                   std::chrono::milliseconds timeout = std::chrono::seconds(9));
    std::vector<Request> take_requests(framework::Uuid region = {});
    bool reply(framework::Uuid channel, framework::Uuid message, Data data);
    using Sender = std::function<void(std::string_view channel, std::string_view destination,
                                      Data& data, std::stop_token stop)>;
    explicit RemoteDataChannels(Sender sender = {});
    ~RemoteDataChannels();
    framework::Uuid send(framework::Uuid item, std::string channel, std::string destination, Data data,
                         framework::Uuid region = {});
    struct Completion {
        framework::Uuid item;
        framework::Uuid region;
        framework::Uuid message;
        std::string channel;
        Data data;
    };
    std::vector<Completion> take_completed(framework::Uuid region = {});
    void cancel_script(framework::Uuid item, framework::Uuid region = {});
    void cancel_region(framework::Uuid region);

  private:
    void cancel(framework::Uuid region, std::optional<framework::Uuid> item);
    struct OutboundWorker {
        framework::Uuid item;
        framework::Uuid region;
        bool finished = false;
        std::jthread thread;
    };
    struct Pending {
        framework::Uuid channel;
        Receiver receiver;
        std::optional<Data> reply;
        bool cancelled = false;
    };
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::map<framework::Uuid, Receiver> channels_;
    std::map<framework::Uuid, std::shared_ptr<Pending>> pending_;
    std::deque<Request> requests_;
    Sender sender_;
    std::vector<Completion> completed_;
    std::list<OutboundWorker> workers_;
};
}
