#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>

#include "tvheadend.h"
#include "tvhlog.h"
#include "sbuf.h"

int tvh_thread_debug;

int tvheadend_running = 1;

tvh_mutex_t fork_lock;

int                      tvhlog_level = LOG_TRACE;

void _tvhlog ( const char *file, int line, int severity,
               int subsys, const char *fmt, ... )
{
  va_list args;
  va_start(args, fmt);
  printf("%s: %d %d %d ", file, line, severity, subsys);
  vprintf(fmt, args);
  printf("\n");
  va_end(args);
}

void tvhlogv ( const char *file, int line, int severity,
               int subsys, const char *fmt, va_list *args )
{
  printf("%s: %d %d %d ", file, line, severity, subsys);
  vprintf(fmt, *args);
  printf("\n");
}

int tvh__mutex_unlock(tvh_mutex_t *mutex) {
  return 0;
}

int tvh__mutex_lock(tvh_mutex_t *mutex, const char *filename, int lineno) {
  return 0;
}

/*
int
tvh_pipe(int flags, th_pipe_t *p)
{
  int fd[2], err;
  tvh_mutex_lock(&fork_lock);
  err = pipe(fd);
  if (err != -1) {
    fcntl(fd[0], F_SETFD, fcntl(fd[0], F_GETFD) | FD_CLOEXEC);
    fcntl(fd[1], F_SETFD, fcntl(fd[1], F_GETFD) | FD_CLOEXEC);
    fcntl(fd[0], F_SETFL, fcntl(fd[0], F_GETFL) | flags);
    fcntl(fd[1], F_SETFL, fcntl(fd[1], F_GETFL) | flags);
    p->rd = fd[0];
    p->wr = fd[1];
  }
  tvh_mutex_unlock(&fork_lock);
  return err;
}

void
tvh_pipe_close(th_pipe_t *p)
{
  close(p->rd);
  close(p->wr);
  p->rd = -1;
  p->wr = -1;
}
*/

int64_t   __mdispatch_clock;

int
rate_to_sri(int rate)
{
  return rate;
}