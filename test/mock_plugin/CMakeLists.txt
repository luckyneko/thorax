# INTERFACE library exposing mock_plugin.h to the test binary and to sibling
# mocks that share the same MockService / ServiceA / ServiceB definitions.
# Header-only so consumers don't pick up a runtime link to mock_plugin.
add_library(mock_plugin_headers INTERFACE)
target_include_directories(mock_plugin_headers INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(mock_plugin_headers INTERFACE ${PROJECT_NAME})

add_library(mock_plugin SHARED mock_plugin.cpp)
target_link_libraries(mock_plugin PRIVATE ${PROJECT_NAME} mock_plugin_headers)
thx_set_warnings(mock_plugin)
target_compile_options(mock_plugin PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
thx_plugin_auto_manifest(mock_plugin)
