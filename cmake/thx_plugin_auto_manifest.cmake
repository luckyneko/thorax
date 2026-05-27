# cmake/thx_plugin_auto_manifest.cmake
# Provides thx_plugin_auto_manifest(<target> [DESTINATION <dir>]).
#
# Wires a POST_BUILD command on <target> that invokes `thx_emit_manifest` to
# derive the `<basename>.thx.json` sidecar from the plugin's own IPlugin
# (name / version / required / provides). The plugin's IPlugin class is the
# single source of truth; the sidecar is just a build-time cache.
#
# Replaces hand-written `thx_plugin_manifest(...)` calls for plugins that
# follow the IPlugin contract correctly. The hand-written helper stays
# available for cases where the IPlugin can't be introspected at build time
# (e.g., plugins built in a separate pass that can't link against the
# `thx_emit_manifest` tool).
#
# Usage:
#   add_library(my_plugin SHARED src/plugin.cpp)
#   target_link_libraries(my_plugin PRIVATE Thorax::thorax)
#   thx_plugin_auto_manifest(my_plugin
#       [DESTINATION <relative-dir>]   # optional: also install the sidecar
#   )
#
# The output path matches the discover() pairing rule:
# `<TARGET_FILE_DIR>/<TARGET_FILE_PREFIX><TARGET_FILE_BASE_NAME>.thx.json`.
#
# If DESTINATION is given, the sidecar is added to install(FILES) at that
# path, so consumers running discover() against the install prefix pair
# sidecar to DSO correctly. Plugins not meant for installation (mocks,
# examples) just omit DESTINATION.

function(thx_plugin_auto_manifest target)
    cmake_parse_arguments(ARG
        ""              # options
        "DESTINATION"   # one-value
        ""              # multi-value
        ${ARGN}
    )

    if(NOT TARGET thx_emit_manifest)
        message(FATAL_ERROR
            "thx_plugin_auto_manifest(${target}): the thx_emit_manifest tool "
            "target is not defined. Include Thorax via add_subdirectory or "
            "find_package before calling this helper.")
    endif()

    set(_manifest_path
        "$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_PREFIX:${target}>$<TARGET_FILE_BASE_NAME:${target}>.thx.json"
    )

    # No BYPRODUCTS: CMake forbids target-dependent generator expressions
    # (TARGET_FILE_DIR / TARGET_FILE_PREFIX / TARGET_FILE_BASE_NAME) in the
    # BYPRODUCTS argument — see cmake-generator-expressions. We instead use
    # the ADDITIONAL_CLEAN_FILES target property (which does accept them)
    # so `cmake --build --target clean` still removes the sidecar.
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND $<TARGET_FILE:thx_emit_manifest>
                "$<TARGET_FILE:${target}>"
                "${_manifest_path}"
        COMMENT "Emitting manifest for ${target}"
        VERBATIM
    )

    set_property(TARGET ${target} APPEND PROPERTY
        ADDITIONAL_CLEAN_FILES "${_manifest_path}")

    # POST_BUILD runs after the target's link step, but the tool itself must
    # already exist by then. add_dependencies ensures the tool is built
    # before any consumer that runs the POST_BUILD.
    add_dependencies(${target} thx_emit_manifest)

    if(ARG_DESTINATION)
        install(FILES "${_manifest_path}" DESTINATION "${ARG_DESTINATION}")
    endif()
endfunction()
