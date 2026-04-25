add_library(plugin_io SHARED
    src/io_plugin.cpp
)
target_link_libraries(plugin_io PRIVATE ${PROJECT_NAME})
target_include_directories(plugin_io PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(MSVC)
    string(REGEX REPLACE "/W[0-4]" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    target_compile_options(plugin_io PRIVATE /WX /W4)
else()
    target_compile_options(plugin_io PRIVATE -Werror -Wall -Wextra)
endif()
