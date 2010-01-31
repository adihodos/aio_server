#ifndef _DEBUG_HELPERS_H
#define _DEBUG_HELPERS_H

/*!
 * @brief Prints a stack trace.
 */
void print_stack_trace(void);

/*!
 * @brief Wrapper to handle SIGSEGV/exceptions on Linux/Windows.
 */
void set_unhandled_exceptions_filter(void);

void output_formatted_string_to_debugger(const char* file, 
                                         int line, 
                                         const char* fstring,
                                         ...);

#define DUMMY_VA_MACRO_ARG

#if defined(__OS_LINUX__) || defined(__OS_FREEBSD__)

#include <errno.h>

static inline void debug_break(void) {
  __asm__ __volatile__("int3");
}

#define D_FUNCFAIL_WINAPI(funcname) (void)(0)
#define D_FUNCFAIL_WSAAPI(funcname) (void)(0)

#if defined(__DEBUG_ENABLED__)

#define BUGSTOP_IF(cond, descr)                                                 \
  do {                                                                          \
    if ((cond)) {                                                               \
      output_formatted_string_to_debugger(__FILE__, __LINE__, descr);           \
      print_stack_trace();                                                      \
      debug_break();                                                            \
    }                                                                           \
  } while (0)

#define D_FMTSTRING(string, ...)                                                \
  output_formatted_string_to_debugger(__FILE__,                                 \
                                      __LINE__,                                 \
                                      string,                                   \
                                      ##__VA_ARGS__)

#define D_FUNCFAIL_ERRNO(funcname)                                              \
  output_formatted_string_to_debugger(__FILE__,                                 \
                                      __LINE__,                                 \
                                      "Function %s failed, error %d",           \
                                      #funcname,                                \
                                      errno)

#define D_FUNCFAIL(funcname, errcode, descr, ...)                               \
  output_formatted_string_to_debugger(__FILE__,                                 \
                                      __LINE__,                                 \
                                      "Function %s failed, error %d" descr,     \
                                      #funcname,                                \
                                      errcode,                                  \
                                      ##__VA_ARGS__)

#else

#define BUGSTOP_IF(cond, descr)                     (void)(0) 
#define D_FMTSTRING(string, ...)                    (void)(0) 
#define D_FUNCFAIL_ERRNO(funcname)                  (void)(0) 
#define D_FUNCFAIL(funcname, errcode, descr, ...)   (void)(0) 

#endif /* __OS_LINUX__ || __OS_FREEBSD__ && !__DEBUG_ENABLED__ */

#elif defined(__OS_WINDOWS__)

#include <stdio.h>
#include <stdarg.h>
#include <windows.h>
#include <dbghelp.h>
#include <tchar.h>
#include <strsafe.h>


static __inline void debug_break(void) {
  DebugBreak();
}


#define D_FUNCFAIL_ERRNO(funcname)                  (void)(0) 
#define D_FUNCFAIL(funcname, errcode, descr, ...)   (void)(0) 

#if defined(__DEBUG_ENABLED__)

#define BUGSTOP_IF(cond, descr)                                                 \
  do {                                                                          \
    if ((cond)) {                                                               \
      output_formatted_string_to_debugger(__FILE__, __LINE__, descr);           \
      print_stack_trace();                                                      \
      debug_break();                                                            \
    }                           \
  } while (0)

#define D_FMTSTRING(fmtstring, ...)                                             \
  output_formatted_string_to_debugger(__FILE__,                                 \
                                      __LINE__,                                 \
                                      fmtstring,                                \
                                      __VA_ARGS__ )

#define D_FUNCFAIL_WINAPI(funcname)                                             \
  output_formatted_string_to_debugger(__FILE__,                                 \
                                      __LINE__,                                 \
                                      "Function %s failed, error %u",           \
                                      #funcname,                                \
                                      GetLastError())

#define D_FUNCFAIL_WSAAPI(funcname)                                             \
  output_formatted_string_to_debugger(__FILE__,                                 \
                                      __LINE__,                                 \
                                      "Function %s failed, error %d",           \
                                      #funcname,                                \
                                      WSAGetLastError())

#else /* __DEBUG_ENABLED__ */

#define BUGSTOP_IF(cond, descr)     (void)(0)
#define D_FMTSTRING(fmstring, ...)  (void)(0)  
#define D_FUNCFAIL_WINAPI(funcname) (void)(0)
#define D_FUNCFAIL_WSAAPI(funcname) (void)(0)

#endif /* !__DEBUG_ENABLED__ */

#else
#error "Configuration not supported!!"
#endif

#if defined(__DEBUG_ENABLED__)

#define D_DBGBREAK() debug_break()
#define D_STACKTRACE() print_stack_trace()

#else

#define D_DBGBREAK()    (void)(0)
#define D_STACKTRACE()  (void)(0) 

#endif

#endif /* _DEBUG_HELPERS_H */
