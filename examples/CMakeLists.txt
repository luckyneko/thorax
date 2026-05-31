include(thx_warnings)
include(thx_plugin_auto_manifest)

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
thx_plugin_auto_manifest(example_plugin_logging)

add_library(example_plugin_file SHARED
    plugin_file/file_plugin.cpp
)
target_link_libraries(example_plugin_file PRIVATE ${PROJECT_NAME})
target_include_directories(example_plugin_file PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
thx_set_warnings(example_plugin_file)
target_compile_options(example_plugin_file PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
thx_plugin_auto_manifest(example_plugin_file)

# A plugin with a real dependency: the greeter service emits through
# ILoggingService and declares that requirement in IPlugin::required().
add_library(example_plugin_greeter SHARED
    plugin_greeter/greeter_plugin.cpp
)
target_link_libraries(example_plugin_greeter PRIVATE ${PROJECT_NAME})
target_include_directories(example_plugin_greeter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
thx_set_warnings(example_plugin_greeter)
target_compile_options(example_plugin_greeter PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
thx_plugin_auto_manifest(example_plugin_greeter)

# All example plugins build into this directory; hosts are pointed at it.
set(example_plugin_dir "$<TARGET_FILE_DIR:example_plugin_logging>")

# example_host — load everything in a directory (discoverAndLoad baseline).
add_executable(example_host
    host/main.cpp
)
target_link_libraries(example_host PRIVATE ${PROJECT_NAME})
target_include_directories(example_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
thx_set_warnings(example_host)
add_dependencies(example_host example_plugin_logging example_plugin_file)

# Run the host with the directory that contains the built plugins.
# $<TARGET_FILE_DIR:...> resolves correctly for both single- and multi-config
# generators (e.g. Ninja vs MSVC).
add_test(
    NAME examples.host
    COMMAND example_host "${example_plugin_dir}"
)

# One focused host per loading pathway. Each is a standalone, copy-pasteable
# recipe; all are pointed at the same plugin directory.
foreach(pathway host_by_name host_by_provides host_with_deps)
    add_executable(example_${pathway} ${pathway}/main.cpp)
    target_link_libraries(example_${pathway} PRIVATE ${PROJECT_NAME})
    target_include_directories(example_${pathway} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    thx_set_warnings(example_${pathway})
    add_dependencies(example_${pathway}
        example_plugin_logging example_plugin_file example_plugin_greeter)
    add_test(
        NAME examples.${pathway}
        COMMAND example_${pathway} "${example_plugin_dir}"
    )
endforeach()
