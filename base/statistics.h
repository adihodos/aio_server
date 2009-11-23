#ifndef _STATISTICS_H
#define _STATISTICS_H

#if defined(__ENABLE_STATISTICS__)
#pragma message("Compiling with statistics enabled!")

struct server_stats {
  atomic32_t  ss_total_connections;
  atomic32_t  ss_concurrent_connections;
  atomic32_t  ss_peak_connections;
  atomic32_t  ss_total_bytes;
  os_lock_t   ss_stats_lock;
};

#define SERVER_STATISTICS_STRUCT_MEMBER_ENTRY(member_name)                     \
  struct server_stats member_name;

typedef enum {
  /*!
   * Connection accepted. No argument.
   */
  kStatsAddConnection = 0x00,
  /*!
   * Connection closed. No argument.
   */
  kStatsDelConnection = 0x01,
  /*!
   * Successfull transfer to client. Expects an unsigned long representing
   * the number of bytes transferred.
   */
  kStatsTransferSuccess = 0x02,
} kStatsType;

void server_update_stats(struct server_stats* ss, kStatsType stat_type, ...);

#define STATS_INITIALIZE(stats)                                                \
  do {                                                                         \
    memset(stats, 0, sizeof(*stats));                                          \
    lock_init(&stats->ss_stats_lock);                                          \
  } while(0)

#define STATS_UNINITIALIZE(stats)                                              \
  do {                                                                         \
    lock_destroy(&stats->ss_stats_lock);                                       \
  } while(0)

#define STATS_INCREMENT_CONNECTION_COUNT(stats)                                \
  server_update_stats(stats, kStatsAddConnection)

#define STATS_DECREMENT_CONNECTION_COUNT(stats)                                \
  server_update_stats(stats, kStatsDelConnection)

#define STATS_UPDATE_TRANSFER_COUNT(stats, amount)                             \
  server_update_stats(stats, kStatsTransferSuccess, amount)

#else

#pragma message("Compiling with statistics disabled!")
#define SERVER_STATISTICS_STRUCT_MEMBER_ENTRY(member_name)
#define STATS_INITIALIZE(stats)                             (void)(0)
#define STATS_UNINITIALIZE(stats)                           (void)(0)
#define STATS_INCREMENT_CONNECTION_COUNT(stats)             (void)(0)
#define STATS_DECREMENT_CONNECTION_COUNT(stats)             (void)(0)
#define STATS_UPDATE_TRANSFER_COUNT(stats, amount)          (void)(0)

#endif

#endif /* _STATISTICS_H */
