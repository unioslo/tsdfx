#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <tsd/tictoc.h>

#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

static long time_us = 0;
static struct timeval tb, te;

void tsd_tic(void)
{
  gettimeofday(&tb, NULL);
}

void tsd_toc(const char *fmt, ...)
{
  char *msgbuffer;
  long s,u;
  va_list ap;

  msgbuffer = NULL;
  
  va_start(ap, fmt);
  if (vasprintf(&msgbuffer, fmt, ap) < 0)
    msgbuffer = strdup(fmt);
  va_end(ap);

  gettimeofday(&te, NULL);
  s=te.tv_sec-tb.tv_sec;
  u=te.tv_usec-tb.tv_usec;
  VERBOSE("%s, measured time: %li.%.6lis\n", msgbuffer, (s*1000000+u)/1000000, (s*1000000+u)%1000000);

  free(msgbuffer);
}
