cmake_minimum_required(VERSION 3.6)
project(test_003)

set(RPC_INCLUDE_DIR             ${CMAKE_HOME_DIRECTORY}/include/)
set(ASIO_INCLUDE_DIR            ${CMAKE_HOME_DIRECTORY}/third/asio-1.18.1/include/)
set(SPDLOG_INCLUDE_DIR          ${CMAKE_HOME_DIRECTORY}/third/spdlog/include/)
set(NLOHMANN_INCLUDE_DIR        ${CMAKE_HOME_DIRECTORY}/third/nlohmann/include/)

message(VERBOSE "rpc        directory: " ${RPC_INCLUDE_DIR})
message(VERBOSE "asio       directory: " ${ASIO_INCLUDE_DIR})
message(VERBOSE "spdlog     directory: " ${SPDLOG_INCLUDE_DIR})
message(VERBOSE "cppcoro    directory: " ${NLOHMANN_INCLUDE_DIR})
message(VERBOSE "nlohmann   directory: " ${NLOHMANN_INCLUDE_DIR})

include_directories(
    ${PROJECT_SOURCE_DIR}/src/
    ${CMAKE_HOME_DIRECTORY}/third/wlog/include/

    ${RPC_INCLUDE_DIR}
    ${ASIO_INCLUDE_DIR}
    ${SPDLOG_INCLUDE_DIR}
    ${CPPCORO_INCLUDE_DIR}
    ${NLOHMANN_INCLUDE_DIR}
)

message(${CMAKE_GENERATOR_PLATFORM})

add_definitions(-D_WIN32_WINNT=0x0701 -DNTDDI_VERSION=NTDDI_WIN7 )
add_definitions(/std:c++17 /utf-8 /await)
add_definitions(-DASIO_STANDALONE -DRPC_USE_LOG -D_LOG_CONSOLE -DPROJECT_NAME="${PROJECT_NAME}" -D_USE_COROUTINE)

file(GLOB SERVER_SOURCE_FILES ./src/*.cpp ./src/*.h ./src/server/*.cpp ./src/server/*.h)
file(GLOB CLIENT_SOURCE_FILES ./src/*.cpp ./src/*.h ./src/client/*.cpp ./src/client/*.h)
file(GLOB CORO_LIB_SOURCE_FILES ${CPPCORO_INCLUDE_DIR}/../lib/*.cpp)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_HOME_DIRECTORY}/bin/)
add_executable(${PROJECT_NAME}_server ${SERVER_SOURCE_FILES})
add_executable(${PROJECT_NAME}_client ${CLIENT_SOURCE_FILES} )
target_link_libraries(${PROJECT_NAME}_client Synchronization.lib)