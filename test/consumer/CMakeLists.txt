cmake_minimum_required(VERSION 3.20)
project(ThoraxConsumer CXX)

find_package(Thorax REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE Thorax::thorax)

# The in-tree plugins (spdlog, http) ship no public headers — they implement the
# core thx::log::ILogService / thx::io::IProtocol interfaces, which are installed
# with the library. So this consumer needs only core headers and always builds.
add_executable(consumer_plugins consumer_plugins.cpp)
target_link_libraries(consumer_plugins PRIVATE Thorax::thorax)
