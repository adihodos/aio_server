#ifndef _ALLOCATOR_H
#define _ALLOCATOR_H

#include <stdio.h>

struct allocator {
  void* (*al_mem_alloc)(struct allocator*, size_t);
  void  (*al_mem_release)(struct allocator*, void*);
  void* al_internals;
  int   (*al_dump_statistics)(struct allocator*, void*);
};

extern struct allocator* allocator_handle;

#endif
