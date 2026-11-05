// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/lludp.hpp"
#include "opensim/framework/ordered_dictionary.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
namespace opensim::scene {
using XferBytes = std::shared_ptr<const std::vector<std::uint8_t>>;
using XferClock = std::function<std::int32_t()>;
using XferSink =
    std::function<void(std::uint64_t, std::uint32_t, const std::vector<std::uint8_t>&, int, int)>;
network::lludp::Packet xfer_packet(std::uint64_t id, std::uint32_t packet, std::span<const std::uint8_t> data,
                                   int offset, int length);
class XferDownload {
public:
    XferDownload(std::string filename, XferBytes, std::uint64_t id, int burst, XferClock,
                 std::function<int()> ping, XferSink);
    void start();
    void done();
    bool ack(std::uint32_t);
    bool check_time(std::int32_t now);
    bool deleted() const;

private:
    void burst();
    void send(int packet);
    std::string filename_;
    XferBytes data_;
    std::uint64_t id_;
    int burst_size_;
    XferClock clock_;
    std::function<int()> ping_;
    XferSink sink_;
    mutable std::recursive_mutex mutex_;
    bool deleted_ = false;
    std::int32_t last_ack_time_ = 0;
    int last_packet_ = 0, last_bytes_ = 0, last_sent_ = 0, last_ack_ = 0, retries_ = 0;
};
// Source file registry and transfer ownership. Clock, ping and packet delivery are
// supplied by the region; this component never writes temporary files to disk.
class XferFiles {
public:
    explicit XferFiles(XferClock);
    bool add(std::string filename, XferBytes);
    void request(std::string_view filename, std::uint64_t id, int task_throttle, int ping_ms,
                 std::function<int()> current_ping, XferSink);
    void ack(std::uint64_t, std::uint32_t);
    void abort(std::uint64_t);
    void transfers_tick(std::int32_t);
    void expire(std::int32_t);
    void heartbeat();
    std::size_t files() const;
    std::size_t transfers() const;

private:
    struct File {
        XferBytes data;
        std::int32_t refs = 1, timestamp = 0;
    };
    XferClock clock_;
    mutable std::recursive_mutex mutex_;
    framework::OrderedDictionary<std::string, File> files_;
    framework::OrderedDictionary<std::uint64_t, std::shared_ptr<XferDownload>> transfers_;
    std::int32_t last_tick_, last_expire_;
};
}
