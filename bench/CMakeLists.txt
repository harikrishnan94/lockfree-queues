option(BUILD_LOCKFREE_QUEUE_BENCH "Enable Building Benchmark" ON)

if(BUILD_LOCKFREE_QUEUE_BENCH)
    add_executable(LockfreeQueueBench bench.cpp waitevent.cpp)
    target_link_libraries(LockfreeQueueBench PRIVATE LockfreeQueue)

    add_warning_flags(LockfreeQueueBench)
    add_sanitizer_flags(LockfreeQueueBench)
endif()
