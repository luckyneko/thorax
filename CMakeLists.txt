cmake_minimum_required(VERSION 3.15...3.28)

# disable testsuite if bundled
if(NOT DEFINED PROJECT_NAME)
  set(NOT_SUBPROJECT ON)
endif()

# Disable building in SRC
if (CMAKE_BINARY_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR "Building in-source is not supported! Create a build dir and remove ${CMAKE_SOURCE_DIR}/CMakeCache.txt")
endif()

# Default build release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose Release or Debug" FORCE)
endif()

project(thorax LANGUAGES CXX VERSION 0.0.1)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

# Options
option(THORAX_BUILD_TESTING "Build UnitTesting project" ${NOT_SUBPROJECT})

# Load Catch2
if(THORAX_BUILD_TESTING)
    include(addcatch2)
endif()

# Generate version header from project VERSION field
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/thx/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/thx/version.h"
    @ONLY
)

# Static Library Target
find_package(Threads REQUIRED)
add_library(${PROJECT_NAME} STATIC
    # Headers
    include/thx/detail/hash.h
    include/thx/detail/semver.h
    include/thx/thorax.h
    include/thx/service_id.h
    include/thx/version_type.h
    include/thx/version.h.in

    # Source
    src/thorax.cpp
)
target_include_directories(${PROJECT_NAME} PUBLIC
    include
    "${CMAKE_CURRENT_BINARY_DIR}/include"
)
#target_link_libraries(${PROJECT_NAME} PUBLIC)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)
if(MSVC)
    string(REGEX REPLACE "/W[0-4]" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    target_compile_options(${PROJECT_NAME} PRIVATE /WX /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Werror -Wall -Wextra)
endif()

# Testing Target
if(THORAX_BUILD_TESTING)
    enable_testing()
    add_executable(test-${PROJECT_NAME}
        test/thorax.cpp
        test/test_service_id.cpp
        test/test_version.cpp
    )
    target_link_libraries(test-${PROJECT_NAME} ${PROJECT_NAME} Catch2::Catch2WithMain)
    catch_discover_tests(test-${PROJECT_NAME} TEST_PREFIX "(${PROJECT_NAME}) ")
endif()
