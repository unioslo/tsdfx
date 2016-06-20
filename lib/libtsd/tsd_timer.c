#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <tsd/log.h>
#include <sys/time.h>
#include <time.h>

static struct timeval tb, te;

void
tsd_timer_start(void)
{
	gettimeofday(&tb, NULL);
}

double
tsd_timer_stop(void)
{
	gettimeofday(&te, NULL);
	return (double)(te.tv_sec - tb.tv_sec) + (double)(te.tv_usec - tb.tv_usec)/1e6;
}
