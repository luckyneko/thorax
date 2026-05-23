include(thx_warnings)
include(thx_plugin_manifest)

enable_testing()

# All example targets include the examples/ root so that
# #include "interfaces/logging_service.h" resolves correctly.

add_library(example_plugin_logging SHARED
    plugin_logging/logging_plugin.cpp
)
target_link_libraries(example_plugin_logging PRIVATE ${PROJECT_NAME})
target_include_directories(example_plugin_logging PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
thx_set_warnings(example_plugin_logging)
target_compile_options(example_plugin_logging PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
thx_plugin_manifest(example_plugin_logging
    NAME     "examples.ILoggingService"
    VERSION  "1.0.0"
    PROVIDES "examples.ILoggingService"
)

add_library(example_plugin_file SHARED
    plugin_file/file_plugin.cpp
)
target_link_libraries(example_plugin_file PRIVATE ${PROJECT_NAME})
target_include_directories(example_plugin_file PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
thx_set_warnings(example_plugin_file)
target_compile_options(example_plugin_file PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
thx_plugin_manifest(example_plugin_file
    NAME     "examples.IFileService"
    VERSION  "1.0.0"
    PROVIDES "examples.IFileService"
)

add_executable(example_host
    host/main.cpp
)
target_link_libraries(example_host PRIVATE ${PROJECT_NAME})
target_include_directories(example_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
thx_set_warnings(example_host)
add_dependencies(example_host example_plugin_logging example_plugin_file)

# Run the host with the directory that contains the two built plugins.
# $<TARGET_FILE_DIR:...> resolves correctly for both single- and multi-config
# generators (e.g. Ninja vs MSVC).
add_test(
    NAME examples.host
    COMMAND example_host "$<TARGET_FILE_DIR:example_plugin_logging>"
)
