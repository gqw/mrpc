cmake_minimum_required(VERSION 3.6)
project(test_101)

add_definitions(-DPROJECT_NAME="${PROJECT_NAME}" )
include_directories( ${PROJECT_SOURCE_DIR}/src/ )
file(GLOB SOURCE_FILES ./src/*.cpp ./src/*.h ./src/client/*.cpp ./src/client/*.h)

add_executable(${PROJECT_NAME}_client ${SOURCE_FILES})
if(CMAKE_HOST_UNIX)
    target_link_libraries(${PROJECT_NAME}_client pthread)
endif()