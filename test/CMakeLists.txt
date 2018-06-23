cmake_minimum_required(VERSION 3.8)
project(test_runner)

set(CMAKE_CXX_STANDARD 17)

set(SOURCE_FILES test_main.cpp include/catch.hpp src/cqueue_test.cpp)

include_directories(include)
include_directories(${CQUEUE_INCLUDE_DIR})

add_executable(test_runner ${SOURCE_FILES})
target_link_libraries(test_runner cqueue wait_event)

install(TARGETS test_runner DESTINATION bin)
