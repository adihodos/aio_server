#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "statistics.h"

#if defined(__ENABLE_STATISTICS__)
void server_update_stats(struct server_stats* ss, kStatsType stat_type, ...) {
  va_list args_ptr;
  DWORD bytes_transferred;

  va_start(args_ptr, stat_type);
  lock_acquire(&ss->ss_stats_lock);

  switch (stat_type) {
  case kStatsAddConnection :
    ++ss->ss_total_connections;
    ++ss->ss_concurrent_connections;
    if (ss->ss_peak_connections < ss->ss_concurrent_connections) {
      ss->ss_peak_connections = ss->ss_concurrent_connections;
    }
    break;

  case kStatsDelConnection :
    --ss->ss_concurrent_connections;
    --ss->ss_total_connections;
    break;

  case kStatsTransferSuccess :
    bytes_transferred = va_arg(args_ptr, DWORD);
    ss->ss_total_bytes += bytes_transferred;
    break;

  default :
    break;
  }

  lock_release(&ss->ss_stats_lock);
  va_end(args_ptr);
}

void server_dump_stats(struct server_stats* ss) {
  char buffer_stats[1024];
#if defined(__OS_WINDOWS__)
  DWORD bytes_written;
  _snprintf_s(buffer_stats, 
              sizeof(buffer_stats), 
              sizeof(buffer_stats) - 1,
              "\nTotal connections %d"
              "\nMaximum simultaneous connections %d"
              "\nTotal bytes transferred %d",
              ss->ss_total_connections,
              ss->ss_peak_connections,
              ss->ss_total_bytes);
  WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
            buffer_stats,
            (DWORD) strlen(buffer_stats),
            &bytes_written,
            NULL);
#elif defined(__OS_LINUX__) || defined(__OS_FREEBSD__)
#else
#error Unknown architecture!!
#endif
}

#endif /* __ENABLE_STATISTICS__ */
