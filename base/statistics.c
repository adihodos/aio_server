#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "lock.h"
#include "atomic.h"
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
#endif /* __ENABLE_STATISTICS__ */
