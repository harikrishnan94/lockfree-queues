#ifndef WAIT_EVENT_H
#define WAIT_EVENT_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <time.h>

	struct wait_event_s;
	typedef struct wait_event_s wait_event_t;

	/********* Return true to wait *********/
	typedef bool (*wait_event_predicate_t)(void *arg);

	extern wait_event_t *CreateWaitEvent(void);
	extern void DestroyWaitEvent(wait_event_t *);

#define WaitEventWait(wevent, predicate, pred_arg) \
	WaitEventTimedWait(wevent, predicate, pred_arg, NULL)

	extern int WaitEventTimedWait(wait_event_t *wevent,
	                              wait_event_predicate_t predicate,
	                              void *pred_arg,
	                              const struct timespec *abstime);
	extern int WaitEventNumWaiters(wait_event_t *wevent);
	extern int WaitEventWakeupOneWaiter(wait_event_t *wevent);
	extern int WaitEventWakeupAllWaiters(wait_event_t *wevent);

#ifdef __cplusplus
}
#endif

#endif /* WAIT_EVENT_H */
