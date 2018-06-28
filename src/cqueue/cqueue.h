#ifndef CQUEUE_H
#define CQUEUE_H

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus

#include <stdbool.h>

	/******** MPSC Queue *********/
	struct cqueue_mpsc_s;
	typedef struct cqueue_mpsc_s cqueue_mpsc_t;

	extern cqueue_mpsc_t *CreateMPSCQueue(int queue_size, int max_threads);
	extern void DestroyMPSCQueue(cqueue_mpsc_t *mpsc);

	extern bool MPSCQueueTryPush(cqueue_mpsc_t *mpsc, void *elem);
	extern bool MPSCQueueTryPop(cqueue_mpsc_t *mpsc, void **elem_out);

	extern bool MPSCQueueIsEmpty(cqueue_mpsc_t *mpsc);
	extern bool MPSCQueueIsFull(cqueue_mpsc_t *mpsc);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif /* CQUEUE_H */
