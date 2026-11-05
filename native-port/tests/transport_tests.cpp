/*
Copyright (c) Contributors, http://opensimulator.org/
See CONTRIBUTORS.TXT for a full list of copyright holders.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenSimulator Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "opensim/network/circuit_transport.hpp"
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
using namespace opensim::network;
using namespace opensim::framework;
namespace {
int failures = 0;
void check(bool value, const char* name) {
    if (!value) {
        ++failures;
        std::cerr << "FAIL: " << name << '\n';
    }
}
struct Fixture {
    AgentCircuitManager manager;
    std::shared_ptr<AgentCircuitData> data = std::make_shared<AgentCircuitData>();
    Fixture() {
        data->agent_id = Uuid::random();
        data->session_id = Uuid::random();
        data->circuit_code = 42;
        manager.add_new_circuit(42, data);
    }
    lludp::Packet use() {
        auto p = lludp::make_packet("UseCircuitCode");
        p.sequence = 123;
        p.block("CircuitCode")["Code"] = std::uint64_t{42};
        p.block("CircuitCode")["ID"] = data->agent_id;
        p.block("CircuitCode")["SessionID"] = data->session_id;
        return p;
    }
};
void transport_tests() {
    Fixture f;
    int now = 1000, delivered = 0, removed = 0;
    bool allow = false;
    std::vector<lludp::Packet> sent;
    UdpEndpoint endpoint{"127.0.0.1", 10000};
    CircuitTransport transport(
        f.manager,
        [&](const auto&, auto bytes) {
            sent.push_back(lludp::decode_datagram(bytes));
            return true;
        },
        [&](const auto&, const auto&, const auto&) { return allow; },
        [&](const auto&, const auto&) { ++delivered; }, [&](const auto&) { ++removed; }, {},
        [&] { return now; });
    auto receive = [&](const auto& packet) { transport.receive(endpoint, lludp::encode_datagram(packet)); };
    auto use = f.use();
    use.block("CircuitCode")["SessionID"] = Uuid::random();
    receive(use);
    check(sent.size() == 1 && sent.back().sequence == 0 && sent.back().spec->name == "PacketAck" &&
              !transport.connection(f.data->agent_id),
          "unauthorized circuit gets receipt ACK only");
    receive(f.use());
    check(!transport.connection(f.data->agent_id), "scene must admit authenticated circuit");
    allow = true;
    receive(f.use());
    check(transport.connection(f.data->agent_id).has_value(), "authenticated scene admission");
    auto message = lludp::make_packet("AgentUpdate");
    message.sequence = 9;
    message.reliable = true;
    message.block("AgentData")["AgentID"] = f.data->agent_id;
    message.block("AgentData")["SessionID"] = Uuid::random();
    receive(message);
    check(delivered == 0 && transport.statistics().rejected_ids == 1, "packet session validation");
    for (const auto* name : {"ObjectBuy", "MoneyBalanceRequest"}) {
        auto request = lludp::make_packet(name);
        auto& identity = request.block("AgentData");
        identity["AgentID"] = f.data->agent_id;
        identity["SessionID"] = Uuid{};
        receive(request);

        identity["AgentID"] = Uuid{};
        identity["SessionID"] = f.data->session_id;
        receive(request);
    }
    check(delivered == 0 && transport.statistics().rejected_ids == 5,
          "economy requests reject mismatched agents and sessions before dispatch");
    message.block("AgentData")["SessionID"] = f.data->session_id;
    receive(message);
    receive(message);
    check(delivered == 1 && transport.statistics().duplicates == 1, "reliable duplicate delivered once");
    now += 100;
    transport.poll();
    check(sent.back().spec->name == "PacketAck" && sent.back().blocks.at("Packets").size() == 2,
          "duplicates still acknowledged");
    auto outgoing = lludp::make_packet("CompletePingCheck");
    outgoing.reliable = true;
    auto sequence = transport.send(f.data->agent_id, outgoing);
    const auto before = sent.size();
    now += 1000;
    transport.poll();
    check(transport.statistics().unacked_bytes > 0,
          "reliable datagram contributes to pending acknowledgement bytes");
    check(sent.size() == before, "initial resend bucket waits for its first refill");
    now += 100;
    transport.poll();
    check(sent.size() == before + 1 && sent.back().sequence == sequence && sent.back().resent,
          "expired reliable packet resends with original sequence");
    auto ack = lludp::make_packet("PacketAck");
    ack.reliable = false;
    auto row = lludp::make_block(ack.spec->blocks.front());
    row["ID"] = static_cast<std::uint64_t>(*sequence);
    ack.blocks["Packets"].push_back(row);
    receive(ack);
    const auto after = sent.size();
    now += 1000;
    transport.poll();
    check(sent.size() == after, "ACK prevents further resends");
    check(transport.statistics().unacked_bytes == 0, "acknowledged datagram leaves the pending byte total");
    auto ping = lludp::make_packet("StartPingCheck");
    ping.reliable = false;
    ping.block("PingID")["PingID"] = std::uint64_t{17};
    receive(ping);
    check(sent.back().spec->name == "CompletePingCheck" &&
              std::get<std::uint64_t>(sent.back().block("PingID").at("PingID")) == 17,
          "ping reply preserves identifier");
    transport.set_paused(f.data->agent_id, true);
    now += 60001;
    transport.poll();
    check(transport.connection(f.data->agent_id).has_value(), "paused circuit extends timeout");
    transport.set_paused(f.data->agent_id, false);
    transport.poll();
    check(!transport.connection(f.data->agent_id) && removed == 1, "idle disconnect removes indexes");
    receive(f.use());
    check(!transport.remove_client_if_session(f.data->agent_id, Uuid::random()) && removed == 1,
          "stale session cannot disconnect current circuit");
    check(transport.remove_client_if_session(f.data->agent_id, f.data->session_id) && removed == 2,
          "matching session disconnects current circuit");
    transport.close();
    receive(f.use());
    check(!transport.connection(f.data->agent_id), "closed transport rejects admission");
}
void socket_test(const std::string& address) {
    Fixture f;
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<lludp::Packet> replies;
    CircuitTransport* transport_ptr = nullptr;
    UdpSocket* server_ptr = nullptr;
    UdpSocket server({address, 0},
                     [&](const auto& source, auto bytes) { transport_ptr->receive(source, bytes); });
    server_ptr = &server;
    CircuitTransport transport(
        f.manager, [&](const auto& target, auto bytes) { return server_ptr->send_to(target, bytes); },
        [](const auto&, const auto&, const auto&) { return true; });
    transport_ptr = &transport;
    const auto bound = server.start();
    UdpSocket client({address, 0}, [&](const auto&, auto bytes) {
        std::lock_guard lock(mutex);
        replies.push_back(lludp::decode_datagram(bytes));
        changed.notify_all();
    });
    client.start();
    client.send_to(bound, lludp::encode_datagram(f.use()));
    {
        std::unique_lock lock(mutex);
        check(changed.wait_for(lock, std::chrono::seconds(2), [&] { return !replies.empty(); }),
              "socket circuit receipt ACK");
    }
    auto ping = lludp::make_packet("StartPingCheck");
    ping.reliable = false;
    client.send_to(bound, lludp::encode_datagram(ping));
    {
        std::unique_lock lock(mutex);
        check(changed.wait_for(lock, std::chrono::seconds(2), [&] { return replies.size() >= 2; }),
              "socket admitted circuit ping");
        check(replies.size() >= 2 && replies.back().spec->name == "CompletePingCheck",
              "socket ping packet decoding");
    }
    client.stop();
    server.stop();
    transport.close();
}
void reentrant_teardown_test() {
    Fixture f;
    CircuitTransport* owner = nullptr;
    bool remove_on_send = false;
    int now = 1000;
    CircuitTransport transport(
        f.manager,
        [&](const auto&, auto) {
            if (remove_on_send)
                owner->remove_client(f.data->agent_id);
            return true;
        },
        [](const auto&, const auto&, const auto&) { return true; }, {}, {}, {}, [&] { return now; });
    owner = &transport;
    transport.receive({"127.0.0.1", 10001}, lludp::encode_datagram(f.use()));
    remove_on_send = true;
    auto packet = lludp::make_packet("CompletePingCheck");
    packet.reliable = true;
    transport.send(f.data->agent_id, packet);
    check(!transport.connection(f.data->agent_id), "sender callback can remove its own circuit safely");
    remove_on_send = false;
    transport.receive({"127.0.0.1", 10001}, lludp::encode_datagram(f.use()));
    transport.send(f.data->agent_id, packet, [&](const auto&) { transport.remove_client(f.data->agent_id); });
    now += 1000;
    transport.poll();
    check(!transport.connection(f.data->agent_id), "expiry callback can remove its own circuit safely");
}
void pending_admission_test() {
    Fixture f;
    int now = 1000, admissions = 0, delivered = 0;
    std::vector<std::function<void()>> jobs;
    std::vector<lludp::Packet> sent;
    CircuitTransport transport(
        f.manager,
        [&](const auto&, auto bytes) {
            sent.push_back(lludp::decode_datagram(bytes));
            return true;
        },
        [&](const auto&, const auto&, const auto&) {
            ++admissions;
            return true;
        },
        [&](const auto&, const auto&) { ++delivered; }, {}, {}, [&] { return now; },
        [&](auto job) { jobs.push_back(std::move(job)); });
    const UdpEndpoint endpoint{"127.0.0.1", 10002};
    auto receive = [&](const auto& packet) { transport.receive(endpoint, lludp::encode_datagram(packet)); };
    receive(f.use());
    receive(f.use());
    check(jobs.size() == 1 && sent.size() == 2 && admissions == 0,
          "pending circuit retransmits ACK without duplicate admission");
    auto message = lludp::make_packet("AgentUpdate");
    message.block("AgentData")["AgentID"] = f.data->agent_id;
    message.block("AgentData")["SessionID"] = f.data->session_id;
    receive(message);
    check(delivered == 0, "pending scene queues incoming datagrams");
    jobs.front()();
    check(admissions == 1 && delivered == 1 && transport.connection(f.data->agent_id).has_value(),
          "scene completion reinjects queued packet");
    transport.remove_client(f.data->agent_id);
    jobs.clear();
    receive(f.use());
    receive(message);
    now += 60001;
    transport.poll();
    jobs.front()();
    check(delivered == 1, "expired pending queue is discarded");
    transport.remove_client(f.data->agent_id);
    jobs.clear();
    receive(f.use());
    transport.close();
    jobs.front()();
    check(!transport.connection(f.data->agent_id), "late admission job cannot reopen closed transport");
    std::function<void()> orphan;
    {
        CircuitTransport temporary(
            f.manager, [](const auto&, auto) { return true; },
            [](const auto&, const auto&, const auto&) { return true; }, {}, {}, {}, {},
            [&](auto job) { orphan = std::move(job); });
        temporary.receive(endpoint, lludp::encode_datagram(f.use()));
    }
    orphan();
    check(true, "released owner leaves executor job safe to invoke");
}
void concurrent_admission_test() {
    Fixture f;
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, release = false;
    std::thread worker;
    std::atomic<int> delivered = 0;
    CircuitTransport transport(
        f.manager, [](const auto&, auto) { return true; },
        [&](const auto&, const auto&, const auto&) {
            std::unique_lock lock(mutex);
            entered = true;
            changed.notify_all();
            changed.wait(lock, [&] { return release; });
            return true;
        },
        [&](const auto&, const auto&) { ++delivered; }, {}, {}, {},
        [&](auto job) { worker = std::thread(std::move(job)); });
    const UdpEndpoint endpoint{"127.0.0.1", 10003};
    transport.receive(endpoint, lludp::encode_datagram(f.use()));
    {
        std::unique_lock lock(mutex);
        check(changed.wait_for(lock, std::chrono::seconds(2), [&] { return entered; }),
              "scene executor begins admission");
    }
    auto message = lludp::make_packet("AgentUpdate");
    message.block("AgentData")["AgentID"] = f.data->agent_id;
    message.block("AgentData")["SessionID"] = f.data->session_id;
    transport.receive(endpoint, lludp::encode_datagram(message));
    check(delivered == 0, "receiver continues while scene admission blocks");
    {
        std::lock_guard lock(mutex);
        release = true;
    }
    changed.notify_all();
    worker.join();
    check(delivered == 1, "worker completion delivers pending datagrams");
}
void queued_fragment_test() {
    Fixture f;
    int now = 1000;
    std::vector<lludp::Packet> sent;
    CircuitTransport transport(
        f.manager,
        [&](const auto&, auto bytes) {
            sent.push_back(lludp::decode_datagram(bytes));
            check(bytes.size() <= 1200, "split outgoing datagram fits MTU");
            return true;
        },
        [](const auto&, const auto&, const auto&) { return true; }, {}, {}, {}, [&] { return now; });
    transport.receive({"127.0.0.1", 10004}, lludp::encode_datagram(f.use()));
    sent.clear();
    auto packet = lludp::make_packet("ObjectUpdateCompressed");
    packet.reliable = false;
    const auto& block = packet.spec->blocks.back();
    for (int i = 0; i < 4; ++i) {
        auto row = lludp::make_block(block);
        row["Data"] = std::vector<std::uint8_t>(700, static_cast<std::uint8_t>(i + 1));
        packet.blocks.at(block.name).push_back(std::move(row));
    }
    check(transport.enqueue(f.data->agent_id, packet, ThrottleOutPacketType::Task),
          "categorized packet accepted into throttle queue");
    transport.poll();
    check(sent.empty(), "queued fragments wait for bandwidth");
    for (int i = 0; i < 4; ++i) {
        now += 100;
        transport.poll();
    }
    check(sent.size() == 4, "all fragments pass through category queue");
    for (std::size_t i = 0; i < sent.size(); ++i) {
        check(sent[i].sequence == i + 1, "fragments receive independent consecutive sequences");
        const auto& rows = sent[i].blocks.at(block.name);
        check(rows.size() == 1 && std::get<std::vector<std::uint8_t>>(rows.front().at("Data")) ==
                                      std::vector<std::uint8_t>(700, static_cast<std::uint8_t>(i + 1)),
              "fragment preserves original object data and order");
    }
    auto packed = transport.packed_throttles(f.data->agent_id);
    check(packed && packed->size() == 28, "circuit exposes viewer throttle settings");
    transport.set_throttles(f.data->agent_id, *packed);
}
void update_scheduler_test() {
    Fixture f;
    int now = 1000, updates = 0;
    bool available = true;
    std::vector<std::function<void()>> jobs;
    CircuitTransport transport(
        f.manager, [](const auto&, auto) { return true; },
        [](const auto&, const auto&, const auto&) { return true; }, {}, {}, {}, [&] { return now; },
        [&](auto job) { jobs.push_back(std::move(job)); });
    transport.receive({"127.0.0.1", 10005}, lludp::encode_datagram(f.use()));
    jobs.front()();
    jobs.clear();
    transport.set_update_callbacks(
        f.data->agent_id,
        [&](const auto&, auto flags) {
            check(static_cast<int>(flags) == 63, "empty queue supplies all category flags");
            return available;
        },
        [&](const auto&, auto) { ++updates; });
    auto run = [&] {
        auto job = std::move(jobs.front());
        jobs.erase(jobs.begin());
        job();
    };
    transport.poll();
    transport.poll();
    check(jobs.size() == 1 && updates == 0, "update producer scheduled once while running");
    run();
    now += 19;
    transport.poll();
    check(jobs.empty(), "update producer obeys twenty millisecond minimum");
    ++now;
    transport.poll();
    check(jobs.size() == 1, "update producer resumes at twenty milliseconds");
    run();
    available = false;
    now += 20;
    transport.poll();
    check(jobs.empty(), "no update callback when host has no updates");
    available = true;
    transport.set_update_callbacks(
        f.data->agent_id, [](const auto&, auto) { return true; },
        [](const auto&, auto) { throw std::runtime_error("producer failure"); });
    transport.poll();
    run();
    check(transport.statistics().callback_errors == 1, "producer exception is contained");
    now += 20;
    transport.poll();
    check(jobs.size() == 1, "producer exception releases running gate");
    transport.remove_client(f.data->agent_id);
    run();
    check(transport.statistics().callback_errors == 1 && updates == 2,
          "late producer job ignores disconnected circuit");
}
void texture_refill_test() {
    Fixture fixture;
    int now = 1000;
    CircuitTransport transport(
        fixture.manager, [](const auto&, auto) { return true; },
        [](const auto&, const auto&, const auto&) { return true; }, {}, {}, {}, [&] { return now; });
    transport.receive({"127.0.0.1", 10010}, lludp::encode_datagram(fixture.use()));
    const auto agent = fixture.data->agent_id;
    const auto session = fixture.data->session_id;
    transport.poll();
    check(!transport.take_texture_refill(agent, Uuid{}), "stale session cannot claim texture refill");
    check(transport.take_texture_refill(agent, session), "empty texture category permits refill");
    transport.poll();
    check(!transport.take_texture_refill(agent, session), "texture refill is paced");

    auto packet = lludp::make_packet("ImagePacket");
    packet.reliable = false;
    for (int i = 0; i < 8; ++i)
        transport.enqueue(agent, packet, ThrottleOutPacketType::Texture);
    now += 20;
    transport.poll();
    check(!transport.take_texture_refill(agent, session), "texture backlog blocks refill");
    for (int i = 0; i < 8; ++i) {
        now += 100;
        transport.poll();
    }
    check(transport.take_texture_refill(agent, session), "texture refill resumes after backlog drains");
}
void outbound_zero_encoding_test() {
    Fixture fixture;
    const UdpEndpoint endpoint{"127.0.0.1", 10009};
    std::vector<std::uint8_t> wire;
    CircuitTransport transport(
        fixture.manager,
        [&](const auto&, auto bytes) {
            wire.assign(bytes.begin(), bytes.end());
            return true;
        },
        [](const auto&, const auto&, const auto&) { return true; }, {}, {});
    transport.receive(endpoint, lludp::encode_datagram(fixture.use()));

    auto packet = lludp::make_packet("PacketAck");
    packet.zerocoded = true;
    auto row = lludp::make_block(packet.spec->blocks.front());
    row["ID"] = std::uint64_t{0x01010001};
    packet.blocks["Packets"].assign(10, row);
    const auto raw_size = lludp::encode_packet(packet).size();
    transport.send(fixture.data->agent_id, packet);
    const auto uncompressed = lludp::decode_datagram(wire);
    check(wire.size() == raw_size && !uncompressed.zerocoded && uncompressed.blocks == packet.blocks,
          "outbound zero encoding is disabled when it expands the packet");

    row["ID"] = std::uint64_t{0};
    packet.blocks["Packets"].assign(10, row);
    transport.send(fixture.data->agent_id, packet);
    const auto compressed = lludp::decode_datagram(wire);
    check(wire.size() < raw_size && compressed.zerocoded && compressed.blocks == packet.blocks,
          "outbound zero encoding is retained when it shrinks the packet");
}
void ack_threshold_test() {
    Fixture fixture;
    const UdpEndpoint endpoint{"127.0.0.1", 10008};
    std::vector<lludp::Packet> sent;
    CircuitTransport transport(
        fixture.manager,
        [&](const auto&, auto bytes) {
            sent.push_back(lludp::decode_datagram(bytes));
            return true;
        },
        [](const auto&, const auto&, const auto&) { return true; }, {}, {}, {}, [] { return 1000; });
    transport.receive(endpoint, lludp::encode_datagram(fixture.use()));
    sent.clear();

    auto packet = lludp::make_packet("AgentUpdate");
    packet.reliable = true;
    packet.zerocoded = false;
    auto& identity = packet.block("AgentData");
    identity["AgentID"] = fixture.data->agent_id;
    identity["SessionID"] = fixture.data->session_id;

    const auto packet_size = lludp::encode_datagram(packet).size();
    const auto count_before_flush = 2800 / packet_size;
    for (std::size_t i = 0; i < count_before_flush; ++i) {
        ++packet.sequence;
        transport.receive(endpoint, lludp::encode_datagram(packet));
    }
    check(sent.empty(), "immediate ACK waits until reliable traffic exceeds two legacy MTUs");

    ++packet.sequence;
    transport.receive(endpoint, lludp::encode_datagram(packet));
    check(sent.size() == 1 && sent.front().spec->name == "PacketAck" &&
              sent.front().blocks.at("Packets").size() == count_before_flush + 1,
          "crossing two legacy MTUs flushes pending ACKs");
}
void region_bandwidth_test() {
    Fixture f;
    int now = 1000;
    CircuitTransportOptions options;
    options.scene_throttle_max_bps = 64000;
    CircuitTransport transport(
        f.manager, [](const auto&, auto) { return true; },
        [](const auto&, const auto&, const auto&) { return true; }, {}, {}, options, [&] { return now; });
    transport.receive({"127.0.0.1", 10006}, lludp::encode_datagram(f.use()));
    const auto single = *transport.bytes_can_send(f.data->agent_id, ThrottleOutPacketType::Task, 1000);
    auto other = std::make_shared<AgentCircuitData>();
    other->agent_id = Uuid::random();
    other->session_id = Uuid::random();
    other->circuit_code = 43;
    f.manager.add_new_circuit(43, other);
    auto use = f.use();
    use.block("CircuitCode")["Code"] = std::uint64_t{43};
    use.block("CircuitCode")["ID"] = other->agent_id;
    use.block("CircuitCode")["SessionID"] = other->session_id;
    transport.receive({"127.0.0.1", 10007}, lludp::encode_datagram(use));
    const auto shared = *transport.bytes_can_send(f.data->agent_id, ThrottleOutPacketType::Task, 1000);
    check(single > 0 && shared > 0 && shared < single,
          "region bandwidth parent shares capacity across agents");
    transport.remove_client(other->agent_id);
    check(*transport.bytes_can_send(f.data->agent_id, ThrottleOutPacketType::Task, 1000) == single,
          "disconnect releases region bandwidth request");
}
}
int main() {
    try {
        transport_tests();
        reentrant_teardown_test();
        pending_admission_test();
        concurrent_admission_test();
        queued_fragment_test();
        update_scheduler_test();
        texture_refill_test();
        outbound_zero_encoding_test();
        ack_threshold_test();
        region_bandwidth_test();
        socket_test("127.0.0.1");
        socket_test("::1");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return failures ? 1 : 0;
}
