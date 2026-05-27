include(thx_warnings)

# thx_emit_manifest is a regular external consumer of libthorax's public API.
# No private include dir, no THX_TESTING — it uses thx::plugin::inspect() +
# thx::plugin::serialiseManifest() through the installed-shape headers.
add_executable(thx_emit_manifest emit_manifest.cpp)
target_link_libraries(thx_emit_manifest PRIVATE ${PROJECT_NAME})
thx_set_warnings(thx_emit_manifest)
