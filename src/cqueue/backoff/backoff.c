#include "backoff.h"

#include <assert.h>
#include <stddef.h>

static int inline min(int a, int b) { return a < b ? a : b; }
static int inline max(int a, int b) { return a > b ? a : b; }

void
backoff_init(backoff_t *boff, unsigned start_delay, unsigned max_delay)
{
	assert(boff != NULL);

	boff->current_delay = max(start_delay, 1);
	boff->max_delay     = max_delay;
}

static inline unsigned
do_backoff(backoff_t *boff, unsigned step)
{
	unsigned current_delay = boff->current_delay;

	boff->current_delay = min(current_delay * step, boff->max_delay);

	return boff->current_delay;
}

unsigned
backoff_do_eb(backoff_t *boff)
{
	return do_backoff(boff, 2);
}

unsigned
backoff_do_cb(backoff_t *boff)
{
	return do_backoff(boff, 1);
}
