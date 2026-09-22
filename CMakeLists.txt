cmake_minimum_required(VERSION 3.17)

# 包含版本管理模块
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(Version)

# 版本文件路径
set(VERSION_FILE "${CMAKE_SOURCE_DIR}/version.txt")

# 仅打包时递增版本号（build.bat -p / install.bat 传 -DBUILD_PACKAGE=1）；
# 普通构建只读取当前版本，不递增
if(BUILD_PACKAGE)
  increment_version(${VERSION_FILE})
  set(PROJECT_VERSION ${UPDATED_VERSION})
else()
  read_version(${VERSION_FILE})
  set(PROJECT_VERSION ${CURRENT_VERSION})
endif()

project(
	workBFF
	VERSION ${PROJECT_VERSION} 
	DESCRIPTION "工作搭档"
	LANGUAGES CXX
)

#设置c++ 版本
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
SET(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

# 全局宏定义
add_compile_definitions(USE_NLOHMANN_JSON)


if(MSVC)
  if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
  endif()
  
  # 仅当使用 MSVC 编译器时设置
  add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
  add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
  add_compile_options("$<$<CONFIG:Debug>:/ZI>")
  add_link_options("$<$<CONFIG:Debug>:/INCREMENTAL>")
  
  #set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,EditAndContinue,ProgramDatabase>")
endif()

add_subdirectory(third_part/mtNet)
add_subdirectory(third_part/mtPool)
add_subdirectory(third_part/qwindowkit)
# 禁用 cppzmq 自带的 tests/examples 构建（避免 find_package(cppzmq) 失败）
set(CPPZMQ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(workbff)
