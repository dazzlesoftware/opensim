// Source .NET8.0.31 XmlResolver.ResolveUri and XmlDownloadManager.GetStream.
// Copyright .NET Foundation and Contributors; MIT .NET license.
#include "opensim/framework/nini_xml_resolver.hpp"
#include "opensim/framework/nini_xml_file_input.hpp"
#include "opensim/framework/nini_xml_resolver_policy.hpp"
namespace opensim::framework {
ConfigUriObjectPtr native_nini_xml_resolve_uri(ConfigUriObjectPtr base, NiniWriterText relative,
                                               const std::shared_ptr<ConfigUriObjectBackend>& backend) {
    if (!backend || !backend->create || !backend->combine_string)
        throw std::invalid_argument("XML resolver requires complete typed System.Uri operations");
    if (!base || (!base->is_absolute() && base->original_string()->empty())) {
        auto create = backend->create;
        auto uri = create(relative, ConfigUriObjectKind::RelativeOrAbsolute);
        if (!uri)
            throw std::logic_error("URI constructor returned null");
        if (!uri->is_absolute() && !uri->original_string()->empty()) {
            auto path = native_nini_xml_full_path(relative);
            create = backend->create;
            uri = create(path, ConfigUriObjectKind::Absolute);
        }
        return uri;
    }
    if (!relative || relative->empty())
        return base;
    if (!base->is_absolute())
        throw NiniXmlProviderError("System.NotSupportedException", 0x80131515,
                                   u"Relative URIs are not supported.");
    auto combine = backend->combine_string;
    return combine(std::move(base), std::move(relative));
}
namespace {
class UrlResolver final : public NiniXmlUriReaderResolver {
    std::shared_ptr<ConfigUriObjectBackend> uri_;
    std::function<std::shared_ptr<NativeNiniXmlByteInput>(ConfigUriObjectPtr)> nonfile_;
    std::optional<bool> locking_;

  public:
    UrlResolver(std::shared_ptr<ConfigUriObjectBackend> u,
                std::function<std::shared_ptr<NativeNiniXmlByteInput>(ConfigUriObjectPtr)> n,
                std::optional<bool> l)
        : uri_(std::move(u)), nonfile_(std::move(n)), locking_(l) {
    }
    std::shared_ptr<NiniXmlReaderUri> resolve_uri(NiniWriterText relative) override {
        auto object = native_nini_xml_resolve_uri({}, std::move(relative), uri_);
        auto result = std::make_shared<NiniXmlReaderUri>();
        result->config_uri = object;
        result->identity = object;
        result->to_string = [object] { return object->to_string(); };
        return result;
    }
    std::shared_ptr<NativeNiniXmlByteInput> get_entity(std::shared_ptr<NiniXmlReaderUri> uri) override {
        if (!uri || !uri->config_uri)
            throw std::invalid_argument("XML URL resolver requires its retained typed URI object");
        auto object = uri->config_uri;
        if (*object->scheme() == u"file")
            return native_nini_xml_file_input(object->local_path(), locking_);
        auto download = nonfile_;
        return download(std::move(object));
    }
};
}
std::shared_ptr<NiniXmlUriReaderBackend> native_nini_xml_url_reader_backend(
    std::shared_ptr<ConfigUriObjectBackend> uri,
    std::function<std::shared_ptr<NativeNiniXmlByteInput>(ConfigUriObjectPtr)> entity,
    std::optional<bool> locking) {
    if (!uri || !uri->create || !uri->combine_string || !entity)
        throw std::invalid_argument("Native XML URL reader requires URI and non-file transport operations");
    auto result = std::make_shared<NiniXmlUriReaderBackend>();
    result->get_temp_resolver = [uri = std::move(uri), entity = std::move(entity), locking] {
        return std::make_shared<UrlResolver>(uri, entity, locking);
    };
    return result;
}

namespace {
class FileSystemResolver final : public NiniXmlUriReaderResolver {
    std::shared_ptr<ConfigUriObjectBackend> uri_;
    std::optional<bool> locking_;

  public:
    FileSystemResolver(std::shared_ptr<ConfigUriObjectBackend> u, std::optional<bool> l)
        : uri_(std::move(u)), locking_(l) {
    }
    std::shared_ptr<NiniXmlReaderUri> resolve_uri(NiniWriterText relative) override {
        auto object = native_nini_xml_resolve_uri({}, std::move(relative), uri_);
        auto result = std::make_shared<NiniXmlReaderUri>();
        result->config_uri = object;
        result->identity = object;
        result->to_string = [object] { return object->to_string(); };
        return result;
    }
    std::shared_ptr<NativeNiniXmlByteInput> get_entity(std::shared_ptr<NiniXmlReaderUri> value) override {
        if (!value || !value->config_uri)
            throw std::invalid_argument("XML file resolver requires its typed URI");
        if (*value->config_uri->scheme() == u"file")
            return native_nini_xml_file_input(value->config_uri->local_path(), locking_);
        NiniXmlProviderError error("System.Xml.XmlException", 0x80131940, u"Object type is not supported.");
        error.line_number = 0;
        error.line_position = 0;
        error.source_uri = u"";
        throw error;
    }
};
}
std::shared_ptr<NiniXmlUriReaderBackend> native_nini_xml_default_uri_reader_backend(
    std::shared_ptr<ConfigUriObjectBackend> uri, std::shared_ptr<NiniXmlResolverPolicy> policy,
    std::function<std::shared_ptr<NativeNiniXmlByteInput>(ConfigUriObjectPtr)> entity,
    std::optional<bool> locking, std::function<std::shared_ptr<NiniXmlUriReaderResolver>()> explicit_getter) {
    if (!policy)
        throw std::invalid_argument("Default XML resolver requires shared AppContext policy");
    auto url = native_nini_xml_url_reader_backend(uri, std::move(entity), locking);
    auto file = std::make_shared<FileSystemResolver>(std::move(uri), locking);
    auto result = std::make_shared<NiniXmlUriReaderBackend>();
    result->get_temp_resolver = [policy = std::move(policy), url = std::move(url), file = std::move(file),
                                 explicit_getter = std::move(explicit_getter)] {
        if (explicit_getter) {
            auto explicit_resolver = explicit_getter();
            if (explicit_resolver)
                return explicit_resolver;
        }
        if (policy->networking_enabled_by_default())
            return url->get_temp_resolver();
        return std::static_pointer_cast<NiniXmlUriReaderResolver>(file);
    };
    return result;
}
}
