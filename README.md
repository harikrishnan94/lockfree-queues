# Lockfree Multi-Producer Multi-Consumer queue
Based on [MPMC Ring Buffer](https://www.linuxjournal.com/content/lock-free-multi-producer-multi-consumer-queue-ring-buffer)

# Lockfree Multi-Producer Single-Consumer queue
Simplified version of MPMC queue

# Lockfree Multi-Producer Single-Consumer Partitioned Per-CPU queue
- Queue is partitioned into multiple per-cpu queues.
    - Forgoes FIFO ordering, since queue is partitioned.
- Super fast - Uses only atomic_release and atomic_acquire operations.
- Uses [RSEQ](https://www.efficios.com/blog/2019/02/08/linux-restartable-sequences/) Syscall, for maintaining correctness while manipulating per-cpu queues

# Lockfree Single-Producer Single-Consumer queue
Simple FIFO queue
