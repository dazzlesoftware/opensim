#include "opensim/script/lsl_runtime.hpp"
#include "opensim/scene/remote_data_channels.hpp"
#include "opensim/framework/config_http_headers.hpp"
#include "opensim/script/asset_text.hpp"
#include "opensim/script/http_request_throttle.hpp"
#include "opensim/scene/terrain.hpp"
#include "opensim/scene/script_mailbox.hpp"
#include "opensim/network/script_smtp.hpp"
#include "opensim/network/script_http_transport.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/config_http_environment_proxy.hpp"
#include "opensim/framework/config_http_environment_proxy_native.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/primitive_base_shape.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <thread>
#include <future>
using namespace opensim;
using namespace script;
static void check(bool b, const char* text) {
    if (!b)
        throw std::runtime_error(text);
}
static std::string hex(std::string_view text) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (unsigned char c : text) {
        out += digits[c >> 4];
        out += digits[c & 15];
    }
    return out;
}
static std::string unhex(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); i += 2)
        out += char(std::stoi(std::string(text.substr(i, 2)), nullptr, 16));
    return out;
}
static std::string quote(std::string_view text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out + '"';
}
static LslContext context() {
    LslContext c;
    c.object = framework::Uuid("00112233-4455-6677-8899-aabbccddeeff");
    c.owner = framework::Uuid("11223344-5566-7788-99aa-bbccddeeff00");
    c.object_name = "Object";
    return c;
}
static std::string api_probe(int mode, int count, int channel, const std::string& text) {
    std::int64_t clock = 0;
    std::string output;
    auto c = context();
    if (mode & 1)
        c.group = c.owner;
    LslHost host;
    host.clock_ticks = [&] {
        return clock;
    };
    if (mode & 2)
        host.console_channel = channel;
    host.chat = [&](const LslChat& x) {
        output += (x.type == LslChatType::Owner ? "O:" : "S:") + std::to_string(x.channel) + ":" +
                  hex(x.text) + ";";
    };
    host.listener = [&](const LslChat& x) {
        output += "L:" + std::to_string(x.channel) + ":" + hex(x.text) + ";";
    };
    host.console = [&](const std::string& text) {
        output += "C:" + hex(text) + ";";
    };
    host.request_sleep = [&](int delay) {
        output += "D:" + std::to_string(delay) + ";";
    };
    if (mode & 4)
        host.script_delay_factor = .001f;
    std::string source = "default{state_entry(){";
    for (int i = 0; i < count; i++) {
        source +=
            "llSay(" + std::to_string(channel) + "," + quote(text) + ");llOwnerSay(" + quote(text) + ");";
    }
    source += "}}";
    auto compiled = compile_lsl(source);
    check(bool(compiled), "probe compile");
    LslInstance instance(compiled.program, c, host);
    for (int i = 0; i < 1000; i++) {
        auto r = instance.run(1);
        check(r.status != LslRunStatus::Faulted, "probe fault");
        if (r.status == LslRunStatus::Idle)
            break;
        if (r.status == LslRunStatus::Sleeping)
            clock += 20000000;
        else if (mode & 8)
            clock += 5000000;
    }
    return output;
}
int main(int argc, char** argv) {
    try {
        {
            auto service = std::make_shared<network::ScriptHttpService>(
                std::make_shared<framework::OutboundUrlFilter>(""), 16384, 2000, 1);
            for (const auto* name : {"Ho\xc2\xad" "st", "Pro\xc2\xad" "xy-Example", "Se\xc2\xad" "c-Example"}) {
                network::ScriptHttpModule module(service, {0.f, 1.f, 0.f, 1.f});
                const auto result = module.submit({}, 17, {}, {}, "http://127.0.0.1/",
                                                  {{5, "5"}, {{}, name}, {{}, "fixture"}}, {}, {});
                check(result.key.empty() && result.errors.size() == 1 &&
                          result.errors.front().find("Name is invalid as a custom header") != std::string::npos,
                      "script HTTP forbidden header names and prefixes use invariant collation");
            }
            check(service->active_workers() == 0, "invalid custom header must be rejected before enqueue");
        }
        if (argc == 1) {
            const auto program = compile_lsl(R"LSL(
                default {
                    state_entry() {
                        llOpenRemoteDataChannel();
                        key request = llSendRemoteData("", "", 0, "");
                        llRemoteDataReply("", "", "", 0);
                        llCloseRemoteDataChannel("");
                        llOwnerSay("result:" + (string)request);
                    }
                }
            )LSL");
            check(bool(program), "remote data APIs compile");
            std::int64_t clock = 0;
            std::vector<std::string> messages;
            LslHost host;
            host.clock_ticks = [&] { return clock; };
            host.chat = [&](const LslChat& chat) { messages.push_back(chat.text); };
            host.script_delay_factor = .5f;
            LslInstance script(program.program, context(), host);
            for (const int milliseconds : {500, 1500, 1500, 500}) {
                check(script.run(100).status == LslRunStatus::Sleeping && messages.empty(),
                      "remote data without XML-RPC observes the managed delay");
                clock += std::int64_t(milliseconds) * 10000;
            }
            check(script.run(100).status == LslRunStatus::Idle && messages == std::vector<std::string>{"result:"},
                  "remote data without XML-RPC returns an empty key");
        }
        if (argc == 3 && std::string_view(argv[1]) == "--shared-http-probe") {
            auto service = std::make_shared<network::ScriptHttpService>(
                std::make_shared<framework::OutboundUrlFilter>(""), 16384, 2000, 1);
            check(service->active_workers() == 0, "script HTTP should start without worker threads");
            const HttpRequestThrottleSettings throttle{0.f, 1.f, 0.f, 1.f};
            network::ScriptHttpModule first(service, throttle), second(service, throttle), removed(service, throttle);
            const auto part = framework::Uuid::random();
            const auto item = framework::Uuid::random();
            const auto owner = framework::Uuid::random();
            constexpr std::uint32_t local_id = 17;
            const auto submit = [&](network::ScriptHttpModule& module, framework::Uuid script, std::string_view path) {
                return module.submit(part, local_id, owner, script, argv[2] + std::string(path), {}, {}, {}).key;
            };
            const auto first_key = submit(first, item, "/shared-first");
            const auto removed_key = submit(removed, item, "/shared-cancelled");
            const auto second_key = submit(second, item, "/shared-second");
            removed.stop_all();
            std::cout << "queued" << std::endl;
            check(!first_key.empty() && !removed_key.empty() && !second_key.empty() &&
                      first_key != framework::Uuid{}.str() && second_key != framework::Uuid{}.str() &&
                      removed_key != framework::Uuid{}.str(),
                  "regional script HTTP throttles should remain independent");
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
            bool first_done = false, second_done = false;
            while ((!first_done || !second_done) && std::chrono::steady_clock::now() < deadline) {
                const auto check_reply = [&](network::ScriptHttpModule& module, const std::string& key,
                                             std::string_view body, bool& done) {
                    const auto replies = module.take_completed();
                    for (const auto& reply : replies) {
                        check(!done && reply.request.str() == key && reply.part == part && reply.local_id == local_id &&
                                  reply.reply.status == 200 && reply.reply.body == body,
                              "shared HTTP worker delivered a reply to the wrong region");
                        done = true;
                    }
                };
                check_reply(first, first_key, "first", first_done);
                check_reply(second, second_key, "second", second_done);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            check(first_done && second_done && removed.take_completed().empty(),
                  "shared HTTP workers must deliver both regional replies and suppress cancelled requests");
            const auto retirement_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
            while (service->active_workers() && std::chrono::steady_clock::now() < retirement_deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            check(service->active_workers() == 0, "idle script HTTP worker did not retire");
            auto& restarted = removed;
            const auto restarted_key = submit(restarted, item, "/shared-restart");
            const auto restart_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (std::chrono::steady_clock::now() < restart_deadline) {
                const auto replies = restarted.take_completed();
                if (!replies.empty()) {
                    check(replies.size() == 1 && replies.front().request.str() == restarted_key &&
                              replies.front().reply.status == 200 && replies.front().reply.body == "restarted",
                          "script HTTP worker returned the wrong reply after restarting");
                    network::ScriptHttpModule stopped(service, throttle);
                    const auto stopped_key = submit(stopped, item, "/shared-restart");
                    check(!stopped_key.empty() && stopped_key != framework::Uuid{}.str(),
                          "queue a second response for teardown");
                    const auto idle_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
                    while (service->active_workers() && std::chrono::steady_clock::now() < idle_deadline)
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    check(service->active_workers() == 0, "response finishes before script teardown");
                    stopped.stop(local_id, item);
                    check(stopped.take_completed().empty(), "script teardown discards its queued HTTP reply");
                    return 0;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            throw std::runtime_error("script HTTP worker did not restart for a new request");
        }
        if (argc == 5 && std::string_view(argv[1]) == "--http-proxy-bypass-probe") {
            const auto uris = framework::native_config_uri_object_backend();
            const auto uri = uris->create(std::make_shared<const std::u16string>(framework::utf16(argv[2])),
                                          framework::ConfigUriObjectKind::Absolute);
            const auto proxy = std::dynamic_pointer_cast<framework::ConfigHttpProxy>(
                network::make_script_http_proxy(argv[3], argv[4]));
            std::cout << (proxy && proxy->is_bypassed(uri)) << '\n';
            return 0;
        }
        const auto probe = argc > 1 ? std::string_view(argv[1]) : std::string_view{};
        const bool custom_content_type = argc == 5 && std::string_view(argv[4]) == "--custom-content-type";
        const bool invalid_header_name = argc == 5 && std::string_view(argv[4]) == "--invalid-header-name";
        const bool custom_content_length = argc == 5 && std::string_view(argv[4]).starts_with("--content-length=");
        constexpr std::string_view method_option = "--method=";
        const bool custom_method = argc == 5 && std::string_view(argv[4]).starts_with(method_option);
        if (argc >= 4 && argc <= 8 &&
            (probe == "--http-transport-probe" || probe == "--http-transport-insecure-probe")) {
            const auto uris = framework::native_config_uri_object_backend();
            network::ScriptHttpRequest request;
            request.uri = uris->create(std::make_shared<const std::u16string>(framework::utf16(argv[2])),
                                       framework::ConfigUriObjectKind::Absolute);
            request.max_body_bytes = std::stoi(argv[3]);
            request.method = "POST";
            request.body = "payload";
            if (custom_method)
                request.method = std::string_view(argv[4]).substr(method_option.size());
            if (request.method == "CONNECT") {
                request.headers.emplace_back("Host", "fixture.invalid:443");
                request.body.clear();
            }
            if (custom_content_type)
                request.headers.emplace_back("Content-Type", "text/custom");
            if (invalid_header_name)
                request.headers.emplace_back("Bad Header", "ignored");
            if (invalid_header_name) {
                request.headers.emplace_back("X-Fixture", "first");
                request.headers.emplace_back("user-agent", "one");
                request.headers.emplace_back("User-Agent", "two");
                request.headers.emplace_back("Cookie", "one");
                request.headers.emplace_back("cookie", "two");
                request.headers.emplace_back("Transfer-Encoding", "gzip;level=\"a,b\", ChUnKeD");
                request.headers.emplace_back("aLLoW", "GET");
                request.headers.emplace_back("ETag", "\"fixture\"");
            }
            if (custom_content_length)
                request.headers.emplace_back("Content-Length", std::string_view(argv[4]).substr(17));
            if (argc == 8) {
                const std::string_view encoding = argv[7];
                if (encoding == "latin1") {
                    request.body = "caf\xe9";
                    request.mime_type = "text/plain; charset=\"iso-8859-1\"";
                } else if (encoding == "utf16-bom") {
                    request.body = std::string("\xff\xfe" "c\0a\0f\0\xe9\0", 10);
                    request.mime_type = "text/plain";
                } else if (encoding == "utf32be") {
                    request.body = std::string("\0\0\xfe\xff\0\0\0c\0\0\0a\0\0\0f\0\0\0\xe9", 20);
                    request.mime_type = "text/plain; charset=utf-32BE";
                } else if (encoding == "utf8-bom") {
                    request.body = "\xef\xbb\xbf" "caf\xc3\xa9";
                } else if (encoding == "invalid") {
                    request.mime_type = "text/plain; charset=unsupported-fixture-encoding";
                } else {
                    throw std::invalid_argument("Unknown Digest fixture encoding");
                }
            }
            request.verify_certificate = probe == "--http-transport-probe";
            request.pragma_no_cache = true;
            request.headers.emplace_back("X-Fixture", "kept");
            const auto initial = request.uri->http_components();
            auto filter = std::make_shared<framework::OutboundUrlFilter>(
                framework::OutboundUrlFilter::default_disallow, "127.0.0.1:" + std::to_string(initial->port()));
            const auto proxy = argc >= 6 ? network::make_script_http_proxy(argv[4], argv[5]) : nullptr;
            if (argc >= 7) {
                const auto concrete = std::dynamic_pointer_cast<framework::ConfigHttpProxy>(proxy);
                check(bool(concrete), "script HTTP credential probe requires a proxy");
                if (std::string_view(argv[6]) == "--environment-credentials") {
                    std::string proxy_url = argv[4];
                    proxy_url.insert(proxy_url.find("://") + 3, "f%C3%AFxture:p%C3%A4ss@");
                    const auto backend = framework::native_config_http_environment_proxy_backend();
                    const auto create = [&](std::string_view value) {
                        return framework::config_http_environment_proxy_uri(
                            std::make_shared<const std::u16string>(framework::utf16(value)), backend);
                    };
                    concrete->credentials(framework::ConfigHttpEnvironmentProxyCredentials::try_create(
                        create("socks4://other:secret@127.0.0.1:1"), create(proxy_url), backend));
                } else if (std::string_view(argv[6]) == "--kerberos-default-credentials") {
                    concrete->credentials(framework::config_http_default_network_credentials());
                } else if (std::string_view(argv[6]) == "--kerberos-credentials") {
                    concrete->credentials(std::make_shared<framework::ConfigHttpNetworkCredential>(
                        std::make_shared<const std::u16string>(u"fixture"),
                        std::make_shared<const std::u16string>(u"fixture-password"),
                        std::make_shared<const std::u16string>(u"OPENSIM.TEST")));
                } else {
                    concrete->credentials(std::make_shared<framework::ConfigHttpNetworkCredential>(
                        std::make_shared<const std::u16string>(u"f\u00efxture"),
                        std::make_shared<const std::u16string>(u"p\u00e4ss"), nullptr));
                }
            }
            if (argc == 7 && std::string_view(argv[6]) == "--pooled-credentials") {
                network::WebHttpHandlerSettings settings;
                settings.use_proxy = true;
                settings.proxy = proxy;
                settings.no_verify_cert_chain = true;
                settings.no_verify_cert_hostname = true;
                network::WebHttpClient client(network::native_web_http_handler_factory()(settings), 1500, 32768);
                for (int index = 0; index < 2; ++index) {
                    framework::ConfigXmlHttpOperation operation;
                    operation.method = "GET";
                    operation.response_headers = [](const auto& response) {
                        check(response.status == 200, "authenticated SOCKS pool response status");
                    };
                    const auto body = client.send_headers(request.uri, operation);
                    std::array<std::uint8_t, 8> buffer{};
                    check(body && body->read(buffer) == 2, "authenticated SOCKS pool response body");
                    body->close();
                }
                return 0;
            }
            network::ScriptHttpTransport transport(filter, network::native_web_http_handler_factory(), 1500, 8, proxy);
            const auto cancel_after = argc == 5 && !custom_content_type && !custom_content_length &&
                                      !custom_method && !invalid_header_name
                ? std::stoi(argv[4]) : -1;
            const auto started = std::chrono::steady_clock::now();
            const auto reply = transport.send(request, [&] {
                return cancel_after >= 0 &&
                    std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(cancel_after);
            });
            if (reply)
                std::cout << reply->status << ' ' << hex(reply->body) << '\n';
            else
                std::cout << "cancelled\n";
            return 0;
        }
        if (argc == 7 && std::string_view(argv[1]) == "--smtp-probe") {
            const std::string_view mode = argv[4];
            const auto tls = mode == "implicit" ? network::ScriptSmtpTls::Implicit :
                mode == "1" || mode == "required-legacy" ? network::ScriptSmtpTls::StartTls :
                                                          network::ScriptSmtpTls::Automatic;
            const bool legacy = mode == "legacy" || mode == "required-legacy";
            network::ScriptSmtpSecurity security{tls, std::string_view(argv[5]) == "1",
                std::string_view(argv[6]) == "1", legacy ? "" : "fixture-user",
                legacy ? "" : "fixture-password"};
            const auto body = mode == "eight-bit" || mode == "encoded" ? "first\n.café\nlast" :
                                                                        "first\n.second\nlast";
            network::send_script_smtp(argv[2], std::stoi(argv[3]), "\"Simulátor, Native\" <from@example.invalid>",
                "Fixture Recipient <to@example.invalid>", "Fixture", body,
                "fixture-owner", "fixture-object", security);
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--api-probe") {
            int mode, count, channel;
            std::string text;
            while (std::cin >> mode >> count >> channel >> text)
                std::cout << api_probe(mode, count, channel, unhex(text == "-" ? "" : text)) << "\n";
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--http-throttle-probe") {
            HttpRequestThrottleSettings settings;
            std::cin >> settings.prim_rate >> settings.prim_burst >> settings.owner_rate >> settings.owner_burst;
            HttpRequestThrottle throttle(settings);
            char operation;
            std::uint32_t prim;
            std::string owner;
            double time;
            while (std::cin >> operation >> prim >> owner >> time) {
                if (operation == 'S')
                    throttle.stop(prim, time);
                else
                    std::cout << throttle.allow(prim, framework::Uuid(owner), time) << '\n';
            }
            return 0;
        }
        int checks = 0;
        auto test = [&](bool b, const char* what) {
            check(b, what);
            checks++;
        };
        {
            const auto owner = context().owner;
            const auto other = context().object;
            HttpRequestThrottle throttle;
            test(throttle.allow(1, owner, 0) && throttle.allow(1, owner, 0) &&
                     throttle.allow(2, owner, 0) && !throttle.allow(3, owner, 0),
                 "script HTTP initializes the shared owner bucket with the prim burst");
            test(throttle.allow(3, other, 0) && throttle.allow(3, other, 0) &&
                     !throttle.allow(3, other, 0) && throttle.allow(3, other, .01),
                 "owner rejection still consumes prim credit and fractional refill permits a request");
            HttpRequestThrottle stopped;
            test(stopped.allow(7, owner, 0) && stopped.allow(7, owner, 0) && stopped.allow(7, owner, 0),
                 "new prim and owner buckets grant the first request");
            stopped.stop(7, .125);
            test(stopped.allow(7, other, .125) && stopped.allow(7, other, .125),
                 "script HTTP stop uses the owner refill rate to expire a prim bucket");
        }
        {
            const std::string source = R"LSL(
                integer counter = 40;
                default {
                    state_entry() { state working; }
                }
                state working {
                    state_entry() {
                        integer increment = 2;
                        llSetTimerEvent(2.0);
                        llSleep(1.0);
                        counter += increment;
                        llSay(0, (string)counter);
                    }
                    touch_start(integer count) { llSay(0, (string)count); }
                    timer() {
                        counter++;
                        llSay(0, (string)counter);
                        llSetTimerEvent(0.0);
                    }
                }
            )LSL";
            const auto original = compile_lsl(source);
            const auto recompiled = compile_lsl(source);
            test(bool(original) && bool(recompiled), "compile checkpoint fixture");
            std::int64_t source_clock = 10000000;
            LslHost source_host;
            source_host.clock_ticks = source_host.monotonic_ticks = [&] {
                return source_clock;
            };
            LslInstance sender(original.program, context(), source_host);
            test(sender.run(256).status == LslRunStatus::Sleeping, "checkpoint source sleeps in named state");
            LslDetector detector;
            detector.key = framework::Uuid::random();
            detector.touch_face = 3;
            detector.position = {1, 2, 3};
            test(sender.post_event({"touch_start", {std::int32_t{77}}, {detector}}),
                 "queue event before checkpoint");
            source_clock += 2500000;
            const auto snapshot = sender.checkpoint();
            test(snapshot && snapshot->state == "working" && snapshot->sleep_remaining == 7500000 &&
                     snapshot->timer_remaining == 17500000 && snapshot->queued_events.size() == 1,
                 "capture named state, relative clocks and queued event");

            std::int64_t destination_clock = 90000000;
            std::vector<std::string> messages;
            LslHost destination_host;
            destination_host.clock_ticks = destination_host.monotonic_ticks = [&] {
                return destination_clock;
            };
            destination_host.chat = [&](const LslChat& chat) {
                messages.push_back(chat.text);
            };
            LslInstance receiver(recompiled.program, context(), destination_host);
            const auto encoded = encode_lsl_checkpoint(*snapshot);
            const auto decoded = decode_lsl_checkpoint(encoded);
            test(decoded && encode_lsl_checkpoint(*decoded) == encoded &&
                     decoded->queued_events.front().detectors.front().key == detector.key &&
                     decoded->queued_events.front().detectors.front().position == detector.position,
                 "checkpoint wire round trip preserves event detector data");
            test(!decode_lsl_checkpoint(std::span(encoded).first(encoded.size() - 1)),
                 "reject truncated checkpoint payload");
            auto with_listeners = *snapshot;
            with_listeners.listeners = {
                {detector.key, 3, -8, "Speaker", "Message", false},
                {{}, 8, 27, "", "", true},
            };
            const auto listener_wire = encode_lsl_checkpoint(with_listeners);
            const auto restored_listeners = decode_lsl_checkpoint(listener_wire);
            test(encoded[3] == 1 && listener_wire[3] == 2 && restored_listeners &&
                     encode_lsl_checkpoint(*restored_listeners) == listener_wire &&
                     restored_listeners->listeners.size() == 2 &&
                     restored_listeners->listeners.front().sender == detector.key &&
                     !restored_listeners->listeners.front().active,
                 "versioned checkpoints preserve listener handles, filters, and activation");
            const auto separate_listeners = encode_lsl_listeners(with_listeners.listeners);
            const auto decoded_listeners = decode_lsl_listeners(separate_listeners);
            test(encode_lsl_checkpoint(with_listeners, false) == encoded && decoded_listeners &&
                     encode_lsl_listeners(*decoded_listeners) == separate_listeners &&
                     !decode_lsl_listeners(std::span(separate_listeners).first(separate_listeners.size() - 1)),
                 "crossing sends legacy execution state and validates the separate listener extension");
            with_listeners.listeners.back().handle = with_listeners.listeners.front().handle;
            test(!decode_lsl_checkpoint(encode_lsl_checkpoint(with_listeners)),
                 "reject checkpoint with duplicate listener handles");
            auto with_start_parameter = *snapshot;
            with_start_parameter.start_parameter = -73;
            with_start_parameter.permissions_granter = detector.key;
            with_start_parameter.permissions_mask = 16;
            const auto parameter_wire = encode_lsl_checkpoint(with_start_parameter);
            const auto restored_parameter = decode_lsl_checkpoint(parameter_wire);
            const auto crossing_parameter = decode_lsl_checkpoint(encode_lsl_checkpoint(with_start_parameter, false));
            test(parameter_wire[3] == 4 && restored_parameter && restored_parameter->start_parameter == -73 &&
                     restored_parameter->permissions_granter == detector.key &&
                     crossing_parameter && crossing_parameter->start_parameter == -73 &&
                     crossing_parameter->permissions_granter.is_zero() && decoded->start_parameter == 0,
                 "version four preserves start parameters without changing older checkpoint layouts");
            test(!decode_lsl_checkpoint(std::span(parameter_wire).first(parameter_wire.size() - 1)),
                 "reject checkpoint truncated inside the start parameter");
            auto with_sensors = with_start_parameter;
            with_sensors.sensors = {{"Nearest", detector.key, 4, 40., 1.5, .25},
                                    {"Once", {}, 1, 20., 3., std::nullopt}};
            const auto sensor_wire = encode_lsl_checkpoint(with_sensors, false);
            const auto crossing_sensors = decode_lsl_checkpoint(sensor_wire);
            test(sensor_wire[3] == 5 && crossing_sensors && crossing_sensors->sensors.size() == 2 &&
                     crossing_sensors->sensors[0].name == "Nearest" &&
                     crossing_sensors->sensors[0].key == detector.key &&
                     crossing_sensors->sensors[0].type == 4 && crossing_sensors->sensors[0].range == 40. &&
                     crossing_sensors->sensors[0].arc == 1.5 && crossing_sensors->sensors[0].interval == .25 &&
                     !crossing_sensors->sensors[1].interval && crossing_sensors->start_parameter == -73 &&
                     encode_lsl_checkpoint(*crossing_sensors, false) == sensor_wire && decoded->sensors.empty(),
                 "version five preserves repeat and one-shot sensors in crossing checkpoints");
            test(!decode_lsl_checkpoint(std::span(sensor_wire).first(sensor_wire.size() - 1)),
                 "reject checkpoint truncated inside a sensor record");
            auto invalid = *decoded;
            invalid.instruction = std::numeric_limits<std::size_t>::max();
            test(!receiver.restore_checkpoint(invalid), "reject checkpoint with invalid instruction");
            test(receiver.restore_checkpoint(*decoded),
                 "restore checkpoint after recompilation and wire decode");
            test(receiver.run(256).status == LslRunStatus::Sleeping && messages.empty(),
                 "restored sleep does not finish early");
            destination_clock += 7500000;
            receiver.run(256);
            test(messages == std::vector<std::string>{"42", "77"},
                 "resume at saved instruction with global, local and queued argument");
            destination_clock += 10000000;
            receiver.run(256);
            test(messages == std::vector<std::string>{"42", "77", "43"}, "restored timer fires once");
        }
        std::vector<std::string> messages;
        LslHost host;
        host.chat = [&](const LslChat& x) {
            messages.push_back(x.text);
        };
        host.clock_ticks = [] {
            return std::int64_t(0);
        };
        {
            const auto program = compile_lsl(R"LSL(default {
                state_entry() {
                    llSetLocalRot(<0.0, 0.0, 1.0, 0.0>);
                    rotation local = llGetLocalRot();
                    rotation root = llGetRootRotation();
                    rotation world = llGetRot();
                    if (local.z == 1.0 && root.x == 1.0 && world.y == 1.0)
                        llSay(0, "live rotation");
                }
            })LSL");
            test(bool(program), "live rotation script compiles");
            auto rotation_host = host;
            rotation_host.script_delay_factor = 0.f;
            LslRotation current;
            rotation_host.set_object_rotation = [&](auto, LslRotation rotation, bool local) {
                if (local)
                    current = rotation;
            };
            rotation_host.object_rotation = [&](auto, LslRotationSpace space) -> std::optional<LslRotation> {
                switch (space) {
                case LslRotationSpace::Local: return current;
                case LslRotationSpace::Root: return LslRotation{1, 0, 0, 0};
                case LslRotationSpace::World: return LslRotation{0, 1, 0, 0};
                }
                return {};
            };
            LslInstance instance(program.program, context(), rotation_host);
            test(instance.run(100).status == LslRunStatus::Idle &&
                     messages == std::vector<std::string>{"live rotation"},
                 "rotation queries use live host values in the same VM run");
            messages.clear();
        }
        {
            auto url_host = host;
            bool delivered = false;
            url_host.load_url = [&](auto, auto, const std::string&, const std::string&) { delivered = true; };
            auto url_context = context();
            url_context.group = framework::Uuid::random();
            const auto program = compile_lsl(
                "default { state_entry() { llLoadURL(llGetOwner(), \"Message\", \"ftp://example.invalid\"); } }");
            test(bool(program), "llLoadURL compiles");
            LslInstance rejected(program.program, url_context, url_host);
            test(rejected.run(100).status == LslRunStatus::Sleeping && !delivered &&
                     messages == std::vector<std::string>{"llLoadURL: Invalid url schema"},
                 "llLoadURL rejects non-HTTP URLs before viewer delivery");
            messages.clear();
            url_context.group = url_context.owner;
            LslInstance group_owned(program.program, url_context, url_host);
            test(group_owned.run(100).status == LslRunStatus::Idle && !delivered && messages.empty(),
                 "llLoadURL ignores group-owned objects before URL validation");
        }
        {
            const auto program = compile_lsl(R"LSL(default {
                state_entry() {
                    llMapDestination("Owner map", <1, 2, 3>, ZERO_VECTOR);
                }
                touch_start(integer count) {
                    llMapDestination("Detected map", <1, 2, 3>, <9, 9, 9>);
                    llMapBeacon("Detected map", <1, 2, 3>, [BEACON_MAP, 1, 0]);
                    llMapBeacon("Detected map", <1, 2, 3>, []);
                }
            })LSL");
            test(bool(program), "map requests compile");
            auto map_host = host;
            map_host.script_delay_factor = 0.f;
            auto script_context = context();
            const auto target = framework::Uuid::random();
            std::vector<std::uint32_t> options;
            map_host.map_destination = [&](auto object, auto recipient, const std::string& name,
                                           auto position, std::uint32_t flags) {
                test(object == script_context.object && position == framework::Vector3d{1, 2, 3},
                     "map request forwards the source prim and destination");
                test((recipient == target && name == "Detected map") ||
                         (recipient == script_context.owner && name == "Owner map"),
                     "map request uses the detected target or attachment owner");
                options.push_back(flags);
            };
            LslInstance rezzed(program.program, script_context, map_host);
            rezzed.run(100);
            test(options.empty(), "map request on a rezzed prim without a detector is ignored");
            LslDetector detector;
            detector.key = target;
            rezzed.post_event({"touch_start", {std::int32_t(1)}, {detector}});
            rezzed.run(100);
            test(options == std::vector<std::uint32_t>{3, 1, 0}, "map destination and beacon flags");
            script_context.attachment_point = 5;
            LslInstance worn(program.program, script_context, map_host);
            worn.run(100);
            test(options == std::vector<std::uint32_t>{3, 1, 0, 3}, "worn script opens the owner's map");
        }
        {
            const auto program = compile_lsl(
                "default { state_entry() { llDie(); llSay(0, \"still running\"); } }");
            test(bool(program), "llDie compiles");
            auto script_context = context();
            LslInstance rezzed(program.program, script_context, host);
            test(rezzed.run(100).status == LslRunStatus::DeleteLinkset && messages.empty(),
                 "llDie requests deletion and aborts the running event");
            script_context.attachment_point = 5;
            LslInstance attached(program.program, script_context, host);
            test(attached.run(100).status == LslRunStatus::Idle &&
                     messages == std::vector<std::string>{"still running"},
                 "llDie has no effect on attachments");
            messages.clear();
        }
        {
            const auto program = compile_lsl(R"LSL(default {
                state_entry() {
                    integer calls = 0;
                    llCollisionSprite((string)(++calls));
                    llPointAt(<1, 2, 3>);
                    llStopPointAt();
                    llSay(0, (string)calls);
                }
            })LSL");
            test(bool(program), "legacy no-op commands compile");
            LslInstance script(program.program, context(), host);
            test(script.run(100).status == LslRunStatus::Idle &&
                     messages == std::vector<std::string>{"1"},
                 "legacy no-op commands evaluate arguments without warnings or a sleep");
            messages.clear();
        }
        {
            const auto program = compile_lsl("default { state_entry() { llSetBuoyancy(1.25); } }");
            test(bool(program), "llSetBuoyancy compiles");
            const auto script_context = context();
            auto buoyancy_host = host;
            bool called = false;
            buoyancy_host.set_buoyancy = [&](auto object, float buoyancy) {
                called = object == script_context.object && buoyancy == 1.25f;
            };
            LslInstance instance(program.program, script_context, buoyancy_host);
            test(instance.run(100).status == LslRunStatus::Idle && called,
                 "llSetBuoyancy forwards the object and unclamped value");
            test(!compile_lsl("default { state_entry() { integer result = llSetBuoyancy(1); } }"),
                 "llSetBuoyancy has no return value");
            test(bool(compile_lsl("default { state_entry() { for (llSetBuoyancy(1); 0; llSetBuoyancy(0)) {} } }")),
                 "llSetBuoyancy can appear in discarded for-loop clauses");
        }
        {
            const auto program = compile_lsl(R"LSL(
                default {
                    state_entry() {
                        llSetForce(<2, 3, 4>, TRUE);
                        llSetTorque(<1, 2, 3>, TRUE);
                        if (llGetForce() == <-2, -3, 4> && llGetTorque() == <1, 2, 3>)
                            llSetForceAndTorque(<5, 6, 7>, <7, 8, 9>, FALSE);
                    }
                }
            )LSL");
            test(bool(program), "force and torque setters and getters compile");
            auto script_context = context();
            script_context.world_rotation = LslRotation{0, 0, 1, 0};
            auto force_host = host;
            std::vector<opensim::framework::Vector3> forces;
            std::vector<std::pair<opensim::framework::Vector3, bool>> torques;
            force_host.set_force = [&](auto object, auto force) {
                test(object == script_context.object, "force setter forwards the host prim");
                forces.push_back(force);
            };
            force_host.get_force = [&](auto) { return forces.back(); };
            force_host.set_torque = [&](auto object, auto torque, bool local) {
                test(object == script_context.object, "torque setter forwards the host prim");
                torques.emplace_back(torque, local);
            };
            force_host.get_torque = [&](auto) { return torques.back().first; };
            LslInstance instance(program.program, script_context, force_host);
            test(instance.run(100).status == LslRunStatus::Idle &&
                     forces == std::vector<opensim::framework::Vector3>{{-2, -3, 4}, {5, 6, 7}} &&
                     torques == std::vector<std::pair<opensim::framework::Vector3, bool>>{
                         {{1, 2, 3}, true}, {{7, 8, 9}, false}},
                 "force and torque dispatch with their respective coordinate modes");
        }
        {
            const auto program = compile_lsl(R"LSL(
                default {
                    state_entry() {
                        llSetVelocity(<1, 2, 3>, TRUE);
                        llSetAngularVelocity(<4, 5, 6>, FALSE);
                        llApplyImpulse(<7, 8, 9>, FALSE);
                        llApplyRotationalImpulse(<10, 11, 12>, TRUE);
                    }
                }
            )LSL");
            test(bool(program), "linear and angular velocity setters compile");
            const auto script_context = context();
            auto velocity_host = host;
            using Update = std::tuple<opensim::framework::Vector3, bool, bool>;
            std::vector<Update> updates;
            std::vector<Update> impulses;
            velocity_host.set_velocity = [&](auto object, auto vector, bool local, bool angular) {
                test(object == script_context.object, "velocity setter forwards the host prim");
                updates.emplace_back(vector, local, angular);
            };
            velocity_host.apply_impulse = [&](auto object, auto vector, bool local, bool angular) {
                test(object == script_context.object, "impulse forwards the host prim");
                impulses.emplace_back(vector, local, angular);
            };
            LslInstance instance(program.program, script_context, velocity_host);
            test(instance.run(100).status == LslRunStatus::Idle &&
                     updates == std::vector<Update>{{{1, 2, 3}, true, false}, {{4, 5, 6}, false, true}} &&
                     impulses == std::vector<Update>{{{7, 8, 9}, false, false}, {{10, 11, 12}, true, true}},
                 "velocity and impulse calls dispatch the vector and coordinate mode");
        }
        {
            const auto program = compile_lsl(R"LSL(
                default {
                    state_entry() {
                        key target = "12345678-1234-1234-1234-123456789abc";
                        vector angular = ZERO_VECTOR;
                        llPushObject(target, <2, 3, 4>, (angular = <8, 9, 10>), TRUE);
                        if (angular == <8, 9, 10>)
                            llSay(0, "angular evaluated");
                        llPushObject("invalid", <1, 1, 1>, ZERO_VECTOR, FALSE);
                        llPushObject(NULL_KEY, <1, 1, 1>, ZERO_VECTOR, FALSE);
                    }
                }
            )LSL");
            if (!program)
                throw std::runtime_error("llPushObject compile failed: " + program.diagnostics.front().message);
            const auto script_context = context();
            auto push_host = host;
            bool angular_evaluated = false;
            push_host.chat = [&](const LslChat& chat) {
                angular_evaluated = chat.text == "angular evaluated";
            };
            int pushes = 0;
            push_host.push_object = [&](auto source, auto target, auto impulse, bool local) {
                ++pushes;
                test(source == script_context.object &&
                         target.str() == "12345678-1234-1234-1234-123456789abc" &&
                         impulse == opensim::framework::Vector3{2, 3, 4} && local,
                     "llPushObject forwards the linear impulse and local flag");
            };
            LslInstance instance(program.program, script_context, push_host);
            test(instance.run(100).status == LslRunStatus::Idle && pushes == 1 && angular_evaluated,
                 "llPushObject evaluates the angular argument and skips invalid and zero target keys");
        }
        {
            const auto program = compile_lsl(R"LSL(
                default {
                    state_entry() {
                        llLookAt(llGetPos() + <1, 0, 0>, 2, 3);
                        llRotLookAt(ZERO_ROTATION, 4, 5);
                        llStopLookAt();
                    }
                }
            )LSL");
            test(bool(program), "look-at calls compile");
            auto look_host = host;
            using Command = std::tuple<std::optional<opensim::framework::Quaternion>, double, double, bool>;
            std::vector<Command> commands;
            look_host.look_at = [&](auto, auto rotation, double strength, double damping, bool point_target) {
                commands.emplace_back(rotation, strength, damping, point_target);
                return false;
            };
            LslInstance instance(program.program, context(), look_host);
            using opensim::framework::Quaternion;
            test(instance.run(100).status == LslRunStatus::Idle &&
                     commands == std::vector<Command>{{Quaternion{0, .707107f, 0, .707107f}, 2., 3., true},
                                                      {Quaternion{0, 0, 0, 1}, 4., 5., false},
                                                      {std::nullopt, 0., 0., false}},
                 "look-at calls deliver point and rotation targets and stop");
        }
        {
            const auto program = compile_lsl(
                "default { state_entry() { llMoveToTarget(<1, 2, 3>, 0.25); llStopMoveToTarget(); } }");
            test(bool(program), "move-to-target start and stop compile");
            auto movement_host = host;
            std::vector<opensim::framework::PrimMoveTarget> commands;
            movement_host.move_to_target = [&](auto, auto target) { commands.push_back(target); };
            LslInstance instance(program.program, context(), movement_host);
            test(instance.run(100).status == LslRunStatus::Idle &&
                     commands == std::vector<opensim::framework::PrimMoveTarget>{{{1, 2, 3}, .25f}, {}},
                 "move-to-target calls deliver the destination and stop command");
        }
        {
            const auto program = compile_lsl(
                "default { state_entry() { llSetHoverHeight(3, TRUE, 0.25); llStopHover(); } }");
            test(bool(program), "hover start and stop compile");
            auto hover_host = host;
            std::vector<opensim::framework::PrimHover> settings;
            hover_host.set_hover = [&](auto, auto hover) { settings.push_back(hover); };
            LslInstance instance(program.program, context(), hover_host);
            test(instance.run(100).status == LslRunStatus::Idle &&
                     settings == std::vector<opensim::framework::PrimHover>{{3.f, .25f, true}, {}},
                 "hover calls deliver settings and a stop command");
        }
        {
            const auto program = compile_lsl(R"LSL(
                default {
                    state_entry() {
                        integer order = 0;
                        llSound("", 1, order = order * 10 + 1, order = order * 10 + 2);
                        llSoundPreload("");
                        llMakeExplosion(1, 1, 1, 1, 1, "", ZERO_VECTOR);
                        llMakeFountain(1, 1, 1, 1, 1, 0, "", ZERO_VECTOR, 0);
                        llMakeSmoke(1, 1, 1, 1, 1, "", ZERO_VECTOR);
                        llMakeFire(1, 1, 1, 1, 1, "", ZERO_VECTOR);
                        llTakeCamera(NULL_KEY);
                        llReleaseCamera(NULL_KEY);
                        llRemoteLoadScript(NULL_KEY, "", 0, 0);
                        llRemoteDataSetRegion();
                        llSetPrimURL("");
                        llRefreshPrimURL();
                        llSay(0, (string)order);
                    }
                }
            )LSL");
            test(bool(program), "deprecated APIs retain their managed signatures");
            std::int64_t clock = 0;
            int sleep_ms = 0;
            std::vector<std::string> notices;
            std::vector<std::string> deliveries;
            LslHost legacy_host;
            legacy_host.script_delay_factor = .5f;
            legacy_host.clock_ticks = [&] { return clock; };
            legacy_host.request_sleep = [&](int delay) { sleep_ms = delay; };
            legacy_host.chat = [&](const LslChat& chat) {
                if (chat.type == LslChatType::Debug) {
                    check(chat.channel == 2147483647, "deprecated API uses the debug channel");
                    notices.push_back(chat.text);
                }
            };
            legacy_host.listener = [&](const LslChat& chat) { deliveries.push_back(chat.text); };
            LslInstance script(program.program, context(), legacy_host);
            const std::pair<std::string_view, int> expected[] = {
                {"llSound: Command deprecated - Use llPlaySound instead", 1000},
                {"llSoundPreload: Command deprecated - Use llPreloadSound instead", 1000},
                {"llMakeExplosion: Command deprecated - Use llParticleSystem instead", 50},
                {"llMakeFountain: Command deprecated - Use llParticleSystem instead", 50},
                {"llMakeSmoke: Command deprecated - Use llParticleSystem instead", 50},
                {"llMakeFire: Command deprecated - Use llParticleSystem instead", 50},
                {"llTakeCamera: Command deprecated - Use llSetCameraParams instead", 1000},
                {"llReleaseCamera: Command deprecated - Use llClearCameraParams instead", 1000},
                {"llRemoteLoadScript: Command deprecated - Use llRemoteLoadScriptPin instead", 1500},
                {"llRemoteDataSetRegion: Command deprecated - Use llOpenRemoteDataChannel instead", 1000},
                {"llSetPrimURL: Command deprecated - Use llSetPrimMediaParams instead", 1000},
                {"llRefreshPrimURL: Command deprecated", 10000},
            };
            for (const auto& [message, delay] : expected) {
                const auto previous = notices.size();
                test(script.run(100).status == LslRunStatus::Sleeping && notices.size() == previous + 1 &&
                         notices.back() == message && deliveries == notices && sleep_ms == delay,
                     "deprecated API reports its diagnostic and uses the managed delay");
                clock += std::int64_t(delay) * 10000;
            }
            test(script.run(100).status == LslRunStatus::Idle && deliveries.back() == "12",
                 "deprecated API preserves YEngine call argument evaluation order");
        }
        {
            const auto program = compile_lsl(R"LSL(
                default {
                    state_entry() {
                        llLinkSetSoundQueueing(LINK_ALL_CHILDREN, TRUE);
                        llLinkSetSoundRadius(LINK_ALL_CHILDREN, 12.5);
                        llLinkPlaySound(LINK_ALL_CHILDREN, "Bell", 0.5, SOUND_LOOP | SOUND_SYNC);
                        llLinkPlaySound(LINK_ALL_CHILDREN, "Bell", 0.5, SOUND_SYNC);
                        llLinkPlaySound(LINK_ALL_CHILDREN, "Bell", 0.5, SOUND_LOOP | SOUND_TRIGGER);
                        llLinkStopSound(LINK_ALL_CHILDREN);
                        llLinkAdjustSoundVolume(LINK_ALL_CHILDREN, 2);
                    }
                }
            )LSL");
            test(bool(program), "linked sound APIs compile");
            const auto source = context();
            const auto child = framework::Uuid::random();
            const auto asset = framework::Uuid::random();
            auto sound_host = host;
            std::vector<std::string> calls;
            sound_host.linked_prim_parts = [&](auto object, int selector) {
                check(object == source.object && selector == -3, "sound link selector");
                return std::vector{child};
            };
            sound_host.resolve_sound_asset = [&](auto object, const auto& name) {
                check(object == source.object && name == "Bell", "sound resolved in source inventory");
                return asset;
            };
            sound_host.set_sound_queueing = [&](auto part, bool enabled) {
                check(part == child && enabled, "linked sound queue");
                calls.emplace_back("queue");
            };
            sound_host.set_sound_radius = [&](auto part, float radius) {
                check(part == child && radius == 12.5f, "linked sound radius");
                calls.emplace_back("radius");
            };
            sound_host.play_sound = [&](auto part, const auto& name, double gain, auto flags, bool triggered,
                                        const auto& bounds) {
                check(part == child && name == asset.str() && gain == 0.5 && flags == 4 && !triggered &&
                          !bounds,
                      "linked sound plays as slave");
                calls.emplace_back("play");
            };
            sound_host.loop_sound = [&](auto part, const auto& name, double gain, auto flags) {
                check(part == child && (name ? *name == asset.str() && gain == 0.5 && flags == 4
                                            : gain == 0 && flags == 0),
                      "linked sound loop and stop");
                calls.emplace_back(name ? "loop" : "stop");
            };
            sound_host.adjust_sound_gain = [&](auto part, float gain) {
                check(part == child && gain == 1.f, "linked sound gain clamps");
                calls.emplace_back("gain");
            };
            LslInstance script(program.program, source, sound_host);
            test(script.run(100).status == LslRunStatus::Sleeping &&
                     calls == std::vector<std::string>{"queue", "radius", "loop", "play", "stop", "gain"},
                 "linked sound targets the selected prim and preserves playback modes and gain delay");
        }
        const auto friendship_program = compile_lsl(R"LSL(
            default {
                state_entry() {
                    key avatar = "d008df35-916d-4832-a494-57690e40b8a2";
                    llSay(0, (string)llIsFriend(avatar));
                    llSay(0, (string)llIsFriend("invalid key"));
                }
            }
        )LSL");
        test(bool(friendship_program), "friendship query compiles");
        auto friendship_context = context();
        friendship_context.group = framework::Uuid("d008df35-916d-4832-a494-57690e40b8a2");
        auto friendship_host = host;
        for (const auto result : {std::optional<bool>{true}, std::optional<bool>{false}, std::optional<bool>{}}) {
            int lookups = 0;
            friendship_host.object_friendship = [&](framework::Uuid object, framework::Uuid avatar) {
                test(object == friendship_context.object && avatar == friendship_context.group,
                     "friendship lookup receives the object and parsed avatar key");
                ++lookups;
                return result;
            };
            const auto start = messages.size();
            LslInstance script(friendship_program.program, friendship_context, friendship_host);
            test(script.run(100).status == LslRunStatus::Idle && lookups == 1 &&
                     std::vector<std::string>(messages.begin() + start, messages.end()) ==
                         std::vector<std::string>{result.value_or(true) ? "1" : "0", "0"},
                 "friendship query honors the host result and falls back to group membership");
        }
        auto dataserver_program = compile_lsl(R"LSL(
            default {
                dataserver(key query_id, string data) {
                    if (query_id)
                        llSay(0, (string)query_id + ":" + data);
                }
            }
        )LSL");
        test(bool(dataserver_program), "typed dataserver handler compiles");
        LslInstance dataserver_script(dataserver_program.program, context(), host);
        const auto query = framework::Uuid("d008df35-916d-4832-a494-57690e40b8a2").str();
        const auto dataserver_start = messages.size();
        test((dataserver_script.event_mask() & (1ull << 8)) != 0 &&
                 !dataserver_script.post_event({"dataserver", {query}, {}}) &&
                 !dataserver_script.post_event({"dataserver", {std::int32_t{1}, std::string("bad")}, {}}) &&
                 dataserver_script.post_event({"dataserver", {query, std::string("first")}, {}}) &&
                 dataserver_script.post_event({"dataserver", {query, std::string("second")}, {}}),
             "dataserver exposes its source event bit and validates both arguments");
        test(dataserver_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + dataserver_start, messages.end()) ==
                     std::vector<std::string>{query + ":first", query + ":second"},
             "queued dataserver replies preserve key type, data, and event order");
        {
            const auto response_program = compile_lsl(R"LSL(
                default {
                    http_response(key request, integer status, list metadata, string body) {
                        llSay(0, (string)request + ":" + (string)status + ":" +
                                 (string)llGetListLength(metadata) + ":" + body);
                    }
                }
            )LSL");
            if (!response_program)
                throw std::runtime_error("http_response compile failed: " + response_program.diagnostics.front().message);
            LslInstance receiver(response_program.program, context(), host);
            const auto metadata = std::make_shared<const LslList>();
            test((receiver.event_mask() & (1ull << 10)) != 0 &&
                     !receiver.post_event({"http_response", {query, 200, std::string{}, std::string{}}, {}}) &&
                     receiver.post_event({"http_response", {query, 200, metadata, std::string("reply")}, {}}),
                 "http_response exposes the managed event bit and validates metadata");
            const auto checkpoint = receiver.checkpoint();
            test(bool(checkpoint), "queued http_response can be saved");
            const auto restored_checkpoint = decode_lsl_checkpoint(encode_lsl_checkpoint(*checkpoint));
            LslInstance restored(response_program.program, context(), host);
            const auto message_count = messages.size();
            test(restored_checkpoint && restored.restore_checkpoint(*restored_checkpoint) &&
                     restored.run(100).status == LslRunStatus::Idle &&
                     messages.size() == message_count + 1 && messages.back() == query + ":200:0:reply",
                 "http_response preserves its key, status, metadata and body through checkpoint restore");
        }
        {
            const auto program = compile_lsl(R"LSL(
                default {
                    remote_data(integer kind, key channel, key request, string sender, integer number, string data) {
                        if (kind == REMOTE_DATA_REPLY)
                            llSay(0, (string)channel + ":" + (string)request + ":" + sender + ":" +
                                     (string)number + ":" + data);
                    }
                }
            )LSL");
            check(bool(program), "remote_data handler compiles");
            LslInstance receiver(program.program, context(), host);
            const auto channel = framework::Uuid::random().str();
            LslEvent reply{"remote_data", {3, channel, query, std::string("sender"), 42, std::string("reply")}, {}};
            auto malformed = reply;
            malformed.arguments[4] = std::string("42");
            check(receiver.event_mask() == (1ull << 23) && !receiver.post_event(std::move(malformed)) &&
                      receiver.post_event(std::move(reply)), "remote_data validates six arguments and exposes bit 23");
            const auto checkpoint = receiver.checkpoint();
            check(bool(checkpoint), "queued remote_data can be saved");
            const auto saved = decode_lsl_checkpoint(encode_lsl_checkpoint(*checkpoint));
            LslInstance restored(program.program, context(), host);
            const auto before = messages.size();
            check(saved && restored.restore_checkpoint(*saved) && restored.run(100).status == LslRunStatus::Idle &&
                      messages.size() == before + 1 && messages.back() == channel + ":" + query + ":sender:42:reply",
                  "remote_data preserves its arguments through checkpoint restore");
        }
        {
            const auto program = compile_lsl(R"LSL(
                default {
                    state_entry() { llOpenRemoteDataChannel(); }
                    remote_data(integer kind, key channel, key request, string sender, integer number, string data) {
                        if (kind == REMOTE_DATA_CHANNEL && request == NULL_KEY && sender == "" && number == 0 && data == "") {
                            llSay(0, (string)channel);
                            llCloseRemoteDataChannel(channel);
                        }
                    }
                }
            )LSL");
            check(bool(program), "remote data channel script compiles");
            auto channel_host = host;
            const auto script_context = context();
            const auto channel = framework::Uuid::random();
            scene::RemoteDataChannels channels;
            std::int64_t channel_clock = 0;
            channel_host.clock_ticks = [&] { return channel_clock; };
            channel_host.script_delay_factor = 1;
            channel_host.open_remote_data_channel = [&](auto object, auto item) {
                check(object == script_context.object && item == script_context.item,
                      "remote data provider receives the running script identity");
                return channels.open(17, item, channel);
            };
            channel_host.close_remote_data_channel = [&](auto item, auto value) {
                check(item == script_context.item && value == channel.str(), "remote data close identity");
                channels.close(channel);
            };
            LslInstance instance(program.program, script_context, channel_host);
            const auto before = messages.size();
            check(instance.run(100).status == LslRunStatus::Sleeping && messages.size() == before,
                  "remote data channel event waits for the opening delay");
            channel_clock += 10000000;
            const auto receiver = channels.find(channel);
            check(receiver && receiver->local_id == 17 && receiver->item == script_context.item &&
                      channels.open(18, script_context.item) == channel, "remote data reuses the existing item channel");
            check(instance.run(100).status == LslRunStatus::Sleeping && messages.size() == before + 1 &&
                      messages.back() == channel.str(), "remote data channel event delivers the provider channel");
            check(!channels.find(channel), "closed remote data channel is removed");
            const auto reopened = channels.open(17, script_context.item);
            const auto other_region = framework::Uuid::random();
            const auto other_channel = channels.open(17, script_context.item, {}, other_region);
            check(other_channel != reopened, "regions can host the same script item independently");
            using namespace std::chrono_literals;
            auto pending = std::async(std::launch::async, [&] {
                return channels.request(reopened, {42, "request"}, 2s);
            });
            std::vector<scene::RemoteDataChannels::Request> requests;
            const auto deadline = std::chrono::steady_clock::now() + 1s;
            while (requests.empty() && std::chrono::steady_clock::now() < deadline) {
                check(channels.take_requests(other_region).empty(),
                      "a region cannot drain another region's remote data requests");
                requests = channels.take_requests();
                if (requests.empty())
                    std::this_thread::sleep_for(1ms);
            }
            check(requests.size() == 1 && requests.front().receiver.item == script_context.item &&
                      requests.front().data.integer == 42 && requests.front().data.text == "request",
                  "remote data request queues the receiver and payload");
            check(channels.reply(reopened, requests.front().message, {43, "response"}),
                  "remote data reply correlates with the pending message");
            const auto response = pending.get();
            check(response.data && response.data->integer == 43 && response.data->text == "response",
                  "remote data request returns the correlated response");
            channels.cancel_script(script_context.item, other_region);
            check(!channels.find(other_channel) && channels.find(reopened),
                  "cancelling a script in one region preserves its counterpart");
            const auto closing_channel = channels.open(17, script_context.item, {}, other_region);
            auto closing_request = std::async(std::launch::async, [&] {
                return channels.request(closing_channel, {0, "shutdown"}, 3s);
            });
            std::vector<scene::RemoteDataChannels::Request> closing_events;
            const auto closing_deadline = std::chrono::steady_clock::now() + 1s;
            while (closing_events.empty() && std::chrono::steady_clock::now() < closing_deadline) {
                closing_events = channels.take_requests(other_region);
                if (closing_events.empty())
                    std::this_thread::sleep_for(1ms);
            }
            check(closing_events.size() == 1, "request is waiting for a reply before shutdown");
            channels.cancel_region(other_region);
            check(closing_request.wait_for(1s) == std::future_status::ready &&
                      closing_request.get().status == scene::RemoteDataChannels::Status::TimedOut &&
                      !channels.find(closing_channel) && channels.find(reopened),
                  "region shutdown wakes pending callers and preserves channels in other regions");
            check(channels.request(reopened, {0, ""}, 0ms).status == scene::RemoteDataChannels::Status::TimedOut &&
                      channels.take_requests().empty(), "remote data timeout removes undelivered requests");
            std::promise<void> release_slow;
            auto release = release_slow.get_future();
            scene::RemoteDataChannels outbound([&](auto, auto destination, auto& data, std::stop_token) {
                if (destination == "slow")
                    release.wait_for(3s);
                data.text = std::string(destination);
            });
            const auto slow = outbound.send(script_context.item, "", "slow", {0, ""});
            const auto fast = outbound.send(script_context.item, "", "fast", {0, ""});
            std::vector<scene::RemoteDataChannels::Completion> completions;
            const auto fast_deadline = std::chrono::steady_clock::now() + 1s;
            while (completions.empty() && std::chrono::steady_clock::now() < fast_deadline) {
                completions = outbound.take_completed();
                if (completions.empty())
                    std::this_thread::sleep_for(1ms);
            }
            release_slow.set_value();
            check(slow != fast && completions.size() == 1 && completions.front().message == fast &&
                      completions.front().data.text == "fast", "slow remote data sends do not block other requests");
            std::promise<void> started;
            std::promise<void> stopped;
            auto started_signal = started.get_future();
            auto stopped_signal = stopped.get_future();
            scene::RemoteDataChannels cancellable([&](auto, auto, auto&, std::stop_token stop) {
                started.set_value();
                const auto deadline = std::chrono::steady_clock::now() + 2s;
                while (!stop.stop_requested() && std::chrono::steady_clock::now() < deadline)
                    std::this_thread::sleep_for(1ms);
                if (stop.stop_requested())
                    stopped.set_value();
            });
            const auto cancelled_channel = cancellable.open(17, script_context.item);
            cancellable.send(script_context.item, "", "pending", {0, ""});
            check(started_signal.wait_for(1s) == std::future_status::ready, "remote data send starts");
            cancellable.cancel_script(script_context.item);
            check(stopped_signal.wait_for(1s) == std::future_status::ready &&
                      !cancellable.find(cancelled_channel) && cancellable.take_completed().empty(),
                  "script cancellation closes its channel and suppresses the outbound reply");
            channel_clock += 10000000;
            check(instance.run(100).status == LslRunStatus::Idle, "remote data close observes its delay");
        }
        scene::ScriptMailbox limited_mailbox(1, 1);
        const auto throttle_start = scene::ScriptMailbox::Clock::now();
        const auto sender_id = context().owner;
        test(limited_mailbox.allow_owner(sender_id, throttle_start) &&
                 limited_mailbox.allow_owner(sender_id, throttle_start) &&
                 !limited_mailbox.allow_owner(sender_id, throttle_start) &&
                 limited_mailbox.allow_owner(sender_id, throttle_start + std::chrono::hours(1)),
             "script mail owner throttle preserves the original first-send allowance and refill");
        test(limited_mailbox.allow_recipient("prim", throttle_start) &&
                 limited_mailbox.allow_recipient("prim", throttle_start) &&
                 !limited_mailbox.allow_recipient("prim", throttle_start) &&
                 limited_mailbox.allow_recipient("other", throttle_start),
             "script mail recipient limits are independent");
        scene::ScriptMailbox smtp_mailbox(500, 50, 4, 1);
        const auto smtp_start = scene::ScriptMailbox::Clock::now();
        test(smtp_mailbox.allow_smtp("recipient", smtp_start) &&
                 smtp_mailbox.allow_smtp("recipient", smtp_start) &&
                 !smtp_mailbox.allow_smtp("recipient", smtp_start) &&
                 smtp_mailbox.allow_smtp("second recipient", smtp_start) &&
                 !smtp_mailbox.allow_smtp("third recipient", smtp_start + std::chrono::hours(24)),
             "SMTP charges the daily limit before the recipient limit and preserves elapsed-time arithmetic");
        const auto email_program = compile_lsl(R"LSL(
            default {
                state_entry() { llGetNextEmail("sender", "subject"); }
                email(string stamp, string sender, string subject, string body, integer remaining) {
                    llSay(0, stamp + ":" + sender + ":" + subject + ":" + body + ":" + (string)remaining);
                }
            }
        )LSL");
        test(bool(email_program), "typed email handler compiles");
        const auto email_context = context();
        auto email_host = host;
        bool mailbox_requested = false;
        email_host.request_email = [&](auto object, std::string address, std::string subject) {
            mailbox_requested = object == email_context.object && address == "sender" && subject == "subject";
        };
        LslInstance email_script(email_program.program, email_context, email_host);
        test((email_script.event_mask() & (1ull << 9)) != 0 &&
                 !email_script.post_event({"email", {"123", "sender", "subject", "body", "2"}, {}}) &&
                 email_script.post_event({"email", {"123", "sender", "subject", "body", std::int32_t{2}}, {}}),
             "email exposes its source event bit and validates its typed arguments");
        test(email_script.run(100).status == LslRunStatus::Idle && mailbox_requested &&
                 messages.back() == "123:sender:subject:body:2",
             "email dispatch preserves all five fields");
        const auto email_send_program = compile_lsl(
            "default{state_entry(){llEmail(\"sender\",\"subject\",\"body\");}}");
        test(bool(email_send_program), "llEmail compiles");
        bool email_queued = false;
        email_host.send_email = [&](auto object, auto item, auto recipient, std::string subject,
                                    std::string body) {
            email_queued = object == email_context.object && item == email_context.item &&
                           recipient == decltype(recipient){"sender"} && subject == "subject" && body == "body";
        };
        LslInstance email_sender(email_send_program.program, email_context, email_host);
        test(email_sender.run(100).status == LslRunStatus::Sleeping && email_queued,
             "llEmail schedules delivery and sleeps");
        constexpr std::string_view idle_source = R"lsl(
            integer counter = 1;
            default {
                state_entry() { llSay(0, "startup"); }
            }
            state zed {
                timer() { llSay(0, (string)counter); }
            }
            state alpha {
                timer() { llSay(0, "wrong"); }
            }
        )lsl";
        const auto idle_program = compile_lsl(idle_source);
        const auto idle_wire = unhex(
            "0c10000001000000ffffffff090278610000000009026368000000000902646f000000000902696e"
            "010000000c2a00000009026c690000000009026f62000000000902726f000000000902737400000000"
            "09027665000000000902736300000000090464655b5d0000000000");
        const auto idle_bytes = std::span(reinterpret_cast<const std::uint8_t*>(idle_wire.data()), idle_wire.size());
        const auto idle_snapshot = decode_yengine_snapshot(idle_bytes);
        test(bool(idle_program) && idle_snapshot && idle_snapshot->state_index == 1 &&
                 !decode_yengine_snapshot(idle_bytes.first(idle_bytes.size() - 1)),
             "YEngine v12 idle snapshot decodes and rejects truncated state");
        LslInstance idle_script(idle_program.program, email_context, email_host);
        test(!idle_script.restore_yengine_snapshot(*idle_snapshot, "00000000000000000000000000000000", idle_source) &&
                 idle_script.restore_yengine_snapshot(*idle_snapshot, "9403F8233FCB2421F4E7983DA100DCC2", idle_source) &&
                 idle_script.queued_events() == 0 && idle_script.post_event({"timer", {}, {}}) &&
                 idle_script.run(100).status == LslRunStatus::Idle && messages.back() == "42",
             "YEngine idle restore verifies source and resumes the saved named state and globals without startup");

        constexpr std::string_view frame_free_source = R"lsl(
            integer counter = 1;
            default {
                state_entry() { llSay(0, "unexpected startup"); }
                state_exit() { llSay(0, "exit:" + (string)counter); }
            }
            state zed {
                state_entry() { llSay(0, "entered"); }
                dataserver(key query, string data) { llSay(0, data + (string)counter); }
            }
        )lsl";
        constexpr std::string_view frame_free_hash = "D7DA66FCE9F3A2DE17A0F666DC0EBBA7";
        const auto frame_free_program = compile_lsl(frame_free_source);
        const auto decode_snapshot = [](std::string_view hex_snapshot) {
            const auto wire = unhex(hex_snapshot);
            return decode_yengine_snapshot(
                {reinterpret_cast<const std::uint8_t*>(wire.data()), wire.size()});
        };
        const auto transition = decode_snapshot(
            "0c1000000000000001000000090278610000000009026368000000000902646f000000000902696e"
            "010000000c2a00000009026c690000000009026f62000000000902726f000000000902737400000000"
            "09027665000000000902736300000000090464655b5d00000000102a2a6e65775374617465436f6465"
            "2a2a0100000000");
        const auto active_reply = decode_snapshot(
            "0c1009026f62020000000d2463666162313233342d313233342d343332312d313233342d3132333435"
            "363738396162630d07616e737765723a00010000000800000009027861000000000902636800000000"
            "0902646f000000000902696e010000000c2a00000009026c690000000009026f62000000000902726f"
            "00000000090273740000000009027665000000000902736300000000090464655b5d0000000000");
        test(bool(frame_free_program) && transition && active_reply,
             "YEngine frame-free event and transition snapshots decode");
        LslInstance changing_state(frame_free_program.program, email_context, email_host);
        const auto previous_messages = messages.size();
        test(changing_state.restore_yengine_snapshot(*transition, frame_free_hash, frame_free_source) &&
                 changing_state.run(100).status == LslRunStatus::Idle &&
                 messages.size() == previous_messages + 2 &&
                 messages[previous_messages] == "exit:42" && messages.back() == "entered" &&
                 changing_state.checkpoint()->state == "zed",
             "YEngine state transition runs state_exit and enters the saved destination");
        LslInstance receiving_reply(frame_free_program.program, email_context, email_host);
        test(receiving_reply.restore_yengine_snapshot(*active_reply, frame_free_hash, frame_free_source) &&
                 receiving_reply.run(100).status == LslRunStatus::Idle && messages.back() == "answer:42",
             "YEngine frame-free dataserver resumes with its saved arguments and globals");

        constexpr std::string_view reset_source = R"lsl(
            integer counter = 1;
            list values = [3, 4];
            default {
                state_entry() { llSay(0, (string)counter + ":" + (string)llGetListLength(values)); }
            }
        )lsl";
        constexpr std::string_view reset_hash = "8379A48ED4C8EF7B6498C7E6011BCB86";
        const auto reset_snapshot_program = compile_lsl(reset_source);
        const auto reset_snapshot = decode_snapshot(
            "0c100001000000001e000000090278610000000009026368000000000902646f000000000902696e"
            "010000000c2a00000009026c69010000000009026f62000000000902726f0000000009027374000000"
            "0009027665000000000902736300000000090464655b5d0000000000");
        test(bool(reset_snapshot_program) && reset_snapshot && reset_snapshot->initialize_globals,
             "YEngine reset snapshot preserves pending global initialization and null list slots");
        for (const bool active_entry : {true, false}) {
            auto saved = *reset_snapshot;
            if (!active_entry)
                saved.event_code = -1;
            LslInstance resetting(reset_snapshot_program.program, email_context, email_host);
            test(resetting.restore_yengine_snapshot(saved, reset_hash, reset_source),
                 "YEngine reset snapshot restores before startup");
            if (!active_entry)
                resetting.restore_queued_events({{"state_entry", {}, {}}});
            const auto before = messages.size();
            test(resetting.run(1).status == LslRunStatus::BudgetExhausted &&
                     resetting.checkpoint()->initialized_globals == 1 && messages.size() == before &&
                     resetting.run(100).status == LslRunStatus::Idle && messages.size() == before + 1 &&
                     messages.back() == "1:2",
                 "YEngine globals initialize once before the active or queued startup event");
        }

        const auto listen_control = compile_lsl(R"lsl(
            default {
                state_entry() {
                    llListenControl(19, TRUE);
                    llListenRemove(19);
                }
            }
        )lsl");
        const auto region_size_query = compile_lsl(R"lsl(
            default {
                state_entry() {
                    vector size = osGetRegionSize();
                    llSay(0, (string)((size.x == 512) && (size.y == 768) && (size.z == 0)));
                }
            }
        )lsl");
        test(bool(region_size_query), "osGetRegionSize compiles as a vector query");
        auto size_context = email_context;
        size_context.region_size = {512., 768., 0.};
        auto size_host = email_host;
        size_host.ossl_enabled = true;
        LslInstance varregion_size(region_size_query.program, size_context, size_host);
        test(varregion_size.run(100).status == LslRunStatus::Idle && messages.back() == "1",
             "osGetRegionSize reports independent horizontal axes");
        size_host.ossl_enabled = false;
        LslInstance disabled_region_size(region_size_query.program, size_context, size_host);
        test(disabled_region_size.run(100).status == LslRunStatus::Faulted,
             "osGetRegionSize honors the global OSSL switch");
        const auto discarded_queries = compile_lsl(R"lsl(
            default {
                state_entry() {
                    integer count;
                    osGetRegionSize();
                    osGetScriptEngineName();
                    osGetGridName();
                    osGetGridNick();
                    osSHA256((string)++count);
                    (count += 2);
                    llGetPos();
                    llSay(0, (string)count);
                }
            }
        )lsl");
        test(bool(discarded_queries) &&
                 !compile_lsl("default { state_entry() { missingFunction(); } }"),
             "discarded expressions accept known functions and reject unknown calls");
        size_host.ossl_enabled = true;
        size_host.ossl_permission_error = [](auto, auto, auto) { return std::string{}; };
        LslInstance discard_results(discarded_queries.program, size_context, size_host);
        test(discard_results.run(100).status == LslRunStatus::Idle && messages.back() == "3",
             "discarded return values preserve expression side effects");
        size_host.ossl_permission_error = [](auto, auto, std::string_view function) {
            return function == "osGetScriptEngineName" ? std::string("permission denied") : std::string{};
        };
        LslInstance discard_denied(discarded_queries.program, size_context, size_host);
        test(discard_denied.run(100).status == LslRunStatus::Faulted,
             "discarded OSSL calls still enforce permissions");
        test(bool(listen_control), "listener control and removal compile");
        std::vector<std::optional<bool>> listener_actions;
        email_host.change_listener = [&](auto item, std::int32_t handle, std::optional<bool> active) {
            if (item == email_context.item && handle == 19)
                listener_actions.push_back(active);
        };
        LslInstance listener_script(listen_control.program, email_context, email_host);
        test(listener_script.run(100).status == LslRunStatus::Idle &&
                 listener_actions == std::vector<std::optional<bool>>{true, std::nullopt},
             "listener control enables the handle and removal closes it");

        const auto regex_listener = compile_lsl(R"lsl(
            default {
                state_entry() {
                    integer handle = osListenRegex(19, "^Speaker$", NULL_KEY, "[0-9]+",
                        OS_LISTEN_REGEX_NAME | OS_LISTEN_REGEX_MESSAGE);
                    llSay(0, (string)handle);
                    llSay(0, (string)osListenRegex(19, "", "invalid UUID", "", 0));
                    osListenRegex(19, "[", NULL_KEY, "", OS_LISTEN_REGEX_NAME);
                }
            }
        )lsl");
        test(bool(regex_listener), "osListenRegex compiles with both flag constants");
        auto regex_host = host;
        std::vector<std::string> regex_messages;
        regex_host.chat = [&](const auto& chat) { regex_messages.push_back(chat.text); };
        int regex_registrations = 0;
        regex_host.add_listener = [&](auto item, auto part, int channel, std::string name,
                                      auto sender, std::string message, int flags) {
            ++regex_registrations;
            test(item == email_context.item && part == email_context.object && channel == 19 &&
                     name == "^Speaker$" && sender.is_zero() && message == "[0-9]+" && flags == 3,
                 "osListenRegex preserves listener filters and flags");
            return 42;
        };
        regex_host.ossl_permission_error = [](auto, auto, auto) { return std::string{}; };
        LslInstance regex_script(regex_listener.program, email_context, regex_host);
        test(regex_script.run(100).status == LslRunStatus::Sleeping && regex_registrations == 1 &&
                 regex_messages == std::vector<std::string>{"42", "-1", "Name regex is invalid."},
             "osListenRegex returns its handle and rejects invalid keys and patterns");
        regex_host.ossl_permission_error = [](auto, auto, auto function) {
            return std::string(function) + " permission denied";
        };
        LslInstance denied_listener(regex_listener.program, email_context, regex_host);
        const auto denied_result = denied_listener.run(100);
        test(denied_result.status == LslRunStatus::Faulted && regex_registrations == 1,
             "osListenRegex applies OSSL permissions before registering");

        const auto regex_match = compile_lsl(R"lsl(
            default {
                state_entry() {
                    llSay(0, (string)osRegexIsMatch("prefix 42", "(?<=prefix )[0-9]+"));
                    llSay(0, (string)osRegexIsMatch("prefix 42", "^[0-9]+$"));
                    llSay(0, (string)osRegexIsMatch("", "^$"));
                    integer invalid = osRegexIsMatch("text", "[");
                    llSay(0, (string)invalid);
                }
            }
        )lsl");
        test(bool(regex_match), "osRegexIsMatch compiles");
        std::int64_t regex_clock = 0;
        regex_host.clock_ticks = [&] { return regex_clock; };
        regex_host.ossl_permission_error = [&](auto, auto, std::string_view function) {
            test(function == "osRegexIsMatch", "osRegexIsMatch uses its own OSSL permission");
            return std::string{};
        };
        regex_messages.clear();
        LslInstance regex_match_script(regex_match.program, email_context, regex_host);
        test(regex_match_script.run(100).status == LslRunStatus::Sleeping &&
                 regex_messages == std::vector<std::string>{
                     "1", "0", "1", "Possible invalid regular expression detected."},
             "osRegexIsMatch searches, handles empty input, and reports invalid patterns");
        regex_clock += 10000000;
        test(regex_match_script.run(100).status == LslRunStatus::Idle && regex_messages.back() == "0",
             "osRegexIsMatch returns false for an invalid pattern");

        const auto regex_strings = compile_lsl(R"lsl(
            default {
                state_entry() {
                    string source = llChar(128512) + "a1 a2";
                    llSay(0, llDumpList2String(osMatchString(source, "(a)([0-9])", -2), "|"));
                    llSay(0, llDumpList2String(osMatchString("a", "(b)?(a)", 0), "|"));
                    llSay(0, llDumpList2String(osMatchString("ab", "(?=.)", 0), "|"));
                    llSay(0, (string)llGetListLength(osMatchString("ab", "[", 2)));
                    llSay(0, osReplaceString("a1 a2", "(?<n>[0-9])", "<${n}>", 1, -2));
                    llSay(0, osReplaceString("a1 a2", "[0-9]", "x", -1, 0));
                    llSay(0, osReplaceString("ab", "", "-", -1, 0));
                    llSay(0, osReplaceString("ab", ".", "x", 0, 0));
                    llSay(0, osReplaceString("ab", "[", "x", 2, 2));
                }
            }
        )lsl");
        test(bool(regex_strings), "OSSL regex matching and replacement compile");
        regex_host.ossl_permission_error = [&](auto, auto, std::string_view function) {
            test(function == "osMatchString" || function == "osReplaceString",
                 "OSSL regex string operations use their own permission rules");
            return std::string{};
        };
        regex_messages.clear();
        LslInstance regex_strings_script(regex_strings.program, email_context, regex_host);
        test(regex_strings_script.run(100).status == LslRunStatus::Idle &&
                 regex_messages == std::vector<std::string>{
                     "a2|5|a|5|2|6", "a|0|a|0", "|0||1", "0",
                     "a1 a<2>", "ax ax", "-a-b-", "ab", "ab"},
             "OSSL regex string operations preserve captures, UTF-16 offsets, zero-width matches, and counts");

        const auto remote_load = compile_lsl(R"lsl(
            default {
                state_entry() {
                    llSetRemoteScriptAccessPin(-17);
                    llRemoteLoadScriptPin(llGetKey(), "Payload", -17, TRUE, 22);
                    llRemoteLoadScriptPin((key)"01234567-89ab-cdef-0123-456789abcdef", "Payload", -17, FALSE, 42);
                }
            }
        )lsl");
        test(bool(remote_load), "remote script loading accepts a key and signed PIN");
        auto remote_host = host;
        std::vector<std::int32_t> remote_delays;
        remote_host.request_sleep = [&](auto delay) { remote_delays.push_back(delay); };
        bool pin_set = false;
        remote_host.set_remote_script_pin = [&](auto object, auto pin) {
            pin_set = object == email_context.object && pin == -17;
        };
        int remote_attempts = 0;
        remote_host.remote_load_script = [&](auto source, auto target, const auto& name, auto pin,
                                             bool running, auto start_parameter) {
            ++remote_attempts;
            test(source == email_context.object && target.str() == "01234567-89ab-cdef-0123-456789abcdef" &&
                     name == "Payload" && pin == -17 && !running && start_parameter == 42,
                 "remote load passes the destination, PIN, running flag and start parameter");
            return LslRemoteScriptLoad::Attempted;
        };
        LslInstance remote_script(remote_load.program, email_context, remote_host);
        test(remote_script.run(100).status == LslRunStatus::Sleeping && pin_set && remote_attempts == 1 &&
                 remote_delays == std::vector<std::int32_t>{3000},
             "remote load ignores the same prim and applies a three-second delay to an attempt");
        const auto control_event_program = compile_lsl(R"LSL(
            default {
                control(key avatar, integer held, integer changed) {
                    llSay(0, (string)avatar + ":" + (string)(held & CONTROL_FWD)
                        + ":" + (string)(changed & CONTROL_ROT_LEFT));
                }
            }
        )LSL");
        test(bool(control_event_program), "typed control handler and control constants compile");
        LslInstance control_script(control_event_program.program, context(), host);
        test((control_script.event_mask() & (1ull << 7)) != 0 &&
                 !control_script.post_event({"control", {query, std::int32_t{1}}, {}}) &&
                 !control_script.post_event({"control", {std::int32_t{1}, std::int32_t{1}, std::int32_t{256}}, {}}) &&
                 control_script.post_event({"control", {query, std::int32_t{1}, std::int32_t{256}}, {}}),
             "control exposes its event bit and validates all three arguments");
        test(control_script.run(100).status == LslRunStatus::Idle && messages.back() == query + ":1:256",
             "control dispatch preserves the avatar key and both bitmasks");
        auto cloud = compile_lsl(
            "default{state_entry(){llSay(0,(string)(llCloud(<10,-20,30>)+llCloud(ZERO_VECTOR)));}}");
        test(bool(cloud), "cloud query compiles");
        LslInstance cloud_script(cloud.program, context(), host);
        test(cloud_script.run(100).status == LslRunStatus::Idle && messages.back() == "0.000000",
             "cloud query returns the source zero for both offset forms");
        auto ground = compile_lsl(
            R"LSL(
                default {
                    state_entry() {
                        vector offset = <1, -2, 999>;
                        llSay(0, (string)llGround(offset + <.5, 0, 0>));
                        llSay(0, (string)llGround(ZERO_VECTOR));
                    }
                }
            )LSL");
        test(bool(ground), "ground query with numeric offsets compiles");
        auto terrain_context = context();
        terrain_context.position = {1.f, 2.f, 3.f};
        auto terrain_host = host;
        terrain_host.object_position = [](auto, bool, bool) {
            return std::optional{framework::Vector3{20.f, 30.f, 40.f}};
        };
        std::vector<std::pair<float, float>> sampled;
        terrain_host.ground_height = [&](float x, float y) {
            sampled.emplace_back(x, y);
            return x + y;
        };
        LslInstance ground_script(ground.program, terrain_context, terrain_host);
        const auto messages_before_ground = messages.size();
        test(ground_script.run(100).status == LslRunStatus::Idle &&
                 sampled == std::vector<std::pair<float, float>>{{21.5f, 28.f}, {20.f, 30.f}} &&
                 messages.size() == messages_before_ground + 2 &&
                 messages[messages_before_ground] == "49.500000" && messages.back() == "50.000000",
             "ground query samples object position plus XY offsets and ignores Z");
        const auto modify_land = compile_lsl(R"lsl(
            default {
                state_entry() {
                    llModifyLand(LAND_RAISE, LAND_SMALL_BRUSH);
                    llModifyLand(256 + LAND_REVERT, 256 + LAND_LARGE_BRUSH);
                }
            }
        )lsl");
        test(bool(modify_land), "terrain edits compile with standard action and brush constants");
        std::vector<std::tuple<framework::Uuid, std::uint8_t, std::uint8_t>> terrain_edits;
        terrain_host.modify_land = [&](auto object, auto action, auto brush) {
            terrain_edits.emplace_back(object, action, brush);
        };
        LslInstance modify_land_script(modify_land.program, terrain_context, terrain_host);
        test(modify_land_script.run(100).status == LslRunStatus::Idle &&
                 terrain_edits == decltype(terrain_edits){{terrain_context.object, 1, 1},
                                                        {terrain_context.object, 5, 3}},
             "terrain edits pass the host object and narrow action and brush to bytes");
        auto targets = compile_lsl(
            "default{state_entry(){integer id=llTarget(<10,20,30>,2);llTargetRemove(id);state next;}}"
            "state next{state_entry(){}}");
        test(bool(targets), "position target calls compile");
        auto target_host = host;
        const auto waypoint_context = context();
        bool registered = false;
        bool removed = false;
        int target_clears = 0;
        target_host.register_position_target = [&](auto object, auto item, auto target, float range) {
            registered = object == waypoint_context.object && item == waypoint_context.item &&
                         target == framework::Vector3{10, 20, 30} && range == 2.f;
            return 71;
        };
        target_host.remove_position_target = [&](auto object, std::int32_t id) {
            removed = registered && object == waypoint_context.object && id == 71;
        };
        target_host.clear_targets = [&](auto item) {
            if (item == waypoint_context.item)
                ++target_clears;
        };
        LslInstance waypoint_script(targets.program, waypoint_context, target_host);
        test(waypoint_script.run(100).status == LslRunStatus::Idle && removed && target_clears == 1,
             "position target calls pass script identity and state change clears registrations");
        waypoint_script.reset();
        test(target_clears == 2, "script reset clears target registrations");
        auto wind = compile_lsl(
            "default{state_entry(){llSay(0,(string)(llWind(<-1.75,2.75,999>) == <14,257,0>));}}");
        test(bool(wind), "wind query compiles");
        auto wind_host = host;
        wind_host.object_position = [](auto, bool, bool) {
            return std::optional{framework::Vector3{16.f, 255.f, 40.f}};
        };
        wind_host.wind = [](std::int32_t x, std::int32_t y) {
            return framework::Vector3{static_cast<float>(x), static_cast<float>(y), 7.f};
        };
        LslInstance wind_script(wind.program, terrain_context, wind_host);
        test(wind_script.run(100).status == LslRunStatus::Idle && messages.back() == "1",
             "wind query uses live world position, truncates XY, and ignores Z");
        auto environment = compile_lsl(
            "default{state_entry(){llSay(0,(string)(llGetDayLength() == 9000 && "
            "llGetRegionMoonRotation() == <0,0,1,0> && "
            "llGetSunDirection() == <1,0,0>));}}");
        test(bool(environment), "environment queries compile");
        auto environment_host = terrain_host;
        environment_host.environment_timing = [](framework::Vector3 position, bool region) {
            return std::pair{!region && position.x == 20.f && position.y == 30.f ? 9000 : 0, 0};
        };
        environment_host.environment_rotation = [](framework::Vector3 position, bool region, bool moon) {
            if (!region && !moon)
                return std::optional{LslRotation{0, 0.0009, 0, 0}};
            return std::optional{region && moon && position.z == 40.f
                                     ? LslRotation{0, 0, 1, 0} : LslRotation{}};
        };
        LslInstance environment_script(environment.program, terrain_context, environment_host);
        test(environment_script.run(100).status == LslRunStatus::Idle && messages.back() == "1",
             "environment queries use live parcel position and live altitude for region rotation");
        scene::Terrain terrain(32, 32);
        terrain.set(20, 30, 10.f);
        terrain.set(22, 29, 12.f);
        terrain.set(21, 30, 13.f);
        terrain.set(21, 29, 50.f);
        terrain.set(19, 30, 9.9995f);
        terrain.set(18, 31, 10.f);
        terrain_host.ground_normal = [&](float x, float y, float offset_x, float offset_y) {
            return terrain.script_ground_normal(x, y, offset_x, offset_y);
        };
        auto terrain_vectors = compile_lsl(R"LSL(
            default {
                state_entry() {
                    vector offset = <1.5, -0.5, 999>;
                    llSay(0, (string)(llVecDist(llGroundNormal(offset), llVecNorm(<-2,-3,1>)) < .00001));
                    llSay(0, (string)(llVecDist(llGroundSlope(offset), llVecNorm(<-2,-3,-13>)) < .00001));
                    llSay(0, (string)(llVecDist(llGroundContour(offset), <3,-2,0>/llVecMag(<-2,-3,-13>)) < .00001));
                    vector edge = <100,100,0>;
                    llSay(0, (string)(llGroundNormal(edge) == <0,0,1>));
                    llSay(0, (string)(llGroundSlope(edge) == ZERO_VECTOR));
                    llSay(0, (string)(llGroundSlope(<-2,0,0>) == ZERO_VECTOR));
                }
            }
        )LSL");
        test(bool(terrain_vectors), "terrain vector queries compile");
        LslInstance terrain_vectors_script(terrain_vectors.program, terrain_context, terrain_host);
        const auto terrain_vector_start = messages.size();
        test(
            terrain_vectors_script.run(200).status == LslRunStatus::Idle &&
                std::vector<std::string>(messages.begin() + terrain_vector_start, messages.end()) ==
                    std::vector<std::string>(6, "1"),
            "terrain vectors use the prim base height, offset neighbors, flat edges, and source slope cutoff");
        auto unix_query = compile_lsl("default{state_entry(){llSay(0,(string)(llGetUnixTime()+1));}}");
        test(bool(unix_query), "Unix time query compiles");
        auto time_host = host;
        time_host.unix_time = [] {
            return std::int32_t{1700000000};
        };
        LslInstance time_script(unix_query.program, context(), time_host);
        test(time_script.run(100).status == LslRunStatus::Idle && messages.back() == "1700000001",
             "Unix time query returns host epoch seconds as an LSL integer");
        auto utc_query =
            compile_lsl("default{state_entry(){llSay(0,llGetDate());llSay(0,llGetTimestamp());}}");
        test(bool(utc_query), "UTC date and timestamp queries compile");
        time_host.clock_ticks = [] {
            return std::int64_t{621355967999999999LL};
        };
        LslInstance utc_script(utc_query.program, context(), time_host);
        const auto utc_start = messages.size();
        test(utc_script.run(100).status == LslRunStatus::Idle && messages.size() == utc_start + 2 &&
                 messages[utc_start] == "1969-12-31" && messages.back() == "1969-12-31T23:59:59.9999999Z",
             "UTC date rolls over before the Unix epoch and retains seven fractional digits");
        auto clock_query = compile_lsl(
            "default{state_entry(){llSay(0,(string)llGetGMTclock());llSay(0,(string)(llGetWallclock()-llGetTimeOfDay()));}}");
        test(bool(clock_query), "GMT and local clock queries compile");
        time_host.clock_ticks = [] {
            return std::int64_t{621356831992500000LL};
        };
        LslInstance clock_script(clock_query.program, context(), time_host);
        const auto clock_start = messages.size();
        test(clock_script.run(100).status == LslRunStatus::Idle && messages.size() == clock_start + 2 &&
                 messages[clock_start] == "86399.250000" && std::stoi(messages.back()) % 14400 == 0,
             "GMT clock preserves fractional seconds and local time follows the four-hour cycle");
        auto elapsed_query = compile_lsl(R"(
            default {
                state_entry() {
                    llSay(1, (string)llGetTime());
                    llSay(1, (string)llGetAndResetTime());
                    llSay(1, (string)llGetTime());
                    llResetTime();
                    llSay(1, (string)llGetTime());
                }
            }
        )");
        test(bool(elapsed_query), "elapsed time queries and reset compile");
        std::int64_t monotonic = 0;
        time_host.monotonic_ticks = [&] {
            return monotonic;
        };
        LslInstance elapsed_script(elapsed_query.program, context(), time_host);
        monotonic = 12500;
        const auto elapsed_start = messages.size();
        elapsed_script.run(1);
        monotonic = 27500;
        elapsed_script.run(1);
        monotonic = 47500;
        elapsed_script.run(1);
        elapsed_script.run(1);
        monotonic = 72500;
        test(elapsed_script.run(1).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + elapsed_start, messages.end()) ==
                     std::vector<std::string>{"0.001000", "0.003000", "0.002000", "0.002000"},
             "elapsed time uses monotonic ticks and both reset paths update its baseline");
        auto order_query = compile_lsl(R"(
            default {
                state_entry() {
                    llSay(1, (string)(llGetTime() - llGetAndResetTime()));
                    llSay(1, (string)llCloud(<0, 0, llGetAndResetTime()>));
                    llSay(1, (string)llGetTime());
                }
            }
        )");
        test(bool(order_query), "clock side effects compile inside expressions and vector arguments");
        monotonic = 0;
        LslInstance order_script(order_query.program, context(), time_host);
        monotonic = 10000000;
        order_script.run(1);
        test(messages.back() == "-1.000000", "binary expressions evaluate the right operand first");
        monotonic = 20000000;
        order_script.run(1);
        test(order_script.run(1).status == LslRunStatus::Idle && messages.back() == "0.000000",
             "unused terrain vector components still evaluate side effects");
        const auto camera_query = compile_lsl(R"(
            default {
                state_entry() {
                    llSay(1, (string)llGetCameraPos());
                    llSay(1, (string)llGetCameraRot());
                    llSay(1, (string)llGetCameraFOV());
                    llSay(1, (string)llGetCameraAspect());
                }
            }
        )");
        test(bool(camera_query), "camera position query compiles");
        LslHost camera_host;
        std::vector<LslChat> camera_chat;
        camera_host.chat = [&](const LslChat& chat) {
            camera_chat.push_back(chat);
        };
        const auto granter = context().owner;
        std::pair<opensim::framework::Uuid, std::int32_t> camera_permissions;
        camera_host.script_permissions = [&](auto, auto) {
            return camera_permissions;
        };
        std::vector<opensim::framework::Uuid> camera_lookups;
        bool camera_present = true;
        camera_host.camera = [&](auto id) -> std::optional<LslCamera> {
            camera_lookups.push_back(id);
            if (!camera_present)
                return std::nullopt;
            return LslCamera{{10, 20, 30}, {0, 0, 1, 0}, 1.25f, 1.5f};
        };
        const auto camera_messages = [&] {
            std::vector<std::string> result;
            for (const auto& chat : camera_chat)
                result.push_back(chat.text);
            return result;
        };
        const std::vector<std::string> defaults{
            "<0.000000, 0.000000, 0.000000>", "<0.000000, 0.000000, 0.000000, 1.000000>",
            "0.000000", "1.000000"};
        LslInstance no_granter(camera_query.program, context(), camera_host);
        no_granter.run(100);
        test(camera_messages() == defaults && camera_lookups.empty(),
             "camera queries without a granter return their defaults without looking up an avatar");

        camera_chat.clear();
        camera_permissions = {granter, 0};
        LslInstance denied(camera_query.program, context(), camera_host);
        denied.run(100);
        test(!camera_chat.empty() && camera_chat.front().type == LslChatType::Debug &&
                 camera_chat.front().text == "llGetCameraPos: No permissions to track the camera" &&
                 camera_lookups.empty(),
             "camera queries without tracking permission report a script error");

        camera_chat.clear();
        camera_permissions.second = 0x400;
        LslInstance granted(camera_query.program, context(), camera_host);
        granted.run(100);
        test(camera_messages() == std::vector<std::string>{
                 "<10.000000, 20.000000, 30.000000>", "<0.000000, 0.000000, 1.000000, 0.000000>",
                 "1.250000", "1.500000"} && camera_lookups == std::vector(4, granter),
             "camera queries use the avatar that granted tracking permission");

        camera_chat.clear();
        camera_present = false;
        LslInstance departed(camera_query.program, context(), camera_host);
        departed.run(100);
        auto departed_defaults = defaults;
        departed_defaults[2] = "1.400000";
        test(camera_messages() == departed_defaults,
             "camera queries use the managed fallback values after the avatar departs");

        const auto health_query = compile_lsl(R"(
            default {
                state_entry() {
                    llSay(1, (string)llGetHealth(llGetOwner()));
                    llSay(1, (string)llGetHealth(NULL_KEY));
                    llSay(1, (string)llGetHealth("invalid key"));
                }
            }
        )");
        test(bool(health_query), "avatar health query compiles");
        const auto health_context = context();
        camera_host.agent_health = [&](auto id) -> std::optional<float> {
            if (id == health_context.owner)
                return 72.5f;
            return std::nullopt;
        };
        camera_chat.clear();
        LslInstance health_instance(health_query.program, health_context, camera_host);
        health_instance.run(100);
        test(camera_messages() == std::vector<std::string>{"72.500000", "-1.000000", "-1.000000"},
             "health queries return the current health or the missing-avatar sentinel");

        const auto hud_projection = compile_lsl(R"(
            default {
                state_entry() {
                    llSay(1, (string)llWorldPosToHUD(<20, 24, 32>));
                    llSay(1, (string)llWorldPosToHUD(<0, 24, 32>));
                }
            }
        )");
        test(bool(hud_projection), "HUD projection compiles");
        auto hud_context = context();
        hud_context.owner = granter;
        camera_host.camera = [&](auto id) -> std::optional<LslCamera> {
            test(id == hud_context.owner, "HUD projection looks up the owner");
            return LslCamera{{10, 20, 30}, {}, 1.57079632679f, 1.5f,
                             {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        };
        camera_chat.clear();
        LslInstance world_projection(hud_projection.program, hud_context, camera_host);
        world_projection.run(100);
        test(camera_messages() == std::vector(2, std::string("<0.000000, 0.000000, 0.000000>")),
             "HUD projection returns zero outside a HUD attachment");

        hud_context.attachment_point = 34;
        camera_chat.clear();
        LslInstance hud_projection_granted(hud_projection.program, hud_context, camera_host);
        hud_projection_granted.run(100);
        test(camera_messages() == std::vector<std::string>{
                 "<1.000000, -0.570000, -0.410000>", "<-1.000000, 3.250000, 1.500000>"},
             "HUD projection applies the top-left anchor and preserves behind-camera coordinates");

        const auto media_query = compile_lsl(R"(
            default {
                state_entry() {
                    llSay(1, llDumpList2String(llGetPrimMediaParams(0,
                        [PRIM_MEDIA_CURRENT_URL, PRIM_MEDIA_WHITELIST, PRIM_MEDIA_PERMS_INTERACT]), "|"));
                    llSay(1, llDumpList2String(llGetLinkMedia(2, 1, [PRIM_MEDIA_WIDTH_PIXELS]), "|"));
                    llSay(1, llDumpList2String(llGetLinkMedia(LINK_ROOT, 0, [999]), "|"));
                    llSay(1, llDumpList2String(llGetPrimMediaParams(-1, [PRIM_MEDIA_CURRENT_URL]), "|"));
                }
            }
        )");
        test(bool(media_query), "prim and link media queries compile");
        auto media = std::make_shared<framework::ShapeMediaEntry>();
        media->current_url = u"https://example.test";
        media->width = 512;
        media->interact_permissions = 4;
        media->whitelist = std::make_shared<framework::ShapeArray<std::optional<std::u16string>>>(
            std::vector<std::optional<std::u16string>>{u"example.test/a b", u"*other.test"});
        auto media_host = host;
        media_host.script_delay_factor = 0;
        std::vector<std::pair<int, int>> media_targets;
        media_host.prim_media = [&](framework::Uuid object, int link,
                                    int face) -> std::shared_ptr<const framework::ShapeMediaEntry> {
            test(object == context().object, "media query uses the script's prim");
            media_targets.emplace_back(link, face);
            return face < 0 ? nullptr : media;
        };
        const auto media_start = messages.size();
        LslInstance media_script(media_query.program, context(), media_host);
        media_script.run(100);
        test(media_targets == std::vector<std::pair<int, int>>{{-4, 0}, {2, 1}, {1, 0}, {-4, -1}} &&
                 std::vector<std::string>(messages.begin() + media_start, messages.end()) ==
                     std::vector<std::string>{"https://example.test|example.test%2Fa%20b, %2Aother.test|4",
                                              "512", "1000", ""},
             "media queries resolve link selectors, escape whitelist entries and preserve result types");
        auto arithmetic = compile_lsl(R"LSL(
            default {
                state_entry() {
                    llSay(1, (string)(2 + 3 * 4 - 8 / 4 / 2));
                    llSay(1, (string)(0x80000000 / -1));
                    llSay(1, (string)(-5.5 % 2));
                    llSay(1, (string)(1.0 / 0));
                }
            }
        )LSL");
        test(bool(arithmetic), "multiplicative expressions compile");
        LslInstance arithmetic_script(arithmetic.program, context(), host);
        const auto arithmetic_start = messages.size();
        const auto arithmetic_result = arithmetic_script.run(100);
        test(arithmetic_result.status == LslRunStatus::Faulted &&
                 arithmetic_result.error == "Division by Zero" &&
                 std::vector<std::string>(messages.begin() + arithmetic_start, messages.end()) ==
                     std::vector<std::string>{"13", "-2147483648", "-1.500000"},
             "multiplicative precedence, left associativity, integer wrapping, and floating faults");
        auto branches = compile_lsl(R"LSL(
            default {
                touch_start(integer n) {
                    if (n)
                        if (n - 1)
                            llSay(1, "many");
                        else
                            llSay(1, "one");

                    if (n - 1)
                        llSay(1, "other");
                    else {
                        llSay(1, "single");
                    }
                }
            }
        )LSL");
        test(bool(branches), "nested conditional statements compile");
        LslInstance branch_script(branches.program, context(), host);
        branch_script.run(100);
        const auto branch_start = messages.size();
        for (auto n : {0, 1, 2}) {
            test(branch_script.post_event({"touch_start", {std::int32_t(n)}, {}}), "conditional event posts");
            while (branch_script.run(1).status == LslRunStatus::BudgetExhausted) {
            }
        }
        test(std::vector<std::string>(messages.begin() + branch_start, messages.end()) ==
                 std::vector<std::string>{"other", "one", "single", "many", "other"},
             "branches bind else to the nearest if and resume across instruction budgets");
        auto loops = compile_lsl(R"LSL(
            default {
                state_entry() {
                    while (0)
                        llSay(1, "skipped");

                    do
                        llSay(1, "once");
                    while (0);

                    while (llGetTime()) {
                        llSay(1, "again");
                        llResetTime();
                    }
                }
            }
        )LSL");
        test(bool(loops), "while and do-while compile");
        monotonic = 0;
        LslInstance loop_script(loops.program, context(), time_host);
        monotonic = 10000000;
        const auto loop_start = messages.size();
        auto loop_status = LslRunStatus::BudgetExhausted;
        for (int step = 0; step < 20 && loop_status == LslRunStatus::BudgetExhausted; ++step)
            loop_status = loop_script.run(1).status;
        test(loop_status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + loop_start, messages.end()) ==
                     std::vector<std::string>{"once", "again"},
             "loops test at the correct point and resume after yielding");
        auto endless = compile_lsl("default{state_entry(){while(1);}}");
        test(bool(endless), "empty loop compiles");
        LslInstance endless_script(endless.program, context(), host);
        test(endless_script.run(16).status == LslRunStatus::BudgetExhausted,
             "empty infinite loop respects the instruction budget");
        auto comparisons = compile_lsl(R"LSL(
            default {
                state_entry() {
                    if (1 + 2 >= 3.0)
                        llSay(1, (string)(2 < 3 == 4 > 3));
                    llSay(1, (string)("alpha" != "beta"));
                    llSay(1, (string)llCloud(<1 < 2, 0, (3 > 2)>));
                }
            }
        )LSL");
        test(bool(comparisons), "comparison expressions compile, including inside vector arguments");
        LslInstance comparison_script(comparisons.program, context(), host);
        const auto comparison_start = messages.size();
        test(comparison_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + comparison_start, messages.end()) ==
                     std::vector<std::string>{"1", "1", "0.000000"},
             "comparisons promote numeric operands and produce integer truth values");
        const auto ordinal_source = std::string("default{state_entry(){llSay(1,(string)(") +
                                    quote("\xf0\x90\x80\x80") + "<" + quote("\xee\x80\x80") + "));}}";
        auto ordinal = compile_lsl(ordinal_source);
        test(bool(ordinal), "ordinal string ordering compiles");
        LslInstance ordinal_script(ordinal.program, context(), host);
        test(ordinal_script.run(100).status == LslRunStatus::Idle && messages.back() == "1",
             "ordinal comparisons order UTF-16 code units across the surrogate boundary");
        auto logical = compile_lsl(R"LSL(
            default {
                state_entry() {
                    llSay(1, (string)(1 || 0 && 0));
                    llSay(1, (string)(!0 && "text"));
                    llSay(1, (string)(llGetAndResetTime() || 1));
                    llSay(1, (string)llGetTime());
                }
            }
        )LSL");
        test(bool(logical), "logical expressions compile");
        monotonic = 0;
        LslInstance logical_script(logical.program, context(), time_host);
        monotonic = 10000000;
        const auto logical_start = messages.size();
        test(logical_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + logical_start, messages.end()) ==
                     std::vector<std::string>{"0", "1", "1", "0.000000"},
             "logical operators share precedence and evaluate both operands");
        auto short_logic = compile_lsl(
            "default{"
                "state_entry(){"
                    "llSay(1,(string)(1|||0&&&0));"
                    "llSay(1,(string)(0&&&llGetAndResetTime()));"
                    "llSay(1,(string)llGetTime());"
                    "llSay(1,(string)(llGetAndResetTime()&&&llGetTime()));"
                "}"
            "}");
        test(bool(short_logic), "YEngine short-circuit expressions compile");
        monotonic = 0;
        LslInstance short_script(short_logic.program, context(), time_host);
        monotonic = 10000000;
        const auto short_start = messages.size();
        test(short_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + short_start, messages.end()) ==
                     std::vector<std::string>{"1", "0", "1.000000", "0"},
             "short-circuit expressions honor precedence, skip operands, and evaluate left first");
        auto flags = compile_lsl(R"LSL(
            default {
                state_entry() {
                    llSay(1, (string)(1 | 2 ^ 3 & 1));
                    llSay(1, (string)(-8 >> 33));
                    llSay(1, (string)(1 << -1));
                    llSay(1, (string)~0);
                }
            }
        )LSL");
        test(bool(flags), "bitwise expressions compile");
        LslInstance flag_script(flags.program, context(), host);
        const auto flag_start = messages.size();
        test(flag_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + flag_start, messages.end()) ==
                     std::vector<std::string>{"3", "-4", "-2147483648", "-1"},
             "bitwise precedence, masked shifts, arithmetic right shift, and complement");
        auto early_return = compile_lsl(R"LSL(
            default {
                state_entry() {
                    while (1) {
                        return;
                    }
                    llSay(1, "skipped");
                }
                touch_start(integer n) {
                    if (n)
                        return;
                    llSay(1, "handled");
                }
            }
        )LSL");
        test(bool(early_return), "event return compiles in loops and branches");
        LslInstance return_script(early_return.program, context(), host);
        const auto return_start = messages.size();
        test(return_script.run(10).status == LslRunStatus::Idle && messages.size() == return_start,
             "return exits the entire event from a loop");
        return_script.post_event({"touch_start", {std::int32_t(1)}, {}});
        return_script.post_event({"touch_start", {std::int32_t(0)}, {}});
        test(return_script.run(10).status == LslRunStatus::Idle && messages.size() == return_start + 1 &&
                 messages.back() == "handled",
             "return preserves subsequent queued events");
        test(!compile_lsl("default{state_entry(){return 1;}}"), "events reject return values");
        auto variables = compile_lsl(R"LSL(
            default {
                touch_start(integer n) {
                    integer count;
                    while (count < n) {
                        count = count + 1;
                    }
                    {
                        float count = 2;
                        llSay(1, (string)count);
                    }
                    n = count + 1;
                    llSay(1, (string)n);
                }
            }
        )LSL");
        test(bool(variables), "local declarations and assignment compile");
        LslInstance variable_script(variables.program, context(), host);
        const auto variable_start = messages.size();
        for (auto n : {3, 1}) {
            variable_script.post_event({"touch_start", {std::int32_t(n)}, {}});
            auto status = LslRunStatus::BudgetExhausted;
            for (int step = 0; step < 40 && status == LslRunStatus::BudgetExhausted; ++step)
                status = variable_script.run(1).status;
            test(status == LslRunStatus::Idle, "local variables survive yields and loops terminate");
        }
        test(std::vector<std::string>(messages.begin() + variable_start, messages.end()) ==
                 std::vector<std::string>{"2.000000", "4", "2.000000", "2"},
             "local variables honor block scope and initialize on each event");
        auto for_loop = compile_lsl(
            "default{state_entry(){integer n;integer total;for(n=1,total=0;n<=3;n++) total=total+n;llSay(1,(string)total);for(;;) return;}}");
        test(bool(for_loop), "for loop assignment clauses and omitted clauses compile");
        LslInstance for_script(for_loop.program, context(), host);
        auto for_status = LslRunStatus::BudgetExhausted;
        for (int step = 0; step < 40 && for_status == LslRunStatus::BudgetExhausted; ++step)
            for_status = for_script.run(1).status;
        test(for_status == LslRunStatus::Idle && messages.back() == "6",
             "for initializer, condition, body and update run in order across yields");
        auto increments = compile_lsl(R"LSL(
            default {
                state_entry() {
                    integer n = 2147483647;
                    llSay(1, (string)n++);
                    llSay(1, (string)n);

                    float f = 2;
                    llSay(1, (string)--f);
                    ++f;
                    llSay(1, (string)f--);
                    llSay(1, (string)f);
                }
            }
        )LSL");
        test(bool(increments), "prefix and postfix updates compile");
        LslInstance increment_script(increments.program, context(), host);
        const auto increment_start = messages.size();
        test(
            increment_script.run(100).status == LslRunStatus::Idle &&
                std::vector<std::string>(messages.begin() + increment_start, messages.end()) ==
                    std::vector<std::string>{"2147483647", "-2147483648", "1.000000", "2.000000", "1.000000"},
            "updates return the appropriate value and wrap integer overflow");
        auto assignments = compile_lsl(
            "default{state_entry(){integer a;integer b;float c;llSay(1,(string)(c=a=b=3));if(a=b-3) llSay(1,\"skipped\");llSay(1,(string)(a+b));}}");
        test(bool(assignments), "nested and chained assignments compile");
        LslInstance assignment_script(assignments.program, context(), host);
        const auto assignment_start = messages.size();
        test(assignment_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + assignment_start, messages.end()) ==
                     std::vector<std::string>{"3.000000", "3"},
             "assignments associate right to left and return the converted stored value");
        test(!compile_lsl("default{state_entry(){llSay(1,(string)(1=2));}}"),
             "assignments require a writable variable");
        auto compound = compile_lsl(R"LSL(
            default {
                state_entry() {
                    integer n = 2;
                    integer m = 3;
                    n += m *= 4;
                    n <<= 1;
                    n ^= 3;

                    string s = "a";
                    s += "b";
                    float f = 5;
                    f /= 2;
                    llSay(1, (string)n + "/" + s + "/" + (string)f);
                }
            }
        )LSL");
        test(bool(compound), "compound assignment expressions compile");
        LslInstance compound_script(compound.program, context(), host);
        test(compound_script.run(100).status == LslRunStatus::Idle && messages.back() == "31/ab/2.500000",
             "compound assignments combine arithmetic, bitwise, and string operations");
        auto globals = compile_lsl(
            "integer count=2;float start=count+1;default{state_entry(){llSay(1,(string)start);}touch_start(integer n){count+=n;llSay(1,(string)count);}}");
        test(bool(globals), "script globals compile");
        LslInstance global_script(globals.program, context(), host);
        const auto global_start = messages.size();
        for (int cycle = 0; cycle < 2; ++cycle) {
            global_script.post_event({"touch_start", {std::int32_t(3)}, {}});
            global_script.post_event({"touch_start", {std::int32_t(1)}, {}});
            test(global_script.run(100).status == LslRunStatus::Idle, "globals execute across events");
            global_script.reset();
        }
        test(std::vector<std::string>(messages.begin() + global_start, messages.end()) ==
                 std::vector<std::string>{"3.000000", "5", "6", "3.000000", "5", "6"},
             "globals initialize in order, persist across events, and reinitialize on reset");
        auto explicit_sleep_program = compile_lsl(
            R"LSL(
                default {
                    state_entry() {
                        llSleep(.0259);
                        llSay(1, "awake");
                        llSleep(0);
                        llSay(1, "resumed");
                    }

                    touch_start(integer n) {
                        llSay(1, "touch");
                    }
                }
            )LSL");
        test(bool(explicit_sleep_program), "llSleep compiles");
        std::int64_t explicit_sleep_clock = 0;
        std::vector<std::int32_t> explicit_delays;
        auto explicit_sleep_host = host;
        explicit_sleep_host.clock_ticks = [&] {
            return explicit_sleep_clock;
        };
        explicit_sleep_host.script_delay_factor = 0;
        explicit_sleep_host.request_sleep = [&](auto delay) {
            explicit_delays.push_back(delay);
        };
        LslInstance sleep_script(explicit_sleep_program.program, context(), explicit_sleep_host);
        const auto sleep_start = messages.size();
        test(sleep_script.run(100).status == LslRunStatus::Sleeping &&
                 explicit_delays == std::vector<std::int32_t>{25},
             "explicit sleep truncates milliseconds and ignores the API delay factor");
        sleep_script.post_event({"touch_start", {std::int32_t(1)}, {}});
        explicit_sleep_clock = 249999;
        test(sleep_script.run(100).status == LslRunStatus::Sleeping && messages.size() == sleep_start,
             "queued events cannot wake a sleeping handler early");
        explicit_sleep_clock = 250000;
        test(sleep_script.run(100).status == LslRunStatus::Sleeping && messages.back() == "awake",
             "sleep resumes at the deadline and a zero delay still yields");
        test(sleep_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + sleep_start, messages.end()) ==
                     std::vector<std::string>{"awake", "resumed", "touch"},
             "the interrupted event resumes before queued events");
        auto timer_program = compile_lsl(
            "default{state_entry(){llSetTimerEvent(.1);llSleep(2);}timer(){llSay(1,\"timer\");llSetTimerEvent(0);}}");
        test(bool(timer_program), "timer handler and scheduling compile");
        std::int64_t timer_clock = 0;
        auto timer_host = host;
        timer_host.clock_ticks = [&] {
            return timer_clock;
        };
        LslInstance timer_script(timer_program.program, context(), timer_host);
        const auto timer_messages = messages.size();
        test(timer_script.run(100).status == LslRunStatus::Sleeping, "timer script goes to sleep");
        timer_clock = 4999999;
        timer_script.run(100);
        test(timer_script.queued_events() == 0, "timer observes the minimum interval");
        for (auto time : {5000000, 10000000, 15000000}) {
            timer_clock = time;
            timer_script.run(100);
            test(timer_script.queued_events() == 1, "only one timer event is queued while asleep");
        }
        timer_clock = 20000000;
        test(timer_script.run(100).status == LslRunStatus::Idle && messages.size() == timer_messages + 1 &&
                 messages.back() == "timer" && (timer_script.event_mask() & (1ull << 2)),
             "timer handler runs after sleep with the source event mask");
        timer_clock = 40000000;
        timer_script.run(100);
        test(messages.size() == timer_messages + 1, "zero cancels the recurring timer");
        timer_clock = 0;
        timer_host.min_timer_interval = 0.25f;
        LslInstance configured_timer(timer_program.program, context(), timer_host);
        configured_timer.run(100);
        timer_clock = 2500000;
        configured_timer.run(100);
        test(configured_timer.queued_events() == 1, "the host timer minimum overrides the default");
        auto event_delay = compile_lsl(
            "default{state_entry(){llMinEventDelay(1);}touch(integer n){}touch_start(integer n){}changed(integer n){}timer(){}}");
        test(bool(event_delay), "minimum event delay compiles");
        monotonic = 0;
        LslInstance delayed_events(event_delay.program, context(), time_host);
        delayed_events.run(100);
        const auto post_number = [&](const char* name, int n) {
            return delayed_events.post_event({name, {std::int32_t(n)}, {}});
        };
        test(post_number("touch", 1) && !delayed_events.post_event({"timer", {}, {}}) &&
                 !post_number("changed", 8) && post_number("changed", 1) && !post_number("changed", 0) &&
                 post_number("touch_start", 1),
             "timer, touch, and scale changes share the throttle while other events bypass it");
        monotonic = 10000000;
        test(delayed_events.post_event({"timer", {}, {}}), "timer admits an event at the shared deadline");
        auto jump_program = compile_lsl(
            "default{state_entry(){integer n; jump start;float skipped=2;@start;if(++n<3){jump start;} llSay(1,(string)n+\"/\"+(string)skipped);}}");
        test(bool(jump_program), "forward and backward jumps compile");
        LslInstance jump_script(jump_program.program, context(), host);
        auto jump_status = LslRunStatus::BudgetExhausted;
        for (int step = 0; step < 20 && jump_status == LslRunStatus::BudgetExhausted; ++step)
            jump_status = jump_script.run(1).status;
        test(jump_status == LslRunStatus::Idle && messages.back() == "3/0.000000",
             "jumps yield, resolve outer labels, and preserve typed defaults for skipped locals");
        test(!compile_lsl("default{state_entry(){jump inside;{@inside;}}}"), "jumps into nested blocks fail");
        test(!compile_lsl("default{state_entry(){jump missing;}}"), "undefined labels fail");
        auto reset_program = compile_lsl(
            "integer n=2;default{state_entry(){llSay(1,(string)n);}touch_start(integer count){n=3;llResetScript();llSay(1,\"unreachable\");}}");
        test(bool(reset_program), "llResetScript compiles");
        LslInstance reset_script(reset_program.program, context(), host);
        const auto reset_start = messages.size();
        reset_script.run(100);
        reset_script.post_event({"touch_start", {std::int32_t(1)}, {}});
        reset_script.post_event({"touch_start", {std::int32_t(1)}, {}});
        test(reset_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + reset_start, messages.end()) ==
                     std::vector<std::string>{"2", "2"},
             "self reset abandons the current event, clears queued events and restarts globals");
        auto entry_reset_program =
            compile_lsl("integer n; default { state_entry() { ++n; llResetScript(); llSay(1, (string)n); }"
                        "touch_start(integer count) { llSay(1, \"queued\"); } }");
        test(bool(entry_reset_program), "default state_entry self reset compiles");
        auto entry_reset_host = host;
        int url_releases = 0;
        int permission_clears = 0;
        entry_reset_host.release_script_urls = [&](auto) {
            ++url_releases;
        };
        entry_reset_host.clear_script_permissions = [&](auto) {
            ++permission_clears;
        };
        LslInstance entry_reset(entry_reset_program.program, context(), entry_reset_host);
        const auto entry_reset_start = messages.size();
        entry_reset.post_event({"touch_start", {std::int32_t(1)}, {}});
        test(entry_reset.run(100).status == LslRunStatus::Idle && url_releases == 1 &&
                 permission_clears == 0 &&
                 std::vector<std::string>(messages.begin() + entry_reset_start, messages.end()) ==
                     std::vector<std::string>{"1", "queued"},
             "default state_entry self reset releases URLs and keeps executing with queued events");
        auto controller_program = compile_lsl(
            "default { state_entry() { llSay(1, (string)llGetScriptState(\"Target\"));"
            "llSetScriptState(\"Target\", 0); llSay(1, (string)llGetScriptState(\"Target\"));"
            "llResetOtherScript(\"Target\"); llSay(1, (string)llGetScriptState(\"Target\"));"
            "llSetScriptState(\"Target\", -1); llSay(1, (string)llGetScriptState(\"Target\")); } }");
        auto target_program = compile_lsl("default { state_entry() { llSay(1, \"target entry\"); } }");
        test(bool(controller_program) && bool(target_program), "named script controls compile");
        auto target_context = context();
        target_context.item = framework::Uuid("01234567-89ab-cdef-0123-456789abcdef");
        LslInstance target_script(target_program.program, target_context, host);
        target_script.run(100);
        auto controller_host = host;
        controller_host.find_script = [&](auto object, const auto& name) {
            return object == target_context.object && name == "Target" ? target_context.item
                                                                       : framework::Uuid{};
        };
        controller_host.script_running = [&](auto item) {
            return item == target_context.item && target_script.running();
        };
        controller_host.set_script_running = [&](auto item, bool running) {
            test(item == target_context.item, "named state change targets the matching script");
            target_script.running(running);
        };
        controller_host.reset_other_script = [&](auto item) {
            test(item == target_context.item, "named reset targets the matching script");
            target_script.reset();
        };
        LslInstance controller_script(controller_program.program, context(), controller_host);
        const auto control_start = messages.size();
        const auto controller_status = controller_script.run(100).status;
        const auto target_status = target_script.run(100).status;
        test(controller_status == LslRunStatus::Idle && target_status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + control_start, messages.end()) ==
                     std::vector<std::string>{"1", "0", "0", "1", "target entry"},
             "named reset preserves stopped state and restarting runs the new default entry");
        const auto states = compile_lsl(R"LSL(
            integer count;
            default {
                state_entry() {
                    state active;
                }
                state_exit() {
                    count++;
                    llSay(1, "exit");
                }
            }
            state active {
                state_entry() {
                    llSay(1, (string)count);
                    state active;
                    llSay(1, "skipped");
                }
                touch_start(integer n) {
                    llSay(1, "touch");
                }
            }
        )LSL");
        test(bool(states), "named states compile");
        const auto state_context = context();
        auto state_host = host;
        std::vector<framework::Uuid> cancelled_scripts;
        state_host.cancel_remote_data = [&](framework::Uuid item) {
            cancelled_scripts.push_back(item);
        };
        LslInstance state_script(states.program, state_context, state_host);
        const auto state_start = messages.size();
        test(state_script.run(100).status == LslRunStatus::Idle && state_script.handles_event("touch_start"),
             "state transition updates active handler lookup");
        test(cancelled_scripts == std::vector{state_context.item},
             "only a transition to a different state cancels pending remote data");
        test(std::vector<std::string>(messages.begin() + state_start, messages.end()) ==
                 std::vector<std::string>{"exit", "1"},
             "exit precedes entry, globals survive, and same-state transitions exit the event");
        test(!compile_lsl("default{state_entry(){state missing;}}"), "undefined states fail");
        auto timer_transition = compile_lsl(
            "default{"
                "state_entry(){"
                    "llSetTimerEvent(1);"
                    "llSleep(.75);"
                    "state next;"
                "}"
                "timer(){"
                    "llSay(1,\"old timer\");"
                "}"
            "}"
            "state next{"
                "state_entry(){"
                    "llSay(1,\"entered\");"
                "}"
                "timer(){"
                    "llSay(1,\"new timer\");"
                    "llSetTimerEvent(0);"
                "}"
            "}");
        test(bool(timer_transition), "timer transition script compiles");
        timer_clock = 0;
        LslInstance transitioning_timer(timer_transition.program, context(), timer_host);
        const auto transition_start = messages.size();
        test(transitioning_timer.run(100).status == LslRunStatus::Sleeping,
             "transition waits in the original state");
        timer_clock = 7500000;
        test(transitioning_timer.run(100).status == LslRunStatus::Idle && messages.back() == "entered",
             "new state enters before the timer deadline");
        timer_clock = 10000000;
        test(transitioning_timer.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + transition_start, messages.end()) ==
                     std::vector<std::string>{"entered", "new timer"},
             "state transition retains the timer deadline and routes it to the new handler");
        auto identity = compile_lsl(
            "default{"
                "touch_start(integer n){"
                    "key avatar=llDetectedKey(0);"
                    "if(avatar==llGetOwner())llSay(1,(string)llGetKey());"
                    "if(llDetectedKey(-1))llSay(1,\"invalid\");"
                    "llSay(1,(string)llDetectedKey(n));"
                    "llSay(1,(string)llDetectedTouchFace(0));"
                    "llSay(1,(string)llDetectedTouchFace(n));"
                "}"
            "}");
        test(bool(identity), "object, owner, and detector keys compile");
        auto object_query_program = compile_lsl("default { state_entry() {"
                                                "llSay(1, (string)llGetObjectPermMask(MASK_BASE));"
                                                "llSay(1, (string)llGetObjectPermMask(MASK_OWNER));"
                                                "llSay(1, (string)llGetObjectPermMask(99));"
                                                "llSay(1, (string)llGetObjectLinkKey(llGetKey(), LINK_THIS));"
                                                "llSay(1, (string)llGetObjectLinkKey(\"invalid\", 1)); } }");
        test(bool(object_query_program), "object permission and link queries compile");
        auto object_query_host = host;
        object_query_host.object_permission = [](auto, auto selector) -> std::optional<std::uint32_t> {
            if (selector == 0)
                return 0x8e000u;
            if (selector == 1)
                return 0x42000u;
            return std::nullopt;
        };
        object_query_host.link_key = [](auto id, auto number) {
            return number == -4 ? id : framework::Uuid{};
        };
        LslInstance object_query(object_query_program.program, context(), object_query_host);
        const auto query_start = messages.size();
        test(object_query.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + query_start, messages.end()) ==
                     std::vector<std::string>{"2147483647", "8192", "-1", context().object.str(),
                                              framework::Uuid{}.str()},
             "object queries convert permission bits and preserve LINK_THIS and NULL_KEY behavior");
        auto sit_target_program = compile_lsl("default { state_entry() { llSitTarget(<1,2,3>, <0,0,0,0>);"
                                              "llLinkSitTarget(2, <4,5,6>, <0,.5,0,.5>); } }");
        test(bool(sit_target_program), "sit target functions compile");
        auto sit_target_host = host;
        using SitCall = std::tuple<std::int32_t, framework::Vector3d, LslRotation>;
        std::vector<SitCall> sit_targets;
        sit_target_host.set_sit_target = [&](auto part, auto link, auto offset, auto rotation) {
            test(part == context().object, "sit target receives the calling prim");
            sit_targets.emplace_back(link, offset, rotation);
        };
        LslInstance sit_target_script(sit_target_program.program, context(), sit_target_host);
        test(sit_target_script.run(100).status == LslRunStatus::Idle &&
                 sit_targets ==
                     std::vector<SitCall>{{-4, {1, 2, 3}, {0, 0, 0, 1}}, {2, {4, 5, 6}, {0, .5, 0, .5}}},
             "sit targets select the calling or linked prim and normalize only an all-zero rotation");
        auto sit_flags_program =
            compile_lsl("default { state_entry() { integer value; llSetLinkSitFlags(++value, ++value);"
                        "llSay(1, (string)value); llSay(1, (string)llGetLinkSitFlags(LINK_THIS)); } }");
        test(bool(sit_flags_program), "sit flag functions compile");
        auto sit_flags_host = host;
        sit_flags_host.sit_flags = [](auto, auto link) {
            return link == -4 ? 0x33 : 0;
        };
        LslInstance sit_flags_script(sit_flags_program.program, context(), sit_flags_host);
        const auto flags_start = messages.size();
        test(sit_flags_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + flags_start, messages.end()) ==
                     std::vector<std::string>{"2", "51"},
             "sit flag setter evaluates its arguments and the getter returns the configured flags");
        LslInstance identity_script(identity.program, context(), host);
        const auto identity_start = messages.size();
        identity_script.post_event({"touch_start", {std::int32_t(1)}, {{context().owner, 4}}});
        test(identity_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + identity_start, messages.end()) ==
                     std::vector<std::string>{context().object.str(), "", "4", "-1"},
             "touch keys resolve the detector and out-of-range indices return an empty key");
        auto surface_script = compile_lsl(
            "default{touch_start(integer n){llSay(1,(string)llDetectedTouchPos(0));llSay(1,(string)llDetectedTouchUV(n));}}");
        test(bool(surface_script), "touch vector queries compile");
        LslInstance surface_instance(surface_script.program, context(), host);
        LslDetector hit{context().owner};
        hit.touch_pos = {1.25f, 2.5f, 3.75f};
        const auto surface_start = messages.size();
        surface_instance.post_event({"touch_start", {std::int32_t(1)}, {hit}});
        test(surface_instance.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + surface_start, messages.end()) ==
                     std::vector<std::string>{"<1.250000, 2.500000, 3.750000>",
                                              "<-1.000000, -1.000000, 0.000000>"},
             "touch vectors retain packet values and return source defaults for missing detectors");
        auto detector_program = compile_lsl(
            "default { touch_start(integer n) {"
            "llSay(1, llDetectedName(0) + \"|\" + (string)llDetectedOwner(0) + \"|\" +"
            "(string)llDetectedType(0) + \"|\" + (string)llDetectedGroup(0) + \"|\" +"
            "(string)llDetectedLinkNumber(0));"
            "llSay(1, (string)llDetectedPos(0) + (string)llDetectedVel(0) + (string)llDetectedGrab(0));"
            "llSay(1, (string)llDetectedRot(0) + (string)llDetectedRot(n));"
            "llSay(1, llDetectedName(n) + (string)llDetectedOwner(n) +"
            "(string)llDetectedGroup(n) + (string)llDetectedType(n) + (string)llDetectedLinkNumber(n));"
            "} }");
        test(bool(detector_program), "detector metadata queries compile");
        hit.name = "Detected Avatar";
        hit.owner = context().owner;
        hit.position = {1, 2, 3};
        hit.velocity = {4, 5, 6};
        hit.grab = {7, 8, 9};
        hit.rotation = {0, 0, 0, 1};
        hit.type = 3;
        hit.link_number = 2;
        LslInstance detector_instance(detector_program.program, context(), host);
        const auto detector_start = messages.size();
        detector_instance.post_event({"touch_start", {std::int32_t(1)}, {hit}});
        test(
            detector_instance.run(100).status == LslRunStatus::Idle &&
                std::vector<std::string>(messages.begin() + detector_start, messages.end()) ==
                    std::vector<std::string>{
                        "Detected Avatar|" + context().owner.str() + "|3|1|2",
                        "<1.000000, 2.000000, 3.000000><4.000000, 5.000000, 6.000000><7.000000, 8.000000, 9.000000>",
                        "<0.000000, 0.000000, 0.000000, 1.000000><0.000000, 0.000000, 0.000000, 0.000000>",
                        "000"},
            "detectors preserve metadata and return source defaults for missing indices");
        auto vector_program = compile_lsl(
            "vector initial=<16777217,2,3>;"
            "default{"
                "state_entry(){"
                    "vector v;"
                    "llSay(1,(string)v);"
                    "v=initial;"
                    "llSay(1,(string)v);"
                "}"
                "touch_start(integer n){"
                    "vector p=llDetectedTouchPos(0);"
                    "llSay(1,(string)p);"
                "}"
            "}");
        test(bool(vector_program), "vector literals and variables compile");
        LslInstance vector_script(vector_program.program, context(), host);
        const auto vector_start = messages.size();
        vector_script.post_event({"touch_start", {std::int32_t(1)}, {hit}});
        test(vector_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + vector_start, messages.end()) ==
                     std::vector<std::string>{"<0.000000, 0.000000, 0.000000>",
                                              "<16777217.000000, 2.000000, 3.000000>",
                                              "<1.250000, 2.500000, 3.750000>"},
             "vector declarations preserve literals, assignments and touch query values");
        auto vector_ops = compile_lsl(
            "default{"
                "state_entry(){"
                    "vector a=<1,0,0>;"
                    "vector b=<0,1,0>;"
                    "a+=b;"
                    "llSay(1,(string)(a*b));"
                    "llSay(1,(string)(a%b));"
                    "llSay(1,(string)(-2*a/2));"
                    "llSay(1,(string)(a==<1,1,0>));"
                "}"
            "}");
        test(bool(vector_ops), "vector operators compile");
        LslInstance vector_arithmetic(vector_ops.program, context(), host);
        const auto vector_ops_start = messages.size();
        test(vector_arithmetic.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + vector_ops_start, messages.end()) ==
                     std::vector<std::string>{"1.000000", "<0.000000, 0.000000, 1.000000>",
                                              "<-1.000000, -1.000000, -0.000000>", "1"},
             "vector dot and cross products, scaling, compound addition and equality");
        auto components = compile_lsl(
            "default{touch_start(integer n){vector v=<1,2,3>;llSay(1,(string)(v.x+v.y+v.z+llDetectedTouchPos(0).z));}}");
        test(bool(components), "vector component expressions compile");
        LslInstance component_script(components.program, context(), host);
        component_script.post_event({"touch_start", {std::int32_t(1)}, {hit}});
        test(component_script.run(100).status == LslRunStatus::Idle && messages.back() == "9.750000",
             "vector component reads work on locals and function results");
        auto component_writes = compile_lsl(
            "vector v=<1,2,3>;default{state_entry(){v.x=4;v.y+=v.x;llSay(1,(string)(v.z=v.y/2));llSay(1,(string)v);}}");
        test(bool(component_writes), "vector component assignments compile");
        LslInstance component_writer(component_writes.program, context(), host);
        const auto writes_start = messages.size();
        test(component_writer.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + writes_start, messages.end()) ==
                     std::vector<std::string>{"3.000000", "<4.000000, 6.000000, 3.000000>"},
             "component writes preserve neighboring coordinates and return the stored value");
        test(!compile_lsl("default{state_entry(){llSay(1,(string)((<1,2,3>).x=4));}}"),
             "components of temporary vectors are not writable");
        auto component_updates = compile_lsl(
            "default{state_entry(){vector v=<1,2,3>;llSay(1,(string)v.x++);llSay(1,(string)--v.y);llSay(1,(string)v);}}");
        test(bool(component_updates), "vector component increments compile");
        LslInstance component_updater(component_updates.program, context(), host);
        const auto updates_start = messages.size();
        test(component_updater.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + updates_start, messages.end()) ==
                     std::vector<std::string>{"1.000000", "1.000000", "<2.000000, 1.000000, 3.000000>"},
             "component updates return the correct old and new values");
        auto geometry = compile_lsl(
            "default{"
                "state_entry(){"
                    "vector v=<3,4,0>;"
                    "llSay(1,(string)llVecMag(v));"
                    "llSay(1,(string)llVecDist(v,<3,4,5>));"
                    "llSay(1,(string)llVecNorm(v));"
                    "llSay(1,(string)llVecNorm(<1e-24,0,0>));"
                "}"
            "}");
        test(bool(geometry), "vector geometry functions compile");
        LslInstance geometry_script(geometry.program, context(), host);
        const auto geometry_start = messages.size();
        test(geometry_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + geometry_start, messages.end()) ==
                     std::vector<std::string>{"5.000000", "5.000000", "<0.600000, 0.800000, 0.000000>",
                                              "<0.000000, 0.000000, 0.000000>"},
             "vector magnitude, distance, and normalization match the source formulas");
        auto colors = compile_lsl(
            "default{state_entry(){llSay(1,(string)llLinear2sRGB(<-0.001,0.003,0.5>));llSay(1,(string)llsRGB2Linear(<-0.01,0.04,0.5>));}}");
        test(bool(colors), "color conversions compile");
        LslInstance color_script(colors.program, context(), host);
        const auto color_start = messages.size();
        test(color_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + color_start, messages.end()) ==
                     std::vector<std::string>{"<-0.012920, 0.038760, 0.735357>",
                                              "<-0.000774, 0.003096, 0.214041>"},
             "color conversions apply the source formulas per channel without clamping");
        auto modpow =
            compile_lsl("default{state_entry(){integer result=llModPow(-3,3,5);llSay(1,(string)result);}}");
        test(bool(modpow), "modular power compiles");
        auto modpow_host = host;
        std::int64_t modpow_ticks = 100;
        modpow_host.monotonic_ticks = [&] {
            return modpow_ticks;
        };
        modpow_host.clock_ticks = modpow_host.monotonic_ticks;
        LslInstance modpow_script(modpow.program, context(), modpow_host);
        const auto modpow_start = messages.size();
        const auto modpow_sleep = modpow_script.run(100);
        test(modpow_sleep.status == LslRunStatus::Sleeping && modpow_sleep.events_completed == 0 &&
                 messages.size() == modpow_start,
             "modular power delays the next statement");
        modpow_ticks += 10000000;
        const auto modpow_resume = modpow_script.run(100);
        test(modpow_resume.status == LslRunStatus::Idle && modpow_resume.events_completed == 1 &&
                 messages.size() == modpow_start + 1 && messages.back() == "-2",
             "modular power uses the signed remainder");
        const auto sim_stats = compile_lsl(R"(
            default {
                state_entry() {
                    llSay(1, (string)llGetSimStats(SIM_STAT_AGENT_COUNT) + "|" +
                             (string)llGetSimStats(SIM_STAT_UNACKED_BYTES) + "|" +
                             (string)llGetSimStats(SIM_STAT_AI_MS) + "|" +
                             (string)llGetSimStats(SIM_STAT_ASSET_DOWNLOADS) + "|" +
                             (string)llGetSimStats(-1) + "|" +
                             (string)llGetSimStats(27));
                }
            }
        )");
        test(bool(sim_stats), "simulator statistics compile with LSL selectors");
        auto stats_host = host;
        stats_host.simulator_stat = [](std::size_t id) { return static_cast<float>(id); };
        LslInstance stats_script(sim_stats.program, context(), stats_host);
        test(stats_script.run(100).status == LslRunStatus::Idle &&
                 messages.back() == "13.000000|24.000000|38.000000|0.000000|0.000000|0.000000",
             "simulator statistics map selectors and return zero for unsupported values");

        const auto object_mass = compile_lsl(R"(
            default {
                state_entry() {
                    llSay(1, (string)llGetObjectMass(llGetOwner()) + "|" +
                             (string)llGetObjectMass(NULL_KEY) + "|" +
                             (string)llGetObjectMass("invalid"));
                }
            }
        )");
        test(bool(object_mass), "object mass query compiles");
        auto mass_host = host;
        mass_host.object_mass = [](framework::Uuid) { return 1.5f; };
        LslInstance mass_script(object_mass.program, context(), mass_host);
        test(mass_script.run(100).status == LslRunStatus::Idle &&
                 messages.back() == "1.500000|0.000000|0.000000",
             "object mass queries the scene only for a valid nonzero key");
        const auto script_mass = compile_lsl(R"(
            default {
                state_entry() {
                    llSay(1, (string)llGetMass() + "|" + (string)llGetMassMKS());
                }
            }
        )");
        test(bool(script_mass), "script mass queries compile");
        mass_host.script_mass = [](framework::Uuid part) {
            return part == context().object ? 1.75f : 0.f;
        };
        LslInstance script_mass_instance(script_mass.program, context(), mass_host);
        test(script_mass_instance.run(100).status == LslRunStatus::Idle &&
                 messages.back() == "1.750000|175.000000",
             "script mass uses the hosting part and scales the MKS result by 100");

        const auto visual_params = compile_lsl(R"(
            default {
                state_entry() {
                    list params = llGetVisualParams(llGetOwner(), [33, "height", "hover", "Height", 33.0, "male"]);
                    llSay(1, llDumpList2String(params, "|"));
                    llSay(1, (string)llGetListLength(llGetVisualParams("invalid", ["height"])));
                }
            }
        )");
        test(bool(visual_params), "avatar visual parameter query compiles");
        auto visual_host = host;
        auto visual_bytes = std::make_shared<std::vector<std::uint8_t>>(253);
        (*visual_bytes)[25] = 128;
        (*visual_bytes)[252] = 255;
        visual_host.agent_visual_params = [visual_bytes](framework::Uuid) { return visual_bytes; };
        LslInstance visual_script(visual_params.program, context(), visual_host);
        const auto visual_start = messages.size();
        test(visual_script.run(100).status == LslRunStatus::Idle &&
                 messages.size() == visual_start + 2 &&
                 messages[visual_start] == "0.501961|0.501961|1|||0" && messages.back() == "0",
             "avatar visual parameters preserve selector aliases, string values, and missing results");

        const auto linked_texture = compile_lsl(R"(
            default {
                state_entry() {
                    llSetLinkAlpha(LINK_ALL_OTHERS, 0.25, ALL_SIDES);
                }
            }
        )");
        test(bool(linked_texture), "linked face setter compiles");
        auto linked_host = host;
        const auto source_part = context().object;
        const framework::Uuid child_a("11111111-1111-4111-8111-111111111111");
        const framework::Uuid child_b("22222222-2222-4222-8222-222222222222");
        linked_host.linked_prim_parts = [&](framework::Uuid source, std::int32_t link) {
            test(source == source_part && link == -2, "linked face setter forwards the source and selector");
            return std::vector{child_a, child_b};
        };
        std::vector<framework::Uuid> edited_parts;
        linked_host.set_texture_property =
            [&](framework::Uuid source, framework::Uuid target, std::int32_t face,
                LslTextureProperty property, const LslValue& value) {
                test(source == source_part && face == -1 && property == LslTextureProperty::Alpha &&
                         std::get<double>(value) == 0.25,
                     "linked face setter preserves the hosting prim, face, and value");
                edited_parts.push_back(target);
            };
        LslInstance linked_texture_script(linked_texture.program, context(), linked_host);
        test(linked_texture_script.run(100).status == LslRunStatus::Idle &&
                 edited_parts == std::vector{child_a, child_b},
             "linked face setter visits all selected targets in order");
        const auto texture_animation = compile_lsl(R"(
            default {
                state_entry() {
                    llSetLinkTextureAnim(LINK_ALL_OTHERS, ANIM_ON | LOOP, ALL_SIDES, 4, 8, 1.25, 2.5, -3);
                    llSetTextureAnim(0, ALL_SIDES, 0, 0, 0, 0, 0);
                }
            }
        )");
        test(bool(texture_animation), "texture animation setters compile");
        std::vector<std::pair<framework::Uuid, std::vector<std::uint8_t>>> animations;
        linked_host.set_texture_animation = [&](framework::Uuid target, const std::vector<std::uint8_t>& bytes) {
            animations.emplace_back(target, bytes);
        };
        LslInstance animation_script(texture_animation.program, context(), linked_host);
        const std::vector<std::uint8_t> expected_animation{3, 255, 4, 8, 0, 0, 160, 63, 0, 0, 32, 64, 0, 0, 64, 192};
        test(animation_script.run(100).status == LslRunStatus::Idle && animations.size() == 3 &&
                 animations[0] == std::pair{child_a, expected_animation} &&
                 animations[1] == std::pair{child_b, expected_animation} &&
                 animations[2].first == source_part && animations[2].second.empty(),
             "texture animation packs the managed byte layout and clears on mode zero");
        const auto particle_program = compile_lsl(R"(
            default {
                state_entry() {
                    llParticleSystem([PSYS_PART_FLAGS, 0]);
                    llLinkParticleSystem(LINK_ALL_OTHERS, [
                        PSYS_SRC_TEXTURE, "Particle Texture",
                        PSYS_SRC_TARGET_KEY, "10213243-5465-7687-98a9-bacbdcedfe0f",
                        PSYS_SRC_PATTERN, PSYS_SRC_PATTERN_ANGLE_CONE,
                        PSYS_PART_FLAGS, PSYS_PART_EMISSIVE_MASK | PSYS_PART_INTERP_COLOR_MASK |
                            PSYS_PART_INTERP_SCALE_MASK,
                        PSYS_SRC_ANGLE_BEGIN, 1.25, PSYS_SRC_BURST_PART_COUNT, 255,
                        PSYS_SRC_ACCEL, <-2, 3, 0.5>,
                        PSYS_PART_START_COLOR, <0.5, 0.25, 1>, PSYS_PART_START_ALPHA, 0.8,
                        PSYS_PART_START_SCALE, <9, 0.5, 0>,
                        PSYS_PART_END_GLOW, 0.5, PSYS_PART_BLEND_FUNC_SOURCE, PSYS_PART_BF_ONE
                    ]);
                    llParticleSystem([]);
                    osParticleSystem([PSYS_SRC_MAX_AGE, 0.75]);
                    osLinkParticleSystem(LINK_ALL_OTHERS, [PSYS_SRC_MAX_AGE, 0]);
                }
            }
        )");
        test(bool(particle_program), "particle system setters compile");
        std::vector<std::pair<framework::Uuid, std::string>> particle_updates;
        std::vector<std::optional<float>> particle_ages;
        linked_host.particle_inventory_asset = [&](auto part, const std::string& name) {
            test(part == source_part && name == "Particle Texture", "particle texture resolves on the hosting prim");
            return framework::Uuid("00112233-4455-6677-8899-aabbccddeeff");
        };
        linked_host.set_particle_system = [&](auto part, std::vector<std::uint8_t> bytes, std::optional<float> age) {
            particle_updates.emplace_back(part, std::string(bytes.begin(), bytes.end()));
            particle_ages.push_back(age);
        };
        LslInstance particle_script(particle_program.program, context(), linked_host);
        const auto default_particles = unhex(
            "010000000000000000000000000000190000000001000101008000800080008000800080000000000000000000000000"
            "000000000000000000000000000000000000000000000000000AFFFFFFFFFFFFFFFF20202020");
        const auto extended_particles = unhex(
            "440000000100000002000000080000000028001900000000010001FF008000800080007F808140800011223344556677"
            "8899AABBCCDDEEFF102132435465768798A9BACBDCEDFE0F1600000003010300000A7F3FFFCCFFFFFFFFFE102020007F0009");
        test(particle_script.run(100).status == LslRunStatus::Idle && particle_updates.size() == 7 &&
                 particle_updates[0] == std::pair{source_part, default_particles} &&
                 particle_updates[1] == std::pair{child_a, extended_particles} &&
                 particle_updates[2] == std::pair{child_b, extended_particles} &&
                 particle_updates[3] == std::pair{source_part, std::string{}},
             "particle rules match bundled SDK legacy and extended bytes and clear on an empty list");
        test(particle_updates[4].first == source_part &&
                 particle_updates[5] == std::pair{child_a, default_particles} &&
                 particle_updates[6] == std::pair{child_b, default_particles} &&
                 particle_ages == std::vector<std::optional<float>>{{}, {}, {}, {}, .75f, 0.f, 0.f},
             "OSSL particle calls forward source ages for the host and each linked prim");
        const auto aliased_particle_key = compile_lsl(R"LSL(
            default {
                state_entry() {
                    key count = (key)"1";
                    llParticleSystem([PSYS_SRC_BURST_PART_COUNT, count]);
                }
            }
        )LSL");
        test(bool(aliased_particle_key), "particle rule using a YEngine key alias compiles");
        LslInstance aliased_particle_script(aliased_particle_key.program, context(), linked_host);
        test(aliased_particle_script.run(100).status == LslRunStatus::Idle && particle_updates.size() == 8 &&
                 particle_updates.back() == std::pair{source_part, default_particles},
             "YEngine boxes a key expression as LSLString and accepts numeric text in a particle rule");
        particle_updates.pop_back();
        auto diagnostic_host = linked_host;
        std::vector<LslChat> particle_chat, particle_listeners;
        std::vector<std::int32_t> particle_delays;
        diagnostic_host.chat = [&](const auto& message) { particle_chat.push_back(message); };
        diagnostic_host.listener = [&](const auto& message) { particle_listeners.push_back(message); };
        diagnostic_host.request_sleep = [&](auto delay) { particle_delays.push_back(delay); };
        diagnostic_host.script_delay_factor = 1;
        const auto verify_particle_error = [&](std::string_view command, std::string_view expected) {
            particle_chat.clear();
            particle_listeners.clear();
            particle_delays.clear();
            const auto program = compile_lsl("default{state_entry(){" + std::string(command) + ";}}");
            test(bool(program), "invalid particle rules compile for runtime diagnostics");
            LslInstance instance(program.program, context(), diagnostic_host);
            test(instance.run(100).status == LslRunStatus::Sleeping && particle_chat.size() == 1 &&
                     particle_chat[0].type == LslChatType::Debug && particle_chat[0].text == expected &&
                     particle_chat[0].channel == INT32_MAX && particle_listeners.size() == 1 &&
                     particle_listeners[0].type == LslChatType::Shout && particle_listeners[0].text == expected &&
                     particle_delays == std::vector<std::int32_t>{1000} && particle_updates.size() == 7,
                 "particle type errors use managed debug text, listener delivery and delay without replacing the emitter");
        };
        verify_particle_error("llParticleSystem([<1,2,3>,0])",
            "llParticleSystem: Error running particle system params index #0: particle system parameter type must be integer");
        verify_particle_error("osParticleSystem([PSYS_PART_FLAGS,0,PSYS_PART_END_COLOR,7])",
            "osParticleSystem: Error running rule PSYS_PART_END_COLOR: arg #3 - parameter 1 must be vector");
        verify_particle_error("llParticleSystem([PSYS_SRC_TEXTURE,7])",
            "llParticleSystem: Error running rule PSYS_SRC_TEXTURE: arg #1 - parameter 1 must be string or key");
        for (const auto& [command, expected] : std::array{
                 std::pair{"llParticleSystem([PSYS_SRC_BURST_RATE])", "Index was outside the bounds of the array."},
                 std::pair{"llParticleSystem([PSYS_SRC_BURST_RATE,\"invalid\"])",
                           "The input string 'invalid' was not in a correct format."}}) {
            particle_chat.clear();
            const auto program = compile_lsl("default{state_entry(){" + std::string(command) + ";}}");
            test(bool(program), "particle rule fault fixture compiles");
            LslInstance instance(program.program, context(), diagnostic_host);
            const auto fault = instance.run(100);
            test(fault.status == LslRunStatus::Faulted && fault.error == expected && particle_chat.empty() &&
                     particle_updates.size() == 7,
                 "particle bounds and numeric format failures propagate without a type diagnostic");
        }
        const auto link_messages = compile_lsl(R"(
            default {
                link_message(integer sender, integer number, string message, key id) {
                    llSay(1, (string)sender + "|" + (string)number + "|" + message + "|" + (string)id);
                }
            }
        )");
        test(bool(link_messages), "link message event compiles");
        LslInstance link_receiver(link_messages.program, context(), host);
        test(!link_receiver.post_event({"link_message", {1, std::string("wrong"), std::string("a"), std::string("b")}, {}}),
             "link message rejects invalid event argument types");
        test(link_receiver.post_event({"link_message", {1, 42, std::string("message"), std::string("arbitrary id")}, {}}) &&
                 link_receiver.run(100).status == LslRunStatus::Idle &&
                 messages.back() == "1|42|message|arbitrary id",
             "link message preserves integer and string payloads including non-UUID keys");

        auto position_program = compile_lsl(
            "default{touch_start(integer n){llSay(1,llGetRegionName()+(string)(llGetRegionCorner()+llGetPos()));}}");
        test(bool(position_program), "object position query compiles");
        LslInstance position_script(position_program.program, context(), host);
        auto moved_context = context();
        moved_context.position = {12.f, 34.f, 56.f};
        moved_context.region_name = "Test Region ";
        moved_context.region_corner = {256000., 512000., 0.};
        position_script.set_context(moved_context);
        position_script.post_event({"touch_start", {std::int32_t(1)}, {}});
        test(position_script.run(100).status == LslRunStatus::Idle &&
                 messages.back() == "Test Region <256012.000000, 512034.000000, 56.000000>",
             "object position query reads the refreshed scene context");
        auto metadata = compile_lsl(
            "default{touch_start(integer n){llSay(1,llGetObjectName()+\"/\"+llGetObjectDesc()+llGetScriptName());}}");
        test(bool(metadata), "object metadata queries compile");
        LslInstance metadata_script(metadata.program, context(), host);
        auto edited_context = context();
        edited_context.object_name = "Edited";
        edited_context.object_description = "Updated description";
        edited_context.script_name = "/Script";
        metadata_script.set_context(edited_context);
        metadata_script.post_event({"touch_start", {std::int32_t(1)}, {}});
        test(metadata_script.run(100).status == LslRunStatus::Idle &&
                 messages.back() == "Edited/Updated description/Script",
             "metadata queries read current scene values");
        auto edit_metadata = compile_lsl(
            "default{"
                "state_entry(){"
                    "llSetObjectName(\"New Name\");"
                    "llSetObjectDesc(\"New Desc\");"
                    "llSetObjectName(\"New Name\");"
                    "llSay(1,llGetObjectName()+\"/\"+llGetObjectDesc());"
                "}"
            "}");
        test(bool(edit_metadata), "object metadata setters compile");
        auto metadata_host = host;
        std::vector<std::pair<bool, std::string>> metadata_changes;
        metadata_host.set_object_text = [&](auto id, bool description, const std::string& text) {
            if (id != context().object)
                return false;
            metadata_changes.emplace_back(description, text);
            return true;
        };
        LslInstance metadata_editor(edit_metadata.program, context(), metadata_host);
        test(metadata_editor.run(100).status == LslRunStatus::Idle &&
                 messages.back() == "New Name/New Desc" &&
                 metadata_changes ==
                     std::vector<std::pair<bool, std::string>>{{false, "New Name"}, {true, "New Desc"}},
             "metadata changes reach the host and are immediately visible to the script");
        auto scale_program = compile_lsl("default{touch_start(integer n){llSay(1,(string)llGetScale());}}");
        test(bool(scale_program), "object scale query compiles");
        LslInstance scale_script(scale_program.program, context(), host);
        auto scaled_context = context();
        scaled_context.object_scale = {2.f, 4.f, 8.f};
        scale_script.set_context(scaled_context);
        scale_script.post_event({"touch_start", {std::int32_t(1)}, {}});
        test(scale_script.run(100).status == LslRunStatus::Idle &&
                 messages.back() == "<2.000000, 4.000000, 8.000000>",
             "object scale query reads the refreshed scene context");
        auto resize =
            compile_lsl("default{state_entry(){llSetScale(<1,2,99>);llSay(1,(string)llGetScale());}}");
        test(bool(resize), "object scale setter compiles");
        auto resize_host = host;
        resize_host.set_object_scale = [](auto, auto requested) {
            requested.z = std::min(requested.z, 10.f);
            return requested;
        };
        LslInstance resizing(resize.program, context(), resize_host);
        test(resizing.run(100).status == LslRunStatus::Idle &&
                 messages.back() == "<1.000000, 2.000000, 10.000000>",
             "scale getter observes the applied scene size immediately");
        auto click_action = compile_lsl(
            "default{state_entry(){if(TRUE)llSetClickAction(256+CLICK_ACTION_BUY);if(FALSE)llSetClickAction(CLICK_ACTION_TOUCH);llSetClickAction(-1);}}");
        test(bool(click_action), "object click action setter compiles");
        auto click_host = host;
        std::vector<std::uint8_t> actions;
        click_host.set_click_action = [&](auto id, auto action) {
            if (id == context().object)
                actions.push_back(action);
        };
        LslInstance clicking(click_action.program, context(), click_host);
        test(clicking.run(100).status == LslRunStatus::Idle && actions == std::vector<std::uint8_t>{2, 255},
             "click actions use the source byte conversion and target the host object");
        auto motion = compile_lsl(
            "default{state_entry(){llTargetOmega(<0,0,1>,2,1);llSay(1,(string)(llGetVel()+llGetAccel()+llGetOmega()));}}");
        test(bool(motion), "object motion queries compile");
        auto motion_context = context();
        motion_context.velocity = {1, 2, 3};
        motion_context.acceleration = {4, 5, 6};
        auto motion_host = host;
        motion_host.target_omega = [](auto, auto, bool) {
        };
        LslInstance motion_script(motion.program, motion_context, motion_host);
        test(motion_script.run(100).status == LslRunStatus::Idle &&
                 messages.back() == "<5.000000, 7.000000, 11.000000>",
             "motion queries use scene values and immediately observe target omega");
        auto creator_query = compile_lsl("default{state_entry(){llSay(1,(string)llGetCreator());}}");
        test(bool(creator_query), "creator query compiles");
        auto created_context = context();
        created_context.creator = framework::Uuid("12345678-1234-1234-1234-123456789abc");
        LslInstance creator_script(creator_query.program, created_context, host);
        test(creator_script.run(100).status == LslRunStatus::Idle &&
                 messages.back() == created_context.creator.str(),
             "creator query returns the creator UUID independently of ownership");
        auto dilation_query =
            compile_lsl("default{state_entry(){llSay(1,(string)llGetRegionTimeDilation());}}");
        test(bool(dilation_query), "region time dilation query compiles");
        auto dilation_context = context();
        dilation_context.time_dilation = .75f;
        LslInstance dilation_script(dilation_query.program, dilation_context, host);
        test(dilation_script.run(100).status == LslRunStatus::Idle && messages.back() == "0.750000",
             "region time dilation query uses the region context");
        auto inventory_query = compile_lsl(
            "default{state_entry(){llSay(1,(string)llGetInventoryType(\"Script\"));llSay(1,(string)llGetInventoryType(\"script\"));}}");
        test(bool(inventory_query), "inventory type query compiles");
        auto inventory_host = host;
        inventory_host.inventory_type = [&](auto id, const auto& name) {
            return id == context().object && name == "Script" ? 10 : -1;
        };
        LslInstance inventory_script(inventory_query.program, context(), inventory_host);
        const auto inventory_start = messages.size();
        test(inventory_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + inventory_start, messages.end()) ==
                     std::vector<std::string>{"10", "-1"},
             "inventory type queries target the host object and preserve the missing-item result");
        auto inventory_list = compile_lsl(
            "default{"
                "state_entry(){"
                    "llSay(1,(string)llGetInventoryNumber(INVENTORY_ALL));"
                    "llSay(1,llGetInventoryName(INVENTORY_SCRIPT,0));"
                    "llSay(1,llGetInventoryName(INVENTORY_SCRIPT,2));"
                "}"
            "}");
        test(bool(inventory_list), "inventory enumeration compiles");
        inventory_host.inventory_names = [](auto, int type) {
            if (type == -1)
                return std::vector<std::string>{"zeta", "Alpha", "Other"};
            return std::vector<std::string>{"zeta", "Alpha"};
        };
        LslInstance inventory_enumerator(inventory_list.program, context(), inventory_host);
        const auto names_start = messages.size();
        test(inventory_enumerator.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + names_start, messages.end()) ==
                     std::vector<std::string>{"3", "Alpha", ""},
             "inventory enumeration counts all types, sorts names and handles missing indices");
        auto inventory_key = compile_lsl(
            "default{state_entry(){llSay(1,(string)llGetInventoryKey(\"Allowed\"));llSay(1,(string)llGetInventoryKey(\"Missing\"));}}");
        test(bool(inventory_key), "inventory key query compiles");
        inventory_host.inventory_key = [&](auto id, const auto& name) {
            return id == context().object && name == "Allowed" ? context().owner : framework::Uuid{};
        };
        LslInstance inventory_keys(inventory_key.program, context(), inventory_host);
        const auto keys_start = messages.size();
        test(inventory_keys.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + keys_start, messages.end()) ==
                     std::vector<std::string>{context().owner.str(), framework::Uuid{}.str()},
             "inventory key queries return the asset UUID or null key");
        auto desc_program = compile_lsl(
            "default{"
                "state_entry(){"
                    "llSay(1,llGetInventoryDesc(\"present\"));"
                    "llSay(1,(string)llGetInventoryCreator(\"present\"));"
                    "llSay(1,llGetInventoryAcquireTime(\"present\"));"
                    "string missing=llGetInventoryDesc(\"missing\");"
                    "llSay(1,\"[\"+missing+\"]\");"
                "}"
            "}");
        test(bool(desc_program), "inventory description queries compile");
        LslHost desc_host;
        std::int64_t desc_ticks = 0;
        std::vector<LslChat> desc_messages;
        desc_host.clock_ticks = [&] {
            return desc_ticks;
        };
        desc_host.chat = [&](const auto& chat) {
            desc_messages.push_back(chat);
        };
        desc_host.inventory_metadata = [](auto, const auto& name) -> std::optional<LslInventoryMetadata> {
            if (name == "present")
                return LslInventoryMetadata{"description",
                                            opensim::framework::Uuid("12345678-1234-4234-8234-123456789abc"),
                                            4294967295u};
            return std::nullopt;
        };
        LslInstance desc_script(desc_program.program, context(), desc_host);
        test(desc_script.run(100).status == LslRunStatus::Sleeping && desc_messages.size() == 4 &&
                 desc_messages[0].text == "description" &&
                 desc_messages[1].text == "12345678-1234-4234-8234-123456789abc" &&
                 desc_messages[2].text == "2106-02-07T06:28:15Z" &&
                 desc_messages[3].type == LslChatType::Debug &&
                 desc_messages[3].text == "llGetInventoryDesc: Item missing not found",
             "inventory metadata preserves UUID and unsigned UTC time and reports missing items");
        desc_ticks += 10000000;
        test(desc_script.run(100).status == LslRunStatus::Idle && desc_messages.size() == 5 &&
                 desc_messages.back().text == "[]",
             "missing inventory description returns empty text");
        auto permissions = compile_lsl(
            "default{"
                "state_entry(){"
                    "llSay(1,(string)llGetInventoryPermMask(\"Item\",MASK_OWNER));"
                    "llSay(1,(string)(llGetInventoryPermMask(\"Item\",MASK_NEXT)&PERM_COPY));"
                    "llSay(1,(string)llGetInventoryPermMask(\"Missing\",MASK_BASE));"
                "}"
            "}");
        test(bool(permissions), "inventory permission queries compile");
        inventory_host.inventory_permission = [](auto, const auto& name,
                                                 int selector) -> std::optional<std::uint32_t> {
            if (name != "Item")
                return {};
            return selector == 1 ? 0x9e000u : 0x18000u;
        };
        LslInstance permission_script(permissions.program, context(), inventory_host);
        const auto permission_start = messages.size();
        test(permission_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + permission_start, messages.end()) ==
                     std::vector<std::string>{"2147483647", "32768", "-1"},
             "inventory permission queries project full and partial masks and handle missing items");
        auto energy = compile_lsl("default{state_entry(){llSay(1,(string)(llGetEnergy()*2));}}");
        test(bool(energy), "energy query compiles");
        LslInstance energy_script(energy.program, context(), host);
        test(energy_script.run(100).status == LslRunStatus::Idle && messages.back() == "2.000000",
             "energy query returns the source constant as an LSL float");
        auto language = compile_lsl(
            "default{state_entry(){llSay(1,llGetAgentLanguage(llGetOwner())+\"/\"+llGetAgentLanguage(NULL_KEY));}}");
        test(bool(language), "agent language query compiles");
        auto language_host = host;
        const auto language_context = context();
        language_host.agent_language = [&](auto id) {
            test(id == language_context.owner, "agent language query passes the avatar UUID");
            return std::string("fr-ca");
        };
        LslInstance language_script(language.program, language_context, language_host);
        test(language_script.run(100).status == LslRunStatus::Idle && messages.back() == "fr-ca/en-us",
             "agent language reads preferences and falls back for null keys");
        auto memory = compile_lsl(
            "default{"
                "state_entry(){"
                    "integer n=0;"
                    "llScriptProfiler(n++);"
                    "integer result=llSetMemoryLimit(++n);"
                    "llSay(1,llList2CSV([llGetFreeMemory(),llGetUsedMemory(),llGetMemoryLimit(),llGetSPMaxMemory(),result,n]));"
                "}"
            "}");
        test(bool(memory), "memory and profiler calls compile");
        LslInstance memory_script(memory.program, context(), host);
        test(memory_script.run(100).status == LslRunStatus::Idle &&
                 messages.back() == "1048576, 0, 65536, 65536, 0, 2",
             "memory calls expose the YEngine heap while preserving compatibility API values");
        auto allocated =
            compile_lsl("integer count=1; string label=\"abc\"; list values=[1,\" \"];"
                        "default{state_entry(){string local=\"test\";llSay(1,(string)llGetUsedMemory());"
                        "llSay(1,(string)llGetFreeMemory());}}");
        test(bool(allocated), "script with tracked heap values compiles");
        auto heap_host = host;
        heap_host.heap_limit_bytes = 2048;
        LslInstance heap_script(allocated.program, context(), heap_host);
        const auto pointer_bytes = static_cast<std::int32_t>(2 * sizeof(void*));
        test(heap_script.run(100).status == LslRunStatus::Idle &&
                 messages[messages.size() - 2] == std::to_string(24 + pointer_bytes) &&
                 messages.back() == std::to_string(2048 - 24 - pointer_bytes) &&
                 heap_script.heap_bytes_used() == 16 + pointer_bytes,
             "heap counts global values, list entries and live event locals against the configured limit");
        const auto over_limit = compile_lsl("string value=\"ab\"; default{state_entry(){"
                                            "value=\"abc\"; llSay(1,(string)(value=\"abcd\"));}}");
        auto limited_host = host;
        limited_host.heap_limit_bytes = 6;
        std::vector<LslChat> heap_messages;
        limited_host.chat = [&](const LslChat& chat) {
            heap_messages.push_back(chat);
        };
        LslInstance limited(over_limit.program, context(), limited_host);
        const auto heap_fault = limited.run(100);
        test(heap_fault.status == LslRunStatus::Faulted &&
                 heap_fault.error == "OutOfHeap: oldtotal=6, newtotal=8, limit=6" &&
                 limited.heap_bytes_used() == 6 && heap_messages.size() == 1 &&
                 heap_messages.front().type == LslChatType::Owner &&
                 heap_messages.front().text.find("Script must be Reset to re-enable.") != std::string::npos,
             "a growing assignment faults before replacing the value or sending chat");
        test(limited.run(100).status == LslRunStatus::Faulted && heap_messages.size() == 1,
             "a faulted script stays inactive without sending another owner notice");
        limited.reset();
        test(limited.run(100).status == LslRunStatus::Faulted && heap_messages.size() == 2,
             "reset allows the script to run and encounter its heap limit again");
        const auto local_limit =
            compile_lsl("integer count; default{state_entry(){string value=\"ab\"; llSay(1,value);}}");
        LslInstance local_limited(local_limit.program, context(), limited_host);
        test(local_limited.run(100).error == "OutOfHeap: oldtotal=4, newtotal=8, limit=6" &&
                 local_limited.heap_bytes_used() == 4,
             "event locals use the same heap limit as globals and are released on fault");
        const auto key_heap = compile_lsl("key globalKey=(key)\"abc\"; list keys=[(key)\"def\"];"
                                          "default{state_entry(){key localKey=(key)\"ghi\";"
                                          "llSay(1,(string)llGetUsedMemory());}}");
        test(bool(key_heap), "key heap fixture compiles");
        LslInstance key_heap_script(key_heap.program, context(), host);
        test(key_heap_script.run(100).status == LslRunStatus::Idle &&
                 messages.back() == std::to_string(12 + sizeof(void*)),
             "global and boxed list keys use string storage while local keys do not use tracked heap");
        const auto ossl_url = compile_lsl("default{state_entry(){osRequestSecureURL([\"allowXss\"]);}}");
        test(bool(ossl_url), "extended secure URL request compiles as a statement");
        auto url_host = host;
        bool url_called = false;
        url_host.request_url = [&](auto, auto, bool secure, bool allow_xss) {
            url_called = secure && allow_xss;
            return framework::Uuid::random();
        };
        LslInstance denied_url(ossl_url.program, context(), url_host);
        test(denied_url.run(100).error == "OSSL Permission Error: osRequestSecureURL permission denied" &&
                 !url_called,
             "OSSL permission denial prevents allocation");
        url_host.ossl_permission_error = [](auto, auto, std::string_view) {
            return std::string{};
        };
        LslInstance allowed_url(ossl_url.program, context(), url_host);
        test(allowed_url.run(100).status == LslRunStatus::Idle && url_called,
             "authorized OSSL requests pass TLS and the case-sensitive CORS option to the host");
        const auto force_sit = compile_lsl(R"LSL(
            default {
                state_entry() {
                    key avatar = "12345678-1234-4234-8234-123456789abc";
                    osForceOtherSit(avatar);
                    osForceOtherSit(avatar, "87654321-4321-4321-8321-cba987654321");
                    osForceOtherSit(avatar, "invalid");
                }
            }
        )LSL");
        test(bool(force_sit), "both forced sit overloads compile");
        auto seat_host = host;
        using SeatRequest = std::pair<framework::Uuid, framework::Uuid>;
        std::vector<SeatRequest> seat_requests;
        seat_host.force_sit = [&](auto avatar, auto target) {
            seat_requests.emplace_back(avatar, target);
        };
        LslInstance denied_seat(force_sit.program, context(), seat_host);
        test(denied_seat.run(100).error == "OSSL Permission Error: osForceOtherSit permission denied" &&
                 seat_requests.empty(),
             "denied forced sit does not reach the region");
        seat_host.ossl_permission_error = [](auto, auto, std::string_view function) {
            return function == "osForceOtherSit" ? std::string{} : std::string("Unexpected OSSL function");
        };
        const auto seat_context = context();
        LslInstance allowed_seat(force_sit.program, seat_context, seat_host);
        const framework::Uuid seated_avatar("12345678-1234-4234-8234-123456789abc");
        const framework::Uuid explicit_seat("87654321-4321-4321-8321-cba987654321");
        test(allowed_seat.run(100).status == LslRunStatus::Idle &&
                 seat_requests ==
                     std::vector<SeatRequest>{
                         {seated_avatar, seat_context.object},
                         {seated_avatar, explicit_seat},
                     },
             "forced sit uses the host prim or explicit target and ignores malformed keys");
        const auto scripted_teleport = compile_lsl(R"LSL(
            default {
                state_entry() {
                    osTeleportAgent("12345678-1234-4234-8234-123456789abc", <1, 2, 3>, ZERO_VECTOR);
                    osTeleportOwner("Destination", <4, 5, 6>, ZERO_VECTOR);
                    osTeleportAgent("12345678-1234-4234-8234-123456789abc", 17, 23, <7, 8, 9>, ZERO_VECTOR);
                }
            }
        )LSL");
        test(bool(scripted_teleport), "scripted teleport overloads compile");
        auto teleport_host = host;
        std::vector<std::string> teleport_permission_checks;
        std::vector<std::pair<framework::Uuid, LslTeleport>> teleport_requests;
        teleport_host.ossl_permission_error = [&](auto, auto, std::string_view function) {
            teleport_permission_checks.emplace_back(function);
            return std::string{};
        };
        teleport_host.teleport_agent = [&](auto, auto, auto agent, const LslTeleport& request) {
            teleport_requests.emplace_back(agent, request);
            return 0;
        };
        const auto teleport_context = context();
        LslInstance teleport_script(scripted_teleport.program, teleport_context, teleport_host);
        test(teleport_script.run(100).status == LslRunStatus::Idle && teleport_requests.size() == 3 &&
                 teleport_requests[0].first == seated_avatar &&
                 std::holds_alternative<std::monostate>(teleport_requests[0].second.destination) &&
                 teleport_requests[1].first == teleport_context.owner &&
                 std::get<std::string>(teleport_requests[1].second.destination) == "Destination" &&
                 std::get<std::array<std::int32_t, 2>>(teleport_requests[2].second.destination) ==
                     std::array<std::int32_t, 2>{17, 23} &&
                 teleport_permission_checks == std::vector<std::string>{"osTeleportOwner", "osTeleportAgent"},
             "teleport overloads pass destinations and check their original OSSL permissions");
        const auto viewer_teleport = compile_lsl(R"LSL(
            default {
                state_entry() {
                    llTeleportAgent("12345678-1234-4234-8234-123456789abc", "Landmark", ZERO_VECTOR, <0, 1, 0>);
                    llTeleportAgentGlobalCoords("12345678-1234-4234-8234-123456789abc",
                                                <1287, 778, 0>, <27, 38, 45>, <0, 1, 0>);
                    llTeleportAgent("invalid", "Landmark", ZERO_VECTOR, ZERO_VECTOR);
                }
            }
        )LSL");
        test(bool(viewer_teleport), "both LSL viewer teleport calls compile");
        std::vector<LslViewerTeleport> viewer_teleport_requests;
        unsigned viewer_teleport_invocations = 0;
        teleport_host.teleport_viewer = [&](auto, auto, auto agent, const LslViewerTeleport& request) {
            ++viewer_teleport_invocations;
            if (agent == seated_avatar)
                viewer_teleport_requests.push_back(request);
        };
        LslInstance viewer_teleport_script(viewer_teleport.program, teleport_context, teleport_host);
        test(viewer_teleport_script.run(100).status == LslRunStatus::Idle &&
                 viewer_teleport_invocations == 2 && viewer_teleport_requests.size() == 2 &&
                 std::get<std::string>(viewer_teleport_requests[0].destination) == "Landmark" &&
                 std::get<framework::Vector3d>(viewer_teleport_requests[1].destination) ==
                     framework::Vector3d{1287, 778, 0} &&
                 viewer_teleport_requests[1].position == framework::Vector3d{27, 38, 45},
             "LSL viewer teleports dispatch their destination and ignore malformed avatar keys");
        constexpr auto landmark_handle = (std::uint64_t{256004} << 32) | 256008;
        const std::string landmark_text =
            " \tLandmark version 2\r\n"
            "region_id 12345678-1234-4234-8234-123456789abc\r\n"
            "local_pos 121.5 invalid 30\r\n"
            "region_handle +" + std::to_string(landmark_handle) + "\r\n";
        const auto landmark_bytes = std::make_shared<std::vector<std::uint8_t>>(
            landmark_text.begin(), landmark_text.end());
        const auto landmark = parse_landmark(landmark_bytes);
        test(landmark.region == seated_avatar && landmark.position == framework::Vector3{121.5f, 0, 30} &&
                 landmark.handle == landmark_handle &&
                 landmark_position_relative_to(landmark_bytes, 256000, 256000) ==
                     framework::Vector3d{125.5, 8, 30},
             "landmarks accept signed handles and CRLF while invalid coordinates default to zero");
        const auto agent_ip = compile_lsl(R"LSL(
            default {
                state_entry() {
                    llSay(0, osGetAgentIP("invalid"));
                }
            }
        )LSL");
        test(bool(agent_ip), "osGetAgentIP compiles as a string expression");
        auto ip_host = host;
        bool ip_lookup_called = false;
        ip_host.avatar_ip_address = [&](auto, auto) {
            ip_lookup_called = true;
            return std::string{};
        };
        LslInstance denied_ip(agent_ip.program, context(), ip_host);
        test(denied_ip.run(100).error == "OSSL Permission Error: osGetAgentIP permission denied" &&
                 !ip_lookup_called,
             "OSSL permission denial precedes avatar key validation");
        const auto water_change = compile_lsl(R"LSL(
            default {
                state_entry() {
                    osSetRegionWaterHeight(8001);
                    osSetRegionWaterHeight(-1);
                    osSetRegionWaterHeight(1.0e40);
                }
            }
        )LSL");
        test(bool(water_change), "water height setter accepts integer and float arguments");
        auto water_host = host;
        std::vector<float> water_updates;
        water_host.set_region_water_height = [&](float height) {
            water_updates.push_back(height);
        };
        LslInstance denied_water(water_change.program, context(), water_host);
        test(denied_water.run(100).error ==
                     "OSSL Permission Error: osSetRegionWaterHeight permission denied" &&
                 water_updates.empty(),
             "OSSL permission denial prevents a water update");
        water_host.ossl_permission_error = [](auto, auto, std::string_view) {
            return std::string{};
        };
        LslInstance allowed_water(water_change.program, context(), water_host);
        test(allowed_water.run(100).status == LslRunStatus::Idle &&
                 water_updates == std::vector<float>{8001.f},
             "script water changes allow heights above the estate viewer limit and ignore invalid heights");
        const auto sun_settings = compile_lsl(R"LSL(
            default {
                state_entry() {
                    osSetEstateSunSettings(TRUE, 17.5);
                    osSetRegionSunSettings(FALSE, TRUE, 11.25);
                }
            }
        )LSL");
        test(bool(sun_settings), "both legacy sun setters compile");
        auto sun_host = host;
        std::vector<bool> estate_sun_updates;
        sun_host.set_region_sun_settings = [&](bool use_estate) {
            estate_sun_updates.push_back(use_estate);
        };
        LslInstance denied_sun(sun_settings.program, context(), sun_host);
        test(denied_sun.run(100).error == "OSSL Permission Error: osSetRegionSunSettings permission denied" &&
                 estate_sun_updates.empty(),
             "estate sun is a no-op and region sun requires OSSL permission");
        sun_host.ossl_permission_error = [](auto, auto, std::string_view) {
            return std::string{};
        };
        LslInstance allowed_sun(sun_settings.program, context(), sun_host);
        test(allowed_sun.run(100).status == LslRunStatus::Idle &&
                 estate_sun_updates == std::vector<bool>{false},
             "region sun changes the estate-sun selection");
        const auto apparent_time = compile_lsl(R"LSL(
            default {
                state_entry() {
                    llSay(0, (string)osGetCurrentSunHour());
                    llSay(0, (string)osGetApparentTime());
                    llSay(0, (string)osGetApparentRegionTime());
                    llSay(0, osGetApparentTimeString(TRUE));
                    llSay(0, osGetApparentRegionTimeString(FALSE));
                    llSay(0, (string)osGetSunParam("DAY_LENGTH"));
                }
            }
        )LSL");
        test(bool(apparent_time), "apparent time functions compile");
        auto apparent_time_host = host;
        apparent_time_host.ossl_enabled = true;
        std::vector<std::string> time_messages;
        apparent_time_host.chat = [&](const LslChat& chat) {
            time_messages.push_back(chat.text);
        };
        LslInstance no_environment(apparent_time.program, context(), apparent_time_host);
        test(no_environment.run(100).status == LslRunStatus::Idle && time_messages ==
                                                                         std::vector<std::string>{
                                                                             "0.000000",
                                                                             "0.000000",
                                                                             "0.000000",
                                                                             "00:00:00",
                                                                             "0:00:00 AM",
                                                                             "14400.000000",
                                                                         },
             "apparent time and day length use source defaults with no environment module");
        time_messages.clear();
        apparent_time_host.region_day_fraction = [] {
            return 0.75f;
        };
        apparent_time_host.object_position = [](auto, bool, bool) {
            return std::optional{framework::Vector3{200.f, 150.f, 40.f}};
        };
        apparent_time_host.environment_timing = [](framework::Vector3 position, bool region) {
            return std::pair{!region && position.x == 200.f && position.y == 150.f ? 36000 : 14400, 3600};
        };
        LslInstance with_environment(apparent_time.program, context(), apparent_time_host);
        test(with_environment.run(100).status == LslRunStatus::Idle && time_messages ==
                                                                           std::vector<std::string>{
                                                                               "18.000000",
                                                                               "64800.000000",
                                                                               "64800.000000",
                                                                               "18:00:00",
                                                                               "6:00:00 PM",
                                                                               "36000.000000",
                                                                           },
             "apparent time uses the live day fraction and preserves both clock formats");
        auto clock = compile_lsl("default{state_entry(){llSay(0,osGetApparentTimeString(TRUE));}}");
        test(bool(clock), "apparent clock compiles");
        apparent_time_host.region_day_fraction = [] { return 1e10f; };
        LslInstance out_of_range_clock(clock.program, context(), apparent_time_host);
        test(out_of_range_clock.run(100).status == LslRunStatus::Idle &&
                 time_messages.back() == "-2147483648:-2147483648:-2147483648",
             "apparent clock handles out-of-range integer conversion deterministically");
        const auto drawing = compile_lsl(R"LSL(
            default {
                state_entry() {
                    string commands = osDrawResetTransform("prefix;");
                    commands = osDrawRotationTransform(commands, 90);
                    commands = osDrawScaleTransform(commands, 2, 0.5);
                    commands = osDrawTranslationTransform(commands, -1, 3);
                    commands = osMovePen(commands, 2, 3);
                    commands = osDrawLine(commands, 4, 5, 6, 7);
                    llSay(0, osDrawLine(commands, 8, 9));
                    llSay(0, osDrawFilledPolygon("", [1, "2", 3.5], [4, 5, 6]));
                    llSay(0, "invalid=" + osDrawPolygon("discarded", [1, 2], [3, 4]));
                    llSay(0, osSetPenColor("", <1, 0.5, -1>));
                    llSay(0, osSetPenColor("", <2, 0.1, 0>, 0.5));
                    llSay(0, osDrawImage("", 16, 32, "https://example.invalid/a,b"));
                    llSay(0, osDrawTranslationTransform("", 1.0000005, -0.0000001));
                }
            }
        )LSL");
        test(bool(drawing), "drawing commands compile with all tested overloads");
        auto drawing_host = host;
        std::vector<std::string> drawing_messages;
        drawing_host.chat = [&](const LslChat& chat) {
            drawing_messages.push_back(chat.text);
        };
        LslInstance denied_drawing(drawing.program, context(), drawing_host);
        test(denied_drawing.run(100).error ==
                 "OSSL Runtime Error: permission denied. All unsafe OSSL funtions disabled",
             "safe drawing helpers honor the global OSSL switch");
        drawing_messages.clear();
        drawing_host.ossl_enabled = true;
        LslInstance allowed_drawing(drawing.program, context(), drawing_host);
        test(allowed_drawing.run(100).status == LslRunStatus::Idle &&
                 drawing_messages ==
                     std::vector<std::string>{
                         "prefix;ResetTransf;RotTransf 90.000000;ScaleTransf 2.000000,0.500000;"
                         "TransTransf -1.000000,3.000000;MoveTo 2,3;MoveTo 4,5; LineTo 6,7; LineTo 8,9; ",
                         "FillPolygon 1,4,2,5,3.500000,6; ",
                         "invalid=",
                         "PenColor FFff7f00; ",
                         "PenColor 7fff1900; ",
                         "Image 16,32,https://example.invalid/a,b; ",
                         "TransTransf 1.000001,-0.000000;",
                     },
             "drawing command output preserves source spacing, coercion and ARGB encoding");
        const auto event_heap = compile_lsl("default{dataserver(key query,string data){llSay(1,data);}}");
        test(bool(event_heap), "event heap fixture compiles");
        LslInstance event_heap_script(event_heap.program, context(), limited_host);
        const auto event = [](std::string data) {
            return LslEvent{
                "dataserver", {std::string("12345678-1234-4234-8234-123456789abc"), std::move(data)}, {}};
        };
        test(event_heap_script.post_event(event("abc")) &&
                 event_heap_script.run(100).status == LslRunStatus::Idle,
             "a dataserver string may fill the heap even with an untracked key parameter");
        test(event_heap_script.post_event(event("abcd")) &&
                 event_heap_script.run(100).error == "OutOfHeap: oldtotal=0, newtotal=8, limit=6" &&
                 heap_messages.back().text.find("event: dataserver") != std::string::npos,
             "a dataserver string exceeding the heap faults before the handler runs");
        auto changed_flags = compile_lsl(
            "default{changed(integer flags){if(flags&(CHANGED_SCALE|CHANGED_POSITION))llSay(1,(string)(CHANGED_REGION_START==CHANGED_REGION_RESTART));}}");
        test(bool(changed_flags), "named change flags compile");
        LslInstance change_script(changed_flags.program, context(), host);
        change_script.post_event({"changed", {std::int32_t{32768}}, {}});
        test(change_script.run(100).status == LslRunStatus::Idle && messages.back() == "1",
             "named flags match event masks and preserve the region-start alias");
        auto math_constants = compile_lsl(
            "default{state_entry(){if(!NULL_KEY&&PI*2==TWO_PI&&PI>3.1415927)llSay(1,(string)(PI_BY_TWO+DEG_TO_RAD+RAD_TO_DEG+SQRT2));}}");
        test(bool(math_constants), "math and null key constants compile");
        LslInstance math_constant_script(math_constants.program, context(), host);
        const auto math_start = messages.size();
        test(math_constant_script.run(100).status == LslRunStatus::Idle && messages.size() == math_start + 1,
             "math constants preserve the source float rounding and null key is false");
        auto rounding = compile_lsl(
            "default{state_entry(){llSay(1,(string)llFloor(-2.1));llSay(1,(string)llCeil(-2.1));llSay(1,(string)llRound(-2.5));llSay(1,(string)llRound(2.5));}}");
        test(bool(rounding), "rounding functions compile");
        LslInstance rounding_script(rounding.program, context(), host);
        const auto rounding_start = messages.size();
        test(rounding_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + rounding_start, messages.end()) ==
                     std::vector<std::string>{"-3", "-2", "-3", "3"},
             "rounding preserves signed floor, ceiling, and midpoint-away-from-zero results");
        auto scalar_math = compile_lsl(
            "default{state_entry(){llSay(1,(string)llAbs(0x80000000));llSay(1,(string)(llFabs(-.5)+llSqrt(9)+llPow(2,3)));}}");
        test(bool(scalar_math), "scalar math functions compile");
        LslInstance scalar_math_script(scalar_math.program, context(), host);
        const auto scalar_math_start = messages.size();
        test(scalar_math_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + scalar_math_start, messages.end()) ==
                     std::vector<std::string>{"-2147483648", "11.500000"},
             "scalar math preserves the minimum integer and floating point results");
        auto trig = compile_lsl(
            "default{state_entry(){llSay(1,(string)(llSin(0)+llCos(0)+llTan(0)+llAsin(0)+llAcos(1)+llLog(1)+llLog10(100)));llSay(1,(string)llAtan2(1,0));}}");
        test(bool(trig), "trigonometric and logarithmic functions compile");
        LslInstance trig_script(trig.program, context(), host);
        const auto trig_start = messages.size();
        test(trig_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + trig_start, messages.end()) ==
                     std::vector<std::string>{"3.000000", "1.570796"},
             "trigonometric and logarithmic functions preserve radian units and atan2 argument order");
        auto random = compile_lsl(
            "default{state_entry(){float p=llFrand(2);float n=llFrand(-2);if(p>=0&&p<2&&n<=0&&n>-2&&llFrand(0)==0)llSay(1,\"range\");}}");
        test(bool(random), "random function compiles");
        LslInstance random_script(random.program, context(), host);
        const auto random_start = messages.size();
        test(random_script.run(100).status == LslRunStatus::Idle && messages.size() == random_start + 1 &&
                 messages.back() == "range",
             "random values follow positive, negative, and zero magnitudes");
        auto generated_keys = compile_lsl(
            "default{state_entry(){key a=llGenerateKey();key b=llGenerateKey();if(a&&b&&a!=b)llSay(1,(string)a);}}");
        test(bool(generated_keys), "key generation compiles");
        LslInstance key_generator(generated_keys.program, context(), host);
        const auto generated_start = messages.size();
        test(key_generator.run(100).status == LslRunStatus::Idle && messages.size() == generated_start + 1 &&
                 messages.back().size() == 36 && messages.back()[14] == '4',
             "generated keys are distinct valid version-four UUIDs");
        auto string_length =
            compile_lsl(std::string("default{state_entry(){llSay(1,(string)llStringLength(") +
                        quote("A\xf0\x90\x80\x80\xc3\xa9") + "));llSay(1,(string)llStringLength(\"\"));}}");
        test(bool(string_length), "string length compiles");
        LslInstance length_script(string_length.program, context(), host);
        const auto lengths_start = messages.size();
        test(length_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + lengths_start, messages.end()) ==
                     std::vector<std::string>{"4", "0"},
             "string length counts UTF-16 units and handles empty strings");
        auto substring_index = compile_lsl(
            std::string("default{state_entry(){llSay(1,(string)llSubStringIndex(") +
            quote("A\xf0\x90\x80\x80z") +
            ",\"z\"));llSay(1,(string)llSubStringIndex(\"\",\"\"));llSay(1,(string)llSubStringIndex(\"z\",\"\"));}}");
        test(bool(substring_index), "substring search compiles");
        LslInstance index_script(substring_index.program, context(), host);
        const auto index_start = messages.size();
        test(index_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + index_start, messages.end()) ==
                     std::vector<std::string>{"3", "-1", "0"},
             "substring search returns UTF-16 offsets and preserves empty input rules");
        auto ranges = compile_lsl(
            "default{"
                "state_entry(){"
                    "llSay(1,llGetSubString(\"abcde\",-2,-1));"
                    "llSay(1,llGetSubString(\"abcde\",3,1));"
                    "llSay(1,llDeleteSubString(\"abcde\",3,1));"
                    "llSay(1,llDeleteSubString(\"abcde\",3,0));"
                "}"
            "}");
        test(bool(ranges), "substring ranges compile");
        LslInstance range_script(ranges.program, context(), host);
        const auto ranges_start = messages.size();
        test(range_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + ranges_start, messages.end()) ==
                     std::vector<std::string>{"de", "abde", "c", "abc"},
             "substring ranges preserve negative indices and source inverted-range behavior");
        const auto emoji = std::string("\xf0\x90\x80\x80");
        const auto destination = quote("a" + emoji + "b");
        auto insertion = compile_lsl("default{state_entry(){llSay(1,llInsertString(" + destination +
                                     ",2,\"X\"));llSay(1,llInsertString(" + destination +
                                     ",-2,\"X\"));llSay(1,llInsertString(\"abc\",99,\"X\"));}}");
        test(bool(insertion), "string insertion compiles");
        LslInstance insertion_script(insertion.program, context(), host);
        const auto insertion_start = messages.size();
        test(insertion_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + insertion_start, messages.end()) ==
                     std::vector<std::string>{"a" + emoji + "Xb", "aX" + emoji + "b", "abcX"},
             "insertion preserves surrogate pairs and clamps out-of-range offsets");
        auto trim = compile_lsl(
            "default{state_entry(){string s=" + quote("\xc2\xa0x\xe3\x80\x80") +
            ";llSay(1,llStringTrim(s,STRING_TRIM));llSay(1,llStringTrim(s,STRING_TRIM_HEAD));llSay(1,llStringTrim(s,STRING_TRIM_TAIL));llSay(1,llStringTrim(s,0));}}");
        test(bool(trim), "string trim compiles");
        LslInstance trim_script(trim.program, context(), host);
        const auto trim_start = messages.size();
        test(trim_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + trim_start, messages.end()) ==
                     std::vector<std::string>{"x", "x\xe3\x80\x80", "\xc2\xa0x", "\xc2\xa0x\xe3\x80\x80"},
             "string trim recognizes Unicode whitespace and preserves unknown modes");
        auto unicode = compile_lsl(
            "default{"
                "state_entry(){"
                    "string s=llChar(128512);"
                    "llSay(1,(string)llOrd(s,0));"
                    "llSay(1,(string)llOrd(s,-1));"
                    "llSay(1,(string)llOrd(s,2));"
                    "llSay(1,llChar(55296));"
                    "llSay(1,llChar(0));"
                "}"
            "}");
        test(bool(unicode), "Unicode code-point functions compile");
        LslInstance unicode_script(unicode.program, context(), host);
        const auto unicode_start = messages.size();
        test(unicode_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + unicode_start, messages.end()) ==
                     std::vector<std::string>{"128512", "128512", "0", "\xef\xbf\xbd", ""},
             "Unicode queries handle both halves of a surrogate pair and invalid inputs");
        auto casing = compile_lsl(
            "default{state_entry(){llSay(1,llToUpper(llChar(233)+llChar(66600)));llSay(1,llToLower(llChar(201)+llChar(66560)));}}");
        test(bool(casing), "Unicode case functions compile");
        LslInstance casing_script(casing.program, context(), host);
        const auto casing_start = messages.size();
        test(casing_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + casing_start, messages.end()) ==
                     std::vector<std::string>{"\xc3\x89\xf0\x90\x90\x80", "\xc3\xa9\xf0\x90\x90\xa8"},
             "Unicode casing handles BMP and supplementary characters");
        auto url = compile_lsl(
            "default{state_entry(){llSay(1,llEscapeURL(\"a +/%%\"+llChar(233)));llSay(1,llUnescapeURL(\"a+%20%2F%C3%A9%ZZ\"));}}");
        test(bool(url), "URL text functions compile");
        LslInstance url_script(url.program, context(), host);
        const auto url_start = messages.size();
        test(url_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + url_start, messages.end()) ==
                     std::vector<std::string>{"a%20%2B%2F%25%25%C3%A9", "a+ /\xc3\xa9%ZZ"},
             "URL functions escape data and preserve plus signs and malformed escapes when decoding");
        auto hashes = compile_lsl(
            "default{state_entry(){llSay(1,llMD5String(\"abc\",-7));llSay(1,llSHA1String(\"abc\"));}}");
        test(bool(hashes), "string hash functions compile");
        LslInstance hash_script(hashes.program, context(), host);
        const auto hash_start = messages.size();
        test(hash_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + hash_start, messages.end()) ==
                     std::vector<std::string>{"fbf65b7363e77693704b6156be30a0fe",
                                              "a9993e364706816aba3e25717850c26c9cd0d89d"},
             "string hashes preserve the nonce delimiter and lowercase hexadecimal output");
        auto sha256 =
            compile_lsl("default{state_entry(){llSay(1,llSHA256String(\"\"));llSay(1,llSHA256String(" +
                        quote(std::string(120, 'a')) + "));}}");
        test(bool(sha256), "SHA-256 string function compiles");
        LslInstance sha256_script(sha256.program, context(), host);
        const auto sha256_start = messages.size();
        test(sha256_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + sha256_start, messages.end()) ==
                     std::vector<std::string>{
                         "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                         "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c"},
             "SHA-256 matches empty and multi-block known digests");
        auto base64 = compile_lsl(
            "default{state_entry(){llSay(1,llStringToBase64(llChar(233)));llSay(1,llBase64ToString(\"w6k= \"));llSay(1,llBase64ToString(\"bad!\"));}}");
        test(bool(base64), "Base64 string functions compile");
        LslInstance base64_script(base64.program, context(), host);
        const auto base64_start = messages.size();
        test(base64_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + base64_start, messages.end()) ==
                     std::vector<std::string>{"w6k=", "\xc3\xa9", ""},
             "Base64 functions encode UTF-8, allow whitespace, and return empty for malformed data");
        auto integer64 = compile_lsl(
            "default{"
                "state_entry(){"
                    "llSay(1,llIntegerToBase64(-1));"
                    "llSay(1,(string)llBase64ToInteger(\"/////w==\"));"
                    "llSay(1,(string)llBase64ToInteger(\"AQ\"));"
                    "llSay(1,(string)llBase64ToInteger(\"A!\"));"
                "}"
            "}");
        test(bool(integer64), "integer Base64 functions compile");
        LslInstance integer64_script(integer64.program, context(), host);
        const auto integer64_start = messages.size();
        test(integer64_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + integer64_start, messages.end()) ==
                     std::vector<std::string>{"/////w==", "-1", "16777216", "0"},
             "integer Base64 preserves signed bits, partial input, and invalid digit results");
        auto replace = compile_lsl(
            "default{"
                "state_entry(){"
                    "llSay(1,llReplaceSubString(\"ab ab\",\"(a)(b)\",\"$2$1\",0));"
                    "llSay(1,llReplaceSubString(\"aa\",\"a\",\"b\",-1));"
                    "llSay(1,llReplaceSubString(\"same\",\"[\",\"x\",0));"
                "}"
            "}");
        test(bool(replace), "regex substring replacement compiles");
        LslInstance replace_script(replace.program, context(), host);
        const auto replace_start = messages.size();
        test(replace_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + replace_start, messages.end()) ==
                     std::vector<std::string>{"ba ba", "ab", "same"},
             "substring replacement handles captures, right-to-left counts, and invalid patterns");
        auto replacement_edges = compile_lsl(
            "default{"
                "state_entry(){"
                    "llSay(1,llReplaceSubString(\"ab\",\"(?=.)\",\"-\",0));"
                    "llSay(1,llReplaceSubString(\"ab ab\",\"(?<word>ab)\",\"[${word}]\",-1));"
                    "llSay(1,llReplaceSubString(\"abc\",\"b\",\"$&$_\",1));"
                "}"
            "}");
        test(bool(replacement_edges), "regex replacement edge cases compile");
        LslInstance replacement_script(replacement_edges.program, context(), host);
        const auto replacement_start = messages.size();
        test(replacement_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + replacement_start, messages.end()) ==
                     std::vector<std::string>{"-a-b", "ab [ab]", "ababcc"},
             "zero-width matches advance and replacement supports named and special captures");
        auto lists = compile_lsl(
            "list data=[1,2.5,\"text\",ZERO_VECTOR];"
            "default{"
                "state_entry(){"
                    "list empty;"
                    "llSay(1,(string)llGetListLength(data));"
                    "llSay(1,(string)llGetListLength(empty));"
                    "empty=data;"
                    "llSay(1,(string)llGetListLength(empty));"
                "}"
            "}");
        test(bool(lists), "heterogeneous list literals and variables compile");
        LslInstance list_script(lists.program, context(), host);
        const auto list_start = messages.size();
        test(list_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + list_start, messages.end()) ==
                     std::vector<std::string>{"4", "0", "4"},
             "list lengths reflect literals, defaults, and assignments");
        auto list_access = compile_lsl(
            "default{"
                "state_entry(){"
                    "list values=[3,2.5,NULL_KEY];"
                    "llSay(1,llList2String(values,1));"
                    "llSay(1,(string)llList2Key(values,-1));"
                    "llSay(1,(string)llList2Key(values,0));"
                    "llSay(1,llList2String(values,-4));"
                "}"
            "}");
        test(bool(list_access), "list string and key access compile");
        LslInstance list_reader(list_access.program, context(), host);
        const auto list_access_start = messages.size();
        test(list_reader.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + list_access_start, messages.end()) ==
                     std::vector<std::string>{"2.500000", framework::Uuid{}.str(), "", ""},
             "list access handles negative indices, type checks, and missing entries");
        auto list_ranges = compile_lsl(
            "default{"
                "state_entry(){"
                    "list a=[0,1,2,3,4];"
                    "list b=llList2List(a,3,1);"
                    "llSay(1,llList2String(b,-1));"
                    "llSay(1,llList2String(llDeleteSubList(a,3,1),0));"
                    "llSay(1,(string)llGetListLength(llDeleteSubList(a,-99,-98)));"
                "}"
            "}");
        test(bool(list_ranges), "list ranges compile");
        LslInstance list_range_script(list_ranges.program, context(), host);
        const auto list_range_start = messages.size();
        test(list_range_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + list_range_start, messages.end()) ==
                     std::vector<std::string>{"4", "2", "4"},
             "list ranges wrap and apply the source deletion bounds");
        auto concatenate = compile_lsl(
            "default{"
                "state_entry(){"
                    "list original=[2];"
                    "list joined=1+original+[3];"
                    "joined+=4;"
                    "llSay(1,llList2String(joined,0)+llList2String(joined,-1));"
                    "llSay(1,(string)llGetListLength(original));"
                "}"
            "}");
        test(bool(concatenate), "list concatenation compiles");
        LslInstance concatenate_script(concatenate.program, context(), host);
        const auto concatenate_start = messages.size();
        test(concatenate_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + concatenate_start, messages.end()) ==
                     std::vector<std::string>{"14", "1"},
             "list addition preserves order and does not mutate the source list");
        auto list_compare = compile_lsl(
            "default{state_entry(){llSay(1,(string)([1]==[2]));llSay(1,(string)([1,2]!=[]));llSay(1,(string)([]!=[1,2]));}}");
        test(bool(list_compare), "list comparison compiles");
        LslInstance list_compare_script(list_compare.program, context(), host);
        const auto list_compare_start = messages.size();
        test(list_compare_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + list_compare_start, messages.end()) ==
                     std::vector<std::string>{"1", "2", "-2"},
             "list equality compares lengths and inequality returns the signed length difference");
        auto dump = compile_lsl(
            "default{state_entry(){llSay(1,llDumpList2String([\"\",2,3.5],\" | \"));llSay(1,llDumpList2String([],\"-\"));llSay(1,llDumpList2String([1,2],\"\"));}}");
        test(bool(dump), "list joining compiles");
        LslInstance dump_script(dump.program, context(), host);
        const auto dump_start = messages.size();
        test(dump_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + dump_start, messages.end()) ==
                     std::vector<std::string>{" | 2 | 3.500000", "", "12"},
             "list joining preserves empty entries and uses separators only between items");
        auto find_list = compile_lsl(
            "default{"
                "state_entry(){"
                    "llSay(1,(string)llListFindList([1,1.0,\"x\",1,\"x\"],[1,\"x\"]));"
                    "llSay(1,(string)llListFindList([1],[1.0]));"
                    "llSay(1,(string)llListFindList([1],[]));"
                    "llSay(1,(string)llListFindList([],[]));"
                "}"
            "}");
        test(bool(find_list), "list search compiles");
        LslInstance find_list_script(find_list.program, context(), host);
        const auto find_list_start = messages.size();
        test(find_list_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + find_list_start, messages.end()) ==
                     std::vector<std::string>{"3", "-1", "0", "-1"},
             "list search matches typed contiguous entries and handles empty lists");
        auto list_replace = compile_lsl(
            "default{"
                "state_entry(){"
                    "list a=[0,1,2,3,4];"
                    "llSay(1,llDumpList2String(llListReplaceList(a,[9],1,2),\"\"));"
                    "llSay(1,llDumpList2String(llListReplaceList(a,[9],3,1),\"\"));"
                    "llSay(1,llDumpList2String(llListReplaceList(a,[9],-99,1),\"\"));"
                "}"
            "}");
        test(bool(list_replace), "list replacement compiles");
        LslInstance list_replace_script(list_replace.program, context(), host);
        const auto list_replace_start = messages.size();
        test(list_replace_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + list_replace_start, messages.end()) ==
                     std::vector<std::string>{"0934", "29", "234"},
             "list replacement follows conventional, wrapped, and far-negative ranges");
        auto list_insert = compile_lsl(
            "default{"
                "state_entry(){"
                    "llSay(1,llDumpList2String(llListInsertList([1,2],[9],-1),\"\"));"
                    "llSay(1,llDumpList2String(llListInsertList([1,2],[9],99),\"\"));"
                    "llSay(1,llDumpList2String(llListInsertList([1,2],[9],-99),\"\"));"
                "}"
            "}");
        test(bool(list_insert), "list insertion compiles");
        LslInstance list_insert_script(list_insert.program, context(), host);
        const auto list_insert_start = messages.size();
        test(list_insert_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + list_insert_start, messages.end()) ==
                     std::vector<std::string>{"192", "129", "912"},
             "list insertion handles end-relative indices and clamping");
        auto csv = compile_lsl(
            "default{"
                "state_entry(){"
                    "llSay(1,llList2CSV([1,<2,3,4>,\"x\"]));"
                    "list a=llCSV2List(\" a , <1, 2, 3>, \");"
                    "llSay(1,llDumpList2String(a,\"|\"));"
                    "llSay(1,(string)llGetListLength(llCSV2List(\"\")));"
                "}"
            "}");
        test(bool(csv), "list CSV functions compile");
        LslInstance csv_script(csv.program, context(), host);
        const auto csv_start = messages.size();
        test(csv_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + csv_start, messages.end()) ==
                     std::vector<std::string>{"1, <2.000000, 3.000000, 4.000000>, x", "a|<1, 2, 3>|", "1"},
             "CSV functions preserve bracketed commas, trim fields, and retain empty fields");
        auto parse_strings = compile_lsl(R"LSL(
            default {
                state_entry() {
                    llSay(0, llDumpList2String(llParseString2List(",a,,b+c,", ["", ","], ["+"]), "|"));
                    llSay(0, llDumpList2String(llParseStringKeepNulls(",a,,b+c,", [","], ["+"]), "|"));
                    llSay(0, llDumpList2String(llParseString2List("a::b", [":"], ["::"]), "|"));
                    llSay(0, llDumpList2String(llParseStringKeepNulls("a::b", ["::", ":"], [":"]), "|"));
                    llSay(0, llDumpList2String(llParseStringKeepNulls("a::b", [":", "::"], []), "|"));
                    llSay(0, llDumpList2String(llParseString2List("a2b3.500000c", [2], [3.5]), "|"));
                    llSay(0, (string)llGetListLength(llParseStringKeepNulls("", [","], [])));
                    llSay(0, llDumpList2String(llParseString2List("  x  ", [], [""]), "|"));
                    llSay(0, llDumpList2String(llParseStringKeepNulls("++", [], ["+"]), "|"));
                    string marker = llChar(0x1f680);
                    llSay(0, llDumpList2String(llParseString2List("A" + marker + "aA", [marker, "A"], []), "|"));
                }
            }
        )LSL");
        test(bool(parse_strings), "string parsing functions compile");
        LslInstance parse_strings_script(parse_strings.program, context(), host);
        const auto parse_start = messages.size();
        test(parse_strings_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + parse_start, messages.end()) ==
                     std::vector<std::string>{"a|b|+|c", "|a||b|+|c|", "a|b", "a|b", "a||b", "a|b|3.500000|c",
                                              "0", "  x  ", "|+||+|", "a"},
             "string parsing handles null fields, delimiter precedence, retained spacers, and coercion");
        const framework::Uuid visitor("138f41bf-27ca-4f4b-948f-78746f891cd8");
        const framework::Uuid resident("e1521291-7f02-4a91-b655-fc96d3d5cb8b");
        auto names_host = host;
        names_host.avatar_key_by_name = [&](std::string_view name) {
            return name == "Jane.Example @grid.example:9000" ? visitor : framework::Uuid{};
        };
        names_host.avatar_name = [&](framework::Uuid id) -> std::optional<std::string> {
            return id == resident ? std::optional<std::string>("Jane Resident") : std::nullopt;
        };
        auto names = compile_lsl(R"LSL(
            default {
                state_entry() {
                    llSay(0, (string)llName2Key(" Jane.Example @ grid.example:9000  "));
                    llSay(0, (string)llName2Key("Jane.Example @ ab"));
                    llSay(0, llGetUsername("e1521291-7f02-4a91-b655-fc96d3d5cb8b"));
                }
            }
        )LSL");
        test(bool(names), "avatar name functions compile");
        LslInstance names_script(names.program, context(), names_host);
        const auto avatar_name_query_start = messages.size();
        test(names_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + avatar_name_query_start, messages.end()) ==
                     std::vector<std::string>{visitor.str(), framework::Uuid{}.str(), "jane"},
             "avatar names normalize Hypergrid spelling and preserve the Resident username rule");
        auto entry_type = compile_lsl(
            "default{"
                "state_entry(){"
                    "list a=[1,2.0,\"text\",(string)NULL_KEY,ZERO_VECTOR];"
                    "if(llGetListEntryType(a,0)==TYPE_INTEGER&&"
                    "llGetListEntryType(a,1)==TYPE_FLOAT&&"
                    "llGetListEntryType(a,2)==TYPE_STRING&&"
                    "llGetListEntryType(a,-2)==TYPE_KEY&&"
                    "llGetListEntryType(a,-1)==TYPE_VECTOR&&"
                    "llGetListEntryType(a,5)==TYPE_INVALID)llSay(1,\"typed\");"
                "}"
            "}");
        test(bool(entry_type), "list entry type queries compile");
        LslInstance entry_type_script(entry_type.program, context(), host);
        const auto entry_type_start = messages.size();
        test(entry_type_script.run(100).status == LslRunStatus::Idle &&
                 messages.size() == entry_type_start + 1 && messages.back() == "typed",
             "list types include UUID-shaped text and invalid indices");
        auto list_integers = compile_lsl(
            "default{"
                "state_entry(){"
                    "list a=[\"  -12suffix\",\"0xffffffff\",\"2147483648\",-2.9,ZERO_VECTOR];"
                    "llSay(1,(string)llList2Integer(a,0));"
                    "llSay(1,(string)llList2Integer(a,1));"
                    "llSay(1,(string)llList2Integer(a,2));"
                    "llSay(1,(string)llList2Integer(a,-2));"
                    "llSay(1,(string)llList2Integer(a,-1));"
                "}"
            "}");
        test(bool(list_integers), "list integer access compiles");
        LslInstance list_integer_script(list_integers.program, context(), host);
        const auto list_integer_start = messages.size();
        test(list_integer_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + list_integer_start, messages.end()) ==
                     std::vector<std::string>{"-12", "-1", "-1", "-2", "0"},
             "list integer conversion preserves prefixes, overflow, truncation and vector defaults");
        auto list_floats = compile_lsl(
            "default{"
                "state_entry(){"
                    "list a=[\" -12.5tail\",\"2e3\",\".5\",7,2.5,ZERO_VECTOR];"
                    "llSay(1,(string)llList2Float(a,0));"
                    "llSay(1,(string)llList2Float(a,1));"
                    "llSay(1,(string)llList2Float(a,2));"
                    "llSay(1,(string)(llList2Float(a,3)+llList2Float(a,-2)+llList2Float(a,-1)));"
                "}"
            "}");
        test(bool(list_floats), "list float access compiles");
        LslInstance list_float_script(list_floats.program, context(), host);
        const auto list_float_start = messages.size();
        test(list_float_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + list_float_start, messages.end()) ==
                     std::vector<std::string>{"-12.500000", "2.000000", "0.000000", "9.500000"},
             "list float conversion preserves the source decimal prefix rules");
        auto list_vectors = compile_lsl(
            "default{"
                "state_entry(){"
                    "list a=[<1,2,3>,\"<4, 5, 6>\",\"<7, bad, 9>\"];"
                    "llSay(1,(string)(llList2Vector(a,0)+llList2Vector(a,1)));"
                    "llSay(1,(string)(llList2Vector(a,-1)+llList2Vector(a,9)));"
                "}"
            "}");
        test(bool(list_vectors), "list vector access compiles");
        LslInstance list_vector_script(list_vectors.program, context(), host);
        const auto list_vector_start = messages.size();
        test(list_vector_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + list_vector_start, messages.end()) ==
                     std::vector<std::string>{"<5.000000, 7.000000, 9.000000>",
                                              "<0.000000, 0.000000, 0.000000>"},
             "list vector conversion handles typed, textual, malformed and missing entries");
        auto rotations = compile_lsl(
            "rotation initial;"
            "default{"
                "state_entry(){"
                    "rotation r=initial;"
                    "llSay(1,(string)r);"
                    "r=<1,2,3,4>;"
                    "r.s+=r.x;"
                    "llSay(1,(string)r);"
                    "llSay(1,(string)llGetListEntryType([ZERO_ROTATION],0));"
                "}"
            "}");
        test(bool(rotations), "rotation declarations and literals compile");
        LslInstance rotation_script(rotations.program, context(), host);
        const auto rotation_start = messages.size();
        test(rotation_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + rotation_start, messages.end()) ==
                     std::vector<std::string>{"<0.000000, 0.000000, 0.000000, 1.000000>",
                                              "<1.000000, 2.000000, 3.000000, 5.000000>", "6"},
             "rotation defaults, component assignment, formatting and list entry type");
        auto rotation_ops = compile_lsl(
            "default{"
                "state_entry(){"
                    "rotation a=<1,0,0,0>;"
                    "rotation b=<0,1,0,0>;"
                    "llSay(1,(string)(a*b));"
                    "llSay(1,(string)(a/b));"
                    "a+=b;"
                    "llSay(1,(string)(a-b==<1,0,0,0>));"
                    "llSay(1,(string)(a!=b));"
                "}"
            "}");
        test(bool(rotation_ops), "rotation operators compile");
        LslInstance rotation_ops_script(rotation_ops.program, context(), host);
        const auto rotation_ops_start = messages.size();
        test(rotation_ops_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + rotation_ops_start, messages.end()) ==
                     std::vector<std::string>{"<0.000000, 0.000000, -1.000000, 0.000000>",
                                              "<0.000000, 0.000000, 1.000000, 0.000000>", "1", "1"},
             "rotation composition order, conjugate division, addition, subtraction and comparisons");
        auto vector_rotation = compile_lsl(
            "default{state_entry(){vector v=<1,0,0>;rotation r=<0,0,1,1>;llSay(1,(string)(v*r));v/=r;llSay(1,(string)v);}}");
        test(bool(vector_rotation), "vector rotation operators compile");
        LslInstance vector_rotation_script(vector_rotation.program, context(), host);
        const auto vector_rotation_start = messages.size();
        test(vector_rotation_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + vector_rotation_start, messages.end()) ==
                     std::vector<std::string>{"<-1.000000, 2.000000, 0.000000>",
                                              "<-1.000000, -2.000000, 0.000000>"},
             "vector rotation and inverse rotation preserve non-unit quaternion behavior");
        auto list_rotations = compile_lsl(
            R"LSL(
                default {
                    state_entry() {
                        list a=[<1,2,3,4>,"<5,6,7,8>","<5,bad,7,8>",3];
                        llSay(1,(string)(llList2Rot(a,0).s+llList2Rot(a,-3).s));
                        llSay(1,(string)(llList2Rot(a,2)==ZERO_ROTATION&&llList2Rot(a,3)==ZERO_ROTATION&&llList2Rot(a,4)==ZERO_ROTATION));
                    }
                }
            )LSL");
        test(bool(list_rotations), "list rotation access compiles");
        LslInstance list_rotation_script(list_rotations.program, context(), host);
        const auto list_rotation_start = messages.size();
        test(list_rotation_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + list_rotation_start, messages.end()) ==
                     std::vector<std::string>{"12.000000", "1"},
             "list rotation conversion handles typed, textual, malformed, nonrotation and missing entries");
        auto rotation_cast = compile_lsl(
            R"LSL(
                default {
                    state_entry() {
                        rotation value = (rotation)"<1,2,3,4>";
                        llSay(1, (string)((quaternion)value).s);
                        llSay(1, (string)((rotation)"invalid" == ZERO_ROTATION));
                    }
                }
            )LSL");
        test(bool(rotation_cast), "rotation casts compile");
        LslInstance rotation_cast_script(rotation_cast.program, context(), host);
        const auto rotation_cast_start = messages.size();
        test(rotation_cast_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + rotation_cast_start, messages.end()) ==
                     std::vector<std::string>{"4.000000", "1"},
             "rotation casts parse strings, preserve rotations and default malformed text");
        auto vector_cast = compile_lsl(
            R"LSL(default{state_entry(){vector v=(vector)"<1,2,3>";llSay(1,(string)((vector)v).z);llSay(1,(string)((vector)"invalid"==ZERO_VECTOR));}})LSL");
        test(bool(vector_cast), "vector casts compile");
        LslInstance vector_cast_script(vector_cast.program, context(), host);
        const auto vector_cast_start = messages.size();
        test(vector_cast_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + vector_cast_start, messages.end()) ==
                     std::vector<std::string>{"3.000000", "1"},
             "vector casts parse strings, preserve vectors and default malformed text");
        auto axis_angle = compile_lsl(
            "default{"
                "state_entry(){"
                    "rotation r=llAxisAngle2Rot(<0,0,2>,1);"
                    "llSay(1,(string)llRot2Axis(r));"
                    "llSay(1,(string)llRot2Angle(r));"
                    "llSay(1,(string)llRot2Angle(<0,0,0,0>));"
                "}"
            "}");
        test(bool(axis_angle), "axis-angle functions compile");
        LslInstance axis_angle_script(axis_angle.program, context(), host);
        const auto axis_angle_start = messages.size();
        test(axis_angle_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + axis_angle_start, messages.end()) ==
                     std::vector<std::string>{"<0.000000, 0.000000, 1.000000>", "1.000000", "0.000000"},
             "axis-angle functions normalize their input and handle zero rotations");
        auto angle_between = compile_lsl(
            "default{"
                "state_entry(){"
                    "llSay(1,(string)llAngleBetween(ZERO_ROTATION,<0,0,2,2>));"
                    "llSay(1,(string)llAngleBetween(ZERO_ROTATION,<0,0,0,-2>));"
                    "llSay(1,(string)llAngleBetween(ZERO_ROTATION,<0,0,0,0>));"
                "}"
            "}");
        test(bool(angle_between), "llAngleBetween compiles");
        LslInstance angle_between_script(angle_between.program, context(), host);
        const auto angle_between_start = messages.size();
        test(angle_between_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + angle_between_start, messages.end()) ==
                     std::vector<std::string>{"1.570796", "0.000000", "0.000000"},
             "llAngleBetween handles scaled, opposite-sign and zero quaternions");
        auto rotation_basis = compile_lsl(
            "default{state_entry(){rotation r=<0,0,2,2>;llSay(1,(string)llRot2Fwd(r));llSay(1,(string)llRot2Left(r));llSay(1,(string)llRot2Up(r));}}");
        test(bool(rotation_basis), "rotation direction functions compile");
        LslInstance rotation_basis_script(rotation_basis.program, context(), host);
        const auto rotation_basis_start = messages.size();
        test(rotation_basis_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + rotation_basis_start, messages.end()) ==
                     std::vector<std::string>{"<0.000000, 1.000000, 0.000000>",
                                              "<-1.000000, 0.000000, 0.000000>",
                                              "<0.000000, 0.000000, 1.000000>"},
             "rotation direction functions normalize scaled quaternions");
        auto rotation_between = compile_lsl(
            "default{"
                "state_entry(){"
                    "llSay(1,(string)llRotBetween(<2,0,0>,<0,3,0>));"
                    "llSay(1,(string)llRotBetween(<1,0,0>,<-1,0,0>));"
                    "llSay(1,(string)(llRotBetween(ZERO_VECTOR,<1,0,0>)==ZERO_ROTATION&&llRotBetween(<1,0,0>,<2,0,0>)==ZERO_ROTATION));"
                "}"
            "}");
        test(bool(rotation_between), "llRotBetween compiles");
        LslInstance rotation_between_script(rotation_between.program, context(), host);
        const auto rotation_between_start = messages.size();
        test(rotation_between_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + rotation_between_start, messages.end()) ==
                     std::vector<std::string>{"<0.000000, 0.000000, 0.707107, 0.707107>",
                                              "<0.000000, 0.000000, 1.000000, 0.000000>", "1"},
             "llRotBetween handles orthogonal, opposite, zero and parallel vectors");
        auto euler_rotation = compile_lsl(
            "default{state_entry(){llSay(1,(string)llRot2Euler(llEuler2Rot(<0.2,0.3,0.4>)));llSay(1,(string)llRot2Euler(<0,-2,0,2>));}}");
        test(bool(euler_rotation), "Euler rotation conversions compile");
        LslInstance euler_rotation_script(euler_rotation.program, context(), host);
        const auto euler_rotation_start = messages.size();
        test(euler_rotation_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + euler_rotation_start, messages.end()) ==
                     std::vector<std::string>{"<0.200000, 0.300000, 0.400000>",
                                              "<0.000000, -1.570796, -0.000000>"},
             "Euler conversion round trip and singularity handling");
        auto axes_rotation = compile_lsl(
            "default{"
                "state_entry(){"
                    "llSay(1,(string)llAxes2Rot(<1,0,0>,<0,1,0>,<0,0,1>));"
                    "llSay(1,(string)llAxes2Rot(<1,0,0>,<0,-1,0>,<0,0,-1>));"
                    "llSay(1,(string)llAxes2Rot(<-1,0,0>,<0,1,0>,<0,0,-1>));"
                    "llSay(1,(string)llAxes2Rot(<-1,0,0>,<0,-1,0>,<0,0,1>));"
                "}"
            "}");
        test(bool(axes_rotation), "llAxes2Rot compiles");
        LslInstance axes_rotation_script(axes_rotation.program, context(), host);
        const auto axes_rotation_start = messages.size();
        test(axes_rotation_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + axes_rotation_start, messages.end()) ==
                     std::vector<std::string>{"<0.000000, 0.000000, 0.000000, 1.000000>",
                                              "<1.000000, 0.000000, 0.000000, 0.000000>",
                                              "<0.000000, 1.000000, 0.000000, 0.000000>",
                                              "<0.000000, 0.000000, 1.000000, 0.000000>"},
             "llAxes2Rot handles identity and half-turns around each axis");
        auto strided = compile_lsl(
            R"LSL(
                default {
                    state_entry() {
                        list a=[0,1,2,3,4];
                        llSay(1,llList2CSV(llList2ListStrided(a,1,-1,2)));
                        llSay(1,llList2CSV(llList2ListStrided(a,4,1,2)));
                        llSay(1,llList2CSV(llList2ListStrided(a,1,2,0)));
                        llSay(1,llList2CSV(llList2ListStrided(a,7,9,2)));
                    }
                }
            )LSL");
        test(bool(strided), "strided list slices compile");
        LslInstance strided_script(strided.program, context(), host);
        const auto strided_start = messages.size();
        test(strided_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + strided_start, messages.end()) ==
                     std::vector<std::string>{"2, 4", "0, 2, 4", "1, 2", ""},
             "strided list slices align to list origin and handle reversed and missing bounds");
        auto stride_slice = compile_lsl(
            R"LSL(
                default {
                    state_entry() {
                        list a=[0,1,2,3,4,5];
                        llSay(1,llList2CSV(llList2ListSlice(a,1,-1,2,-1)));
                        llSay(1,llList2CSV(llList2ListSlice(a,0,5,2,2)));
                        llSay(1,llList2CSV(llList2ListSlice(a,5,1,2,1)));
                    }
                }
            )LSL");
        test(bool(stride_slice), "list stride offsets compile");
        LslInstance stride_slice_script(stride_slice.program, context(), host);
        const auto stride_slice_start = messages.size();
        test(stride_slice_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + stride_slice_start, messages.end()) ==
                     std::vector<std::string>{"3, 5", "", "1, 3, 5"},
             "stride offsets handle negative and invalid offsets and reversed bounds");
        auto statistics = compile_lsl(
            R"LSL(
                default {
                    state_entry() {
                        list a=[2,"4",8,"ignored"];
                        llSay(1,(string)llListStatistics(LIST_STAT_RANGE,a));
                        llSay(1,(string)llListStatistics(LIST_STAT_MEDIAN,a));
                        llSay(1,(string)llListStatistics(LIST_STAT_NUM_COUNT,a));
                        llSay(1,(string)llListStatistics(LIST_STAT_SUM_SQUARES,a));
                    }
                }
            )LSL");
        test(bool(statistics), "list statistics compile");
        LslInstance statistics_script(statistics.program, context(), host);
        const auto statistics_start = messages.size();
        test(statistics_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + statistics_start, messages.end()) ==
                     std::vector<std::string>{"4.000000", "4.000000", "3.000000", "84.000000"},
             "list statistics use numeric strings and compute range as a ratio");
        auto list_sort = compile_lsl(
            R"LSL(
                default {
                    state_entry() {
                        llSay(1,llList2CSV(llListSort([3,"z",1,"a"],1,1)));
                        llSay(1,llList2CSV(llListSortStrided(["a",1,"b",3,"c",2],2,-1,0)));
                        llSay(1,llList2CSV(llListSort([3,2,1],2,1)));
                    }
                }
            )LSL");
        test(bool(list_sort), "list sort compiles");
        LslInstance list_sort_script(list_sort.program, context(), host);
        const auto list_sort_start = messages.size();
        test(list_sort_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + list_sort_start, messages.end()) ==
                     std::vector<std::string>{"1, a, 3, z", "b, 3, c, 2, a, 1", "3, 2, 1"},
             "list sort preserves type positions and moves complete strides");
        auto shuffle = compile_lsl(
            R"(
                default {
                    state_entry() {
                        llSay(1, llList2CSV(llListSort(llListRandomize([1, "a", 2, "b", 3, "c"], 2), 2, 1)));
                        llSay(1, llList2CSV(llListRandomize([3, 2, 1], 2)));
                    }
                }
            )");
        test(bool(shuffle), "list randomization compiles");
        LslInstance shuffle_script(shuffle.program, context(), host);
        const auto shuffle_start = messages.size();
        test(shuffle_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + shuffle_start, messages.end()) ==
                     std::vector<std::string>{"1, a, 2, b, 3, c", "3, 2, 1"},
             "list randomization preserves complete records and leaves incomplete strides alone");
        auto find_next = compile_lsl(
            "default{"
                "state_entry(){"
                    "list a=[1,1,1,2];"
                    "llSay(1,(string)llListFindListNext(a,[1,1],1));"
                    "llSay(1,(string)llListFindListNext(a,[1,1],-2));"
                    "llSay(1,(string)llListFindListNext(a,[],-1));"
                    "llSay(1,(string)llListFindListNext([],[],5));"
                "}"
            "}");
        test(bool(find_next), "list occurrence search compiles");
        LslInstance find_next_script(find_next.program, context(), host);
        const auto find_next_start = messages.size();
        test(find_next_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + find_next_start, messages.end()) ==
                     std::vector<std::string>{"1", "0", "3", "0"},
             "list occurrence search handles overlapping matches, reverse search and empty lists");
        auto find_strided = compile_lsl(
            R"LSL(
                default {
                    state_entry() {
                        list a = [0, 1, 0, 1, 2, 1];
                        llSay(1, (string)llListFindStrided(a, [1, 2], 1, -1, 2));
                        llSay(1, (string)llListFindStrided(a, [1, 2], 0, -1, 2));
                    }
                }
            )LSL");
        test(bool(find_strided), "strided list search compiles");
        LslInstance find_strided_script(find_strided.program, context(), host);
        const auto find_strided_start = messages.size();
        test(find_strided_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + find_strided_start, messages.end()) ==
                     std::vector<std::string>{"3", "-1"},
             "strided list search advances from the requested start");
        auto json_lists = compile_lsl(
            R"LSL(
                default {
                    state_entry() {
                        llSay(1, llList2Json(JSON_ARRAY, [1, JSON_TRUE, " null ", "[2]"]));
                        llSay(1, llList2Json(JSON_OBJECT, ["a/b", " hi "]));
                        llSay(1, (string)(llList2Json(JSON_OBJECT, ["missing"]) == JSON_INVALID));
                    }
                }
            )LSL");
        test(bool(json_lists), "list JSON serialization compiles");
        LslInstance json_lists_script(json_lists.program, context(), host);
        const auto json_lists_start = messages.size();
        test(json_lists_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + json_lists_start, messages.end()) ==
                     std::vector<std::string>{"[1,true,null,[2]]", R"({"a\/b":"hi"})", "1"},
             "list JSON serialization handles markers, embedded JSON and object validation");
        auto json_parse = compile_lsl(
            R"LSL(
                default {
                    state_entry() {
                        list values = llJson2List("{\"z\":true,\"a\":[1,null]}");
                        llSay(1, llList2String(values, 0));
                        llSay(1, (string)(llList2String(values, 1) == JSON_TRUE));
                        llSay(1, llList2String(values, 3));
                        llSay(1, llList2String(llJson2List("[broken"), 0));
                    }
                }
            )LSL");
        test(bool(json_parse), "JSON list parsing compiles");
        LslInstance json_parse_script(json_parse.program, context(), host);
        const auto json_parse_start = messages.size();
        test(json_parse_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + json_parse_start, messages.end()) ==
                     std::vector<std::string>{"z", "1", "[1,null]", "[broken"},
             "JSON list parsing preserves object order, markers, nested JSON and malformed input");
        auto json_lookup = compile_lsl(
            R"LSL(
                default {
                    state_entry() {
                        string document = "{\"a\":[1.2500,null]}";
                        llSay(1, llJsonGetValue(document, ["a", 0]));
                        llSay(1, (string)(llJsonValueType(document, ["a", 1]) == JSON_NULL));
                        llSay(1, (string)(llJsonValueType("true", []) == JSON_TRUE));
                        llSay(1, (string)(llJsonGetValue(document, ["a", -1]) == JSON_INVALID));
                    }
                }
            )LSL");
        test(bool(json_lookup), "JSON lookup compiles");
        LslInstance json_lookup_script(json_lookup.program, context(), host);
        const auto json_lookup_start = messages.size();
        test(json_lookup_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + json_lookup_start, messages.end()) ==
                     std::vector<std::string>{"1.25", "1", "1", "1"},
             "JSON lookup handles nested paths, scalar types and invalid indices");
        auto json_set = compile_lsl(
            R"LSL(
                default {
                    state_entry() {
                        string document = llJsonSetValue("", ["a", 0], "2");
                        llSay(1, document);
                        document = llJsonSetValue(document, ["a", JSON_APPEND], JSON_TRUE);
                        llSay(1, document);
                        llSay(1, llJsonSetValue(document, ["a", 0], JSON_DELETE));
                    }
                }
            )LSL");
        test(bool(json_set), "JSON updates compile");
        LslInstance json_set_script(json_set.program, context(), host);
        const auto json_set_start = messages.size();
        test(json_set_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + json_set_start, messages.end()) ==
                     std::vector<std::string>{R"({"a":[2]})", R"({"a":[2,true]})", R"({"a":[true]})"},
             "JSON updates create nested containers, append values and delete array entries");
        auto hash = compile_lsl(
            R"(default{state_entry(){llSay(1,(string)llHash("abc"));llSay(1,(string)llHash(llChar(128512)));llSay(1,(string)llHash(""));}})");
        test(bool(hash), "llHash compiles");
        LslInstance lsl_hash_script(hash.program, context(), host);
        const auto lsl_hash_start = messages.size();
        test(lsl_hash_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + lsl_hash_start, messages.end()) ==
                     std::vector<std::string>{"807794786", "128512", "0"},
             "llHash wraps the accumulator and hashes supplementary characters as one code point");
        auto compute_hash = compile_lsl(
            R"LSL(
                default {
                    state_entry() {
                        llSay(1,llComputeHash("abc","md5"));
                        llSay(1,llComputeHash("abc","md5_sha1"));
                        llSay(1,llComputeHash("abc","sha1"));
                        llSay(1,llComputeHash("abc","sha224"));
                        llSay(1,llComputeHash("abc","sha256"));
                        llSay(1,llComputeHash("abc","sha384"));
                        llSay(1,llComputeHash("abc","sha512"));
                        llSay(1,llComputeHash("abc","SHA256"));
                    }
                }
            )LSL");
        test(bool(compute_hash), "llComputeHash compiles");
        LslInstance compute_hash_script(compute_hash.program, context(), host);
        const auto compute_hash_start = messages.size();
        test(
            compute_hash_script.run(100).status == LslRunStatus::Idle &&
                std::vector<std::string>(messages.begin() + compute_hash_start, messages.end()) ==
                    std::vector<std::string>{
                        "900150983cd24fb0d6963f7d28e17f72",
                        "900150983cd24fb0d6963f7d28e17f72a9993e364706816aba3e25717850c26c9cd0d89d",
                        "a9993e364706816aba3e25717850c26c9cd0d89d",
                        "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7",
                        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                        "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7",
                        "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
                        ""},
            "llComputeHash matches known digests and rejects unsupported algorithm names");
        for (
            const auto& [key, message, algorithm, expected] :
            std::vector<std::tuple<std::string, std::string, std::string, std::string>>{
                {"key", "abc", "md5", "d2fe98063f876b03193afb49b4979591"},
                {"key", "abc", "sha1", "4fd0b215276ef12f2b3e4c8ecac2811498b656fc"},
                {"key", "abc", "sha224", "f524670b7e34f31467de0aa96593861cf65117d414fb2d86158d760e"},
                {"key", "abc", "sha256", "9c196e32dc0175f86f4b1cb89289d6619de6bee699e4c378e68309ed97a1a6ab"},
                {"key", "abc", "sha384",
                 "30ddb9c8f347cffbfb44e519d814f074cf4047a55d6f563324f1c6a33920e5edfb2a34bac60bdc96cd33a95623d7d638"},
                {"key", "abc", "sha512",
                 "3926a207c8c42b0c41792cbd3e1a1aaaf5f7a25704f62dfc939c4987dd7ce060009c5bb1c2447355b3216f10b537e9afa7b64a4e5391b0d631172d07939e087a"},
                {"kkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkk", "abc", "sha224",
                 "d7c08f2894c969ffb6680dcda70d297ed9628ef6c480a727123ef73f"},
                {"", "abc", "sha256", ""},
                {"key", "abc", "md5_sha1", ""}}) {
            const auto source = "default{state_entry(){llSay(1,llHMAC(\"" + key + "\",\"" + message +
                                "\",\"" + algorithm + "\"));}}";
            auto hmac_script = compile_lsl(source);
            test(bool(hmac_script), "llHMAC compiles");
            LslInstance instance(hmac_script.program, context(), host);
            const auto start = messages.size();
            test(instance.run(100).status == LslRunStatus::Idle && messages.size() == start + 1 &&
                     messages.back() == expected,
                 "llHMAC digest matches expected output");
        }
        auto xor_base64 = compile_lsl(
            R"LSL(
                default {
                    state_entry() {
                        llSay(1, llXorBase64StringsCorrect("YWJj", "AQ"));
                        llSay(1, llXorBase64StringsCorrect("YWJj", ""));
                        llSay(1, llXorBase64StringsCorrect("!", "AQ=="));
                        llSay(1, llXorBase64("YWJj!ignored", "AQ?ignored"));
                    }
                }
            )LSL");
        test(bool(xor_base64), "Base64 XOR compiles");
        LslInstance xor_base64_script(xor_base64.program, context(), host);
        const auto xor_base64_start = messages.size();
        test(xor_base64_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + xor_base64_start, messages.end()) ==
                     std::vector<std::string>{"YGNi", "YWJj", "", "YGNi"},
             "Base64 XOR cycles the decoded mask and handles missing padding and invalid data");
        auto legacy_xor = compile_lsl(
            R"LSL(default{state_entry(){llSay(1,llXorBase64Strings("YWJj","AQ"));llSay(1,"awake");}})LSL");
        test(bool(legacy_xor), "legacy Base64 XOR compiles");
        std::int64_t legacy_xor_clock = 0;
        std::vector<std::int32_t> legacy_xor_delays;
        auto legacy_xor_host = host;
        legacy_xor_host.clock_ticks = [&] {
            return legacy_xor_clock;
        };
        legacy_xor_host.script_delay_factor = 0.5f;
        legacy_xor_host.request_sleep = [&](auto delay) {
            legacy_xor_delays.push_back(delay);
        };
        LslInstance legacy_xor_script(legacy_xor.program, context(), legacy_xor_host);
        test(legacy_xor_script.run(100).status == LslRunStatus::Sleeping && messages.back() == "YGJz" &&
                 legacy_xor_delays == std::vector<std::int32_t>{150},
             "legacy XOR uses sextets and applies the API delay factor");
        legacy_xor_clock = 1499999;
        test(legacy_xor_script.run(100).status == LslRunStatus::Sleeping,
             "legacy XOR waits until its deadline");
        legacy_xor_clock = 1500000;
        test(legacy_xor_script.run(100).status == LslRunStatus::Idle && messages.back() == "awake",
             "legacy XOR resumes the handler");
        auto owner_lookup = compile_lsl(R"LSL(default{state_entry(){
  llSay(1,(string)(llGetOwnerKey(llGetKey())==llGetOwner()));
  llSay(1,(string)llGetOwnerKey("00000000-0000-4000-8000-000000000abc"));
  llSay(1,(string)llGetOwnerKey("00000000-0000-4000-8000-000000000DEF"));
  llSay(1,(string)(llGetOwnerKey("invalid")==NULL_KEY));
 }})LSL");
        test(bool(owner_lookup), "owner lookup compiles");
        auto owner_lookup_host = host;
        owner_lookup_host.object_owner =
            [](opensim::framework::Uuid id) -> std::optional<opensim::framework::Uuid> {
            if (id == opensim::framework::Uuid("00000000-0000-4000-8000-000000000abc"))
                return opensim::framework::Uuid("00000000-0000-4000-8000-000000000123");
            return {};
        };
        LslInstance owner_lookup_script(owner_lookup.program, context(), owner_lookup_host);
        const auto owner_lookup_start = messages.size();
        test(owner_lookup_script.run(100).status == LslRunStatus::Idle &&
                 std::vector<std::string>(messages.begin() + owner_lookup_start, messages.end()) ==
                     std::vector<std::string>{"1", "00000000-0000-4000-8000-000000000123",
                                              "00000000-0000-4000-8000-000000000DEF", "1"},
             "owner lookup resolves objects and preserves unmatched UUID spelling");
        for (const auto& [file, expected] : std::vector<std::pair<std::string, std::string>>{
                 {"DefaultScript.lsl", "Script running"}, {"DefaultSayAndTouch.lsl", "I am Alive!"}}) {
            std::ifstream f(std::string(LSL_ASSET_DIR) + "/" + file);
            std::stringstream s;
            s << f.rdbuf();
            test(bool(f), "asset read");
            auto c = compile_lsl(s.str());
            test(bool(c), "actual default asset compile");
            LslInstance i(c.program, context(), host);
            test(i.handles_event("touch_start") == (file == "DefaultSayAndTouch.lsl"),
                 "actual handler presence");
            auto r = i.run(100);
            test(r.status == LslRunStatus::Idle && messages.back() == expected,
                 "actual default asset execution");
            if (file == "DefaultSayAndTouch.lsl") {
                test(i.post_event({"touch_start", {std::int32_t(1)}, {{context().owner}}}),
                     "real touch post");
                i.run(100);
                test(messages.back() == "Touched.", "touch executes source handler");
            }
        }
        auto compiled = compile_lsl(
            "default { state_entry() { {llSay(1, \"x\\tY\\q\\n\" + (string)(2147483647 + 1));} llOwnerSay(\"last\"); } touch_start(integer n) { llSay(n-1, (string)(n + 2)); }}");
        test(bool(compiled), "typed expressions compile");
        LslInstance i(compiled.program, context(), host);
        auto before = messages.size();
        test(i.run(0).status == LslRunStatus::BudgetExhausted && messages.size() == before, "zero budget");
        test(i.run(1).status == LslRunStatus::BudgetExhausted && messages.back() == "x    Yq\n-2147483648",
             "escapes wrapping casts continuation");
        test(i.run(1).status == LslRunStatus::Idle && messages.back() == "last", "resume no replay");
        bool detector = false;
        LslHost touches = host;
        LslInstance* current = nullptr;
        touches.chat = [&](const LslChat& x) {
            detector = current->current_event()->detectors.at(0).key == context().owner;
            messages.push_back(x.text);
        };
        LslInstance t(compile_lsl("default{touch_start(integer n){llSay(n-1,(string)(n+2));}}").program,
                      context(), touches);
        current = &t;
        t.run(100);
        test(t.post_event({"touch_start", {std::int32_t(4)}, {{context().owner}}}), "typed touch post");
        t.run(100);
        test(detector && messages.back() == "6", "parameter and actual detector retained");
        for (const auto& source :
             {"default{state_entry(){ llSay(0,\"oops\"); unknown(); }}", "default{timer(integer n){}}",
              "default{touch_start(string n){}}", "default{state_entry(){llSay(\"0\",\"x\");}}",
              "default{state_entry(){llOwnerSay(1);}}", "default{state_entry(){integer n;integer n;}}",
              "default{state_entry(){llSay(0,\"unterminated);}}", "default{} state default{}",
              "default{state_entry(){}state_entry(){}}", "default{state_entry(){llSay(0,4294967296);}}"})
            test(!compile_lsl(source), "unsupported program rejected before execution");
        auto queue_program = compile_lsl("default{touch_start(integer n){llOwnerSay((string)n);}}");
        LslInstance q(queue_program.program, context(), host);
        q.run(100);
        for (int n = 0; n < 64; n++)
            test(q.post_event({"touch_start", {std::int32_t(n)}, {}}), "per-event queue admission");
        test(!q.post_event({"touch_start", {std::int32_t(65)}, {}}), "queue cap64");
        test(q.post_event({"state_entry", {}, {}}), "independent event count");
        test(q.run(1).status == LslRunStatus::BudgetExhausted && messages.back() == "0",
             "priority empty state_entry");
        q.reset();
        test(q.queued_events() == 1 && q.run(100).status == LslRunStatus::Idle,
             "reset clears pending events");
        const auto omega_program = compile_lsl(
            "default{state_entry(){vector axis=<0,-1,2>;llTargetOmega(axis+<0,.5,0>,1+.5,1);llTargetOmega(<0,-.5,2>,1.5,1e-7);}}");
        test(bool(omega_program), "target omega compiles");
        std::vector<opensim::framework::Vector3> angular_velocities;
        LslHost omega_host;
        omega_host.target_omega = [&](auto id, auto velocity, bool full_refresh) {
            test(id == context().object, "target omega affects the script host");
            test(full_refresh == (velocity == opensim::framework::Vector3{}),
                 "zero gain requests full refresh");
            angular_velocities.push_back(velocity);
        };
        LslInstance omega(omega_program.program, context(), omega_host);
        test(omega.run(100).status == LslRunStatus::Idle &&
                 angular_velocities == std::vector<opensim::framework::Vector3>{{0, -.75f, 3}, {}},
             "target omega scales the axis and clears it at zero gain");
        LslHost missing;
        LslInstance fault(compile_lsl("default{state_entry(){llSay(0,\"x\");}}").program, context(), missing);
        test(fault.run(1).status == LslRunStatus::Faulted, "missing host explicit fault");
        auto control_program = compile_lsl(
            "default{state_entry(){llOwnerSay(\"one\");llOwnerSay(\"two\");}touch_start(integer n){llOwnerSay(\"touch\");}}");
        std::vector<std::string> control_messages;
        LslHost control_host = host;
        control_host.chat = [&](const LslChat& chat) {
            control_messages.push_back(chat.text);
        };
        LslInstance controlled(control_program.program, context(), control_host);
        test(controlled.running() && controlled.run(1).status == LslRunStatus::BudgetExhausted &&
                 control_messages == std::vector<std::string>{"one"},
             "running initial event budget");
        test(controlled.post_event({"touch_start", {std::int32_t(1)}, {}}), "pending event before stop");
        controlled.running(false);
        test(!controlled.running() && controlled.queued_events() == 0 &&
                 controlled.run(100).status == LslRunStatus::Idle && control_messages.size() == 1,
             "stop clears pending without executing active continuation");
        test(controlled.current_event() && controlled.current_event()->name == "state_entry",
             "stop preserves active event context");
        test(controlled.post_event({"touch_start", {std::int32_t(1)}, {}}) &&
                 controlled.queued_events() == 0 && !controlled.post_event({"missing", {}, {}}),
             "stopped known events ignored; unknown events rejected");
        controlled.running(true);
        test(controlled.run(100).status == LslRunStatus::Idle &&
                 control_messages == std::vector<std::string>{"one", "two"},
             "start resumes active PC without replay or stopped event");
        controlled.running(false);
        controlled.reset();
        test(!controlled.running() && controlled.queued_events() == 1 &&
                 controlled.run(100).status == LslRunStatus::Idle && control_messages.size() == 2,
             "reset stopped script preserves false and queues one state entry");
        controlled.running(true);
        controlled.run(100);
        test(control_messages == std::vector<std::string>{"one", "two", "one", "two"},
             "start after stopped reset runs fresh default entry");
        control_messages.clear();
        LslInstance born_stopped(control_program.program, context(), control_host, false);
        test(!born_stopped.running() && born_stopped.queued_events() == 1 &&
                 born_stopped.run(100).status == LslRunStatus::Idle && control_messages.empty(),
             "construction while stopped retains initial entry without execution");
        born_stopped.running(true);
        born_stopped.run(100);
        test(control_messages == std::vector<std::string>{"one", "two"},
             "first start of compiled stopped instance executes initial entry");
        std::int64_t sleep_clock = 0;
        std::vector<std::string> sleep_messages;
        LslHost sleep_host;
        sleep_host.clock_ticks = [&] {
            return sleep_clock;
        };
        sleep_host.chat = [&](const LslChat& chat) {
            sleep_messages.push_back(chat.text);
        };
        std::string sleep_source = "default{state_entry(){";
        for (int n = 0; n < 12; ++n)
            sleep_source += "llSay(0,\"sleep\");";
        sleep_source += "}}";
        LslInstance sleeping(compile_lsl(sleep_source).program, context(), sleep_host);
        test(sleeping.run(100).status == LslRunStatus::Sleeping && sleep_messages.size() > 0 &&
                 sleep_messages.size() < 12,
             "real API sleep suspends active event");
        auto sleeping_count = sleep_messages.size();
        sleeping.running(false);
        sleeping.run(100);
        sleeping.running(true);
        test(sleeping.run(100).status == LslRunStatus::Sleeping && sleep_messages.size() == sleeping_count,
             "stop and start preserve actual sleep deadline");
        sleep_clock += 20000000;
        sleeping.run(100);
        test(sleep_messages.size() == 12, "wake resumes remaining instructions without replay");
        test(fault.running(), "retained execution-faulted instance still reports running");
        auto touch_only = compile_lsl("default { touch_start(integer n) {} }");
        LslInstance touch_mask(touch_only.program, context(), {});
        test(touch_mask.event_mask() == (1ull << 21),
             "implicit runtime state entry is not a declared event registration");
        LslInstance entry_mask(
            compile_lsl("default { state_entry() {} touch(integer n) {} touch_end(integer n) {} }").program,
            context(), {});
        test(entry_mask.event_mask() == ((1ull << 30) | (1ull << 3) | (1ull << 29)),
             "declared state/touch event flags match source enum");
        std::cout << "PASS: " << checks << " native LSL subset/asset/queue/budget checks\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
