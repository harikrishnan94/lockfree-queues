#ifndef BACKOFF_H
#define BACKOFF_H

#ifdef __cplusplus
extern "C"
{
#endif

	typedef struct
	{
		unsigned current_delay;
		unsigned max_delay;
		unsigned delay_step;
	} backoff_t;

	extern void backoff_init(backoff_t *boff, unsigned start_delay, unsigned max_delay);
	extern unsigned backoff_do_eb(backoff_t *boff);
	extern unsigned backoff_do_cb(backoff_t *boff);

#ifdef __cplusplus
}
#endif

#endif /* BACKOFF_H */
