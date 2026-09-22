
set(CMAKE_CXX_STANDARD 17)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(mttool CONFIG REQUIRED)
find_package(log-cpp REQUIRED)
find_package(cpr CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

if(MSVC)
add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
SET( CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}  /uiAccess='false' /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup" )
#SET( CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}  /uiAccess='false' /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup" )
endif()
#
#version 设置
include(${CMAKE_SOURCE_DIR}/cmake/version_setting.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/workbff_rc_setting.cmake)

if(QT5_PATH)
message(${QT5_PATH})
else()
set(QT5_PATH "D:/Qt/5.15.2/msvc2019_64/lib/cmake")
endif()
set(CMAKE_PREFIX_PATH ${QT5_PATH})
find_package(Qt5 COMPONENTS
        Core
        Gui
        Widgets
        Sql
        REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

#获取所有文件
file(GLOB_RECURSE HEADERSRC_DIR_LIST "*.h" )
file(GLOB_RECURSE CPPSRC_DIR_LIST "*.cpp" )
file(GLOB_RECURSE UISRC_DIR_LIST "*.ui" )
file(GLOB_RECURSE QRCSRC_DIR_LIST "*.qrc" )
file(GLOB_RECURSE CSRC_DIR_LIST "*.c" )

set(AllFile ${HEADERSRC_DIR_LIST} ${CPPSRC_DIR_LIST})
foreach(fileItem ${AllFile})
    # Get the directory of the source file
    get_filename_component(PARENT_DIR "${fileItem}" DIRECTORY)
    # Remove common directory prefix to make the group
    string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}" "" GROUP "${PARENT_DIR}")
    # Make sure we are using windows slashes
    string(REPLACE "/" "\\" GROUP "${GROUP}")
    # Group into "Source Files" and "Header Files"
    set(GROUP "${GROUP}")
    source_group("${GROUP}" FILES "${fileItem}")
endforeach()

#设置自动生成UI文件夹
source_group("Ui" FILES ${UISRC_DIR_LIST} ${WRAP_FILES})
# 设置资源目录
set(RESOURCES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/res)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Windows 资源文件（版本信息 + 图标），由 omni_station_rc_setting.cmake 生成
set(APP_RC_FILE "")
if(WIN32 AND WINDOWS_RC_FILE)
    set(APP_RC_FILE ${WINDOWS_RC_FILE})
endif()

#生成exe
add_executable(${PROJECT_NAME}
              ${CPPSRC_DIR_LIST}
              ${CSRC_DIR_LIST}
              ${QRCSRC_DIR_LIST}
              ${UISRC_DIR_LIST}
              ${HEADERSRC_DIR_LIST}
              ${APP_RC_FILE})

target_include_directories(${PROJECT_NAME}  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${VERSION_PATH})

target_link_libraries(${PROJECT_NAME}  PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Sql
    cpr::cpr
    OpenSSL::SSL
    OpenSSL::Crypto
    QWindowKit::Widgets
    nlohmann_json::nlohmann_json
    dwmapi
    pdh
    mttool::mttool
    log-cpp::log-cpp
    mtPool
    mtNet
)
