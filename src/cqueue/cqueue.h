#ifndef CQUEUE_H
#define CQUEUE_H

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus

#include <stdbool.h>
#include <stddef.h>

	/******** MPMC Queue *********/
	struct cqueue_mpmc_s;
	typedef struct cqueue_mpmc_s cqueue_mpmc_t;

	extern cqueue_mpmc_t *CreateMPMCQueue(int queue_size, int max_threads);
	extern void DestroyMPMCQueue(cqueue_mpmc_t *mpmc);

	extern bool MPMCQueueTryPush(cqueue_mpmc_t *mpmc, void *elem);
	extern bool MPMCQueueTryPop(cqueue_mpmc_t *mpmc, void **elem_out);

	extern bool MPMCQueueIsEmpty(cqueue_mpmc_t *mpmc);
	extern bool MPMCQueueIsFull(cqueue_mpmc_t *mpmc);

	/******** Multi-Process aware - MPSC Queue *********/
	struct shmem_mpsc_queue_s;
	typedef struct shmem_mpsc_queue_s shmem_mpsc_queue_t;

	extern size_t CalculateMPSCQueueSize(size_t queue_size, int max_producers);
	extern void InitializeMPSCQueue(shmem_mpsc_queue_t *mpsc, size_t queue_size, int max_producers);
	extern shmem_mpsc_queue_t *CreateMPSCQueue(size_t queue_size, int max_producers);
	extern void DestroyMPSCQueue(shmem_mpsc_queue_t *mpsc);

	extern bool MPSCQueueTryPush(shmem_mpsc_queue_t *mpsc,
	                             int producer_id,
	                             void *elem,
	                             int elem_size);
	extern int MPSCQueueNextElemSize(shmem_mpsc_queue_t *mpsc);
	extern bool MPSCQueuePop(shmem_mpsc_queue_t *mpsc, void *elem);

	extern bool MPSCQueueIsEmpty(shmem_mpsc_queue_t *mpsc);
	extern bool MPSCQueueIsFull(shmem_mpsc_queue_t *mpsc, int elem_size);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif /* CQUEUE_H */
