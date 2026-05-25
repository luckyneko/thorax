include(thx_warnings)

add_executable(thx_emit_manifest emit_manifest.cpp)
target_link_libraries(thx_emit_manifest PRIVATE ${PROJECT_NAME})
# The tool uses internal Library / PluginHandle to dlopen the DSO it inspects.
target_include_directories(thx_emit_manifest PRIVATE ${CMAKE_SOURCE_DIR}/src)
thx_set_warnings(thx_emit_manifest)
