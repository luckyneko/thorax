cmake_minimum_required(VERSION 3.20)
project(ThoraxConsumer CXX)

find_package(Thorax REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE Thorax::thorax)

# Verify plugin headers are correctly installed when they were built. The
# logging plugin ships no header (it implements the core thx::log::ILogService,
# installed with the library), so we gate on the io plugin's header only.
find_path(THX_IO_INCLUDE thx/plugins/io/io_service.h)

if(THX_IO_INCLUDE)
    add_executable(consumer_plugins consumer_plugins.cpp)
    target_link_libraries(consumer_plugins PRIVATE Thorax::thorax)
endif()
