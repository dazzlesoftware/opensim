cmake_minimum_required(VERSION 3.20)
project(OpenSimNative VERSION 0.1.0 LANGUAGES C CXX)

add_library(opensim_framework
    src/texture_face.cpp
    src/texture_entry.cpp
    src/asset_base.cpp src/asset_nrbf.cpp src/asset_types.cpp src/animation_set.cpp
    src/avatar_wearable.cpp
    src/agent_circuit.cpp
    src/circuit_transport.cpp
    src/client_packet_queue.cpp
    src/basic_dos_protector.cpp
    src/crc32.cpp
    src/identity.cpp
    src/lludp.cpp
    src/packet_history.cpp
    src/uuid.cpp
    src/text.cpp src/culture_prefix.cpp
    src/time_zone.cpp
    src/ip_address.cpp src/host_resolver.cpp
    src/http_uri.cpp
    src/http_host.cpp
    src/region_uri.cpp
    src/vector_text.cpp
    src/unacked_packets.cpp
    src/udp_socket.cpp
    src/permissions.cpp
    src/prime_number_helper.cpp
    src/token_bucket.cpp)
target_include_directories(opensim_framework PUBLIC include)
target_compile_features(opensim_framework PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_sources(opensim_framework PRIVATE src/task_inventory_item.cpp src/field_string.cpp)
target_link_libraries(opensim_framework PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(WIN32)
    target_link_libraries(opensim_framework PUBLIC bcrypt ws2_32 iphlpapi)
endif()
set_target_properties(opensim_framework PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(opensim_framework PRIVATE /W4 /permissive- /fp:precise)
else()
    target_compile_options(opensim_framework PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
endif()

add_library(native_sqlite STATIC third_party/sqlite/sqlite3.c)
target_include_directories(native_sqlite SYSTEM PUBLIC third_party/sqlite)
target_link_libraries(native_sqlite PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX)
    target_link_libraries(native_sqlite PUBLIC m)
endif()
add_library(native_inflate STATIC third_party/zlib/adler32.c third_party/zlib/crc32.c third_party/zlib/inffast.c third_party/zlib/inflate.c third_party/zlib/inftrees.c third_party/zlib/zutil.c third_party/zlib/deflate.c third_party/zlib/trees.c)
target_compile_definitions(native_inflate PRIVATE Z_PREFIX)
# Windows .NET 8 x86/x64 uses the pinned Intel zlib profile. Keep it isolated
# from the newer prefixed decoder and optional system-zlib HTTP dependency.
set(OPENSIM_DOTNET_GZIP OFF)
if(WIN32 AND NOT CMAKE_GENERATOR_PLATFORM MATCHES "ARM" AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "ARM|arm|aarch")
    set(OPENSIM_DOTNET_GZIP ON)
    set(dotnet_gzip_sources adler32.c crc32.c crc_folding.c deflate.c deflate_medium.c deflate_quick.c match.c slide_sse.c trees.c x86.c zutil.c)
    list(TRANSFORM dotnet_gzip_sources PREPEND "third_party/dotnet_zlib_intel/")
    add_library(native_dotnet_gzip STATIC ${dotnet_gzip_sources})
    if(MSVC)
        target_compile_options(native_dotnet_gzip PRIVATE /FI"${CMAKE_CURRENT_SOURCE_DIR}/third_party/dotnet_zlib_intel/opensim_prefix.h")
    else()
        target_compile_options(native_dotnet_gzip PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/third_party/dotnet_zlib_intel/opensim_prefix.h")
    endif()
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        # This unit contains only CPU-dispatched PCLMUL/SSSE3/SSE4 CRC routines.
        if(MSVC)
            set_source_files_properties(third_party/dotnet_zlib_intel/crc_folding.c PROPERTIES COMPILE_OPTIONS "/clang:-mpclmul;/clang:-mssse3;/clang:-msse4.2")
        else()
            set_source_files_properties(third_party/dotnet_zlib_intel/crc_folding.c PROPERTIES COMPILE_OPTIONS "-mpclmul;-mssse3;-msse4.2")
        endif()
    endif()
endif()
add_library(native_xml STATIC third_party/pugixml/pugixml.cpp)
target_compile_features(native_xml PUBLIC cxx_std_20)
target_include_directories(native_xml SYSTEM PUBLIC third_party/pugixml)
add_library(opensim_server_core
    src/user_account.cpp
    src/account_provisioner.cpp
    src/appearance_provisioner.cpp
    src/appearance_model_transfer.cpp
    src/avatar_data.cpp
    src/avatar_service.cpp
    src/sqlite_avatar_store.cpp
    src/account_service_urls.cpp
    src/login_agent_factory.cpp
    src/agent_circuit_osd.cpp src/agent_position.cpp src/agent_data.cpp src/agent_update_records.cpp src/animation.cpp
    src/avatar_appearance.cpp src/avatar_appearance_osd.cpp
    src/wearable_cache_item.cpp
    src/avatar_attachment.cpp src/entity_transfer_context.cpp
    src/login_response.cpp src/login_response_assembly.cpp src/login_completion.cpp src/friends_service.cpp src/friends_connector.cpp src/sqlite_friends_store.cpp src/friends_endpoint.cpp
    src/simulation_connector.cpp src/local_simulation.cpp src/scene_access.cpp src/scene_admission.cpp src/scene_client_verification.cpp src/capabilities_module.cpp src/caps_handlers.cpp src/caps.cpp src/http_route_registry.cpp src/poll_request.cpp src/poll_manager.cpp src/seed_caps.cpp src/seed_caps_xml.cpp src/presence_state.cpp src/viewer_name.cpp src/simulation_position_connector.cpp src/osd_request.cpp src/agent_access.cpp src/agent_delete.cpp src/agent_update.cpp src/object_handler.cpp
    src/osd_json.cpp
    src/osd_value.cpp
    src/avatar_wearable_osd.cpp
    src/account_store.cpp src/config.cpp src/rpc.cpp src/login_service.cpp src/login_orchestration.cpp src/login_recovery.cpp src/agent_launch.cpp
    src/login_application.cpp src/http_server.cpp)
target_sources(opensim_server_core PRIVATE src/inventory_store.cpp)
target_sources(opensim_server_core PRIVATE src/poll_runtime.cpp)
target_sources(opensim_server_core PRIVATE src/async_http_server.cpp)
target_sources(opensim_server_core PRIVATE src/poll_http_service.cpp)
target_sources(opensim_server_core PRIVATE src/http_response.cpp src/http_cookies.cpp)
target_sources(opensim_server_core PRIVATE src/event_queue.cpp src/event_queue_messages.cpp)
target_sources(opensim_server_core PRIVATE src/scene_capabilities.cpp)
target_sources(opensim_server_core PRIVATE src/terrain.cpp src/terrain_layer.cpp src/world.cpp src/avatar_update.cpp src/avatar_packets.cpp src/object_add.cpp src/object_update.cpp src/rez_location.cpp src/pos_physics.cpp src/viewer_region.cpp src/region_runtime.cpp)
target_sources(opensim_server_core PRIVATE src/chat_packet.cpp src/lsl_runtime.cpp)
target_sources(opensim_server_core PRIVATE src/movement.cpp)
target_sources(opensim_server_core PRIVATE src/default_animations.cpp src/movement_animation.cpp src/ubode.cpp src/viewer_physics.cpp)
target_sources(opensim_server_core PRIVATE src/root_position.cpp src/region_asset_runtime.cpp src/inventory_caps.cpp src/inventory_descendents.cpp src/region_inventory_runtime.cpp src/region_inventory_descendents_runtime.cpp src/library_inventory_caps.cpp src/library_descendents.cpp src/region_library_descendents_runtime.cpp src/inventory_packets.cpp src/legacy_inventory_requests.cpp src/xfer_download.cpp src/task_inventory_file.cpp src/script_running.cpp src/script_task_caps.cpp src/inventory_asset_caps.cpp src/baked_texture_caps.cpp src/flotsam_memory_cache.cpp src/flotsam_file_access.cpp src/flotsam_cache_files.cpp src/flotsam_cache_module.cpp src/flotsam_cache_cleanup.cpp src/flotsam_file_store.cpp src/region_baked_texture_runtime.cpp src/region_inventory_asset_runtime.cpp src/script_task_update.cpp src/region_script_task_runtime.cpp src/object_properties.cpp src/script_control.cpp src/script_asset_text.cpp src/task_inventory_registry.cpp src/inventory_script_rez.cpp src/script_instance_create.cpp)
target_sources(opensim_server_core PRIVATE src/task_inventory_move.cpp src/task_inventory_remove.cpp src/agent_inventory_add.cpp src/inventory_notifications.cpp src/inventory_item_create.cpp)
target_sources(opensim_server_core PRIVATE src/task_inventory_update.cpp src/inventory_drop_permission.cpp)
target_sources(opensim_server_core PRIVATE src/user_inventory_operations.cpp src/inventory_copy.cpp src/part_script_events.cpp src/inventory_new.cpp src/inventory_asset_update.cpp src/scene_inventory_asset_update.cpp)
target_sources(opensim_server_core PRIVATE src/simulator_features.cpp)
target_sources(opensim_server_core PRIVATE src/http_range.cpp src/get_assets.cpp)
target_sources(opensim_server_core PRIVATE src/object_job_engine.cpp src/get_assets_module.cpp)
target_sources(opensim_server_core PRIVATE src/local_asset_connector.cpp)
target_sources(opensim_server_core PRIVATE src/stored_asset_service.cpp src/sqlite_asset_store.cpp)
target_sources(opensim_framework PRIVATE src/sha1.cpp)
target_sources(opensim_framework PRIVATE src/unicode_regex.cpp)
target_sources(opensim_server_core PRIVATE src/migration.cpp)
set(ASSET_MIGRATION_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../OpenSim/Data/SQLite/Resources/AssetStore.migrations")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ASSET_MIGRATION_SOURCE}")
file(READ "${ASSET_MIGRATION_SOURCE}" ASSET_MIGRATION_SQL)
configure_file(cmake/asset_store_migration_resource.hpp.in generated/asset_store_migration_resource.hpp @ONLY)
set(FRIENDS_MIGRATION_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../OpenSim/Data/SQLite/Resources/FriendsStore.migrations")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FRIENDS_MIGRATION_SOURCE}")
file(READ "${FRIENDS_MIGRATION_SOURCE}" FRIENDS_MIGRATION_SQL)
configure_file(cmake/friends_store_migration_resource.hpp.in generated/friends_store_migration_resource.hpp @ONLY)
set(AVATAR_MIGRATION_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../OpenSim/Data/SQLite/Resources/Avatar.migrations")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${AVATAR_MIGRATION_SOURCE}")
file(READ "${AVATAR_MIGRATION_SOURCE}" AVATAR_MIGRATION_SQL)
configure_file(cmake/avatar_store_migration_resource.hpp.in generated/avatar_store_migration_resource.hpp @ONLY)
target_sources(opensim_server_core PRIVATE src/sqlite_avatar_plugin.cpp)
set(GRID_USER_MIGRATION_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../OpenSim/Data/SQLite/Resources/001_GridUserStore.sql")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GRID_USER_MIGRATION_SOURCE}")
file(READ "${GRID_USER_MIGRATION_SOURCE}" GRID_USER_MIGRATION_HEX HEX)
configure_file(cmake/grid_user_store_migration_resource.hpp.in generated/grid_user_store_migration_resource.hpp @ONLY)
target_sources(opensim_server_core PRIVATE src/sqlite_grid_user_plugin.cpp)
set(USER_ACCOUNT_MIGRATION_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../OpenSim/Data/SQLite/Resources/UserAccount.migrations")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${USER_ACCOUNT_MIGRATION_SOURCE}")
file(READ "${USER_ACCOUNT_MIGRATION_SOURCE}" USER_ACCOUNT_MIGRATION_SQL)
configure_file(cmake/user_account_store_migration_resource.hpp.in generated/user_account_store_migration_resource.hpp @ONLY)
target_sources(opensim_server_core PRIVATE src/sqlite_user_account_plugin.cpp)
set(AUTH_MIGRATION_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../OpenSim/Data/SQLite/Resources/AuthStore.migrations")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${AUTH_MIGRATION_SOURCE}")
file(READ "${AUTH_MIGRATION_SOURCE}" AUTH_MIGRATION_SQL)
configure_file(cmake/auth_store_migration_resource.hpp.in generated/auth_store_migration_resource.hpp @ONLY)
target_sources(opensim_server_core PRIVATE src/sqlite_authentication_plugin.cpp)




target_sources(opensim_server_core PRIVATE src/sqlite_generic_connection.cpp src/sqlite_generic_table.cpp src/sqlite_friends_plugin.cpp)

target_include_directories(opensim_server_core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_sources(opensim_server_core PRIVATE src/sqlite_migration.cpp)
target_sources(opensim_server_core PRIVATE src/asset_storage_factory.cpp)
target_sources(opensim_server_core PRIVATE src/filesystem_asset_loader.cpp)
target_sources(opensim_server_core PRIVATE src/native_asset_storage.cpp src/sqlite_connection.cpp)
target_sources(opensim_server_core PRIVATE src/inventory_service.cpp)
target_sources(opensim_server_core PRIVATE src/inventory_tree.cpp src/library_service.cpp)
target_sources(opensim_server_core PRIVATE src/inventory_login.cpp)
target_sources(opensim_server_core PRIVATE src/presence_service.cpp)
target_sources(opensim_server_core PRIVATE src/login_destination.cpp src/grid_identity.cpp src/grid_user_store.cpp src/grid_user_service.cpp src/grid_region.cpp src/region_store.cpp src/sqlite_region_store.cpp src/grid_service.cpp)
target_sources(opensim_server_core PRIVATE src/gzip_inflate.cpp src/gzip_deflate.cpp src/simulation_body.cpp src/service_protocol.cpp src/service_xml_reader.cpp src/presence_endpoint.cpp src/grid_user_endpoint.cpp src/grid_endpoint.cpp src/account_endpoint.cpp src/avatar_endpoint.cpp)
target_link_libraries(opensim_server_core PUBLIC opensim_framework PRIVATE native_sqlite native_xml native_inflate)
target_sources(opensim_server_core PRIVATE src/auth_info.cpp src/authentication_endpoint.cpp)
target_sources(opensim_server_core PRIVATE src/rest_forms.cpp src/authentication_connector.cpp)
if(OPENSIM_DOTNET_GZIP)
    target_link_libraries(opensim_server_core PRIVATE native_dotnet_gzip)
    target_compile_definitions(opensim_server_core PRIVATE OPENSIM_GZIP_DOTNET_INTEL)
endif()
target_include_directories(opensim_server_core SYSTEM PRIVATE third_party/httplib)
if(MSVC)
    target_compile_options(opensim_server_core PRIVATE /W4 /permissive- /fp:precise /utf-8)
else()
    target_compile_options(opensim_server_core PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
endif()
if(WIN32)
    target_link_libraries(opensim_server_core PRIVATE ws2_32 crypt32)
endif()
option(OPENSIM_REQUIRE_TLS "Require OpenSSL for the native HTTPS server" OFF)
if(OPENSIM_REQUIRE_TLS)
    find_package(OpenSSL 3.0 REQUIRED)
else()
    find_package(OpenSSL 3.0 QUIET)
endif()
if(OpenSSL_FOUND)
    target_compile_definitions(opensim_server_core PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
    target_link_libraries(opensim_server_core PRIVATE OpenSSL::SSL OpenSSL::Crypto)
else()
    message(STATUS "OpenSSL not found: native HTTP is enabled; TLS configuration will fail explicitly")
endif()
if(UNIX)
    find_package(ZLIB REQUIRED)
    target_compile_definitions(opensim_server_core PRIVATE OPENSIM_GZIP_SYSTEM_ZLIB)
else()
    find_package(ZLIB QUIET)
endif()
if(ZLIB_FOUND)
    target_compile_definitions(opensim_server_core PRIVATE CPPHTTPLIB_ZLIB_SUPPORT)
    target_link_libraries(opensim_server_core PRIVATE ZLIB::ZLIB)
endif()
add_executable(opensim-native src/main.cpp)
target_link_libraries(opensim-native PRIVATE opensim_server_core)
if(MINGW)
    target_link_options(opensim-native PRIVATE -municode)
endif()
install(TARGETS opensim-native RUNTIME DESTINATION bin)
install(FILES native-server.ini.example LICENSE.txt CONTRIBUTORS.txt BUILDING.txt DESTINATION share/opensim-native)
install(FILES native-region.ini.example ../bin/data/avataranimations.xml DESTINATION share/opensim-native)
install(FILES ../ThirdPartyLicenses/ODE.txt DESTINATION share/opensim-native/licenses)
if(WIN32)
    set(OPENSIM_UBODE_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/../bin/lib64/ubode.dll")
elseif(APPLE)
    set(OPENSIM_UBODE_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/../bin/lib64/libubode.dylib")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    set(OPENSIM_UBODE_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/../bin/lib64/libubode-arm64.so")
else()
    set(OPENSIM_UBODE_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/../bin/lib64/libubode-x86_64.so")
endif()
install(FILES "${OPENSIM_UBODE_LIBRARY}" DESTINATION lib/opensim-native)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../bin/assets/" DESTINATION share/opensim-native/assets)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../bin/inventory/" DESTINATION share/opensim-native/inventory)
install(FILES third_party/httplib/LICENSE DESTINATION share/opensim-native/licenses RENAME cpp-httplib-LICENSE)
install(FILES third_party/pugixml/LICENSE.md DESTINATION share/opensim-native/licenses RENAME pugixml-LICENSE)
install(FILES third_party/dotnet_zlib_intel/LICENSE DESTINATION share/opensim-native/licenses RENAME dotnet-zlib-intel-LICENSE)
install(FILES third_party/dotnet_collation/LICENSE DESTINATION share/opensim-native/licenses RENAME dotnet-collation-LICENSE)
install(FILES third_party/zlib/LICENSE DESTINATION share/opensim-native/licenses RENAME zlib-LICENSE)
install(FILES third_party/httplib/opensim-raw-requests.patch DESTINATION share/opensim-native/licenses)
install(FILES third_party/httplib/opensim-async-response.patch DESTINATION share/opensim-native/licenses)
install(FILES third_party/dependencies.json DESTINATION share/opensim-native/licenses)
install(FILES protocol/LIBOPENMETAVERSE-LICENSE.txt DESTINATION share/opensim-native/licenses)

option(OPENSIM_SANITIZERS "Enable native address (and non-MSVC undefined behavior) sanitizers" OFF)
if(OPENSIM_SANITIZERS)
    if(MSVC)
        target_compile_options(opensim_framework PUBLIC /fsanitize=address /Zi)
        target_link_options(opensim_framework PUBLIC /INCREMENTAL:NO)
        set(sanitized_dependencies native_xml native_sqlite native_inflate)
        if(TARGET native_dotnet_gzip)
            list(APPEND sanitized_dependencies native_dotnet_gzip)
        endif()
        foreach(dependency ${sanitized_dependencies})
            target_compile_options(${dependency} PRIVATE /fsanitize=address /Zi)
        endforeach()
    else()
        target_compile_options(opensim_framework PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(opensim_framework PUBLIC -fsanitize=address,undefined)
        set(sanitized_dependencies native_xml native_sqlite native_inflate)
        if(TARGET native_dotnet_gzip)
            list(APPEND sanitized_dependencies native_dotnet_gzip)
        endif()
        foreach(dependency ${sanitized_dependencies})
            target_compile_options(${dependency} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        endforeach()
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(user_account_probe tests/user_account_probe.cpp)
    target_link_libraries(user_account_probe PRIVATE opensim_server_core)
    add_executable(account_urls_tests tests/account_urls_tests.cpp)
    target_link_libraries(account_urls_tests PRIVATE opensim_server_core native_sqlite)
    add_test(NAME account_urls_tests COMMAND account_urls_tests)
    add_executable(account_urls_probe tests/account_urls_probe.cpp)
    target_link_libraries(account_urls_probe PRIVATE opensim_server_core)
    add_executable(account_record_probe tests/account_record_probe.cpp)
    target_link_libraries(account_record_probe PRIVATE opensim_server_core native_sqlite)
    add_executable(gzip_compress_probe tests/gzip_compress_probe.cpp)
    target_link_libraries(gzip_compress_probe PRIVATE opensim_server_core)
    add_executable(simulation_body_probe tests/simulation_body_probe.cpp)
    target_link_libraries(simulation_body_probe PRIVATE opensim_server_core)
    add_executable(simulation_lifecycle_probe tests/simulation_lifecycle_probe.cpp)
    target_link_libraries(simulation_lifecycle_probe PRIVATE opensim_server_core)
    add_executable(simulation_position_tests tests/simulation_position_tests.cpp)
    target_link_libraries(simulation_position_tests PRIVATE opensim_server_core)
    add_test(NAME simulation_position_tests COMMAND simulation_position_tests)
    add_executable(simulation_position_probe tests/simulation_position_probe.cpp)
    target_link_libraries(simulation_position_probe PRIVATE opensim_server_core)
    add_executable(simulation_agent_data_probe tests/simulation_agent_data_probe.cpp)
    target_link_libraries(simulation_agent_data_probe PRIVATE opensim_server_core)
    add_executable(http_route_server tests/http_route_server.cpp)
    target_link_libraries(http_route_server PRIVATE opensim_server_core)
    add_executable(http_route_registry_probe tests/http_route_registry_probe.cpp)
    add_executable(poll_request_probe tests/poll_request_probe.cpp)
    add_executable(http_response_probe tests/http_response_probe.cpp)
    add_executable(event_queue_tests tests/event_queue_tests.cpp)
    add_executable(event_queue_probe tests/event_queue_probe.cpp)
    add_executable(event_queue_server tests/event_queue_server.cpp)
    add_executable(scene_capability_events_probe tests/scene_capability_events_probe.cpp)
    add_executable(scene_capabilities_server tests/scene_capabilities_server.cpp)
    add_executable(scene_capabilities_tests tests/scene_capabilities_tests.cpp)
    add_executable(scene_world_tests tests/scene_world_tests.cpp)
    add_executable(terrain_layer_tests tests/terrain_layer_tests.cpp)
    add_executable(avatar_update_tests tests/avatar_update_tests.cpp)
    add_executable(object_update_tests tests/object_update_tests.cpp)
    add_executable(rez_location_tests tests/rez_location_tests.cpp)
    add_executable(pos_physics_tests tests/pos_physics_tests.cpp)
    target_link_libraries(pos_physics_tests PRIVATE opensim_server_core)
    add_test(NAME pos_physics_tests COMMAND pos_physics_tests)
    add_executable(avatar_packets_tests tests/avatar_packets_tests.cpp)
    add_executable(chat_packet_tests tests/chat_packet_tests.cpp)
    add_executable(lsl_runtime_tests tests/lsl_runtime_tests.cpp)
    add_executable(inventory_packets_tests tests/inventory_packets_tests.cpp)
    target_link_libraries(inventory_packets_tests PRIVATE opensim_server_core)
    add_test(NAME inventory_packets_tests COMMAND inventory_packets_tests)
    add_executable(script_instance_create_tests tests/script_instance_create_tests.cpp)
    target_link_libraries(script_instance_create_tests PRIVATE opensim_server_core)
    add_test(NAME script_instance_create_tests COMMAND script_instance_create_tests)
    foreach(inventory_component IN ITEMS flotsam_cache_cleanup flotsam_cache_module flotsam_cache flotsam_file_access flotsam_file_store asset_nrbf flotsam_memory_cache baked_texture_caps inventory_asset_caps scene_inventory_asset_update inventory_asset_update animation_set inventory_new part_script_events inventory_copy user_inventory_operations inventory_drop_permission task_inventory_update task_inventory_move task_inventory_remove agent_inventory_add inventory_notifications inventory_item_create)
        add_executable(${inventory_component}_tests tests/${inventory_component}_tests.cpp)
        target_link_libraries(${inventory_component}_tests PRIVATE opensim_server_core)
        add_test(NAME ${inventory_component}_tests COMMAND ${inventory_component}_tests)
    endforeach()
    add_executable(field_string_tests tests/field_string_tests.cpp)
    target_link_libraries(field_string_tests PRIVATE opensim_framework)
    add_test(NAME field_string_tests COMMAND field_string_tests)
    add_executable(task_inventory_item_tests tests/task_inventory_item_tests.cpp)
    target_link_libraries(task_inventory_item_tests PRIVATE opensim_framework)
    add_test(NAME task_inventory_item_tests COMMAND task_inventory_item_tests)
    add_executable(task_inventory_registry_tests tests/task_inventory_registry_tests.cpp)
    target_link_libraries(task_inventory_registry_tests PRIVATE opensim_server_core)
    add_test(NAME task_inventory_registry_tests COMMAND task_inventory_registry_tests)
    add_executable(inventory_script_rez_tests tests/inventory_script_rez_tests.cpp)
    target_link_libraries(inventory_script_rez_tests PRIVATE opensim_server_core)
    add_test(NAME inventory_script_rez_tests COMMAND inventory_script_rez_tests)
    add_executable(script_control_tests tests/script_control_tests.cpp)
    target_link_libraries(script_control_tests PRIVATE opensim_server_core)
    add_test(NAME script_control_tests COMMAND script_control_tests)
    add_executable(script_asset_text_tests tests/script_asset_text_tests.cpp)
    target_link_libraries(script_asset_text_tests PRIVATE opensim_server_core)
    add_test(NAME script_asset_text_tests COMMAND script_asset_text_tests)
    add_executable(region_script_owner_tests tests/region_script_owner_tests.cpp)
    target_link_libraries(region_script_owner_tests PRIVATE opensim_server_core)
    add_test(NAME region_script_owner_tests COMMAND region_script_owner_tests)
    set_tests_properties(region_script_owner_tests PROPERTIES TIMEOUT 20)
    add_executable(region_inventory_asset_owner_tests tests/region_inventory_asset_owner_tests.cpp)
    target_link_libraries(region_inventory_asset_owner_tests PRIVATE opensim_server_core)
    add_test(NAME region_inventory_asset_owner_tests COMMAND region_inventory_asset_owner_tests)
    set_tests_properties(region_inventory_asset_owner_tests PROPERTIES TIMEOUT 30)
    add_executable(object_properties_tests tests/object_properties_tests.cpp)
    target_link_libraries(object_properties_tests PRIVATE opensim_server_core)
    add_test(NAME object_properties_tests COMMAND object_properties_tests)
    add_executable(script_task_caps_tests tests/script_task_caps_tests.cpp)
    target_link_libraries(script_task_caps_tests PRIVATE opensim_server_core)
    add_test(NAME script_task_caps_tests COMMAND script_task_caps_tests)
    add_executable(script_task_update_tests tests/script_task_update_tests.cpp)
    target_link_libraries(script_task_update_tests PRIVATE opensim_server_core)
    add_test(NAME script_task_update_tests COMMAND script_task_update_tests)
    add_executable(region_script_task_runtime_tests tests/region_script_task_runtime_tests.cpp)
    target_link_libraries(region_script_task_runtime_tests PRIVATE opensim_server_core)
    add_test(NAME region_script_task_runtime_tests COMMAND region_script_task_runtime_tests)
    add_test(NAME region_script_task_expiry_tests COMMAND region_script_task_runtime_tests --expiry)
    set_tests_properties(region_script_task_expiry_tests PROPERTIES TIMEOUT 45)
    add_executable(region_baked_texture_runtime_tests tests/region_baked_texture_runtime_tests.cpp)
    target_link_libraries(region_baked_texture_runtime_tests PRIVATE opensim_server_core)
    add_test(NAME region_baked_texture_runtime_tests COMMAND region_baked_texture_runtime_tests)
    add_test(NAME region_baked_texture_expiry_tests COMMAND region_baked_texture_runtime_tests --expiry)
    set_tests_properties(region_baked_texture_expiry_tests PROPERTIES TIMEOUT 45)
    add_executable(region_inventory_asset_runtime_tests tests/region_inventory_asset_runtime_tests.cpp)
    target_link_libraries(region_inventory_asset_runtime_tests PRIVATE opensim_server_core)
    add_test(NAME region_inventory_asset_runtime_tests COMMAND region_inventory_asset_runtime_tests)
    add_test(NAME region_inventory_asset_expiry_tests COMMAND region_inventory_asset_runtime_tests --expiry)
    set_tests_properties(region_inventory_asset_expiry_tests PROPERTIES TIMEOUT 45)
    add_executable(script_running_tests tests/script_running_tests.cpp)
    target_link_libraries(script_running_tests PRIVATE opensim_server_core)
    add_test(NAME script_running_tests COMMAND script_running_tests)
    add_executable(task_inventory_file_tests tests/task_inventory_file_tests.cpp)
    target_link_libraries(task_inventory_file_tests PRIVATE opensim_server_core)
    add_test(NAME task_inventory_file_tests COMMAND task_inventory_file_tests)
    add_executable(xfer_download_tests tests/xfer_download_tests.cpp)
    target_link_libraries(xfer_download_tests PRIVATE opensim_server_core)
    add_test(NAME xfer_download_tests COMMAND xfer_download_tests)
    add_executable(legacy_inventory_requests_tests tests/legacy_inventory_requests_tests.cpp)
    target_link_libraries(legacy_inventory_requests_tests PRIVATE opensim_server_core)
    add_test(NAME legacy_inventory_requests_tests COMMAND legacy_inventory_requests_tests)
    add_executable(library_inventory_caps_tests tests/library_inventory_caps_tests.cpp)
    target_link_libraries(library_inventory_caps_tests PRIVATE opensim_server_core)
    add_test(NAME library_inventory_caps_tests COMMAND library_inventory_caps_tests)
    add_executable(library_descendents_tests tests/library_descendents_tests.cpp)
    target_link_libraries(library_descendents_tests PRIVATE opensim_server_core)
    add_test(NAME library_descendents_tests COMMAND library_descendents_tests)
    add_executable(region_library_descendents_runtime_tests tests/region_library_descendents_runtime_tests.cpp)
    target_link_libraries(region_library_descendents_runtime_tests PRIVATE opensim_server_core)
    add_test(NAME region_library_descendents_runtime_tests COMMAND region_library_descendents_runtime_tests)
    add_executable(region_inventory_descendents_runtime_tests tests/region_inventory_descendents_runtime_tests.cpp)
    target_link_libraries(region_inventory_descendents_runtime_tests PRIVATE opensim_server_core)
    add_test(NAME region_inventory_descendents_runtime_tests COMMAND region_inventory_descendents_runtime_tests)
    add_executable(region_inventory_runtime_tests tests/region_inventory_runtime_tests.cpp)
    target_link_libraries(region_inventory_runtime_tests PRIVATE opensim_server_core)
    add_test(NAME region_inventory_runtime_tests COMMAND region_inventory_runtime_tests)
    add_executable(inventory_caps_tests tests/inventory_caps_tests.cpp)
    target_link_libraries(inventory_caps_tests PRIVATE opensim_server_core)
    add_test(NAME inventory_caps_tests COMMAND inventory_caps_tests)
    add_executable(inventory_descendents_tests tests/inventory_descendents_tests.cpp)
    target_link_libraries(inventory_descendents_tests PRIVATE opensim_server_core)
    add_test(NAME inventory_descendents_tests COMMAND inventory_descendents_tests)
    add_executable(root_position_tests tests/root_position_tests.cpp)
    target_link_libraries(root_position_tests PRIVATE opensim_server_core)
    add_test(NAME root_position_tests COMMAND root_position_tests)
    add_executable(region_asset_runtime_tests tests/region_asset_runtime_tests.cpp)
    target_link_libraries(region_asset_runtime_tests PRIVATE opensim_server_core)
    add_test(NAME region_asset_runtime_tests COMMAND region_asset_runtime_tests)
    add_executable(region_runtime_tests tests/region_runtime_tests.cpp)
    add_executable(movement_tests tests/movement_tests.cpp)
    add_executable(movement_animation_tests tests/movement_animation_tests.cpp)
    target_link_libraries(movement_animation_tests PRIVATE opensim_server_core)
    add_test(NAME movement_animation_tests COMMAND movement_animation_tests)
    add_executable(avatar_animation_tests tests/avatar_animation_tests.cpp)
    target_link_libraries(avatar_animation_tests PRIVATE opensim_server_core)
    add_test(NAME avatar_animation_tests COMMAND avatar_animation_tests)
    add_executable(ubode_tests tests/ubode_tests.cpp)
    target_link_libraries(ubode_tests PRIVATE opensim_server_core)
    add_test(NAME ubode_tests COMMAND ubode_tests "${OPENSIM_UBODE_LIBRARY}")
    target_link_libraries(movement_tests PRIVATE opensim_server_core)
    add_test(NAME movement_tests COMMAND movement_tests)
    target_link_libraries(region_runtime_tests PRIVATE opensim_server_core)
    add_test(NAME region_runtime_tests COMMAND region_runtime_tests "${OPENSIM_UBODE_LIBRARY}" "${CMAKE_CURRENT_SOURCE_DIR}/../bin/data/avataranimations.xml")
    target_compile_definitions(lsl_runtime_tests PRIVATE LSL_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../bin/assets/ScriptsAssetSet")
    target_link_libraries(lsl_runtime_tests PRIVATE opensim_server_core)
    add_test(NAME lsl_runtime_tests COMMAND lsl_runtime_tests)
    target_link_libraries(chat_packet_tests PRIVATE opensim_server_core)
    add_test(NAME chat_packet_tests COMMAND chat_packet_tests)
    target_link_libraries(avatar_packets_tests PRIVATE opensim_server_core)
    add_test(NAME avatar_packets_tests COMMAND avatar_packets_tests)
    target_link_libraries(rez_location_tests PRIVATE opensim_server_core)
    add_test(NAME rez_location_tests COMMAND rez_location_tests)
    target_link_libraries(object_update_tests PRIVATE opensim_server_core)
    add_test(NAME object_update_tests COMMAND object_update_tests)
    target_link_libraries(avatar_update_tests PRIVATE opensim_server_core)
    add_test(NAME avatar_update_tests COMMAND avatar_update_tests)
    target_link_libraries(terrain_layer_tests PRIVATE opensim_server_core)
    add_test(NAME terrain_layer_tests COMMAND terrain_layer_tests)
    target_link_libraries(scene_world_tests PRIVATE opensim_server_core)
    add_test(NAME scene_world_tests COMMAND scene_world_tests)
    add_executable(viewer_region_tests tests/viewer_region_tests.cpp)
    add_executable(viewer_session_probe tests/viewer_session_probe.cpp tests/legacy_inventory_probe.cpp tests/task_inventory_probe.cpp tests/inventory_script_rez_probe.cpp tests/user_inventory_probe.cpp tests/generic_asset_probe.cpp)
    target_link_libraries(viewer_session_probe PRIVATE opensim_server_core)
    target_link_libraries(viewer_region_tests PRIVATE opensim_server_core)
    add_test(NAME viewer_region_tests COMMAND viewer_region_tests)
    add_executable(simulator_features_probe tests/simulator_features_probe.cpp)
    add_executable(simulator_features_server tests/simulator_features_server.cpp)
    add_executable(http_range_probe tests/http_range_probe.cpp)
    target_link_libraries(http_range_probe PRIVATE opensim_server_core)
    add_executable(get_assets_tests tests/get_assets_tests.cpp)
    add_executable(asset_query_probe tests/asset_query_probe.cpp)
    add_executable(get_assets_server tests/get_assets_server.cpp)
    add_executable(get_assets_probe tests/get_assets_probe.cpp)
    add_executable(local_asset_connector_probe tests/local_asset_connector_probe.cpp)
    add_executable(asset_storage_probe tests/asset_storage_probe.cpp)
    add_executable(asset_service_probe tests/asset_service_probe.cpp)
    add_executable(asset_factory_probe tests/asset_factory_probe.cpp)
    add_executable(migration_parser_probe tests/migration_parser_probe.cpp)
    add_executable(migration_engine_probe tests/migration_engine_probe.cpp)
    add_executable(authentication_initializer_probe tests/authentication_initializer_probe.cpp)
    add_executable(authentication_endpoint_probe tests/authentication_endpoint_probe.cpp)
    add_executable(authentication_connector_probe tests/authentication_connector_probe.cpp)
    add_executable(rest_forms_probe tests/rest_forms_probe.cpp)
    target_link_libraries(rest_forms_probe PRIVATE opensim_server_core)
    add_executable(rest_forms_default_probe tests/rest_forms_default_probe.cpp)
    target_include_directories(rest_forms_default_probe SYSTEM PRIVATE third_party/httplib)
    target_link_libraries(rest_forms_default_probe PRIVATE Threads::Threads)
    if(WIN32)
        target_link_libraries(rest_forms_default_probe PRIVATE ws2_32)
    endif()
    target_link_libraries(authentication_connector_probe PRIVATE opensim_server_core)
    target_link_libraries(authentication_endpoint_probe PRIVATE opensim_server_core)
    target_link_libraries(authentication_initializer_probe PRIVATE opensim_server_core native_sqlite)
    add_executable(user_account_initializer_probe tests/user_account_initializer_probe.cpp)
    target_link_libraries(user_account_initializer_probe PRIVATE opensim_server_core native_sqlite)
    add_executable(grid_user_initializer_probe tests/grid_user_initializer_probe.cpp)
    target_link_libraries(grid_user_initializer_probe PRIVATE opensim_server_core native_sqlite)
    add_executable(avatar_initializer_probe tests/avatar_initializer_probe.cpp)
    target_link_libraries(avatar_initializer_probe PRIVATE opensim_server_core native_sqlite)
    add_executable(sqlite_generic_table_probe tests/sqlite_generic_table_probe.cpp)
    target_link_libraries(sqlite_generic_table_probe PRIVATE opensim_server_core native_sqlite)
    add_executable(friends_initializer_probe tests/friends_initializer_probe.cpp)
    target_link_libraries(friends_initializer_probe PRIVATE opensim_server_core native_sqlite)
    add_executable(sqlite_connection_probe tests/sqlite_connection_probe.cpp)
    target_link_libraries(sqlite_connection_probe PRIVATE opensim_server_core)
    add_executable(asset_initializer_probe tests/asset_initializer_probe.cpp)
    target_link_libraries(asset_initializer_probe PRIVATE opensim_server_core native_sqlite)
    target_link_libraries(migration_engine_probe PRIVATE opensim_server_core native_sqlite)
    target_link_libraries(migration_parser_probe PRIVATE opensim_server_core)
    add_executable(filesystem_asset_loader_probe tests/filesystem_asset_loader_probe.cpp)
    target_link_libraries(filesystem_asset_loader_probe PRIVATE opensim_server_core)
    target_link_libraries(asset_factory_probe PRIVATE opensim_server_core)
    target_link_libraries(asset_service_probe PRIVATE opensim_server_core)
    target_link_libraries(asset_storage_probe PRIVATE opensim_server_core native_sqlite)
    target_link_libraries(local_asset_connector_probe PRIVATE opensim_server_core)
    target_link_libraries(get_assets_probe PRIVATE opensim_server_core)
    target_link_libraries(get_assets_server PRIVATE opensim_server_core)
    target_link_libraries(asset_query_probe PRIVATE opensim_server_core)
    target_link_libraries(get_assets_tests PRIVATE opensim_server_core)
    add_test(NAME get_assets_tests COMMAND get_assets_tests)
    target_link_libraries(simulator_features_server PRIVATE opensim_server_core)
    target_link_libraries(simulator_features_probe PRIVATE opensim_server_core)
    target_link_libraries(scene_capabilities_tests PRIVATE opensim_server_core)
    add_test(NAME scene_capabilities_tests COMMAND scene_capabilities_tests)
    target_link_libraries(scene_capabilities_server PRIVATE opensim_server_core)
    target_link_libraries(scene_capability_events_probe PRIVATE opensim_server_core)
    target_link_libraries(event_queue_server PRIVATE opensim_server_core)
    target_link_libraries(event_queue_probe PRIVATE opensim_server_core)
    target_link_libraries(event_queue_tests PRIVATE opensim_server_core)
    add_test(NAME event_queue_tests COMMAND event_queue_tests)
    target_link_libraries(http_response_probe PRIVATE opensim_server_core)
    add_executable(poll_manager_probe tests/poll_manager_probe.cpp)
    add_executable(poll_runtime_tests tests/poll_runtime_tests.cpp)
    add_executable(async_http_server tests/async_http_server.cpp)
    add_executable(poll_http_server tests/poll_http_server.cpp)
    target_link_libraries(poll_http_server PRIVATE opensim_server_core)
    target_link_libraries(async_http_server PRIVATE opensim_server_core)
    target_link_libraries(poll_runtime_tests PRIVATE opensim_server_core)
    add_test(NAME poll_runtime_tests COMMAND poll_runtime_tests)
    set_tests_properties(poll_runtime_tests PROPERTIES TIMEOUT 30)
    target_link_libraries(poll_manager_probe PRIVATE opensim_server_core)
    target_link_libraries(poll_request_probe PRIVATE opensim_server_core)
    target_link_libraries(http_route_registry_probe PRIVATE opensim_server_core)
    add_executable(culture_prefix_probe tests/culture_prefix_probe.cpp)
    target_link_libraries(culture_prefix_probe PRIVATE opensim_framework)
    add_executable(scene_client_verification_probe tests/scene_client_verification_probe.cpp)
    target_link_libraries(scene_client_verification_probe PRIVATE opensim_server_core)
    add_executable(seed_caps_server tests/seed_caps_server.cpp)
    target_link_libraries(seed_caps_server PRIVATE opensim_server_core)
    add_executable(seed_caps_probe tests/seed_caps_probe.cpp)
    target_link_libraries(seed_caps_probe PRIVATE opensim_server_core)
    add_executable(caps_probe tests/caps_probe.cpp)
    target_link_libraries(caps_probe PRIVATE opensim_server_core)
    add_executable(caps_handlers_server tests/caps_handlers_server.cpp)
    target_link_libraries(caps_handlers_server PRIVATE opensim_server_core)
    add_executable(caps_handlers_probe tests/caps_handlers_probe.cpp)
    target_link_libraries(caps_handlers_probe PRIVATE opensim_server_core)
    add_executable(capabilities_module_probe tests/capabilities_module_probe.cpp)
    target_link_libraries(capabilities_module_probe PRIVATE opensim_server_core)
    add_executable(viewer_name_probe tests/viewer_name_probe.cpp)
    target_link_libraries(viewer_name_probe PRIVATE opensim_server_core)
    add_executable(presence_state_probe tests/presence_state_probe.cpp)
    target_link_libraries(presence_state_probe PRIVATE opensim_server_core)
    add_executable(scene_admission_probe tests/scene_admission_probe.cpp)
    target_link_libraries(scene_admission_probe PRIVATE opensim_server_core)
    add_executable(scene_access_probe tests/scene_access_probe.cpp)
    target_link_libraries(scene_access_probe PRIVATE opensim_server_core)
    add_executable(simulation_object_probe tests/simulation_object_probe.cpp)
    target_link_libraries(simulation_object_probe PRIVATE opensim_server_core)
    add_executable(object_handler_probe tests/object_handler_probe.cpp)
    target_link_libraries(object_handler_probe PRIVATE opensim_server_core)
    add_executable(local_simulation_server tests/local_simulation_server.cpp)
    target_link_libraries(local_simulation_server PRIVATE opensim_server_core)
    add_executable(local_simulation_probe tests/local_simulation_probe.cpp)
    target_link_libraries(local_simulation_probe PRIVATE opensim_server_core)
    add_executable(agent_put_probe tests/agent_put_probe.cpp)
    target_link_libraries(agent_put_probe PRIVATE opensim_server_core)
    add_executable(agent_data_probe tests/agent_data_probe.cpp)
    target_link_libraries(agent_data_probe PRIVATE opensim_server_core)
    add_executable(agent_update_records_probe tests/agent_update_records_probe.cpp)
    target_link_libraries(agent_update_records_probe PRIVATE opensim_server_core)
    add_executable(agent_position_probe tests/agent_position_probe.cpp)
    target_link_libraries(agent_position_probe PRIVATE opensim_server_core)
    add_executable(agent_delete_query_probe tests/agent_delete_query_probe.cpp)
    target_link_libraries(agent_delete_query_probe PRIVATE opensim_server_core)
    add_executable(agent_delete_tests tests/agent_delete_tests.cpp)
    target_link_libraries(agent_delete_tests PRIVATE opensim_server_core)
    add_test(NAME agent_delete_tests COMMAND agent_delete_tests)
    add_executable(agent_delete_probe tests/agent_delete_probe.cpp)
    target_link_libraries(agent_delete_probe PRIVATE opensim_server_core)
    add_executable(agent_post_probe tests/agent_post_probe.cpp)
    target_link_libraries(agent_post_probe PRIVATE opensim_server_core)
    add_executable(agent_access_server tests/agent_access_server.cpp)
    target_link_libraries(agent_access_server PRIVATE opensim_server_core)
    add_executable(agent_access_probe tests/agent_access_probe.cpp)
    target_link_libraries(agent_access_probe PRIVATE opensim_server_core)
    add_executable(friends_connector_probe tests/friends_connector_probe.cpp)
    target_link_libraries(friends_connector_probe PRIVATE opensim_server_core)
    add_executable(service_xml_probe tests/service_xml_probe.cpp)
    target_link_libraries(service_xml_probe PRIVATE opensim_server_core)
    add_executable(friends_endpoint_probe tests/friends_endpoint_probe.cpp)
    target_link_libraries(friends_endpoint_probe PRIVATE opensim_server_core)
    add_executable(friends_tests tests/friends_tests.cpp)
    target_link_libraries(friends_tests PRIVATE opensim_server_core)
    add_test(NAME friends_tests COMMAND friends_tests)
    add_executable(friends_probe tests/friends_probe.cpp)
    target_link_libraries(friends_probe PRIVATE opensim_server_core)
    add_executable(time_zone_tests tests/time_zone_tests.cpp)
    target_link_libraries(time_zone_tests PRIVATE opensim_server_core)
    add_test(NAME time_zone_tests COMMAND time_zone_tests)
    add_executable(time_zone_probe tests/time_zone_probe.cpp)
    target_link_libraries(time_zone_probe PRIVATE opensim_server_core)
    add_executable(host_resolver_tests tests/host_resolver_tests.cpp)
    target_link_libraries(host_resolver_tests PRIVATE opensim_server_core)
    add_test(NAME host_resolver_tests COMMAND host_resolver_tests)
    add_executable(host_resolver_probe tests/host_resolver_probe.cpp)
    target_link_libraries(host_resolver_probe PRIVATE opensim_server_core)
    add_executable(login_response_tests tests/login_response_tests.cpp)
    target_link_libraries(login_response_tests PRIVATE opensim_server_core)
    target_compile_definitions(login_response_tests PRIVATE OPENSIM_REPOSITORY_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
    add_test(NAME login_response_tests COMMAND login_response_tests)
    add_executable(login_response_probe tests/login_response_probe.cpp)
    target_link_libraries(login_response_probe PRIVATE opensim_server_core)
    add_executable(simulation_transport_probe tests/simulation_transport_probe.cpp)
    target_link_libraries(simulation_transport_probe PRIVATE opensim_server_core)
    add_executable(simulation_connector_probe tests/simulation_connector_probe.cpp)
    target_link_libraries(simulation_connector_probe PRIVATE opensim_server_core)
    add_executable(osd_json_probe tests/osd_json_probe.cpp)
    target_link_libraries(osd_json_probe PRIVATE opensim_server_core)
    add_executable(avatar_endpoint_probe tests/avatar_endpoint_probe.cpp)
    target_link_libraries(avatar_endpoint_probe PRIVATE opensim_server_core)
    add_executable(account_endpoint_probe tests/account_endpoint_probe.cpp)
    target_link_libraries(account_endpoint_probe PRIVATE opensim_server_core)
    add_executable(account_provisioner_probe tests/account_provisioner_probe.cpp)
    target_link_libraries(account_provisioner_probe PRIVATE opensim_server_core)
    add_executable(appearance_provisioner_probe tests/appearance_provisioner_probe.cpp)
    target_link_libraries(appearance_provisioner_probe PRIVATE opensim_server_core)
    add_executable(appearance_transfer_tests tests/appearance_transfer_tests.cpp)
    target_link_libraries(appearance_transfer_tests PRIVATE opensim_server_core)
    add_test(NAME appearance_transfer_tests COMMAND appearance_transfer_tests)
    add_executable(avatar_data_probe tests/avatar_data_probe.cpp)
    target_link_libraries(avatar_data_probe PRIVATE opensim_server_core)
    add_executable(avatar_service_probe tests/avatar_service_probe.cpp)
    target_link_libraries(avatar_service_probe PRIVATE opensim_server_core)
    add_executable(login_agent_factory_tests tests/login_agent_factory_tests.cpp)
    target_link_libraries(login_agent_factory_tests PRIVATE opensim_server_core)
    add_test(NAME login_agent_factory_tests COMMAND login_agent_factory_tests)
    add_executable(login_agent_factory_probe tests/login_agent_factory_probe.cpp)
    target_link_libraries(login_agent_factory_probe PRIVATE opensim_server_core)
    add_executable(agent_circuit_osd_tests tests/agent_circuit_osd_tests.cpp)
    target_link_libraries(agent_circuit_osd_tests PRIVATE opensim_server_core)
    add_test(NAME agent_circuit_osd_tests COMMAND agent_circuit_osd_tests)
    add_executable(agent_circuit_osd_probe tests/agent_circuit_osd_probe.cpp)
    target_link_libraries(agent_circuit_osd_probe PRIVATE opensim_server_core)
    add_executable(avatar_appearance_probe tests/avatar_appearance_probe.cpp)
    target_link_libraries(avatar_appearance_probe PRIVATE opensim_server_core)
    add_executable(texture_entry_tests tests/texture_entry_tests.cpp)
    target_link_libraries(texture_entry_tests PRIVATE opensim_framework)
    add_test(NAME texture_entry_tests COMMAND texture_entry_tests)
    set_tests_properties(texture_entry_tests PROPERTIES TIMEOUT 20)
    add_executable(texture_entry_probe tests/texture_entry_probe.cpp)
    target_link_libraries(texture_entry_probe PRIVATE opensim_framework)
    add_executable(texture_face_probe tests/texture_face_probe.cpp)
    target_link_libraries(texture_face_probe PRIVATE opensim_framework)
    add_executable(wearable_cache_probe tests/wearable_cache_probe.cpp)
    target_link_libraries(wearable_cache_probe PRIVATE opensim_server_core)
    add_executable(asset_model_probe tests/asset_model_probe.cpp)
    target_link_libraries(asset_model_probe PRIVATE opensim_framework)
    add_executable(appearance_models_probe tests/appearance_models_probe.cpp)
    target_link_libraries(appearance_models_probe PRIVATE opensim_server_core)
    add_executable(osd_numeric_probe tests/osd_numeric_probe.cpp)
    target_link_libraries(osd_numeric_probe PRIVATE opensim_server_core)
    add_executable(avatar_wearable_probe tests/avatar_wearable_probe.cpp)
    target_link_libraries(avatar_wearable_probe PRIVATE opensim_server_core)
    add_executable(login_orchestration_tests tests/login_orchestration_tests.cpp)
    add_executable(agent_launch_probe tests/agent_launch_probe.cpp)
    target_link_libraries(agent_launch_probe PRIVATE opensim_server_core)
    add_executable(agent_launch_tests tests/agent_launch_tests.cpp)
    target_link_libraries(agent_launch_tests PRIVATE opensim_server_core)
    add_test(NAME agent_launch_tests COMMAND agent_launch_tests)
    target_link_libraries(login_orchestration_tests PRIVATE opensim_server_core)
    add_test(NAME login_orchestration_tests COMMAND login_orchestration_tests)
    add_executable(login_orchestration_probe tests/login_orchestration_probe.cpp)
    target_link_libraries(login_orchestration_probe PRIVATE opensim_server_core)
    add_executable(login_destination_tests tests/login_destination_tests.cpp)
    target_link_libraries(login_destination_tests PRIVATE opensim_server_core)
    add_test(NAME login_destination_tests COMMAND login_destination_tests)
    add_executable(login_destination_probe tests/login_destination_probe.cpp)
    target_link_libraries(login_destination_probe PRIVATE opensim_server_core)
    add_executable(http_uri_tests tests/http_uri_tests.cpp)
    target_link_libraries(http_uri_tests PRIVATE opensim_framework)
    add_test(NAME http_uri_tests COMMAND http_uri_tests)
    add_executable(grid_identity_tests tests/grid_identity_tests.cpp)
    target_link_libraries(grid_identity_tests PRIVATE opensim_server_core)
    add_test(NAME grid_identity_tests COMMAND grid_identity_tests)
    add_executable(grid_identity_probe tests/grid_identity_probe.cpp)
    target_link_libraries(grid_identity_probe PRIVATE opensim_server_core)
    add_executable(http_uri_probe tests/http_uri_probe.cpp)
    target_link_libraries(http_uri_probe PRIVATE opensim_framework)
    add_executable(sqlite_region_tests tests/sqlite_region_tests.cpp)
    target_link_libraries(sqlite_region_tests PRIVATE opensim_server_core)
    add_test(NAME sqlite_region_tests COMMAND sqlite_region_tests)
    add_executable(http_host_tests tests/http_host_tests.cpp)
    target_link_libraries(http_host_tests PRIVATE opensim_framework)
    add_test(NAME http_host_tests COMMAND http_host_tests)
    add_executable(grid_endpoint_probe tests/grid_endpoint_probe.cpp)
    target_link_libraries(grid_endpoint_probe PRIVATE opensim_server_core)
    add_executable(region_uri_tests tests/region_uri_tests.cpp)
    target_link_libraries(region_uri_tests PRIVATE opensim_framework)
    add_test(NAME region_uri_tests COMMAND region_uri_tests)
    add_executable(region_uri_probe tests/region_uri_probe.cpp)
    target_link_libraries(region_uri_probe PRIVATE opensim_framework)
    add_executable(grid_tests tests/grid_tests.cpp)
    target_link_libraries(grid_tests PRIVATE opensim_server_core)
    add_test(NAME grid_tests COMMAND grid_tests)
    add_executable(grid_probe tests/grid_probe.cpp)
    target_link_libraries(grid_probe PRIVATE opensim_server_core)
    add_executable(grid_user_endpoint_probe tests/grid_user_endpoint_probe.cpp)
    target_link_libraries(grid_user_endpoint_probe PRIVATE opensim_server_core)
    add_executable(service_endpoint_tests tests/service_endpoint_tests.cpp)
    target_link_libraries(service_endpoint_tests PRIVATE opensim_server_core)
    add_executable(presence_endpoint_probe tests/presence_endpoint_probe.cpp)
    target_link_libraries(presence_endpoint_probe PRIVATE opensim_server_core)
    add_test(NAME service_endpoint_tests COMMAND service_endpoint_tests)
    add_executable(grid_user_tests tests/grid_user_tests.cpp)
    target_link_libraries(grid_user_tests PRIVATE opensim_server_core)
    add_test(NAME grid_user_tests COMMAND grid_user_tests)
    add_executable(grid_user_probe tests/grid_user_probe.cpp)
    target_link_libraries(grid_user_probe PRIVATE opensim_server_core)
    add_executable(inventory_tests tests/inventory_tests.cpp)
    target_link_libraries(inventory_tests PRIVATE opensim_server_core)
    add_test(NAME inventory_tests COMMAND inventory_tests)
    add_executable(inventory_probe tests/inventory_probe.cpp)
    target_link_libraries(inventory_probe PRIVATE opensim_server_core)
    add_executable(library_probe tests/library_probe.cpp)
    target_link_libraries(library_probe PRIVATE opensim_server_core)
    add_executable(presence_probe tests/presence_probe.cpp)
    target_link_libraries(presence_probe PRIVATE opensim_server_core)
    add_executable(presence_tests tests/presence_tests.cpp)
    target_link_libraries(presence_tests PRIVATE opensim_server_core)
    add_test(NAME presence_tests COMMAND presence_tests)
    set_tests_properties(presence_tests PROPERTIES TIMEOUT 20)
    add_test(NAME inventory_tree_tests COMMAND library_probe --tree)
    add_executable(queue_tests tests/queue_tests.cpp)
    target_link_libraries(queue_tests PRIVATE opensim_framework)
    add_test(NAME queue_tests COMMAND queue_tests)
    add_executable(transport_tests tests/transport_tests.cpp)
    target_link_libraries(transport_tests PRIVATE opensim_framework)
    add_test(NAME transport_tests COMMAND transport_tests)
    set_tests_properties(transport_tests PROPERTIES TIMEOUT 20)
    add_executable(udp_state_tests tests/udp_state_tests.cpp)
    target_link_libraries(udp_state_tests PRIVATE opensim_framework)
    add_test(NAME udp_state_tests COMMAND udp_state_tests)
    add_executable(circuit_tests tests/circuit_tests.cpp)
    target_link_libraries(circuit_tests PRIVATE opensim_framework)
    add_test(NAME circuit_tests COMMAND circuit_tests)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(NAME vendored_dependency_integrity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_dependencies.py")
        add_test(NAME poll_http_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/poll_http_network.py" --server "$<TARGET_FILE:poll_http_server>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/poll-http-network")
        set_tests_properties(poll_http_network PROPERTIES TIMEOUT 90)
        add_test(NAME async_http_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/async_http_network.py" --server "$<TARGET_FILE:async_http_server>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/async-http-network")
        set_tests_properties(async_http_network PROPERTIES TIMEOUT 90)
        if(OpenSSL_FOUND)
            add_test(NAME get_assets_https_network COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/get_assets_network.py" --server "$<TARGET_FILE:get_assets_server>"
                --work "${CMAKE_CURRENT_BINARY_DIR}/get-assets-https-network" --tls)
            set_tests_properties(get_assets_https_network PROPERTIES TIMEOUT 90)
            add_test(NAME simulator_features_https_network COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/simulator_features_network.py" --server "$<TARGET_FILE:simulator_features_server>"
                --work "${CMAKE_CURRENT_BINARY_DIR}/simulator-features-https-network" --tls)
            set_tests_properties(simulator_features_https_network PROPERTIES TIMEOUT 90)
            add_test(NAME scene_capabilities_https_network COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/scene_capabilities_network.py" --server "$<TARGET_FILE:scene_capabilities_server>"
                --work "${CMAKE_CURRENT_BINARY_DIR}/scene-capabilities-https-network" --tls)
            set_tests_properties(scene_capabilities_https_network PROPERTIES TIMEOUT 60)
            add_test(NAME event_queue_https_network COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/event_queue_network.py" --server "$<TARGET_FILE:event_queue_server>"
                --work "${CMAKE_CURRENT_BINARY_DIR}/event-queue-https-network" --tls)
            set_tests_properties(event_queue_https_network PROPERTIES TIMEOUT 60)
            add_test(NAME poll_https_network COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/poll_http_network.py" --server "$<TARGET_FILE:poll_http_server>"
                --work "${CMAKE_CURRENT_BINARY_DIR}/poll-https-network" --tls)
            set_tests_properties(poll_https_network PROPERTIES TIMEOUT 90)
            add_test(NAME async_https_network COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/async_http_network.py" --server "$<TARGET_FILE:async_http_server>"
                --work "${CMAKE_CURRENT_BINARY_DIR}/async-https-network" --tls)
            set_tests_properties(async_https_network PROPERTIES TIMEOUT 90)
        endif()
        add_test(NAME login_orchestration_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/login_orchestration_network.py" --native "$<TARGET_FILE:opensim-native>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/login-orchestration-network")
        set_tests_properties(login_orchestration_network PROPERTIES TIMEOUT 90)
        add_test(NAME lludp_schema_consistency COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_lludp_schema.py" --check)
        add_test(NAME server_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/server_network.py" --native "$<TARGET_FILE:opensim-native>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/network-tests")
        set_tests_properties(server_network PROPERTIES TIMEOUT 90)
    endif()
    add_executable(server_tests tests/server_tests.cpp)
    target_link_libraries(server_tests PRIVATE opensim_server_core native_sqlite)
    add_test(NAME server_tests COMMAND server_tests)
    add_executable(login_probe tests/login_probe.cpp)
    target_link_libraries(login_probe PRIVATE opensim_server_core)
    add_executable(lludp_probe tests/lludp_probe.cpp)
    target_link_libraries(lludp_probe PRIVATE opensim_framework)
    add_executable(native_tests tests/native_tests.cpp)
    target_link_libraries(native_tests PRIVATE opensim_framework)
    add_test(NAME native_tests COMMAND native_tests)

    add_executable(parity_probe tests/parity_probe.cpp)
    target_link_libraries(parity_probe PRIVATE opensim_framework)
    option(OPENSIM_DIFFERENTIAL_TESTS "Compare port with original C# sources (requires .NET SDK and Python)" OFF)
    if(OPENSIM_DIFFERENTIAL_TESTS)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        find_program(DOTNET_EXECUTABLE dotnet REQUIRED)
        add_test(NAME avatar_service_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_avatar_service.py" --native "$<TARGET_FILE:avatar_service_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/avatar-service-reference")
        set_tests_properties(avatar_service_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*avatar service source observations")
        add_test(NAME avatar_data_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_avatar_data.py" --native "$<TARGET_FILE:avatar_data_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/avatar-data-reference")
        set_tests_properties(avatar_data_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*avatar data source observations")
        add_test(NAME appearance_provisioner_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_appearance_provisioner.py" --native "$<TARGET_FILE:appearance_provisioner_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/appearance-provisioner-reference")
        set_tests_properties(appearance_provisioner_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*appearance provisioning source observations")
        add_test(NAME account_provisioner_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_account_provisioner.py" --native "$<TARGET_FILE:account_provisioner_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/account-provisioner-reference")
        set_tests_properties(account_provisioner_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*account provisioning source observations")
        add_test(NAME agent_post_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/agent_post_network.py" --server "$<TARGET_FILE:agent_access_server>" --client "$<TARGET_FILE:simulation_transport_probe>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/agent-post-network")
        set_tests_properties(agent_post_network PROPERTIES TIMEOUT 90)
        add_test(NAME gzip_compress_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_gzip_compress.py" --native "$<TARGET_FILE:gzip_compress_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/gzip-compress-reference")
        set_tests_properties(gzip_compress_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*gzip compression byte comparisons")
        add_test(NAME simulation_body_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_simulation_body.py" --native "$<TARGET_FILE:simulation_body_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/simulation-body-reference")
        set_tests_properties(simulation_body_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*simulation body source observations")
        add_test(NAME simulation_position_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/simulation_position_network.py" --native "$<TARGET_FILE:simulation_position_probe>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/simulation-position-network")
        set_tests_properties(simulation_position_network PROPERTIES TIMEOUT 90)
        add_test(NAME simulation_position_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_simulation_position.py" --native "$<TARGET_FILE:simulation_position_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/simulation-position-reference")
        set_tests_properties(simulation_position_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*simulation position queue/worker source observations")
        add_test(NAME capabilities_module_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/capabilities_module_network.py" --server "$<TARGET_FILE:local_simulation_server>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/capabilities-module-network")
        set_tests_properties(capabilities_module_network PROPERTIES TIMEOUT 90)
        add_test(NAME http_route_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/http_route_network.py" --server "$<TARGET_FILE:http_route_server>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/http-route-network")
        set_tests_properties(http_route_network PROPERTIES TIMEOUT 60)
        add_test(NAME http_response_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_http_response.py" --native "$<TARGET_FILE:http_response_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/http-response-reference")
        add_test(NAME event_queue_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_event_queue.py" --native "$<TARGET_FILE:event_queue_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/event-queue-reference")
        add_test(NAME scene_capability_events_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_scene_capability_events.py" --native "$<TARGET_FILE:scene_capability_events_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/scene-events-reference")
        add_test(NAME simulator_features_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_simulator_features.py" --native "$<TARGET_FILE:simulator_features_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/simulator-features-reference")
        set_tests_properties(simulator_features_source_parity PROPERTIES TIMEOUT 180)
        add_test(NAME simulator_features_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/simulator_features_network.py" --server "$<TARGET_FILE:simulator_features_server>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/simulator-features-network")
        set_tests_properties(simulator_features_network PROPERTIES TIMEOUT 90)
        add_test(NAME scene_capabilities_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/scene_capabilities_network.py" --server "$<TARGET_FILE:scene_capabilities_server>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/scene-capabilities-network")
        set_tests_properties(scene_capabilities_network PROPERTIES TIMEOUT 60)
        add_test(NAME event_queue_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/event_queue_network.py" --server "$<TARGET_FILE:event_queue_server>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/event-queue-network")
        set_tests_properties(event_queue_network PROPERTIES TIMEOUT 60)
        set_tests_properties(http_response_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*HTTP response lifecycle and cookie source observations")
        add_test(NAME poll_manager_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_poll_manager.py" --native "$<TARGET_FILE:poll_manager_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/poll-manager-reference")
        set_tests_properties(poll_manager_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*polling manager source observations")
        add_test(NAME poll_request_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_poll_request.py" --native "$<TARGET_FILE:poll_request_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/poll-request-reference")
        set_tests_properties(poll_request_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*polling request source observations")
        add_test(NAME http_route_registry_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_http_route_registry.py" --native "$<TARGET_FILE:http_route_registry_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/http-route-registry-reference")
        set_tests_properties(http_route_registry_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*HTTP route registry source observations")
        add_test(NAME culture_prefix_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_culture_prefix.py" --native "$<TARGET_FILE:culture_prefix_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/culture-prefix-reference")
        set_tests_properties(culture_prefix_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*cultural prefix observations")
        add_test(NAME scene_client_verification_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/scene_client_verification_network.py" --server "$<TARGET_FILE:seed_caps_server>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/scene-client-verification-network")
        set_tests_properties(scene_client_verification_network PROPERTIES TIMEOUT 60)
        add_test(NAME scene_client_verification_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_scene_client_verification.py" --native "$<TARGET_FILE:scene_client_verification_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/scene-client-verification-reference")
        set_tests_properties(scene_client_verification_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*scene client verification source observations")
        add_test(NAME seed_caps_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/seed_caps_network.py" --server "$<TARGET_FILE:seed_caps_server>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/seed-caps-network")
        set_tests_properties(seed_caps_network PROPERTIES TIMEOUT 60)
        add_test(NAME seed_caps_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_seed_caps.py" --native "$<TARGET_FILE:seed_caps_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/seed-caps-reference")
        set_tests_properties(seed_caps_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*seed capability source observations")
        add_test(NAME caps_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/caps_network.py" --server "$<TARGET_FILE:caps_handlers_server>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/caps-network")
        set_tests_properties(caps_network PROPERTIES TIMEOUT 60)
        add_test(NAME caps_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_caps.py" --native "$<TARGET_FILE:caps_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/caps-reference")
        set_tests_properties(caps_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*concrete caps source observations")
        add_test(NAME caps_handlers_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/caps_handlers_network.py" --server "$<TARGET_FILE:caps_handlers_server>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/caps-handlers-network")
        set_tests_properties(caps_handlers_network PROPERTIES TIMEOUT 60)
        add_test(NAME caps_handlers_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_caps_handlers.py" --native "$<TARGET_FILE:caps_handlers_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/caps-handlers-reference")
        set_tests_properties(caps_handlers_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*capability handler source observations")
        add_test(NAME capabilities_module_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_capabilities_module.py" --native "$<TARGET_FILE:capabilities_module_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/capabilities-module-reference")
        set_tests_properties(capabilities_module_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*capabilities module source observations")
        add_test(NAME viewer_name_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_viewer_name.py" --native "$<TARGET_FILE:viewer_name_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/viewer-name-reference")
        set_tests_properties(viewer_name_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*viewer utility scenarios")
        add_test(NAME presence_state_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_presence_state.py" --native "$<TARGET_FILE:presence_state_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/presence-state-reference")
        set_tests_properties(presence_state_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*presence state source observations")
        add_test(NAME scene_admission_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/scene_admission_network.py" --server "$<TARGET_FILE:local_simulation_server>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/scene-admission-network")
        set_tests_properties(scene_admission_network PROPERTIES TIMEOUT 90)
        add_test(NAME scene_admission_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_scene_admission.py" --native "$<TARGET_FILE:scene_admission_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/scene-admission-reference")
        set_tests_properties(scene_admission_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*scene admission source observations")
        add_test(NAME scene_access_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/scene_access_network.py" --server "$<TARGET_FILE:local_simulation_server>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/scene-access-network")
        set_tests_properties(scene_access_network PROPERTIES TIMEOUT 90)
        add_test(NAME scene_access_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_scene_access.py" --native "$<TARGET_FILE:scene_access_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/scene-access-reference")
        set_tests_properties(scene_access_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*scene access source observations")
        add_test(NAME simulation_object_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/simulation_object_network.py" --client "$<TARGET_FILE:simulation_object_probe>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/simulation-object-network")
        set_tests_properties(simulation_object_network PROPERTIES TIMEOUT 90)
        add_test(NAME simulation_object_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_simulation_object.py" --native "$<TARGET_FILE:simulation_object_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/simulation-object-reference")
        set_tests_properties(simulation_object_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*outgoing object connector source observations")
        add_test(NAME object_handler_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/object_handler_network.py" --server "$<TARGET_FILE:local_simulation_server>" --client "$<TARGET_FILE:simulation_object_probe>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/object-handler-network")
        set_tests_properties(object_handler_network PROPERTIES TIMEOUT 60)
        add_test(NAME object_handler_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_object_handler.py" --native "$<TARGET_FILE:object_handler_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/object-handler-reference")
        set_tests_properties(object_handler_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*object handler source observations")
        add_test(NAME local_simulation_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/local_simulation_network.py" --server "$<TARGET_FILE:local_simulation_server>"
            --data-client "$<TARGET_FILE:simulation_agent_data_probe>" --position-client "$<TARGET_FILE:simulation_position_probe>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/local-simulation-network")
        set_tests_properties(local_simulation_network PROPERTIES TIMEOUT 90)
        add_test(NAME local_simulation_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_local_simulation.py" --native "$<TARGET_FILE:local_simulation_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/local-simulation-reference")
        set_tests_properties(local_simulation_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*local simulation connector source observations")
        add_test(NAME agent_put_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/agent_put_network.py" --server "$<TARGET_FILE:agent_access_server>"
            --data-client "$<TARGET_FILE:simulation_agent_data_probe>" --position-client "$<TARGET_FILE:simulation_position_probe>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/agent-put-network")
        set_tests_properties(agent_put_network PROPERTIES TIMEOUT 90)
        add_test(NAME simulation_agent_data_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_simulation_agent_data.py" --native "$<TARGET_FILE:simulation_agent_data_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/simulation-agent-data-reference")
        set_tests_properties(simulation_agent_data_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*full agent-update connector source observations")
        add_test(NAME agent_put_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_agent_put.py" --native "$<TARGET_FILE:agent_put_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/agent-put-reference")
        set_tests_properties(agent_put_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*inbound agent PUT source observations")
        add_test(NAME agent_data_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_agent_data.py" --native "$<TARGET_FILE:agent_data_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/agent-data-reference")
        set_tests_properties(agent_data_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*full agent-data source observations")
        add_test(NAME agent_update_records_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_agent_update_records.py" --native "$<TARGET_FILE:agent_update_records_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/agent-update-records-reference")
        set_tests_properties(agent_update_records_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*agent transfer record source observations")
        add_test(NAME agent_position_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_agent_position.py" --native "$<TARGET_FILE:agent_position_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/agent-position-reference")
        set_tests_properties(agent_position_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*agent position source observations")
        add_test(NAME agent_delete_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/agent_delete_network.py" --server "$<TARGET_FILE:agent_access_server>" --client "$<TARGET_FILE:simulation_lifecycle_probe>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/agent-delete-network")
        set_tests_properties(agent_delete_network PROPERTIES TIMEOUT 90)
        add_test(NAME agent_delete_query_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_agent_delete_query.py" --native "$<TARGET_FILE:agent_delete_query_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/agent-delete-query-reference")
        set_tests_properties(agent_delete_query_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*agent DELETE auth query observations")
        add_test(NAME simulation_lifecycle_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/simulation_lifecycle_network.py" --native "$<TARGET_FILE:simulation_lifecycle_probe>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/simulation-lifecycle-network")
        set_tests_properties(simulation_lifecycle_network PROPERTIES TIMEOUT 90)
        add_test(NAME simulation_lifecycle_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_simulation_lifecycle.py" --native "$<TARGET_FILE:simulation_lifecycle_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/simulation-lifecycle-reference")
        set_tests_properties(simulation_lifecycle_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*remote agent lifecycle source observations")
        add_test(NAME agent_delete_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_agent_delete.py" --native "$<TARGET_FILE:agent_delete_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/agent-delete-reference")
        set_tests_properties(agent_delete_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*agent release/close source observations")
        add_test(NAME agent_post_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_agent_post.py" --native "$<TARGET_FILE:agent_post_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/agent-post-reference")
        set_tests_properties(agent_post_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*inbound create-agent source observations")
        add_test(NAME agent_access_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/agent_access_network.py" --server "$<TARGET_FILE:agent_access_server>" --client "$<TARGET_FILE:simulation_transport_probe>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/agent-access-network")
        set_tests_properties(agent_access_network PROPERTIES TIMEOUT 90)
        add_test(NAME agent_access_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_agent_access.py" --native "$<TARGET_FILE:agent_access_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/agent-access-reference")
        set_tests_properties(agent_access_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*inbound agent access source observations")
        add_test(NAME friends_connector_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/friends_connector_network.py" --native "$<TARGET_FILE:friends_connector_probe>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/friends-connector-network")
        set_tests_properties(friends_connector_network PROPERTIES TIMEOUT 90)
        add_test(NAME friends_connector_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_friends_connector.py" --native "$<TARGET_FILE:friends_connector_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/friends-connector-reference")
        set_tests_properties(friends_connector_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*friends connector source observations")
        add_test(NAME service_xml_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_service_xml.py" --native "$<TARGET_FILE:service_xml_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/service-xml-reference")
        set_tests_properties(service_xml_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*service XML source observations")
        add_test(NAME friends_endpoint_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_friends_endpoint.py" --native "$<TARGET_FILE:friends_endpoint_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/friends-endpoint-reference")
        set_tests_properties(friends_endpoint_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*friends handler source observations")
        add_test(NAME friends_sqlite_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_friends_sqlite.py" --native "$<TARGET_FILE:friends_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/friends-sqlite-reference")
        set_tests_properties(friends_sqlite_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*friends SQLite source observations")
        add_test(NAME friends_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_friends.py" --native "$<TARGET_FILE:friends_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/friends-reference")
        set_tests_properties(friends_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*friends source observations")
        add_test(NAME time_zone_runtime_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_time_zone.py" --native "$<TARGET_FILE:time_zone_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/time-zone-reference")
        set_tests_properties(time_zone_runtime_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*timezone runtime observations")
        add_test(NAME host_resolver_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_host_resolver.py" --native "$<TARGET_FILE:host_resolver_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/host-resolver-reference")
        set_tests_properties(host_resolver_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*host resolver source observations")
        add_test(NAME login_response_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_login_response.py" --native "$<TARGET_FILE:login_response_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/login-response-reference")
        set_tests_properties(login_response_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*login response source observations")
        add_test(NAME simulation_transport_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/simulation_transport_network.py" --native "$<TARGET_FILE:simulation_transport_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/simulation-transport-network")
        set_tests_properties(simulation_transport_network PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*simulation HTTP transport observations")
        add_test(NAME simulation_connector_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_simulation_connector.py" --native "$<TARGET_FILE:simulation_connector_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/simulation-connector-reference")
        set_tests_properties(simulation_connector_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*simulation connector source observations")
        add_test(NAME osd_json_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_osd_json.py" --native "$<TARGET_FILE:osd_json_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/osd-json-reference")
        set_tests_properties(osd_json_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*SDK JSON observations")
        add_test(NAME avatar_endpoint_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_avatar_endpoint.py" --native "$<TARGET_FILE:avatar_endpoint_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/avatar-endpoint-reference")
        set_tests_properties(avatar_endpoint_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*avatar endpoint source observations")
        add_test(NAME account_endpoint_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_account_endpoint.py" --native "$<TARGET_FILE:account_endpoint_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/account-endpoint-reference")
        set_tests_properties(account_endpoint_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*account endpoint source observations")
        add_test(NAME account_record_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_account_record.py" --native "$<TARGET_FILE:account_record_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/account-record-reference")
        set_tests_properties(account_record_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*account record SQLite provider observations")
        add_test(NAME user_account_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_user_account.py" --native "$<TARGET_FILE:user_account_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/user-account-reference")
        set_tests_properties(user_account_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*user account model observations")
        add_test(NAME account_urls_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_account_urls.py" --native "$<TARGET_FILE:account_urls_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/account-urls-reference")
        set_tests_properties(account_urls_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*account URL persistence/encoding observations")
        add_test(NAME login_agent_factory_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_login_agent_factory.py" --native "$<TARGET_FILE:login_agent_factory_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/login-factory-reference")
        set_tests_properties(login_agent_factory_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*login agent factory calls")
        add_test(NAME agent_circuit_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_agent_circuit_osd.py" --native "$<TARGET_FILE:agent_circuit_osd_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/agent-circuit-reference")
        set_tests_properties(agent_circuit_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*agent circuit state/pack observations")
        add_test(NAME avatar_appearance_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_avatar_appearance.py" --native "$<TARGET_FILE:avatar_appearance_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/avatar-appearance-reference")
        set_tests_properties(avatar_appearance_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*avatar appearance state/pack observations")
        add_test(NAME texture_entry_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_texture_entry.py" --native "$<TARGET_FILE:texture_entry_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/texture-entry-reference")
        set_tests_properties(texture_entry_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*texture entry observations")
        add_test(NAME texture_face_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_texture_face.py" --native "$<TARGET_FILE:texture_face_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/texture-face-reference")
        set_tests_properties(texture_face_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*texture face observations")
        add_test(NAME wearable_cache_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_wearable_cache.py" --native "$<TARGET_FILE:wearable_cache_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/wearable-cache-reference")
        set_tests_properties(wearable_cache_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*wearable cache/asset callback observations")
        add_test(NAME asset_model_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_asset_model.py" --native "$<TARGET_FILE:asset_model_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/asset-model-reference")
        set_tests_properties(asset_model_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*asset metadata/type conversion observations")
        add_test(NAME appearance_models_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_appearance_models.py" --native "$<TARGET_FILE:appearance_models_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/appearance-models-reference")
        set_tests_properties(appearance_models_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*attachment/transfer-context state and pack observations")
        add_test(NAME osd_numeric_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_osd_numeric.py" --native "$<TARGET_FILE:osd_numeric_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/osd-numeric-reference")
        set_tests_properties(osd_numeric_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*SDK numeric conversion observations")
        add_test(NAME avatar_wearable_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_avatar_wearable.py" --native "$<TARGET_FILE:avatar_wearable_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/avatar-wearable-reference")
        set_tests_properties(avatar_wearable_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*source-backed avatar wearable state/pack observations")
        add_test(NAME agent_launch_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_agent_launch.py" --native "$<TARGET_FILE:agent_launch_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/agent-launch-reference")
        set_tests_properties(agent_launch_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: 160 source-backed agent launch and fallback traces")
        add_test(NAME login_orchestration_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_login_orchestration.py" --native "$<TARGET_FILE:login_orchestration_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/login-orchestration-reference")
        set_tests_properties(login_orchestration_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: 512 source-backed login preparation and cleanup traces")
        add_test(NAME login_destination_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_login_destination.py" --native "$<TARGET_FILE:login_destination_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/login-destination-reference")
        set_tests_properties(login_destination_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*login destination/position/flag observations")
        add_test(NAME grid_identity_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_grid_identity.py" --native "$<TARGET_FILE:grid_identity_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/grid-identity-reference")
        set_tests_properties(grid_identity_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*grid identity configuration/state observations")
        add_test(NAME http_uri_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_http_uri.py" --native "$<TARGET_FILE:http_uri_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/http-uri-reference")
        set_tests_properties(http_uri_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*HTTP URI normalization/state observations")
        add_test(NAME grid_endpoint_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_grid_endpoint.py" --native "$<TARGET_FILE:grid_endpoint_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/grid-endpoint-reference")
        set_tests_properties(grid_endpoint_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*grid endpoint dispatch/auth/XML observations")
        add_test(NAME region_uri_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_region_uri.py" --native "$<TARGET_FILE:region_uri_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/region-uri-reference")
        set_tests_properties(region_uri_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: .*RegionURI parser/state observations")
        add_test(NAME grid_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_grid.py" --native "$<TARGET_FILE:grid_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/grid-reference")
        set_tests_properties(grid_source_parity PROPERTIES PASS_REGULAR_EXPRESSION "PASS: 32 registry/provider scenarios")
        add_test(NAME grid_user_endpoint_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_grid_user_endpoint.py" --native "$<TARGET_FILE:grid_user_endpoint_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/grid-user-endpoint-reference")
        add_test(NAME presence_endpoint_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_presence_endpoint.py" --native "$<TARGET_FILE:presence_endpoint_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/presence-endpoint-reference")
        add_test(NAME grid_user_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_grid_user.py" --native "$<TARGET_FILE:grid_user_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/grid-user-reference")
        add_test(NAME presence_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_presence.py" --native "$<TARGET_FILE:presence_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/presence-reference")
        add_test(NAME library_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_library.py" --native "$<TARGET_FILE:library_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/library-reference")
        add_test(NAME inventory_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_inventory.py" --native "$<TARGET_FILE:inventory_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/inventory-reference")
        add_test(NAME source_parity
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare.py"
                --native "$<TARGET_FILE:parity_probe>"
                --dotnet "${DOTNET_EXECUTABLE}"
                --work "${CMAKE_CURRENT_BINARY_DIR}/reference")
        add_test(NAME login_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_login.py" --native "$<TARGET_FILE:login_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/login-reference")
        add_test(NAME lludp_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_lludp.py" --native "$<TARGET_FILE:lludp_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/udp-reference")
    endif()
endif()

# A Python traceback can quote its own print("PASS: ...") source line. Reject
# tracebacks explicitly so a success regex cannot turn an assertion into a pass.
if(BUILD_TESTING)
    if(Python3_EXECUTABLE AND DOTNET_EXECUTABLE)
        add_test(NAME authentication_initializer_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_authentication_initializer.py" --native "$<TARGET_FILE:authentication_initializer_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/authentication-initializer-reference")
        add_test(NAME authentication_endpoint_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_authentication_endpoint.py" --native "$<TARGET_FILE:authentication_endpoint_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/authentication-endpoint-reference")
        add_test(NAME authentication_connector_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_authentication_connector.py" --native "$<TARGET_FILE:authentication_connector_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/authentication-connector-reference")
        add_test(NAME rest_forms_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_rest_forms.py" --native "$<TARGET_FILE:rest_forms_probe>"
            --default-native "$<TARGET_FILE:rest_forms_default_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/rest-forms-reference")
        set_tests_properties(rest_forms_source_parity PROPERTIES TIMEOUT 120)
        add_test(NAME rest_redirects_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_rest_redirects.py" --native "$<TARGET_FILE:rest_forms_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/rest-redirects-reference")
        set_tests_properties(rest_redirects_source_parity PROPERTIES TIMEOUT 120)
        add_test(NAME user_account_initializer_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_user_account_initializer.py" --native "$<TARGET_FILE:user_account_initializer_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/user-account-initializer-reference")
        add_test(NAME account_record_source_provider_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_account_record.py" --native "$<TARGET_FILE:account_record_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --source-provider --work "${CMAKE_CURRENT_BINARY_DIR}/account-record-provider-reference")
        add_test(NAME grid_user_initializer_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_grid_user_initializer.py" --native "$<TARGET_FILE:grid_user_initializer_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/grid-user-initializer-reference")
        add_test(NAME avatar_initializer_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_avatar_initializer.py" --native "$<TARGET_FILE:avatar_initializer_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/avatar-initializer-reference")
        add_test(NAME sqlite_generic_table_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_sqlite_generic_table.py" --native "$<TARGET_FILE:sqlite_generic_table_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/sqlite-generic-table-reference")
        add_test(NAME friends_initializer_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_friends_initializer.py" --native "$<TARGET_FILE:friends_initializer_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/friends-initializer-reference")
        add_test(NAME sqlite_connection_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_sqlite_connection.py" --native "$<TARGET_FILE:sqlite_connection_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/sqlite-connection-reference")
        add_test(NAME migration_parser_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_migration_parser.py" --native "$<TARGET_FILE:migration_parser_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/migration-parser-reference")
        add_test(NAME migration_engine_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_migration_engine.py" --native "$<TARGET_FILE:migration_engine_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/migration-engine-reference")
        add_test(NAME asset_initializer_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_asset_initializer.py" --native "$<TARGET_FILE:asset_initializer_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/asset-initializer-reference")
        add_test(NAME asset_factory_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_asset_factory.py" --native "$<TARGET_FILE:asset_factory_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/asset-factory-reference")
        add_test(NAME filesystem_asset_loader_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_filesystem_asset_loader.py" --native "$<TARGET_FILE:filesystem_asset_loader_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/filesystem-asset-loader-reference")
        add_test(NAME asset_storage_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_asset_storage.py" --native "$<TARGET_FILE:asset_storage_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/asset-storage-reference")
        add_test(NAME asset_service_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_asset_service.py" --native "$<TARGET_FILE:asset_service_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/asset-service-reference")
        add_test(NAME local_asset_connector_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_local_asset_connector.py" --native "$<TARGET_FILE:local_asset_connector_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/local-asset-connector-reference")
        add_test(NAME get_assets_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_get_assets.py" --native "$<TARGET_FILE:get_assets_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/get-assets-reference")
        add_test(NAME get_assets_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/get_assets_network.py" --server "$<TARGET_FILE:get_assets_server>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/get-assets-network")
        add_test(NAME asset_query_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_asset_query.py" --native "$<TARGET_FILE:asset_query_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/asset-query-reference")
        add_test(NAME http_range_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_http_range.py" --native "$<TARGET_FILE:http_range_probe>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/http-range-reference")
    endif()
    if(Python3_EXECUTABLE AND OPENSSL_FOUND)
        add_test(NAME server_http_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/server_network.py" --native "$<TARGET_FILE:opensim-native>"
            --http-only --work "${CMAKE_CURRENT_BINARY_DIR}/network-http-tests")
        set_tests_properties(server_http_network PROPERTIES TIMEOUT 90)
    endif()
    if(Python3_EXECUTABLE)
        add_test(NAME remote_authentication_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/remote_authentication_network.py" --native "$<TARGET_FILE:opensim-native>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/remote-authentication-network")
        set_tests_properties(remote_authentication_network PROPERTIES TIMEOUT 180)
        add_test(NAME authentication_connector_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/authentication_connector_network.py" --native "$<TARGET_FILE:authentication_connector_probe>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/authentication-connector-network")
        set_tests_properties(authentication_connector_network PROPERTIES TIMEOUT 120)
        add_test(NAME authentication_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/authentication_network.py" --native "$<TARGET_FILE:opensim-native>"
            --work "${CMAKE_CURRENT_BINARY_DIR}/authentication-network")
        set_tests_properties(authentication_network PROPERTIES TIMEOUT 90)
        add_test(NAME connected_region_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/connected_region_network.py" --native "$<TARGET_FILE:opensim-native>"
            --viewer "$<TARGET_FILE:viewer_session_probe>" --work "${CMAKE_CURRENT_BINARY_DIR}/connected-region-network")
        set_tests_properties(connected_region_network PROPERTIES TIMEOUT 90)
        add_test(NAME connected_region_touch_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/connected_region_network.py" --native "$<TARGET_FILE:opensim-native>"
            --viewer "$<TARGET_FILE:viewer_session_probe>" --touch --work "${CMAKE_CURRENT_BINARY_DIR}/connected-region-touch-network")
        set_tests_properties(connected_region_touch_network PROPERTIES TIMEOUT 90)
        add_test(NAME connected_region_physics_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/connected_region_network.py" --native "$<TARGET_FILE:opensim-native>"
            --viewer "$<TARGET_FILE:viewer_session_probe>" --touch --physics-library "${OPENSIM_UBODE_LIBRARY}"
            --work "${CMAKE_CURRENT_BINARY_DIR}/connected-region-physics-network")
        set_tests_properties(connected_region_physics_network PROPERTIES TIMEOUT 120)
        add_test(NAME connected_region_assets_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/connected_region_network.py" --native "$<TARGET_FILE:opensim-native>"
            --viewer "$<TARGET_FILE:viewer_session_probe>" --assets --touch --physics-library "${OPENSIM_UBODE_LIBRARY}"
            --work "${CMAKE_CURRENT_BINARY_DIR}/connected-region-assets-network")
        set_tests_properties(connected_region_assets_network PROPERTIES TIMEOUT 120)
        add_test(NAME connected_region_inventory_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/connected_region_network.py" --native "$<TARGET_FILE:opensim-native>"
            --viewer "$<TARGET_FILE:viewer_session_probe>" --inventory --assets --touch --physics-library "${OPENSIM_UBODE_LIBRARY}"
            --work "${CMAKE_CURRENT_BINARY_DIR}/connected-region-inventory-network")
        set_tests_properties(connected_region_inventory_network PROPERTIES TIMEOUT 120)
        add_test(NAME connected_region_folders_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/connected_region_network.py" --native "$<TARGET_FILE:opensim-native>"
            --viewer "$<TARGET_FILE:viewer_session_probe>" --folders --inventory --assets --touch --physics-library "${OPENSIM_UBODE_LIBRARY}"
            --work "${CMAKE_CURRENT_BINARY_DIR}/connected-region-folders-network")
        set_tests_properties(connected_region_folders_network PROPERTIES TIMEOUT 120)
        add_test(NAME connected_region_library_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/connected_region_network.py" --native "$<TARGET_FILE:opensim-native>"
            --viewer "$<TARGET_FILE:viewer_session_probe>" --library --folders --inventory --assets --touch --physics-library "${OPENSIM_UBODE_LIBRARY}"
            --work "${CMAKE_CURRENT_BINARY_DIR}/connected-region-library-network")
        set_tests_properties(connected_region_library_network PROPERTIES TIMEOUT 120)
        add_test(NAME connected_region_legacy_inventory_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/connected_region_network.py" --native "$<TARGET_FILE:opensim-native>"
            --viewer "$<TARGET_FILE:viewer_session_probe>" --legacy --library --folders --inventory --assets --touch --physics-library "${OPENSIM_UBODE_LIBRARY}"
            --work "${CMAKE_CURRENT_BINARY_DIR}/connected-region-legacy-inventory-network")
        set_tests_properties(connected_region_legacy_inventory_network PROPERTIES TIMEOUT 120)
        add_test(NAME connected_region_baked_texture_network COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/connected_region_network.py" --native "$<TARGET_FILE:opensim-native>"
            --viewer "$<TARGET_FILE:viewer_session_probe>" --bakes --legacy --library --folders --inventory --assets --touch --physics-library "${OPENSIM_UBODE_LIBRARY}"
            --work "${CMAKE_CURRENT_BINARY_DIR}/connected-region-baked-texture-network")
        set_tests_properties(connected_region_baked_texture_network PROPERTIES TIMEOUT 120)

    endif()
    if(Python3_EXECUTABLE AND DOTNET_EXECUTABLE)
        add_test(NAME flotsam_config_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_flotsam_config.py" --native "$<TARGET_FILE:flotsam_memory_cache_tests>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/flotsam-config-reference")
        set_tests_properties(flotsam_config_source_parity PROPERTIES TIMEOUT 120)
        foreach(scene_component IN ITEMS flotsam_cache_cleanup flotsam_cache_module flotsam_cache flotsam_file_access flotsam_file_store asset_nrbf flotsam_memory_cache baked_texture_caps inventory_asset_caps scene_inventory_asset_update inventory_asset_update animation_set inventory_new part_script_events inventory_copy user_inventory_operations inventory_drop_permission task_inventory_update task_inventory_move task_inventory_remove agent_inventory_add inventory_notifications inventory_item_create script_instance_create field_string task_inventory_item task_inventory_registry inventory_script_rez script_control script_asset_text object_properties script_task_caps script_task_update script_running task_inventory_file xfer_download scene_world_terrain terrain_layer avatar_update avatar_packets object_update rez_location pos_physics chat_packet movement movement_animation avatar_animation root_position inventory_caps inventory_descendents library_inventory_caps library_descendents inventory_packets legacy_inventory_requests)
            if(scene_component STREQUAL "scene_world_terrain")
                set(scene_probe scene_world_tests)
            else()
                set(scene_probe ${scene_component}_tests)
            endif()
            add_test(NAME ${scene_component}_source_parity COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_${scene_component}.py" --native "$<TARGET_FILE:${scene_probe}>"
                --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/${scene_component}-reference")
            set_tests_properties(${scene_component}_source_parity PROPERTIES TIMEOUT 120)
        endforeach()
        add_test(NAME ubode_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_ubode.py" --native "$<TARGET_FILE:ubode_tests>"
            --dotnet "${DOTNET_EXECUTABLE}" --library "${OPENSIM_UBODE_LIBRARY}" --work "${CMAKE_CURRENT_BINARY_DIR}/ubode-reference")
        set_tests_properties(ubode_source_parity PROPERTIES TIMEOUT 300)
        add_test(NAME lsl_runtime_api_source_parity COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_lsl_runtime_api.py" --native "$<TARGET_FILE:lsl_runtime_tests>"
            --dotnet "${DOTNET_EXECUTABLE}" --work "${CMAKE_CURRENT_BINARY_DIR}/lsl-runtime-reference")
        set_tests_properties(lsl_runtime_api_source_parity PROPERTIES TIMEOUT 120)
    endif()
    get_property(native_registered_tests DIRECTORY PROPERTY TESTS)
    if(native_registered_tests)
        set_tests_properties(${native_registered_tests} PROPERTIES
            FAIL_REGULAR_EXPRESSION "Traceback [(]most recent call last[)];AssertionError;ERROR: AddressSanitizer")
    endif()
endif()
