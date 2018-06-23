cmake_minimum_required(VERSION 3.8)
project(cqueue_bench)

set(CMAKE_CXX_STANDARD 17)

set(SOURCE_FILES bench.cpp)

add_subdirectory(cqueue)

include_directories(cqueue)

add_executable(cqueue_bench ${SOURCE_FILES})
target_link_libraries(cqueue_bench cqueue)

install(TARGETS cqueue_bench DESTINATION bin)
