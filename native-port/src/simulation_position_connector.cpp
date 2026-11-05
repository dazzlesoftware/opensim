// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/simulation_connector.hpp"
#include "opensim/network/osd_request.hpp"
#include "opensim/network/osd_service_requester.hpp"
#include "opensim/framework/text.hpp"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
namespace opensim::services {
using network::Value;
struct SimulationConnector::PositionState {
    std::mutex queue_mutex;
    std::unordered_map<std::string, std::shared_ptr<framework::AgentPosition>> queue;
    mutable std::recursive_timed_mutex cache_mutex;
    framework::OrderedDictionary<std::string, double> failures;
    std::function<double()> clock;
    std::mutex purge_mutex;
    std::mutex timer_mutex;
    std::condition_variable timer_changed;
    bool stop = false;
    std::thread timer;
    explicit PositionState(SimulationPositionOptions options)
        : clock(options.clock ? std::move(options.clock) : std::function<double()>([] {
              return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch())
                  .count();
          })) {
        if (options.automatic_purge)
            timer = std::thread([this] {
                std::unique_lock lock(timer_mutex);
                while (!timer_changed.wait_for(lock, std::chrono::seconds(1), [&] { return stop; })) {
                    lock.unlock();
                    try {
                        purge();
                    } catch (const std::exception&) {
                    }
                    lock.lock();
                }
            });
    }
    ~PositionState() {
        {
            std::lock_guard lock(timer_mutex);
            stop = true;
        }
        timer_changed.notify_all();
        if (timer.joinable())
            timer.join();
    }
    std::unique_lock<std::recursive_timed_mutex> cache_lock() const {
        std::unique_lock lock(cache_mutex, std::defer_lock);
        if (!lock.try_lock_for(std::chrono::seconds(5)))
            throw std::runtime_error("Cache lock could not be acquired after 5000ms");
        return lock;
    }
    bool blocked(const std::string& uri) {
        auto lock = cache_lock();
        return failures.contains(uri);
    }
    void failed(const std::string& uri) {
        auto lock = cache_lock();
        failures[uri] = clock() + 120;
    }
    void purge() {
        std::unique_lock purging(purge_mutex, std::try_to_lock);
        if (!purging.owns_lock())
            return;
        const auto now = clock();
        std::unique_lock lock(cache_mutex, std::defer_lock);
        if (!lock.try_lock_for(std::chrono::seconds(5)))
            return;
        std::vector<std::string> expired;
        for (const auto& [key, expiry] : failures) {
            if (expiry < now)
                expired.push_back(key);
            else
                break;
        }
        for (const auto& key : expired)
            failures.erase(key);
    }
    std::size_t count() const {
        auto lock = cache_lock();
        return failures.size();
    }
};
SimulationConnector::SimulationConnector(Request request, DeleteRequest delete_request,
                                         PositionRequest position_request, SimulationPositionOptions options,
                                         PositionRequest object_request)
    : request_(request ? std::move(request) : Request(network::osd_request)),
      delete_request_(delete_request ? std::move(delete_request)
                                     : DeleteRequest([](const std::optional<std::string>& uri, int timeout) {
                                           if (!uri)
                                               throw std::invalid_argument("Null URI");
                                           return network::osd_delete_request(*uri, timeout);
                                       })),
      position_request_(
          position_request
              ? std::move(position_request)
              : PositionRequest([](const std::string& uri, const Value& data, int timeout, bool compressed) {
                    return network::osd_timed_request(uri, data, "PUT", timeout, compressed, false);
                })),
      object_request_(
          object_request
              ? std::move(object_request)
              : PositionRequest([](const std::string& uri, const Value& data, int timeout, bool compressed) {
                    return network::osd_timed_request(uri, data, "POST", timeout, compressed, false);
                })),
      position_state_(std::make_unique<PositionState>(std::move(options))) {}
SimulationConnector::SimulationConnector(std::shared_ptr<network::OsdServiceRequester> requester,
                                         SimulationPositionOptions options)
    : SimulationConnector(
          [requester](const auto& uri, const Value& data, const auto& method, bool compressed,
                      bool keepalive) {
              return requester->send(framework::utf16_replacing(uri), &data,
                                     framework::utf16_replacing(method), 30000, compressed, false, keepalive);
          },
          [requester](const std::optional<std::string>& uri, int timeout) {
              network::OsdServiceServices::Nullable address;
              if (uri)
                  address = framework::utf16_replacing(*uri);
              return requester->send(address, nullptr, u"DELETE", timeout, false, false);
          },
          [requester](const auto& uri, const Value& data, int timeout, bool compressed) {
              return requester->send(framework::utf16_replacing(uri), &data, u"PUT", timeout, compressed,
                                     false);
          },
          std::move(options),
          [requester](const auto& uri, const Value& data, int timeout, bool compressed) {
              return requester->send(framework::utf16_replacing(uri), &data, u"POST", timeout, compressed,
                                     false);
          }) {
    if (!requester)
        throw std::invalid_argument("Simulation connector requires an OSD requester");
}
SimulationConnector::~SimulationConnector() = default;
void SimulationConnector::purge_failed_simulators() {
    position_state_->purge();
}
std::size_t SimulationConnector::failed_simulator_count() const {
    return position_state_->count();
}
bool SimulationConnector::update_agent(GridRegion& destination,
                                       const std::shared_ptr<framework::AgentPosition>& position) {
    return update_agent(&destination, position);
}
bool SimulationConnector::update_agent(GridRegion* destination,
                                       const std::shared_ptr<framework::AgentPosition>& position) {
    if (!destination)
        throw std::invalid_argument("Null destination");
    auto& state = *position_state_;
    if (state.blocked(destination->server_uri()))
        return false;
    const auto server_uri = destination->server_uri();
    const auto path = agent_path();
    if (!position)
        throw std::invalid_argument("Null agent position");
    const auto uri = server_uri + path + position->agent_id.str() + "/";
    {
        std::lock_guard lock(state.queue_mutex);
        if (state.queue.contains(uri)) {
            state.queue[uri] = position;
            return true;
        }
        state.queue[uri] = position;
    }
    std::shared_ptr<framework::AgentPosition> current;
    while (true) {
        {
            std::lock_guard lock(state.queue_mutex);
            auto previous = current;
            current = state.queue.at(uri);
            if (current == previous) {
                state.queue.erase(uri);
                return true;
            }
        }
        LoginTransferContext context;
        if (!send_agent_position(destination, current, &context, 10000))
            break;
    }
    {
        std::lock_guard lock(state.queue_mutex);
        state.failed(destination->server_uri());
        state.queue.erase(uri);
    }
    return false;
}
bool SimulationConnector::update_agent(GridRegion& destination,
                                       const std::shared_ptr<framework::AgentData>& data,
                                       LoginTransferContext& context) {
    return update_agent(&destination, data, &context);
}
bool SimulationConnector::update_agent(GridRegion* destination,
                                       const std::shared_ptr<framework::AgentData>& data,
                                       LoginTransferContext* context) {
    return send_agent_data(destination, data, context, 200000);
}
bool SimulationConnector::send_agent_position(GridRegion* destination,
                                              const std::shared_ptr<framework::AgentPosition>& position,
                                              LoginTransferContext* context, int timeout) {
    return send_agent_data(destination, position, context, timeout);
}
bool SimulationConnector::send_agent_data(GridRegion* destination,
                                          const std::shared_ptr<framework::IAgentData>& position,
                                          LoginTransferContext* context, int timeout) {
    if (!destination)
        throw std::invalid_argument("Null agent update destination");
    const auto server_uri = destination->server_uri();
    const auto path = agent_path();
    if (!position)
        throw std::invalid_argument("Null agent update data");
    const auto uri = server_uri + path + position->agent_uuid().str() + "/";
    try {
        auto args = position->pack(context);
        if (args.view().kind != Value::Kind::Map)
            throw std::invalid_argument("Null/non-map agent pack result");
        args.set("destination_x", std::to_string(destination->x));
        args.set("destination_y", std::to_string(destination->y));
        args.set("destination_name", destination->name.value_or(""));
        args.set("destination_uuid", destination->id.str());
        if (!context)
            throw std::invalid_argument("Null update context");
        args.set("context", context->pack());
        auto success = [](const Value& result) {
            if (result.view().kind != Value::Kind::Map)
                throw std::invalid_argument("Null/non-map update response");
            return network::osd_boolean(result.get("Success"));
        };
        if (context->outbound_version >= 0.3f)
            return success(position_request_(uri, args, timeout, true));
        auto result = position_request_(uri, args, timeout, true);
        if (success(result))
            return true;
        if (context->outbound_version < 0.2f)
            result = position_request_(uri, args, timeout, false);
        return success(result);
    } catch (const std::exception&) {
        return false;
    }
}
}
