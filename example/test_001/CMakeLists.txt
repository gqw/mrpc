cmake_minimum_required(VERSION 3.6)
project(test_001)

include_directories(
    $ENV{WORKSPACE}/dependence/rpc/include/
    $ENV{WORKSPACE}/ezlib/dep/asio
)
file(GLOB SOURCE_FILES ./src/*.cpp ./src/*.h)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_HOME_DIRECTORY}/bin/)
add_executable(${PROJECT_NAME} ${SOURCE_FILES})