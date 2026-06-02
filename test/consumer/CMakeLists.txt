cmake_minimum_required(VERSION 3.20)
project(ThoraxConsumer CXX)

find_package(Thorax REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE Thorax::thorax)

# The in-tree plugins ship no public headers — they implement the core
# thx::log::ILogService interface (installed with the library) or the thx::io
# interfaces (installed by the io provider plugin to the same thx/io/ include
# path). So this consumer needs only installed headers and always builds.
add_executable(consumer_plugins consumer_plugins.cpp)
target_link_libraries(consumer_plugins PRIVATE Thorax::thorax)
