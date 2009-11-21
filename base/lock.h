/*!
 * @file lock.h Abstract wrapper for an OS specific critical section
 */
#ifndef _LOCK_H
#define _LOCK_H

#if defined(__OS_LINUX__) || defined(__OS_FREEBSD__)

#include <pthread.h>
typedef pthread_mutex_t os_lock_t;

static inline void lock_init(os_lock_t* lock) {
  pthread_mutex_init(lock, NULL);
}

static inline void lock_acquire(os_lock_t* lock) {
  pthread_mutex_lock(lock);
}

static inline void lock_release(os_lock_t* lock) {
  pthread_mutex_unlock(lock);
}

static inline void lock_destroy(os_lock_t* lock) {
  pthread_mutex_destroy(lock);
}

#elif defined(__OS_WINDOWS__) 

#include <windows.h>
typedef CRITICAL_SECTION os_lock_t;

__inline void lock_init(os_lock_t* lock) {
  InitializeCriticalSectionAndSpinCount(lock, 2000);
}

__inline void lock_acquire(os_lock_t* lock) {
  EnterCriticalSection(lock);
}

__inline void lock_release(os_lock_t* lock) {
  LeaveCriticalSection(lock);
}

__inline void lock_destroy(os_lock_t* lock) {
  DeleteCriticalSection(lock);
}

#else /* __OS_WINDOWS__ */
#error Configuration not supported
#endif

#endif /* !_LOCK_H */
