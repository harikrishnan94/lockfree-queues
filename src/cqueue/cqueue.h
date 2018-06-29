#ifndef CQUEUE_H
#define CQUEUE_H

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus

#include <stdbool.h>

	/******** MPMC Queue *********/
	struct cqueue_mpmc_s;
	typedef struct cqueue_mpmc_s cqueue_mpmc_t;

	extern cqueue_mpmc_t *CreateMPMCQueue(int queue_size, int max_threads);
	extern void DestroyMPMCQueue(cqueue_mpmc_t *mpmc);

	extern bool MPMCQueueTryPush(cqueue_mpmc_t *mpmc, void *elem);
	extern bool MPMCQueueTryPop(cqueue_mpmc_t *mpmc, void **elem_out);

	extern bool MPMCQueueIsEmpty(cqueue_mpmc_t *mpmc);
	extern bool MPMCQueueIsFull(cqueue_mpmc_t *mpmc);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif /* CQUEUE_H */
