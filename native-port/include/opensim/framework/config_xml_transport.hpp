// .NET XmlDownloadManager orchestration; MIT third_party/dotnet_collation/LICENSE.
#pragma once
#include "opensim/framework/nini_writer.hpp"
#include <exception>
#include <span>
namespace opensim::framework {
// URI conversion/canonicalization is a required System.Uri boundary, not HTTP policy.
struct ConfigXmlUri {
    std::function<NiniWriterText()> scheme, local_path;
    std::shared_ptr<const void> native_uri;
};
struct ConfigXmlCredentials {
    virtual ~ConfigXmlCredentials() = default;
};
struct ConfigXmlProxy {
    virtual ~ConfigXmlProxy() = default;
};
class ConfigXmlStreamError : public std::runtime_error {
  public:
    ConfigXmlStreamError(std::string, std::u16string, std::uint32_t, NiniWriterText = {});
    std::string source_type;
    std::u16string message;
    std::uint32_t hresult;
    NiniWriterText object_name;
};
class ConfigXmlStream {
  public:
    virtual ~ConfigXmlStream() = default;
    virtual std::int32_t read(std::span<std::uint8_t>) = 0;
    // Exact live stream metadata; custom providers must supply the consumed surface.
    virtual bool can_seek() const;
    virtual std::int64_t length() const;
    virtual std::int64_t position() const;
    // Owns source Stream.CopyTo(MemoryStream), including overrides and partial faults.
    virtual std::vector<std::uint8_t> copy_to_memory();
    // HttpContent.Dispose during synchronous buffered serialization.
    virtual void cancel_buffered_copy() {
        close();
    }
    virtual void close() = 0;
};
class ConfigXmlHttpHandler {
  public:
    virtual ~ConfigXmlHttpHandler() = default;
    virtual void credentials(std::shared_ptr<ConfigXmlCredentials>) = 0;
    virtual void proxy(std::shared_ptr<ConfigXmlProxy>) = 0;
};
class ConfigXmlHttpClient {
  public:
    virtual ~ConfigXmlHttpClient() = default;
    // GetStreamAsync owns URI validation, header retrieval, EnsureSuccessStatusCode,
    // redirect/cookie/proxy/auth/TLS/timeout policy and response-stream creation.
    virtual std::shared_ptr<ConfigXmlStream> get_stream(std::shared_ptr<ConfigXmlUri>) = 0;
    // Client owns its newly supplied handler, as new HttpClient(handler) does.
    virtual void close() = 0;
};
struct ConfigXmlDownloadBackend {
    std::function<std::shared_ptr<ConfigXmlHttpHandler>()> new_handler;
    std::function<std::shared_ptr<ConfigXmlHttpClient>(std::shared_ptr<ConfigXmlHttpHandler>)> new_client;
    // Exact file source recipe: Open,Read,Share.Read,bufferSize=1; async flag differs.
    std::function<std::shared_ptr<ConfigXmlStream>(NiniWriterText, std::int32_t, bool)> open_file;
};
std::shared_ptr<ConfigXmlStream> config_xml_download(std::shared_ptr<ConfigXmlUri>,
                                                     const ConfigXmlDownloadBackend&,
                                                     std::shared_ptr<ConfigXmlCredentials> = {},
                                                     std::shared_ptr<ConfigXmlProxy> = {},
                                                     bool async_file = false);
// A buffered download is initially positioned at zero and retained independently
// of the disposed response/client. Only the consumed read/close surface is exposed.
std::shared_ptr<ConfigXmlStream> config_xml_memory_stream(std::vector<std::uint8_t>);
}
