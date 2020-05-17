#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>

#include "tvheadend.h"
#include "tvhlog.h"
#include "sbuf.h"
#include "idnode.h"

tvh_mutex_t global_lock;

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

void
tvh_str_set(char **strp, const char *src)
{
  free(*strp);
  *strp = src ? strdup(src) : NULL;
}

void
idnode_changed( idnode_t *self )
{
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

void
sbuf_init(sbuf_t *sb)
{
  memset(sb, 0, sizeof(sbuf_t));
}

void
sbuf_free(sbuf_t *sb)
{
  free(sb->sb_data);
  sb->sb_size = sb->sb_ptr = sb->sb_err = 0;
  sb->sb_data = NULL;
}

/**
 * @file
 * @brief Base64 encode/decode
 * @author Ryan Martell <rdm4@martellventures.com> (with lots of Michael)
 */


/* ---------------- private code */
static const uint8_t map2[] =
{
    0x3e, 0xff, 0xff, 0xff, 0x3f, 0x34, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x01,
    0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
    0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11,
    0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x1a, 0x1b,
    0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23,
    0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b,
    0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33
};

int 
base64_decode(uint8_t *out, const char *in, int out_size)
{
  int i, v;
  unsigned int index;
  uint8_t *dst = out;

  v = 0;
  for (i = 0; *in && *in != '='; i++, in++) {
    index = *in - 43;
    if (index >= sizeof(map2) || map2[index] == 0xff)
      return -1;
    v = (v << 6) + map2[index];
    if (i & 3) {
      if (dst - out < out_size) {
        *dst++ = v >> (6 - 2 * (i & 3));
      }
    }
  }

  return dst - out;
}

/*
 * b64_encode: Stolen from VLC's http.c.
 * Simplified by Michael.
 * Fixed edge cases and made it work from data (vs. strings) by Ryan.
 */
char *base64_encode(char *out, int out_size, const uint8_t *in, int in_size)
{
  static const char b64[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char *dst;
  unsigned i_bits = 0;
  int i_shift = 0;
  int bytes_remaining = in_size;

  if (in_size >= UINT_MAX / 4 ||
      out_size < BASE64_SIZE(in_size))
      return NULL;
  dst = out;
  while (bytes_remaining) {
    i_bits = (i_bits << 8) + *in++;
    bytes_remaining--;
    i_shift += 8;
    do {
      *dst++ = b64[(i_bits << 6 >> i_shift) & 0x3f];
      i_shift -= 6;
    } while (i_shift > 6 || (bytes_remaining == 0 && i_shift > 0));
  }
  while ((dst - out) & 3)
    *dst++ = '=';
  *dst = '\0';

  return out;
}

/**
 *
 */
static inline int
hexnibble(char c)
{
  switch(c) {
  case '0' ... '9':    return c - '0';
  case 'a' ... 'f':    return c - 'a' + 10;
  case 'A' ... 'F':    return c - 'A' + 10;
  default:
    return -1;
  }
}

/**
 *
 */
int
hex2bin(uint8_t *buf, size_t buflen, const char *str)
{
  int hi, lo;

  while(*str) {
    if(buflen == 0)
      return -1;
    if((hi = hexnibble(*str++)) == -1)
      return -1;
    if((lo = hexnibble(*str++)) == -1)
      return -1;

    *buf++ = hi << 4 | lo;
    buflen--;
  }
  return 0;
}

/* Initialise hex string */
char *
uuid_get_hex ( const tvh_uuid_t *u, char *dst )
{
  assert(dst);
  return bin2hex(dst, UUID_HEX_SIZE, u->bin, sizeof(u->bin));
}

/**
 *
 */
char *
bin2hex(char *dst, size_t dstlen, const uint8_t *src, size_t srclen)
{
  static const char table[] = "0123456789abcdef";
  char *ret = dst;
  while(dstlen > 2 && srclen > 0) {
    *dst++ = table[*src >> 4];
    *dst++ = table[*src & 0xf];
    src++;
    srclen--;
    dstlen -= 2;
  }
  *dst = 0;
  return ret;
}

