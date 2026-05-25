include(thx_warnings)

add_executable(thx_emit_manifest emit_manifest.cpp)
target_link_libraries(thx_emit_manifest PRIVATE ${PROJECT_NAME})
thx_set_warnings(thx_emit_manifest)
