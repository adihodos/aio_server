#ifndef BASE_EINTR_WRAPPER_H__
#define BASE_EINTR_WRAPPER_H__

#include <errno.h>

/*!
 * @brief Simple macro around system calls that can be interrupted by a signal.
 * @remarks See man signal. Inspired from code found in Google Chrome.
 */
#define HANDLE_EINTR_ON_SYSCALL(syscall_and_args)                              \
  ({                                                                           \
      __typeof__(syscall_and_args) __eintr_result__;                           \
      do {                                                                     \
        __eintr_result__ = (syscall_and_args);                                 \
      } while (__eintr_result__ == -1 && errno == EINTR);                      \
      __eintr_result__;                                                        \
   })


#endif
