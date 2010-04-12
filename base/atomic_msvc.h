#ifndef _ATOMIC_MSVC_H
#define _ATOMIC_MSVC_H

#include <windows.h>
#include "basetypes.h"

/*!
 * The Microsoft C/C++ compiler has acquire semantics for reads
 * and release semantics for writes that operate on volatiles.
 * See this for further explanation: 
 * http://msdn.microsoft.com/en-us/library/ms686355(VS.85).aspx
 */

__inline atomic32_t atomic32_load(volatile atomic32_t* ptr) {
  atomic32_t value = *ptr;
  return(value);
}

__inline void atomic32_store(volatile atomic32_t* ptr , atomic32_t val) {
  *ptr = val;
}

/*!
 * @brief Atomically compares and exchanges two values. The function will
 * compare the value stored in ptr with oldval and if they are equal
 * it will store newval into ptr.
 * @return The value stored in ptr.
 */
__inline atomic32_t atomic32_compare_and_swap(volatile atomic32_t* ptr,
                                              atomic32_t oldval,
                                              atomic32_t newval) {
  return (atomic32_t) InterlockedCompareExchange((LONG volatile*) ptr,
                                                 (LONG) newval,
                                                 (LONG) oldval);
}

/*!
 * @brief Atomically swap two values. This function will store newval into ptr.
 * @return Old value of ptr.
 */
__inline atomic32_t atomic32_swap(volatile atomic32_t* ptr, atomic32_t newval) {
  return (atomic32_t) InterlockedExchange((LONG volatile*) ptr, (LONG) newval);
}

/*!
 * @brief Atomically increment a value. The function will increment the value
 * stored in ptr with newval. Newval can be a negative value.
 * @return The new value stored in ptr.
 */
__inline atomic32_t atomic32_increment(volatile atomic32_t* ptr,
                                       atomic32_t newval) {
  return (atomic32_t) InterlockedExchangeAdd((LONG volatile*) ptr, 
                                             (LONG) newval);
}

#endif
