cmake_minimum_required(VERSION 3.8)
project(backoff C)

set(CMAKE_C_STANDARD 11)
set(SOURCE_FILES backoff.h backoff.c)

add_library(backoff SHARED ${SOURCE_FILES})

install(TARGETS backoff DESTINATION lib)
install(FILES backoff.h DESTINATION include/backoff)
