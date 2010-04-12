/*!
 * @file atomic_gcc.h Atomic operations on platforms that use GCC.
 */
#ifndef _ATOMIC_GCC_H
#define _ATOMIC_GCC_H

#include "basetypes.h"

/*!
 * @brief Generates a memory barrier. Note that this will not work on 
 * processors before the P4 family.
 */
#define MEMORY_BARRIER()  __asm__ __volatile__("mfence":::"memory")

/*!
 * @Brief Loads a value from memory with acquire semantics.
 */
static inline atomic32_t atomic32_load(volatile atomic32_t* val) {
  atomic32_t value = *val;
  MEMORY_BARRIER();
  return value;
}

/*!
 * @Brief Stores a value in memory with release semantics.
 */
static inline void atomic32_store(volatile atomic32_t* ptr, atomic32_t val) {
  MEMORY_BARRIER();
  *ptr = val;
}

/*!
 * @brief Atomically compares and exchanges two values. The function will
 * compare the value stored in ptr with oldval and if they are equal
 * it will store newval into ptr.
 * @return The value stored in ptr.
 */
static inline atomic32_t atomic32_compare_and_swap(volatile atomic32_t* ptr,
                                                   atomic32_t comparand,
                                                   atomic32_t newval) {
  __asm__ __volatile__("lock; cmpxchgl %2, %1\n\t"
                       :"+a"(comparand), "+m"(*ptr) 
                       :"r"(newval) 
                       :"memory");
  /*
   * Old value of *ptr is now stored in comparand
   */
  return comparand;
}

/*!
 * @brief Atomically swap two values. This function will store newval into ptr.
 * @return Old value of ptr.
 */
static inline atomic32_t atomic32_swap(volatile atomic32_t* ptr, 
                                       atomic32_t newval) {
  __asm__ __volatile__("xchgl %0, %1\n\t"
                       :"+q"(newval), "+m"(*ptr) 
                       :
                       :"memory");
  /*
   * Old value of *ptr is now stored in newval
   */
  return newval;
}

/*!
 * @brief Atomically increment a value. The function will increment the value
 * stored in ptr with newval. Newval can be a negative value.
 * @return The new value stored in ptr.
 */
static inline atomic32_t atomic32_increment(volatile atomic32_t* ptr,
                                            atomic32_t newval) {
  atomic32_t old_val = newval;
  __asm__ __volatile__("lock; xaddl %0, %1\n\t"
                       :"+q"(old_val), "+m"(*ptr)
                       :
                       :"memory");
  /*
   * Old value of *ptr is stored in old_val, return sum of old_val and newval
   * ( the new value in *ptr )
   */
  return old_val + newval;
}

#endif /* !_ATOMIC_GCC_H */
