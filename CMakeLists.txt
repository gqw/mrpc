cmake_minimum_required(VERSION 3.6)

project(mrpc)

add_definitions(-DASIO_STANDALONE -DRPC_USE_LOG -D_LOG_CONSOLE)

if(CMAKE_HOST_WIN32)
    add_definitions(-D_WIN32_WINNT=0x0701 -DNTDDI_VERSION=NTDDI_WIN7 )
    add_definitions(/std:c++17 /utf-8)
else()
    add_definitions(-g -O0 -Wall -std=c++2a)
endif()

set(EXECUTABLE_OUTPUT_PATH      ${CMAKE_HOME_DIRECTORY}/bin/)
set(RPC_INCLUDE_DIR             ${CMAKE_HOME_DIRECTORY}/include/)
set(ASIO_INCLUDE_DIR            ${CMAKE_HOME_DIRECTORY}/third/asio-1.18.1/include/)
set(SPDLOG_INCLUDE_DIR          ${CMAKE_HOME_DIRECTORY}/third/spdlog/include/)
set(WLOG_INCLUDE_DIR            ${CMAKE_HOME_DIRECTORY}/third/wlog/include/)
set(NLOHMANN_INCLUDE_DIR        ${CMAKE_HOME_DIRECTORY}/third/nlohmann/include/)

message(VERBOSE "rpc        directory: " ${RPC_INCLUDE_DIR})
message(VERBOSE "asio       directory: " ${ASIO_INCLUDE_DIR})
message(VERBOSE "spdlog     directory: " ${SPDLOG_INCLUDE_DIR})
message(VERBOSE "wlog       directory: " ${WLOG_INCLUDE_DIR})
message(VERBOSE "nlohmann   directory: " ${NLOHMANN_INCLUDE_DIR})

include_directories(
    ${RPC_INCLUDE_DIR}
    ${ASIO_INCLUDE_DIR}
    ${SPDLOG_INCLUDE_DIR}
    ${WLOG_INCLUDE_DIR}
    ${NLOHMANN_INCLUDE_DIR}
)

# add_subdirectory(${CMAKE_HOME_DIRECTORY}/example/test_004)
# add_subdirectory(${CMAKE_HOME_DIRECTORY}/example/test_005)

add_subdirectory(${CMAKE_HOME_DIRECTORY}/example/test_100)
add_subdirectory(${CMAKE_HOME_DIRECTORY}/example/test_101)
add_subdirectory(${CMAKE_HOME_DIRECTORY}/example/test_102)
add_subdirectory(${CMAKE_HOME_DIRECTORY}/example/test_103)
