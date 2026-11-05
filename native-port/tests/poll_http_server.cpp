#include "opensim/server/poll_http_service.hpp"
#include <iostream>
#include <map>
using namespace opensim;
using namespace server;
using namespace network;
struct Row {
    std::string key;
    PollTablePtr data;
};
int main(int argc, char** argv) {
    try {
        std::mutex mutex;
        std::map<framework::Uuid, Row> rows;
        std::atomic<int> drops = 0, errors = 0;
        PollHttpService polls(2, [&](const std::string&, const std::exception&) { ++errors; });
        RegistryCapabilityListener registry(CapabilityListenerInfo{});
        auto normal = std::make_shared<services::CapabilityPollHandler>();
        normal->url = "/poll";
        normal->timeout_ms = 60000;
        normal->request = [&](auto id, const HttpRequest& request) -> PollResponsePtr {
            std::lock_guard lock(mutex);
            rows[id] = {request.body, {}};
            return {};
        };
        normal->has_events = [&](auto id, auto) {
            std::lock_guard lock(mutex);
            auto i = rows.find(id);
            return i != rows.end() && bool(i->second.data);
        };
        normal->get_events = [&](auto id, auto) {
            std::lock_guard lock(mutex);
            auto i = rows.find(id);
            if (i == rows.end())
                return PollTablePtr{};
            auto data = i->second.data;
            rows.erase(i);
            return data;
        };
        normal->drop = [&](auto id, auto) {
            std::lock_guard lock(mutex);
            rows.erase(id);
            ++drops;
        };
        normal->no_events = [](auto, auto) {
            auto data = std::make_shared<PollTable>();
            data->set("int_response_code", int32_t(502));
            return data;
        };
        registry.add_poll(normal);
        auto direct = std::make_shared<services::CapabilityPollHandler>();
        direct->url = "/direct";
        direct->request = [](auto, const HttpRequest& request) {
            auto response = PollHttpService::response(request);
            response->status_code(202);
            response->add_header("Content-Type", std::string("text/plain"));
            response->buffer(std::make_shared<std::vector<uint8_t>>(
                std::initializer_list<uint8_t>{'d', 'i', 'r', 'e', 'c', 't'}));
            return response;
        };
        registry.add_poll(direct);
        std::shared_ptr<HttpResponseState> saved_model;
        std::shared_ptr<HttpMemoryStream> saved_body;
        auto body = std::make_shared<services::CapabilityPollHandler>();
        body->url = "/body";
        body->request = [&](auto, const HttpRequest& request) {
            auto response = PollHttpService::response(request);
            auto model = PollHttpService::response_state(response);
            auto stream = std::dynamic_pointer_cast<HttpMemoryStream>(model->body());
            std::string text = "stream body é";
            stream->write(std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
            response->buffer(
                std::make_shared<std::vector<uint8_t>>(std::initializer_list<uint8_t>{'r', 'a', 'w', ':'}));
            model->content_type("application/octet-stream");
            model->content_length(999999);
            model->chunked(true);
            model->cookies()->add(std::make_shared<ResponseCookie>(
                "session", "a+b c", HttpDateTime{}, HttpName("/scope"), HttpName("example.test")));
            model->priority(5);
            model->priority(2);
            response->keep_alive_timeout(1000);
            model->add_header("X-Priority", std::to_string(model->priority()));
            model->add_header("X-Keep", std::to_string(model->keep_alive()));
            model->add_header("X-Request-Id", std::to_string(model->request_id()));
            model->add_header("X-Arrival-Positive", model->request_timestamp() > 0 ? "yes" : "no");
            {
                std::lock_guard lock(mutex);
                saved_model = model;
                saved_body = stream;
            }
            return response;
        };
        registry.add_poll(body);
        auto negative = std::make_shared<services::CapabilityPollHandler>();
        negative->url = "/keep-negative";
        negative->request = [](auto, const HttpRequest& request) {
            auto response = PollHttpService::response(request);
            response->keep_alive_timeout(-1);
            return response;
        };
        registry.add_poll(negative);
        auto slow_body = std::make_shared<services::CapabilityPollHandler>();
        slow_body->url = "/slow-body";
        slow_body->request = [&](auto, const HttpRequest& request) {
            auto response = PollHttpService::response(request);
            auto model = PollHttpService::response_state(response);
            auto stream = std::dynamic_pointer_cast<HttpMemoryStream>(model->body());
            std::vector<uint8_t> bytes(16 * 1024 * 1024, 'b');
            stream->write(bytes);
            {
                std::lock_guard lock(mutex);
                saved_model = model;
                saved_body = stream;
            }
            return response;
        };
        registry.add_poll(slow_body);
        auto timeout = std::make_shared<services::CapabilityPollHandler>();
        timeout->url = "/timeout";
        timeout->timeout_ms = 30;
        timeout->has_events = [](auto, auto) { return false; };
        timeout->no_events = normal->no_events;
        timeout->drop = [](auto, auto) {};
        registry.add_poll(timeout);
        auto instant = std::make_shared<services::CapabilityPollHandler>();
        instant->url = "/instant";
        instant->has_events = [](auto, auto) { return true; };
        instant->get_events = [](auto, auto) {
            auto data = std::make_shared<PollTable>();
            data->set("content_type", std::string("text/plain"));
            data->set("str_response_string", std::string("instant"));
            return data;
        };
        registry.add_poll(instant);
        HttpServerOptions options;
        options.port = 0;
        options.workers = 1;
        options.queued_requests = 128;
        options.timeout_seconds = 10;
        if (argc == 3) {
            options.certificate = argv[1];
            options.private_key = argv[2];
        }
        AsyncHttpServer http(
            options, [&](const HttpRequest& request, std::shared_ptr<HttpResponseChannel> channel) {
                polls.dispatch(registry, request, channel, [&](const HttpRequest& incoming) {
                    HttpResponse response;
                    if (incoming.path == "/stats") {
                        size_t count;
                        {
                            std::lock_guard lock(mutex);
                            count = rows.size();
                        }
                        response.body =
                            std::to_string(count) + ":" + std::to_string(int32_t(normal->requests_received)) +
                            ":" + std::to_string(int32_t(normal->requests_handled)) + ":" +
                            std::to_string(drops.load()) + ":" +
                            std::to_string(int32_t(direct->requests_received)) + ":" +
                            std::to_string(int32_t(direct->requests_handled)) + ":" +
                            std::to_string(int32_t(timeout->requests_handled)) + ":" +
                            std::to_string(errors.load()) + ":" + std::to_string(polls.pending_count());
                    } else if (incoming.path == "/body-state") {
                        std::shared_ptr<HttpResponseState> model;
                        std::shared_ptr<HttpMemoryStream> stream;
                        {
                            std::lock_guard lock(mutex);
                            model = saved_model;
                            stream = saved_body;
                        }
                        response.body = model ? std::to_string(model->headers_sent()) + ":" +
                                                    std::to_string(model->sent()) + ":" +
                                                    std::to_string(stream->can_read()) + ":" +
                                                    std::to_string(bool(model->header_bytes())) + ":" +
                                                    std::to_string(bool(model->raw_buffer()))
                                              : "none";
                    } else if (incoming.path == "/event") {
                        auto split = incoming.body.find('|');
                        auto key = incoming.body.substr(0, split);
                        auto mode = split == std::string::npos ? "text" : incoming.body.substr(split + 1);
                        auto data = std::make_shared<PollTable>();
                        data->set("content_type", std::string("text/plain"));
                        data->set("str_response_string", key);
                        if (mode == "binary" || mode == "range") {
                            data->set("content_type", std::string("application/octet-stream"));
                            data->set("bin_response_data",
                                      std::make_shared<std::vector<uint8_t>>(
                                          std::initializer_list<uint8_t>{0, 1, 2, 253, 254, 255}));
                            data->set("bin_start", int32_t(1));
                            data->set("int_bytes", int32_t(mode == "binary" ? 3 : -1));
                            auto headers = std::make_shared<PollTable>();
                            headers->set("X-Test", std::string("present"));
                            data->set("headers", headers);
                        }
                        if (mode == "large" || mode == "slow") {
                            data->set("bin_response_data", std::make_shared<std::vector<uint8_t>>(
                                                               (mode == "slow" ? 16 : 1) * 1024 * 1024, 42));
                            data->set("content_type", std::string("application/octet-stream"));
                        }
                        if (mode == "redirect") {
                            data->set("int_response_code", int32_t(301));
                            data->set("str_redirect_location", std::string("/moved"));
                        }
                        if (mode == "custom") {
                            data->set("http_protocol_version", std::string("HTTP/1.0"));
                            data->set("error_status_text", std::string("Custom Ready"));
                            data->set("keepalive", false);
                        }
                        if (mode == "base64") {
                            data->set("content_type", {});
                            data->set("str_response_string", std::string("YWJj"));
                        }
                        if (mode == "created")
                            data->set("int_response_code", int32_t(201));
                        std::lock_guard lock(mutex);
                        bool found = false;
                        for (auto& [id, row] : rows)
                            if (row.key == key) {
                                row.data = data;
                                found = true;
                            }
                        response.status = found ? 200 : 404;
                    } else if (incoming.path == "/stop") {
                        polls.stop();
                        response.body = "stopped";
                    } else if (incoming.path == "/ready") {
                        response.status = 503;
                        response.body = "Native world runtime incomplete\n";
                    } else
                        response.body = "ordinary:" + incoming.path;
                    return response;
                });
            });
        polls.start();
        http.start([](int port) { std::cout << "LISTENING " << port << std::endl; });
        std::string line;
        std::getline(std::cin, line);
        polls.stop();
        http.stop();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
