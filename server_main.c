#include "base/basetypes.h"
#include "base/debug_helpers.h"
#include "server.h"

int main(int argc, char** argv) {
  /*set_unhandled_exceptions_filter();*/
  return server_start_and_run(argc, argv);
}
