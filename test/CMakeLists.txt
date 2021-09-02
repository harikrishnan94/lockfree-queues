option(BUILD_LOCKFREE_QUEUE_TEST "Enable Building Tests" ON)

if(BUILD_LOCKFREE_QUEUE_TEST)
    find_package(doctest CONFIG REQUIRED)

    add_executable(LockfreeQueueTest testbase.cpp lockfree-queue-test.cpp)
    target_link_libraries(LockfreeQueueTest PRIVATE LockfreeQueue doctest::doctest)

    enable_testing()
    add_test(NAME LockfreeQueueTest COMMAND LockfreeQueueTest -d yes) # print test duration

    add_warning_flags(LockfreeQueueTest)
    add_sanitizer_flags(LockfreeQueueTest)
endif()
