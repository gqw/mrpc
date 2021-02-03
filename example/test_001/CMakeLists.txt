cmake_minimum_required(VERSION 3.6)
project(test_001)

message("asio       directory: " $ENV{ASIO_INCLUDE_PATH})
message("nlohmann   directory: " $ENV{WORKSPACE}/dependence/)
message("rpc        directory: " $ENV{WORKSPACE}/dependence/rpc/include/)

add_definitions(-DASIO_STANDALONE -D_WIN32_WINNT=0x0501)
add_definitions(/std:c++17 /utf-8)
include_directories(
    $ENV{ASIO_INCLUDE_PATH}
    $ENV{WORKSPACE}/dependence/
    $ENV{WORKSPACE}/dependence/rpc/include/
)
file(GLOB SOURCE_FILES ./src/*.cpp ./src/*.h)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_HOME_DIRECTORY}/bin/)
add_executable(${PROJECT_NAME} ${SOURCE_FILES})