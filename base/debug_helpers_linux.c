#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <ucontext.h>
#include <execinfo.h>
#include "basetypes.h"
#include "debug_helpers.h"

static const int kMaxSymbolsBacktrace = 64;
static const char* kStkTraceHdr = 
"\n++++++++++++++++++++ Stack trace begins ++++++++++++++++++++\n";
static const char* kStkTraceFtr =
"\n++++++++++++++++++++ Stack trace ends ++++++++++++++++++++\n";

void print_stack_trace(void) {
  void*   buff_trace[kMaxSymbolsBacktrace];

  int sym_count = backtrace(buff_trace, kMaxSymbolsBacktrace);
  write(STDERR_FILENO, kStkTraceHdr, strlen(kStkTraceHdr));
  backtrace_symbols_fd(buff_trace, sym_count, STDERR_FILENO);
  write(STDERR_FILENO, kStkTraceFtr, strlen(kStkTraceFtr));
}

void output_formatted_string_to_debugger(const char* file, 
                                         int line, 
                                         const char* fstring,
                                         ...) {
  char buff_msg[1024];

  int byte_count = snprintf(buff_msg, 
                            _countof(buff_msg), 
                            "\nFile %s, line %d", 
                            file, 
                            line);
  write(STDERR_FILENO, buff_msg, byte_count);

  va_list args_ptr;
  va_start(args_ptr, fstring);
  byte_count = vsnprintf(buff_msg, 
                         _countof(buff_msg),
                         fstring,
                         args_ptr);
  va_end(args_ptr);

  write(STDERR_FILENO, buff_msg, byte_count);
  write(STDERR_FILENO, "\n", 1);
}

static 
void 
linux_sigsegv_handler(
    int UNUSED_POST(signum), 
    siginfo_t* sidata, 
    void* ctx) 
{
  ucontext_t* ctxptr = ctx;
  char buff_msg[1024];

  int bytecount = snprintf(buff_msg,
                           _countof(buff_msg),
                           "\nSIGSEGV received, faulting address %#08x, errno %d"
                           "\nDumping registers :"
                           "\neip = %#08x"
                           "\neax = %#08x"
                           "\nebx = %#08x"
                           "\necx = %#08x"
                           "\nedx = %#08x"
                           "\nebp = %#08x"
                           "\nesp = %#08x",
                           (uintptr_t) sidata->si_addr,
                           sidata->si_errno,
                           ctxptr->uc_mcontext.gregs[REG_EIP],
                           ctxptr->uc_mcontext.gregs[REG_EAX],
                           ctxptr->uc_mcontext.gregs[REG_EBX],
                           ctxptr->uc_mcontext.gregs[REG_ECX],
                           ctxptr->uc_mcontext.gregs[REG_EDX],
                           ctxptr->uc_mcontext.gregs[REG_EBP],
                           ctxptr->uc_mcontext.gregs[REG_ESP]);
  write(STDERR_FILENO, buff_msg, bytecount);
  print_stack_trace();
  exit(EXIT_FAILURE);
}

void set_unhandled_exceptions_filter(void) {
  struct sigaction handle_sigsegv;
  sigemptyset(&handle_sigsegv.sa_mask);
  handle_sigsegv.sa_flags = SA_SIGINFO;
  handle_sigsegv.sa_sigaction = linux_sigsegv_handler;
  sigaction(SIGSEGV, &handle_sigsegv, NULL);
}
