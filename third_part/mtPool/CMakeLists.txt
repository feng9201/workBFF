cmake_minimum_required(VERSION 3.15)
project(mtPool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
    add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
endif()

# 默认静态库；ON 时构建 DLL（进程内 EXE + 各 DLL 共享同一个单例线程池）
option(MTPOOL_BUILD_SHARED "Build mtPool as a shared library (cross-DLL singleton)" OFF)

if(MTPOOL_BUILD_SHARED)
    add_library(${PROJECT_NAME} SHARED
        ${CMAKE_CURRENT_SOURCE_DIR}/src/MtPool.cpp
    )
    target_compile_definitions(${PROJECT_NAME}
        PRIVATE MTPOOL_BUILDING   # 本库编译时导出符号
        PUBLIC  MTPOOL_SHARED     # 使用者头文件中走 dllimport
    )
else()
    add_library(${PROJECT_NAME} STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/MtPool.cpp
    )
endif()

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
