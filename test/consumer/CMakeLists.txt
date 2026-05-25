cmake_minimum_required(VERSION 3.20)
project(ThoraxConsumer CXX)

find_package(Thorax REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE Thorax::thorax)

# Verify plugin headers are correctly installed when they were built.
find_path(THX_LOGGING_INCLUDE thx/plugins/logging/logging_service.h)
find_path(THX_IO_INCLUDE      thx/plugins/io/io_service.h)

if(THX_LOGGING_INCLUDE AND THX_IO_INCLUDE)
    add_executable(consumer_plugins consumer_plugins.cpp)
    target_link_libraries(consumer_plugins PRIVATE Thorax::thorax)
endif()
