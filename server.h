#ifndef _SERVER_H
#define _SERVER_H

#include "base/basetypes.h"

struct server_opts {
  int         so_thread_to_processor_ratio;
  char*       so_www_rootdir;
  int         so_max_clients;
  char*       so_port;
  atomic32_t  so_max_outstanding_accepts;
};

int server_start_and_run(int argc, char** argv);

#endif
