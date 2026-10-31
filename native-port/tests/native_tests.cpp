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
#include <array>
#include <cmath>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

using namespace opensim::framework;
using namespace opensim::network;
namespace {
int failures = 0;
void check(bool condition, const char* description) {
    if (!condition) { std::cerr << "FAIL: " << description << '\n'; ++failures; }
}
template<class F> bool throws(F&& action) {
    try { action(); } catch (const std::exception&) { return true; }
    return false;
}
void crc_tests() {
    const std::array<std::uint8_t, 9> digits{'1','2','3','4','5','6','7','8','9'};
    check(Crc32::compute(digits) == 0xcbf43926u, "CRC32 published check vector");
    check(Crc32::compute({}) == 0, "CRC32 empty input");
    check(Crc32::compute(0x82f63b78u, Crc32::DefaultSeed, digits) == 0xe3069283u,
        "CRC32C custom polynomial check vector");
    Crc32 incremental;
    incremental.update(std::span(digits).first(4));
    incremental.update(std::span(digits).subspan(4));
    check(incremental.value() == Crc32::compute(digits), "CRC32 segmented hashing");
    check(incremental.hash_final() == std::array<std::uint8_t,4>{0xcb,0xf4,0x39,0x26},
        "CRC32 big endian digest");
    incremental.initialize();
    check(incremental.value() == 0, "CRC32 reset");
    incremental.hash_core(digits, 2, 4);
    check(incremental.value() == Crc32::compute(std::span(digits).subspan(2,2)),
        "CRC32 source nonzero-offset behavior retained");
    check(throws([&] { incremental.hash_core(digits, 0, 10); }), "CRC32 rejects out of bounds access");
}
void permission_tests() {
    auto target = 0xffffffffu;
    PermissionsUtil::apply_folded_permissions(1, target);
    check((target & bits(PermissionMask::UnfoldedMask)) == bits(PermissionMask::Transfer), "folded transfer restriction");
    check((target & ~bits(PermissionMask::UnfoldedMask)) == (0xffffffffu & ~bits(PermissionMask::UnfoldedMask)),
        "folded restriction preserves unrelated bits");
    target = 0xffffffffu;
    PermissionsUtil::apply_no_mod_folded_permissions(1, target);
    check((target & bits(PermissionMask::Modify)) != 0, "no-mod folding preserves modify");
    for (const auto folded : {0u, 15u}) {
        target = 0xdeadbeefu;
        PermissionsUtil::apply_folded_permissions(folded, target);
        check(target == 0xdeadbeefu, "invalid or unrestricted folded bits ignored");
    }
    for (std::uint32_t mask = 0; mask < 32; ++mask) {
        const auto permissions = mask << 13;
        const auto fixed = PermissionsUtil::fix_and_fold_permissions(permissions);
        check((fixed & bits(PermissionMask::Move)) != 0, "fixed permissions allow move");
        check((fixed & (bits(PermissionMask::Copy) | bits(PermissionMask::Transfer))) != 0,
            "fixed permissions obey copy-transfer rule");
        check(PermissionsUtil::fix_and_fold_permissions(fixed) == fixed, "permission normalization idempotent");
    }
    check(PermissionsUtil::permissions_to_string(0) == ".", "empty permissions display");
    check(PermissionsUtil::permissions_to_string(0xffffffffu) == "MCTX", "permission display ordering");
    check(PermissionsUtil::log_permissions("object", "transfer", 0, 0x8e000, 0x2000) ==
        "Permissions of \"object\" at \"transfer\": Base . (0000), Current MCT (8E000), NextOwner T (2000)",
        "permission log compatibility");
}
void prime_tests() {
    check(PrimeNumberHelper::get_prime(7919) == 8419, "existing GetPrime regression");
    check(PrimeNumberHelper::get_prime(std::numeric_limits<std::int32_t>::max() - 1) == -1, "prime upper boundary");
    check(PrimeNumberHelper::get_prime(3) == 3, "source GetPrime(3) retained");
    check(PrimeNumberHelper::is_prime(1), "source IsPrime(1) retained");
    check(PrimeNumberHelper::is_prime(9), "source sqrt boundary retained");
    check(PrimeNumberHelper::is_prime(15), "source truncated sqrt bound retained");
    check(!PrimeNumberHelper::is_prime(27), "divisible odd candidate rejected");
    check(PrimeNumberHelper::get_prime(-100) == 2, "negative minimum");
}
void bucket_tests() {
    double now = 100;
    auto clock = [&now] { return now; };
    TokenBucket root(nullptr, 6000, 10000, clock);
    TokenBucket a(&root, 3000, 3000, clock), b(&root, 5000, 5000, clock);
    check(root.total_drip_request() == 8000, "child request aggregation");
    check(a.drip_rate() == 2250 && b.drip_rate() == 3750, "hierarchical proportional allocation");
    check(root.requested_burst() == 7500, "burst hard limit");
    check(!a.remove_tokens(100), "new bucket skips initial drip");
    now += 1;
    check(a.remove_tokens(3000), "positive token balance permits packet overshoot");
    check(a.token_count() == -750, "packet overshoot creates debt");
    check(!a.check_tokens(0), "debt prevents another packet");
    now += .25;
    check(!a.remove_tokens(1), "partial refill cannot spend debt");
    now += .25;
    check(a.remove_tokens(1), "refill recovers debt");
    root.unregister_request(&b);
    check(root.total_drip_request() == 3000 && a.drip_rate() == 3000, "unregister recomputes hierarchy");
    a.requested_drip_rate(-1);
    check(a.requested_drip_rate() == 0 && a.drip_rate() == 500, "child minimum rate retained");
    check(a.get_cat_bytes_can_send(20) == 10, "category rate conversion");
    a.requested_burst(-1);
    check(a.requested_burst() == 0, "negative burst clamp");
    check(throws([&] { root.parent(&a); }), "native cycle guard");

    AdaptiveTokenBucket adaptive(nullptr, 200000, 7500, true, clock);
    check(adaptive.adjusted_drip_rate() == 100000, "adaptive initial rate");
    adaptive.acknowledge_packets(1000);
    check(adaptive.adjusted_drip_rate() == 101000, "acknowledgement increases by count");
    adaptive.expire_packets(1);
    check(adaptive.adjusted_drip_rate() == 50500, "expiration halves rate");
    adaptive.expire_packets(100);
    check(adaptive.adjusted_drip_rate() == 50000, "adaptive floor");
    adaptive.adjusted_drip_rate(300000);
    check(adaptive.adjusted_drip_rate() == 200000, "adaptive ceiling");
    AdaptiveTokenBucket disabled(nullptr, 200000, 7500, false, clock);
    disabled.adaptive_enabled = true;
    disabled.expire_packets(1);
    check(disabled.adjusted_drip_rate() == 200000, "source constructor flag controls adaptation");

    auto ancestor = std::make_unique<TokenBucket>(nullptr, 10000.f, 7500.f, clock);
    auto child = std::make_unique<TokenBucket>(ancestor.get(), 2000.f, 7500.f, clock);
    ancestor->unregister_request(child.get());
    ancestor.reset();
    check(child->parent() == nullptr && child->drip_rate() == 2000,
        "native lifetime guard handles parent destroyed after explicit unregister");
    child.reset();
    auto old_parent = std::make_unique<TokenBucket>(nullptr, 10000.f, 7500.f, clock);
    child = std::make_unique<TokenBucket>(old_parent.get(), 2000.f, 7500.f, clock);
    child->parent(&root);
    child.reset();
    old_parent.reset();
}
void circular_buffer_tests() {
    CircularBuffer<int> buffer(3);
    buffer.put(1); buffer.put(2); buffer.put(3);
    check(throws([&] { buffer.put(4); }), "bounded buffer overflow");
    check(buffer.get() == 1, "buffer head read");
    buffer.put(4);
    check(buffer.to_array() == std::vector<int>{2,3,4}, "buffer wrap order");
    std::vector<int> enumerated;
    for (auto value : buffer) enumerated.push_back(value);
    check(enumerated == buffer.to_array(), "buffer enumeration order");
    check(buffer.remove(999) && buffer.get() == 3, "collection removal ignores supplied item");
    check(buffer.get(3) == std::vector<int>{4,0,0}, "bulk read pads unfilled result with default values");
    check(throws([&] { buffer.get(); }), "empty buffer read");
    check(throws([&] { buffer.capacity(-1); }), "negative capacity");
    CircularBuffer<int> overflow(2, true);
    overflow.put(1); overflow.put(2); overflow.put(3);
    check(overflow.size() == 3 && overflow.to_array() == std::vector<int>{3,2,3},
        "single-item overflow retains source size and repeated enumeration");
    overflow.clear();
    const std::array<int,3> source{1,2,3};
    overflow.put(source);
    check(overflow.size() == 2 && overflow.to_array() == std::vector<int>{3,2},
        "bulk overflow retains distinct source behavior");
    overflow.skip(1);
    check(overflow.size() == 2 && overflow.get() == 2, "Skip changes position without consuming size");
    CircularBuffer<int> copied(3);
    copied.put(source);
    std::array<int,2> destination{};
    copied.copy_to(2, destination, 0, 2);
    check(destination == std::array<int,2>{1,2}, "source CopyTo ignores index parameter");
    copied.get();
    copied.capacity(5);
    check(copied.to_array() == std::vector<int>{3,0}, "source resize retains head index");
    CircularBuffer<int> zero(0);
    check(throws([&] { zero.put(1); }), "zero capacity bounded write");
    check(!zero.is_synchronized() && !zero.is_read_only(), "collection metadata");
    CircularBuffer<bool> booleans(3);
    const std::array<bool,3> boolean_values{true,false,true};
    booleans.put(boolean_values);
    check(booleans.to_array() == std::vector<bool>{true,false,true}, "boolean buffer instantiation");
    check(booleans.get(4) == std::vector<bool>{true,false,true,false}, "boolean bulk read defaults");
    auto old_array = copied.get_buffer();
    copied.capacity(6);
    old_array.at(0) = 99;
    check(old_array.at(0) == 99 && copied.get_buffer().at(0) != 99,
        "buffer array aliases survive resize with separate backing storage");
    CircularBuffer<std::shared_ptr<int>> references(2);
    references.put(std::make_shared<int>(7));
    references.put(std::shared_ptr<int>{});
    check(references.contains({}) && references.to_array()[1] == nullptr,
        "reference-type buffer null equality");
    CircularBuffer<double> floating(2);
    floating.put(std::numeric_limits<double>::quiet_NaN());
    check(floating.contains(std::numeric_limits<double>::quiet_NaN()), "managed floating-point NaN equality");
}
void throttle_tests() {
    ThrottleRates defaults;
    check(defaults.client_max_rate == 640000 && defaults.minimum_adaptive_throttle_rate == 0, "absent throttle section");
    ThrottleRates present([](std::string_view, std::int32_t fallback) { return fallback; });
    check(present.minimum_adaptive_throttle_rate == 32000, "present throttle section defaults");
    for (const auto value : {-1, 0, 6250, 640000, 1000001}) {
        ThrottleRates rates([value](std::string_view key, std::int32_t fallback) {
            return key == "client_throttle_max_bps" ? value : fallback;
        });
        check(rates.client_max_rate == std::clamp(value, 6250, 1000000), "client bandwidth bounds");
    }
    ThrottleRates malformed([](std::string_view, std::int32_t) -> std::int32_t { throw std::invalid_argument("bad integer"); });
    check(malformed.client_max_rate == 640000 && malformed.minimum_adaptive_throttle_rate == 0, "configuration exception preserves partial state");
    check(defaults.get_rate(ThrottleOutPacketType::Resend) == 6625, "resend default");
    check(defaults.get_rate(ThrottleOutPacketType::Unknown) == 0, "unknown category rate");
}
void dos_tests() {
    auto options = std::make_shared<BasicDosProtectorOptions>();
    options->max_requests_in_timeframe = 1;
    options->request_time_span_ms = 1000;
    options->forget_time_span_ms = 100;
    std::int32_t tick = 100;
    BasicDOSProtector protector(options, [&tick] { return tick; }, false);
    check(protector.process("A", "/test"), "request protection first request");
    check(!protector.process("A", "/test") && protector.is_blocked("A"), "per-client request block");
    check(protector.process("B", "/test"), "blocked client does not block another client");
    options->throttled_action = ThrottleAction::DoThrow;
    bool throttled = false;
    try { protector.process("A", "/test"); } catch (const ThrottledError& error) {
        throttled = std::string(error.what()) == "Throttled";
    }
    check(throttled, "throw action preserves throttled exception message");
    tick = 200;
    protector.expire_blocks();
    check(protector.is_blocked("A"), "source cleanup retains block at exact deadline");
    tick = 201;
    protector.expire_blocks();
    check(!protector.is_blocked("A"), "cleanup releases block after deadline");
    options->max_requests_in_timeframe = 0;
    check(protector.process("A", "/test"), "live options can disable request protection");
    check(environment_tick_count_subtract(2, EnvironmentTickCountMask - 2) == 5, "tick wrap subtraction");

    auto timed_options = std::make_shared<BasicDosProtectorOptions>();
    timed_options->max_requests_in_timeframe = 100;
    timed_options->max_concurrent_sessions = 1;
    timed_options->request_time_span_ms = 1000;
    timed_options->forget_time_span_ms = 10;
    std::promise<void> cleaned;
    auto cleanup = cleaned.get_future();
    BasicDOSProtector timed(timed_options, [] { return 100; }, true,
        [&cleaned](const std::string& level, const std::string&) {
            if (level == "info") cleaned.set_value();
        });
    timed.process("A", "/test");
    timed.process("A", "/test");
    timed.process("A", "/test");
    check(cleanup.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
        "native cleanup timer executes without host pumping");
    check(!timed.is_blocked("A"), "native timer runs the source cleanup algorithm");
}
}
int main() {
    crc_tests(); permission_tests(); prime_tests(); bucket_tests(); throttle_tests(); circular_buffer_tests(); dos_tests();
    if (failures) { std::cerr << failures << " checks failed\n"; return 1; }
    std::cout << "Native framework and throttle checks passed\n";
}
