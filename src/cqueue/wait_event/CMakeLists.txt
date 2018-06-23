cmake_minimum_required(VERSION 3.8)
project(wait_event C)

set(CMAKE_C_STANDARD 11)
set(SOURCE_FILES wait_event.h wait_event.c)

add_library(wait_event SHARED ${SOURCE_FILES})

install(TARGETS wait_event DESTINATION lib)
install(FILES wait_event.h DESTINATION include/wait_event)
