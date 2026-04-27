# cmake/thx_warnings.cmake
# Provides thx_set_warnings(<target>).
#
# Applies project-standard warnings-as-errors settings.
# On CMake >= 3.24 the COMPILE_WARNING_AS_ERROR target property is used;
# older releases fall back to explicit compiler flags.

function(thx_set_warnings target)
    if(MSVC)
        # Strip the /W3 CMake injects by default so /W4 doesn't produce D9025.
        string(REGEX REPLACE "/W[0-4]" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" PARENT_SCOPE)
    endif()

    if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24")
        set_target_properties(${target} PROPERTIES COMPILE_WARNING_AS_ERROR ON)
        target_compile_options(${target} PRIVATE
            $<$<CXX_COMPILER_ID:MSVC>:/W4>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra>
        )
    else()
        target_compile_options(${target} PRIVATE
            $<$<CXX_COMPILER_ID:MSVC>:/WX;/W4>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Werror;-Wall;-Wextra>
        )
    endif()
endfunction()
