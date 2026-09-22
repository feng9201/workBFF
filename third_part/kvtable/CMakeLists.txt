cmake_minimum_required(VERSION 3.15)

project(kvtable VERSION 1.0.0 LANGUAGES CXX)

option(KVTB_BUILD_TESTS "Build kvtable_test" ON)
option(KVTB_BUILD_DEMO "Build kvtable_demo" ON)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

add_library(kvtable STATIC
    src/kvtable.cpp
)
target_include_directories(kvtable
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

if(WIN32)
    target_compile_definitions(kvtable PRIVATE KVTB_WINDOWS)
else()
    target_compile_definitions(kvtable PRIVATE KVTB_POSIX)
endif()

if(KVTB_BUILD_TESTS)
    add_executable(kvtable_test test/main.cpp)
    target_link_libraries(kvtable_test PRIVATE kvtable)
endif()

if(KVTB_BUILD_DEMO)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/demo_out")
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/demo_out/app.kvt" KVTB_DEMO_PATH)
    add_executable(kvtable_demo demo/main.cpp)
    target_link_libraries(kvtable_demo PRIVATE kvtable)
    target_compile_definitions(kvtable_demo PRIVATE
        KVTB_DEMO_FILE="${KVTB_DEMO_PATH}"
    )
endif()
