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
// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/crc32.hpp"
#include "opensim/framework/circular_buffer.hpp"
#include "opensim/framework/basic_dos_protector.hpp"
#include "opensim/framework/tick_count.hpp"
#include "opensim/framework/permissions.hpp"
#include "opensim/framework/prime_number_helper.hpp"
#include "opensim/network/throttle_rates.hpp"
#include "opensim/network/token_bucket.hpp"
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace opensim::framework;
using namespace opensim::network;
int main() {
    std::cout.imbue(std::locale::classic());
    std::cout << std::setprecision(std::numeric_limits<double>::max_digits10);
    double now = 0;
    std::int32_t tick_now = 0;
    std::map<int, std::unique_ptr<TokenBucket>> buckets;
    std::map<int, std::unique_ptr<CircularBuffer<int>>> buffers;
    std::map<int, std::unique_ptr<BasicDOSProtector>> protectors;
    const auto state = [](const TokenBucket& b, int result) {
        std::cout << "bucket|" << result << '|' << b.requested_drip_rate() << '|'
            << b.total_drip_request() << '|' << b.drip_rate() << '|' << b.max_drip_rate()
            << '|' << b.requested_burst() << '|' << b.burst() << '|' << b.token_count() << '\n';
    };
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream input(line);
        input.imbue(std::locale::classic());
        std::string op;
        input >> op;
        if (op == "ticktime") {
            input >> tick_now; std::cout << "ticktime\n";
        } else if (op == "ticks") {
            std::int32_t tick, a, b; input >> tick >> a >> b;
            std::cout << "ticks|" << environment_tick_count_subtract(a,b) << '|'
                << environment_tick_count_compare(a,b,tick) << '\n';
        } else if (op.starts_with("dos_")) {
            int id; input >> id;
            std::string result = "-";
            try {
                if (op == "dos_new") {
                    auto options = std::make_shared<BasicDosProtectorOptions>();
                    int action;
                    input >> options->max_requests_in_timeframe >> options->request_time_span_ms
                        >> options->forget_time_span_ms >> options->max_concurrent_sessions >> action;
                    options->throttled_action = static_cast<ThrottleAction>(action);
                    protectors[id] = std::make_unique<BasicDOSProtector>(options,
                        [&tick_now] { return tick_now & EnvironmentTickCountMask; }, false);
                } else {
                    auto& protector = *protectors.at(id);
                    std::string key; input >> key;
                    if (op == "dos_process") result = protector.process(key, "/test") ? "1" : "0";
                    else if (op == "dos_end") protector.process_end(key, "/test");
                    else if (op == "dos_cleanup" && protector.cleanup_enabled()) protector.expire_blocks();
                }
            } catch (const ThrottledError&) { result = "throttled"; }
            catch (const std::exception&) { result = "error"; }
            std::cout << "dos|" << result;
            if (protectors.contains(id)) {
                const auto& p = *protectors.at(id);
                std::cout << '|' << p.cleanup_enabled() << '|' << p.is_blocked("A") << '|'
                    << p.is_blocked("B") << '|' << p.is_blocked("C");
            }
            std::cout << '\n';
        } else if (op.starts_with("buffer_")) {
            int id; input >> id;
            std::string result = "-";
            try {
                if (op == "buffer_new") {
                    int capacity, overflow; input >> capacity >> overflow;
                    buffers[id] = std::make_unique<CircularBuffer<int>>(capacity, overflow != 0);
                } else {
                    auto& b = *buffers.at(id);
                    int value;
                    if (op == "buffer_single") { input >> value; b.put(value); }
                    else if (op == "buffer_resize") { input >> value; b.capacity(value); }
                    else if (op == "buffer_skip") { input >> value; b.skip(value); }
                    else if (op == "buffer_clear") b.clear();
                    else if (op == "buffer_overflow") { input >> value; b.allow_overflow = value != 0; }
                    else if (op == "buffer_get") result = std::to_string(b.get());
                    else if (op == "buffer_remove") { input >> value; result = b.remove(value) ? "1" : "0"; }
                    else if (op == "buffer_contains") { input >> value; result = b.contains(value) ? "1" : "0"; }
                    else if (op == "buffer_raw") { int index; input >> index >> value; b.get_buffer().at(index) = value; }
                    else if (op == "buffer_bulk") {
                        int offset, count; input >> offset >> count;
                        std::vector<int> values;
                        while (input >> value) values.push_back(value);
                        result = std::to_string(b.put(values, offset, count));
                    } else if (op == "buffer_take" || op == "buffer_read" || op == "buffer_copy") {
                        std::vector<int> values;
                        if (op == "buffer_take") { input >> value; values = b.get(value); }
                        else if (op == "buffer_read") {
                            int offset, count, length; input >> offset >> count >> length;
                            values.assign(length, -99);
                            const auto read = b.get(values, offset, count);
                            result = std::to_string(read) + ":";
                        } else {
                            int index, offset, count, length; input >> index >> offset >> count >> length;
                            values.assign(length, -99);
                            b.copy_to(index, values, offset, count);
                        }
                        if (result == "-") result.clear();
                        for (int item : values) result += std::to_string(item) + ",";
                    }
                }
            } catch (const std::exception&) { result = "error"; }
            std::cout << "buffer|" << result;
            if (buffers.contains(id)) {
                const auto& b = *buffers.at(id);
                std::cout << '|' << b.capacity() << '|' << b.size() << '|';
                for (auto value : b.get_buffer()) std::cout << value << ',';
                std::cout << '|';
                try { for (auto value : b) std::cout << value << ','; }
                catch (const std::exception&) { std::cout << "error"; }
            }
            std::cout << '\n';
        } else if (op == "prime") {
            std::int32_t n; input >> n;
            std::cout << "prime|" << PrimeNumberHelper::is_prime(n) << '|' << PrimeNumberHelper::get_prime(n) << '\n';
        } else if (op == "perm") {
            std::uint32_t source, target; input >> source >> target;
            auto folded = target, nomod = target;
            PermissionsUtil::apply_folded_permissions(source, folded);
            PermissionsUtil::apply_no_mod_folded_permissions(source, nomod);
            std::cout << "perm|" << folded << '|' << nomod << '|'
                << PermissionsUtil::fix_and_fold_permissions(target) << '|'
                << PermissionsUtil::permissions_to_string(target) << '\n';
        } else if (op == "log") {
            std::uint32_t a,b,c; input >> a >> b >> c;
            std::cout << "log|" << PermissionsUtil::log_permissions("object", "transfer", a,b,c) << '\n';
        } else if (op == "crc") {
            std::uint32_t poly, seed; std::string hex; std::int32_t start, length;
            input >> poly >> seed >> hex >> start >> length;
            std::vector<std::uint8_t> bytes;
            if (hex != "-")
                for (std::size_t i = 0; i < hex.size(); i += 2)
                    bytes.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i,2), nullptr, 16)));
            Crc32 crc(poly, seed);
            crc.hash_core(bytes, start, length);
            std::cout << "crc|" << Crc32::compute(poly, seed, bytes) << '|' << crc.value();
            for (auto byte : crc.hash_final()) std::cout << '|' << static_cast<int>(byte);
            crc.initialize();
            const auto mid = bytes.size() / 2;
            crc.update(std::span(bytes).first(mid));
            crc.update(std::span(bytes).subspan(mid));
            std::cout << '|' << crc.value() << '\n';
        } else if (op == "throttle") {
            int mode, client, minimum; input >> mode >> client >> minimum;
            ThrottleRates::ReadInt read;
            if (mode) read = [=](std::string_view key, std::int32_t fallback) {
                if (key == "client_throttle_max_bps") {
                    if (mode == 2) throw std::invalid_argument("bad integer");
                    return mode == 4 ? fallback : client;
                }
                if (mode == 3) throw std::invalid_argument("bad integer");
                return mode == 4 ? fallback : minimum;
            };
            ThrottleRates rates(read);
            std::cout << "throttle|" << rates.client_max_rate << '|' << rates.minimum_adaptive_throttle_rate
                << '|' << rates.adaptive_throttles_enabled << '|' << rates.total << '|' << rates.burst_time;
            for (int i = -2; i <= 8; ++i)
                std::cout << '|' << rates.get_rate(static_cast<ThrottleOutPacketType>(i));
            std::cout << '\n';
        } else if (op == "time") {
            input >> now; std::cout << "time\n";
        } else if (op == "new") {
            int id, parent, adaptive; float rate, burst;
            input >> id >> parent >> rate >> burst >> adaptive;
            auto* p = parent < 0 ? nullptr : buckets.at(parent).get();
            if (adaptive < 0) buckets[id] = std::make_unique<TokenBucket>(p, rate, burst, [&now] { return now; });
            else buckets[id] = std::make_unique<AdaptiveTokenBucket>(p, rate, burst, adaptive != 0, [&now] { return now; });
            state(*buckets.at(id), -1);
        } else {
            int id; input >> id;
            auto& bucket = *buckets.at(id);
            int result = -1;
            if (op == "remove" || op == "check" || op == "bytes") {
                std::int32_t value; input >> value;
                if (op == "remove") result = bucket.remove_tokens(value);
                else if (op == "check") result = bucket.check_tokens(value);
                else result = bucket.get_cat_bytes_can_send(value);
            } else if (op == "unregister") {
                int child; input >> child;
                bucket.unregister_request(buckets.at(child).get());
            } else if (op != "state") {
                float value; input >> value;
                if (op == "rate") bucket.requested_drip_rate(value);
                else if (op == "max") bucket.max_drip_rate(value);
                else if (op == "burst") bucket.requested_burst(value);
                else if (op == "total") bucket.total_drip_request(value);
                else {
                    auto& adaptive = dynamic_cast<AdaptiveTokenBucket&>(bucket);
                    if (op == "adjust") adaptive.adjusted_drip_rate(value);
                    else if (op == "ack") adaptive.acknowledge_packets(static_cast<std::int32_t>(value));
                    else if (op == "expire") adaptive.expire_packets(static_cast<std::int32_t>(value));
                    else if (op == "enabled") adaptive.adaptive_enabled = value != 0;
                    else throw std::invalid_argument("Unknown operation: " + op);
                }
            }
            state(bucket, result);
        }
    }
    // Destroy children before parents; avoid map's unspecified element destruction order.
    for (auto it = buckets.rbegin(); it != buckets.rend(); ++it) it->second.reset();
}
