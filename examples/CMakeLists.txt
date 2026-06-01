include(thx_warnings)
include(thx_plugin_auto_manifest)

enable_testing()

# All example targets include the examples/ root so that
# #include "interfaces/asset_service.h" resolves correctly.

# --- Example plugins --------------------------------------------------------
#
# A small "media asset loader" domain that exercises the framework end to end:
#   - plugin_media_core    registers IAssetService (the decode dispatcher) using
#                          the one-service shim THX_DEFINE_SERVICE_PLUGIN.
#   - plugin_image_decoder
#   - plugin_video_decoder each a custom IPlugin (THX_DEFINE_PLUGIN) that
#                          requires IAssetService and *contributes* a decoder to
#                          it — they register no service of their own.

function(thx_add_example_plugin name src)
    add_library(${name} SHARED ${src})
    target_link_libraries(${name} PRIVATE ${PROJECT_NAME})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    thx_set_warnings(${name})
    target_compile_options(${name} PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
    thx_plugin_auto_manifest(${name})
endfunction()

thx_add_example_plugin(example_plugin_media_core    plugin_media_core/media_core_plugin.cpp)
thx_add_example_plugin(example_plugin_image_decoder plugin_image_decoder/image_decoder_plugin.cpp)
thx_add_example_plugin(example_plugin_video_decoder plugin_video_decoder/video_decoder_plugin.cpp)

# All example plugins build into this directory; hosts are pointed at it.
set(example_plugin_dir "$<TARGET_FILE_DIR:example_plugin_media_core>")

set(example_media_plugins
    example_plugin_media_core
    example_plugin_image_decoder
    example_plugin_video_decoder)

# --- Hosts ------------------------------------------------------------------
# One focused host per loading pathway; each is a standalone, copy-pasteable
# recipe pointed at the example plugin directory.

# The media-domain hosts: tour (load-all), by-name, with-deps.
foreach(pathway host host_by_name host_with_deps)
    set(target example_${pathway})
    if(pathway STREQUAL "host")
        set(target example_host) # keep the historical target/test name
    endif()
    add_executable(${target} ${pathway}/main.cpp)
    target_link_libraries(${target} PRIVATE ${PROJECT_NAME})
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    thx_set_warnings(${target})
    add_dependencies(${target} ${example_media_plugins})
    add_test(NAME examples.${pathway} COMMAND ${target} "${example_plugin_dir}")
endforeach()

# host_logging demonstrates selecting a logging backend at runtime. It loads the
# in-tree spdlog plugin, so it is only built when the plugins are available.
if(THORAX_BUILD_PLUGINS AND TARGET plugin_spdlog)
    add_executable(example_host_logging host_logging/main.cpp)
    target_link_libraries(example_host_logging PRIVATE ${PROJECT_NAME})
    target_include_directories(example_host_logging PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    thx_set_warnings(example_host_logging)
    add_dependencies(example_host_logging plugin_spdlog)
    add_test(
        NAME examples.host_logging
        COMMAND example_host_logging "$<TARGET_FILE_DIR:plugin_spdlog>"
    )
endif()
