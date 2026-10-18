#define __USE_GNU
#define TVH_THREAD_C 1
#include "tvheadend.h"
#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#include "settings.h"
#include "tvhpoll.h"

#ifdef PLATFORM_LINUX
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif

#ifdef PLATFORM_FREEBSD
#include <pthread_np.h>
#endif

#if ENABLE_TRACE
int tvh_thread_debug;
static int tvhwatch_done;
static pthread_t thrwatch_tid;
static pthread_mutex_t thrwatch_mutex = PTHREAD_MUTEX_INITIALIZER;
static TAILQ_HEAD(, tvh_mutex) thrwatch_mutexes = TAILQ_HEAD_INITIALIZER(thrwatch_mutexes);
static int64_t tvh_thread_crash_time;
#endif

/*
 * thread routines
 */

static void doquit(int sig)
{
}

struct
thread_state {
  void *(*run)(void*);
  void *arg;
  char name[17];
};

static inline int
thread_get_tid(void)
{
#ifdef SYS_gettid
  return syscall(SYS_gettid);
#elif ENABLE_ANDROID
  return gettid();
#else
  return -1;
#endif
}

int
tvh_thread_create
  (pthread_t *thread, const pthread_attr_t *attr,
   void *(*start_routine) (void *), void *arg, const char *name)
{
  int r;
  pthread_attr_t _attr;
  if (attr == NULL) {
    pthread_attr_init(&_attr);
    attr = &_attr;
  }
  r = pthread_create(thread, attr, start_routine, arg);
  return r;
}

/* linux style: -19 .. 20 */
int
tvh_thread_renice(int value)
{
  int ret = 0;
#if defined(SYS_gettid) || ENABLE_ANDROID
  pid_t tid = thread_get_tid();
  ret = setpriority(PRIO_PROCESS, tid, value);
#elif defined(PLATFORM_DARWIN)
  /* Currently not possible */
#elif defined(PLATFORM_FREEBSD)
  /* Currently not possible */
#else
#warning "Implement renice for your platform!"
#endif
  return ret;
}

static inline void tvh_mutex_check_magic(tvh_mutex_t *mutex, const char *filename, int lineno)
{
}

int tvh_mutex_init(tvh_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
  memset(mutex, 0, sizeof(*mutex));
#if ENABLE_TRACE
  mutex->magic1 = TVH_THREAD_MUTEX_MAGIC1;
  mutex->magic2 = TVH_THREAD_MUTEX_MAGIC2;
#endif
  return pthread_mutex_init(&mutex->mutex, attr);
}

int tvh_mutex_destroy(tvh_mutex_t *mutex)
{
  tvh_mutex_check_magic(mutex, NULL, 0);
  return pthread_mutex_destroy(&mutex->mutex);
}

/*
 * thread condition variables - monotonic clocks
 */

int
tvh_cond_init
  ( tvh_cond_t *cond, int monotonic )
{
  int r;

  pthread_condattr_t attr;
  pthread_condattr_init(&attr);
#if defined(PLATFORM_DARWIN)
  /*
   * pthread_condattr_setclock() not supported on platform Darwin.
   * We use pthread_cond_timedwait_relative_np() which doesn't
   * need it.
   */
  r = 0;
#else
  if (monotonic) {
    if (r) {
      fprintf(stderr, "Unable to set monotonic clocks for conditions! (%d)", r);
      abort();
    }
  }
#endif
  return pthread_cond_init(&cond->cond, &attr);
}

int
tvh_cond_destroy
  ( tvh_cond_t *cond )
{
  return pthread_cond_destroy(&cond->cond);
}

int
tvh_cond_signal
  ( tvh_cond_t *cond, int broadcast )
{
  if (broadcast)
    return pthread_cond_broadcast(&cond->cond);
  else
    return pthread_cond_signal(&cond->cond);
}

int
tvh_cond_wait
  ( tvh_cond_t *cond, tvh_mutex_t *mutex)
{
  int r;
  
  r = pthread_cond_wait(&cond->cond, &mutex->mutex);
  return r;
}

int
tvh_cond_timedwait
  ( tvh_cond_t *cond, tvh_mutex_t *mutex, int64_t monoclock )
{
  int r;

#if defined(PLATFORM_DARWIN)
  /* Use a relative timedwait implementation */
  int64_t now = getmonoclock();
  int64_t relative = monoclock - now;
  struct timespec ts;

  if (relative < 0)
    return 0;

  ts.tv_sec  = relative / MONOCLOCK_RESOLUTION;
  ts.tv_nsec = (relative % MONOCLOCK_RESOLUTION) *
               (1000000000ULL/MONOCLOCK_RESOLUTION);

  r = pthread_cond_timedwait_relative_np(&cond->cond, &mutex->mutex, &ts);
#else
  struct timespec ts;
  ts.tv_sec = monoclock / MONOCLOCK_RESOLUTION;
  ts.tv_nsec = (monoclock % MONOCLOCK_RESOLUTION) *
               (1000000000ULL/MONOCLOCK_RESOLUTION);
  r = pthread_cond_timedwait(&cond->cond, &mutex->mutex, &ts);
#endif

  return r;
}

int tvh_cond_timedwait_ts(tvh_cond_t *cond, tvh_mutex_t *mutex, struct timespec *ts)
{
  int r;
  
  r = pthread_cond_timedwait(&cond->cond, &mutex->mutex, ts);
  return r;
}

void
tvh_mutex_not_held(const char *file, int line)
{
  tvherror(LS_THREAD, "Mutex not held at %s:%d", file, line);
  fprintf(stderr, "Mutex not held at %s:%d\n", file, line);
  abort();
}

struct tvhpoll
{
  tvh_mutex_t lock;
  int fd;
};

tvhpoll_t *
tvhpoll_create ( size_t n )
{
  int fd;
  fd = -1;
  tvhpoll_t *tp = calloc(1, sizeof(tvhpoll_t));
  tvh_mutex_init(&tp->lock, NULL);
  tp->fd = fd;
  return tp;
}

void tvhpoll_destroy ( tvhpoll_t *tp )
{
  if (tp == NULL)
    return;
  tvh_mutex_destroy(&tp->lock);
  free(tp);
}

int tvhpoll_add
  ( tvhpoll_t *tp, tvhpoll_event_t *evs, size_t num )
{
  int r;

  tvh_mutex_lock(&tp->lock);
  tvh_mutex_unlock(&tp->lock);
  return r;
}

