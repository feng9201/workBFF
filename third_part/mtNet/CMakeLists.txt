CMAKE_MINIMUM_REQUIRED(VERSION 3.20)
project(mtNet LANGUAGES C CXX)

# mtPool 的 BS::thread_pool 要求 C++17
set(CMAKE_CXX_STANDARD 17)

find_package(OpenSSL REQUIRED)

if(MSVC)
    add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
    add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)

add_library(${PROJECT_NAME} STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/mongoose.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mtHttpServer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mtWsClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mtWsServer.cpp
)

# === 对外公共 include (PUBLIC) ===
# 使用者只需要 include/ 下的两个头文件
target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# === 内部 include (PRIVATE) ===
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${OPENSSL_INCLUDE_DIR}
)

# 链接库目录
target_link_directories(${PROJECT_NAME} PRIVATE
    ${OPENSSL_LIBRARIES}
)

# OpenSSL 3.0 + Windows 系统库
target_link_libraries(${PROJECT_NAME}
    OpenSSL::SSL
    OpenSSL::Crypto
    mtPool
)



