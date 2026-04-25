cmake_minimum_required(VERSION 3.15)
project(ThoraxConsumer CXX)

find_package(Thorax REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE Thorax::thorax)
