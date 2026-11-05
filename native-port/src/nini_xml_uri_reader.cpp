// Source .NET runtime v8.0.31 XmlTextReaderImpl URL constructor/OpenUrl.
// Copyright (c) .NET Foundation and Contributors; MIT .NET license.
#include "opensim/framework/nini_xml_uri_reader.hpp"
#include "opensim/framework/nini_xml_reader.hpp"
namespace opensim::framework {
namespace {
[[noreturn]] void null_reference() {
    throw NiniXmlProviderError("System.NullReferenceException", 0x80004003,
                               u"Object reference not set to an instance of an object.");
}
}
std::shared_ptr<NativeNiniXmlReader>
native_nini_xml_uri_reader(NiniWriterText url, const std::shared_ptr<NiniXmlUriReaderBackend>& backend) {
    if (!backend || !backend->get_temp_resolver)
        throw std::invalid_argument("Native XML URI reader requires an exact resolver/URI/stream provider");
    if (!url)
        throw NiniXmlProviderError("System.ArgumentNullException", 0x80004003,
                                   u"Value cannot be null. (Parameter 'url')", u"url");
    if (url->empty())
        throw NiniXmlProviderError("System.ArgumentException", 0x80070057,
                                   u"The value cannot be an empty string. (Parameter 'url')", u"url");
    auto get = backend->get_temp_resolver;
    auto resolver = get();
    if (!resolver)
        null_reference();
    auto uri = resolver->resolve_uri(url);
    if (!uri)
        null_reference();
    if (!uri->to_string)
        throw std::invalid_argument("XML resolved URI requires exact Uri.ToString");
    auto stringify = uri->to_string;
    auto reported = stringify();
    auto opener = [backend, uri, original = url]() {
        (void)original;
        auto get = backend->get_temp_resolver;
        auto resolver = get();
        if (!resolver)
            null_reference();
        return resolver->get_entity(uri);
    };
    return NativeNiniXmlReader::from_deferred_uri(std::move(reported), std::move(opener),
                                                  NiniXmlReaderSettings::legacy_document());
}
std::function<std::shared_ptr<NativeNiniXmlReader>(NiniWriterText)>
native_nini_xml_uri_reader_factory(const std::shared_ptr<NiniXmlUriReaderBackend>& backend) {
    if (!backend || !backend->get_temp_resolver)
        throw std::invalid_argument("Native XML URI reader requires an exact resolver provider");
    return [backend](NiniWriterText url) { return native_nini_xml_uri_reader(std::move(url), backend); };
}
}
