#include "opensim/server/config_xml_startup.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/config_xml_typed_http_services.hpp"
#include "opensim/framework/config_xml_native_proxy.hpp"
#include "opensim/framework/config_http_cookies_native.hpp"
#include "opensim/framework/config_xml_reader_stream.hpp"
#include "opensim/framework/nini_xml_document_save.hpp"
#include "opensim/framework/nini_xml_resolver.hpp"
#include "opensim/framework/nini_xml_reader.hpp"
namespace opensim::server {
std::function<std::shared_ptr<framework::NiniConfigSourceBase>(framework::NiniWriterText)>
native_startup_xml_source(std::shared_ptr<framework::NiniConfigArena> arena,
                          framework::ConfigXmlNativeHttpServices services,
                          std::shared_ptr<framework::NiniXmlResolverPolicy> policy) {
    if (!arena)
        throw std::invalid_argument("Startup XML source requires its configuration arena");
    if (!policy)
        policy = std::make_shared<framework::NiniXmlResolverPolicy>(
            [](std::u16string_view) -> std::optional<bool> { return {}; });
    // Delay transport validation and all I/O until the loader actually reads a URL.
    // Local INI loading must remain independent of unused HTTP policy providers.
    return [arena = std::move(arena), services = std::move(services),
            policy = std::move(policy)](framework::NiniWriterText path) {
        auto uris = framework::native_config_uri_object_backend();
        framework::ConfigXmlUriObjects objects;
        auto readers = framework::native_nini_xml_default_uri_reader_backend(
            uris, policy, [objects, services, uris](auto uri) {
                // Resolver policy and file dispatch run before HTTP provider validation.
                auto bound = services;
                // Bind inside this URI provenance domain, after resolver/file policy dispatch.
                // Explicit legacy or handler-scoped proxy policies keep precedence.
                if (!bound.proxy && !bound.proxy_plan && !bound.proxy_provider)
                    bound = framework::config_xml_bind_native_proxy(std::move(bound), objects);
                auto download = framework::config_xml_native_http_backend(
                    framework::config_xml_bind_typed_http(std::move(bound), objects, *uris, []() {
                        return framework::native_invariant_config_http_cookies_backend();
                    }));
                return framework::config_xml_reader_stream(
                    framework::config_xml_download(objects.wrap(std::move(uri)), download));
            });
        // ConfigurationLoader uses XmlReader.Create(uri, DtdProcessing.Ignore),
        // then the Nini reader overload; XmlConfigSource(uri) parses DTD defaults.
        auto resolver = readers->get_temp_resolver();
        auto uri = resolver->resolve_uri(path);
        framework::NiniXmlReaderSettings settings;
        settings.dtd_processing = framework::NiniXmlDtdProcessing::Ignore;
        settings.close_input = true;
        settings.base_uri = uri->to_string();
        auto reader =
            framework::NativeNiniXmlReader::from_stream(resolver->get_entity(uri), std::move(settings));
        auto xml =
            framework::native_nini_xml_config_backend(framework::native_xml_document_io(std::move(readers)));
        return framework::NiniXmlConfigSource::create(arena, std::move(xml), std::move(reader));
    };
}
}
