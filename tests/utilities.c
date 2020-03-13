#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>

#include "tvheadend.h"
#include "tvhlog.h"
#include "sbuf.h"

int tvh_thread_debug;

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

static void
sbuf_alloc_fail(size_t len)
{
  fprintf(stderr, "Unable to allocate %zd bytes\n", len);
  abort();
}

void
sbuf_init_fixed(sbuf_t *sb, int len)
{
  memset(sb, 0, sizeof(sbuf_t));
  sb->sb_data = malloc(len);
  if (sb->sb_data == NULL)
    sbuf_alloc_fail(len);
  sb->sb_size = len;
}

void
sbuf_append(sbuf_t *sb, const void *data, int len)
{
  sbuf_alloc(sb, len);
  memcpy(sb->sb_data + sb->sb_ptr, data, len);
  sb->sb_ptr += len;
}

void
sbuf_reset(sbuf_t *sb, int max_len)
{
  sb->sb_ptr = sb->sb_err = 0;
  if (sb->sb_size > max_len) {
    void *n = realloc(sb->sb_data, max_len);
    if (n) {
      sb->sb_data = n;
      sb->sb_size = max_len;
    }
  }
}

void
sbuf_alloc_(sbuf_t *sb, int len)
{
  if(sb->sb_data == NULL) {
    sb->sb_size = len * 4 > 4000 ? len * 4 : 4000;
    sb->sb_data = malloc(sb->sb_size);
    return;
  } else {
    sb->sb_size += len * 4;
    sb->sb_data = realloc(sb->sb_data, sb->sb_size);
  }

  if(sb->sb_data == NULL)
    sbuf_alloc_fail(sb->sb_size);
}
