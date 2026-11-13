// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/nwc_http.hpp"
#include "opensim/network/web_http_clients.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/config_xml_typed_http_services.hpp"
#include "opensim/framework/config_http_cookies_native.hpp"
#include "opensim/framework/nini_streams.hpp"
#include "opensim/framework/nini_xml_reader.hpp"
#include "opensim/framework/text.hpp"
namespace opensim::network {
namespace {
class BodyStream final : public framework::NiniByteStream {
    std::shared_ptr<framework::ConfigXmlStream> stream_;

  public:
    explicit BodyStream(std::shared_ptr<framework::ConfigXmlStream> s) : stream_(std::move(s)) {
    }
    bool can_read() override {
        return true;
    }
    bool can_seek() override {
        return false;
    }
    bool can_write() override {
        return false;
    }
    std::int64_t position() override {
        throw std::logic_error("Response body does not seek");
    }
    std::size_t read(std::span<std::uint8_t> b, std::size_t offset, std::size_t count) override {
        return static_cast<std::size_t>(stream_->read(b.subspan(offset, count)));
    }
    void write(std::span<const std::uint8_t>) override {
        throw std::logic_error("Response body is read-only");
    }
    void flush() override {
    }
    void close() override {
        stream_->close();
    }
};
}
struct NwcXmlRpcHttpTransport::State {
    std::shared_ptr<framework::ConfigUriObjectBackend> uris = framework::native_config_uri_object_backend();
    framework::ConfigXmlUriObjects objects;
    framework::ConfigXmlHttpRequester request;
    explicit State(framework::ConfigXmlNativeHttpServices services) {
        auto bound = framework::config_xml_bind_typed_http(std::move(services), objects, *uris, [] {
            return framework::native_invariant_config_http_cookies_backend();
        });
        request = framework::config_xml_native_http_requester(std::move(bound), false);
    }
    explicit State(std::shared_ptr<WebHttpClients> shared) : clients(std::move(shared)) {
        if (!clients)
            throw std::invalid_argument("XML-RPC requires shared HTTP clients");
    }
    std::shared_ptr<WebHttpClients> clients;
};
NwcXmlRpcHttpTransport::NwcXmlRpcHttpTransport(framework::ConfigXmlNativeHttpServices services)
    : state_(std::make_shared<State>(std::move(services))) {
}
NwcXmlRpcHttpTransport::NwcXmlRpcHttpTransport(std::shared_ptr<WebHttpClients> clients)
    : state_(std::make_shared<State>(std::move(clients))) {
}
std::shared_ptr<NwcXmlRpcResponse>
NwcXmlRpcHttpTransport::send(const std::shared_ptr<NwcXmlRpcRequest>& request,
                             const std::optional<std::u16string>& url, std::int32_t timeout,
                             std::function<bool()> interrupted) {
    auto s = state_;
    auto client = s->clients ? s->clients->get_new_global_client(timeout) : nullptr;
    framework::ConfigUriObjectPtr uri;
    if (url && !url->empty())
        uri = s->uris->create(std::make_shared<const std::u16string>(*url),
                              framework::ConfigUriObjectKind::RelativeOrAbsolute);
    // Request URI construction precedes serialization, but the client's absolute
    // URI check follows it, matching HttpRequestMessage + HttpClient.Send.
    auto body = nwc_xml_rpc_request_bytes(request);
    if (!uri || !uri->is_absolute())
        throw NwcXmlRpcError(
            "System.InvalidOperationException",
            u"An invalid request URI was provided. Either the request URI must be an absolute URI or BaseAddress must be set.");
    if (*uri->scheme() != u"http" && *uri->scheme() != u"https")
        throw NwcXmlRpcError("System.NotSupportedException",
                             u"The '" + *uri->scheme() + u"' scheme is not supported.");
    framework::ConfigXmlHttpOperation operation;
    operation.method = "POST";
    operation.request_interrupted = interrupted;
    operation.body_interrupted = std::move(interrupted);
    operation.body = std::move(body);
    operation.timeout_milliseconds = timeout > 0 ? timeout : 30000;
    operation.redirect_limit = 10;
    operation.headers = {{"Connection", framework::nini_writer_text(u"Keep-Alive")},
                         {"Keep-Alive", framework::nini_writer_text(u"timeout=30, max=10")},
                         {"Content-Type", framework::nini_writer_text(u"text/xml")},
                         {"Content-Length", framework::nini_writer_text(
                                                framework::utf16(std::to_string(operation.body.size())))}};
    std::shared_ptr<framework::ConfigXmlStream> stream;
    try {
        stream = client ? client->send_headers(std::move(uri), operation, true)
                        : s->request(s->objects.wrap(std::move(uri)), operation);
    } catch (const framework::ConfigXmlHttpError& error) {
        if (error.source_type == "System.Threading.Tasks.TaskCanceledException")
            throw NwcXmlRpcError("System.Net.Http.HttpRequestException", u"request timeout");
        throw;
    }
    std::shared_ptr<NwcXmlRpcResponse> result;
    std::shared_ptr<framework::NativeNiniXmlTextInput> input;
    std::exception_ptr error;
    try {
        input = framework::native_xml_stream_reader_input(std::make_shared<BodyStream>(stream));
        NwcXmlRpcResponseDeserializer parser;
        result = parser.deserialize_reader(input);
    } catch (...) {
        error = std::current_exception();
    }
    // Keep the outer StreamReader using-scope, including reader-constructor errors.
    try {
        if (input)
            input->close();
    } catch (...) {
        error = std::current_exception();
    }
    // HttpResponseMessage disposal closes content even after StreamReader disposal.
    stream->close();
    if (error)
        std::rethrow_exception(error);
    return result;
}
}
