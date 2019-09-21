#include <stdarg.h>
#include <stdio.h>

#include "tvhlog.h"

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
