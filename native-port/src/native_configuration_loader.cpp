// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/native_configuration_loader.hpp"
#include "opensim/framework/config_uri.hpp"
#include "opensim/framework/configuration_culture.hpp"
#include "opensim/framework/network_servers_info.hpp"
#include "opensim/framework/nini_streams.hpp"
#include "opensim/framework/text.hpp"
namespace opensim::framework {
NiniWriterText native_configuration_exception_message(std::exception_ptr error) {
    if (!error)
        throw std::invalid_argument("A captured configuration exception is required");
    try {
        std::rethrow_exception(error);
    } catch (const NiniConfigError &e) {
        return nini_writer_text(e.message);
    } catch (const NiniStreamError &e) {
        return nini_writer_text(e.message);
    } catch (const NiniDocumentError &e) {
        return nini_writer_text(e.message);
    } catch (const NiniReaderError &e) {
        return nini_writer_text(e.message_utf16());
    } catch (const NiniWriterError &e) {
        return nini_writer_text(e.message);
    } catch (const DirectoryEntriesError &e) {
        return nini_writer_text(e.message);
    } catch (const EnvironmentConfigError &e) {
        return nini_writer_text(e.message);
    } catch (const std::exception &e) {
        return nini_writer_text(utf16_replacing(e.what()));
    }
}
void bind_native_startup_configuration_services(RegionConfigurationBackend &backend,
                                                std::shared_ptr<EnvironmentConfigBackend> environment) {
    backend.is_http_uri = [](auto value) { return is_http_configuration_uri(std::move(value)); };
    backend.starts_with_current = [](auto value, auto prefix) {
        if (!prefix || *prefix != u"Include-")
            throw std::invalid_argument(
                "Native startup configuration supports only the consumed Include- prefix");
        return startup_invariant_configuration_include_key(std::move(value));
    };
    backend.merge_environment = [environment = std::move(environment)](auto source) {
        merge_environment_to_config(std::move(source), environment);
    };
    backend.replace_key_values = [](auto source) {
        if (!source)
            throw NiniConfigError(NiniConfigError::Kind::NullReference,
                                  u"Object reference not set to an instance of an object.");
        source->replace_key_values();
    };
    bind_network_servers_info(backend);
    backend.exception_message = native_configuration_exception_message;
}
}
