cmake_minimum_required(VERSION 3.8)
project(cqueue C)

set(CMAKE_C_STANDARD 11)
set(SOURCE_FILES cqueue.h mpmc.c mpsc.c)

add_library(cqueue SHARED ${SOURCE_FILES})

add_subdirectory(wait_event)
add_subdirectory(backoff)
include_directories(wait_event backoff)
target_link_libraries(cqueue wait_event backoff)

install(TARGETS cqueue DESTINATION lib)
install(FILES cqueue.h DESTINATION include/cqueue)
