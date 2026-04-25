add_library(plugin_logging SHARED
    src/logging_plugin.cpp
)
target_link_libraries(plugin_logging PRIVATE ${PROJECT_NAME})
target_include_directories(plugin_logging
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include  # for internal src/ includes
)

if(MSVC)
    string(REGEX REPLACE "/W[0-4]" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    target_compile_options(plugin_logging PRIVATE /WX /W4)
else()
    target_compile_options(plugin_logging PRIVATE -Werror -Wall -Wextra)
endif()
