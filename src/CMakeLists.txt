find_package(Boost 1.75 REQUIRED)
find_package(Threads REQUIRED)

set(LIB_NAME LockfreeQueue)
set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)

include(PackableLibrary)
add_packable_library(
    ${LIB_NAME}
    NAMESPACE
    IMC
    ROOT_INCLUDE_DIR
    ${INCLUDE_DIR}
    INTERFACE_INCLUDE_DIRS
    lockfree-queue
    DEPENDENCIES_CMAKE
    Dependencies.cmake)

target_sources(${LIB_NAME} PRIVATE mpsc_pc.cpp rseq.cpp)
target_include_directories(${LIB_NAME} PRIVATE ${INCLUDE_DIR})
target_compile_features(
    ${LIB_NAME}
    PRIVATE cxx_std_17
    INTERFACE cxx_std_17)
target_link_libraries(${LIB_NAME} PUBLIC Boost::boost Threads::Threads)

add_warning_flags(LockfreeQueue)

include(CPack)
