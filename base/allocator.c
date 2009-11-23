#include "basetypes.h"
#include "allocator.h"
#include "atomic.h"
#include "debug_helpers.h"

#if defined(__OS_LINUX__) || defined(__OS_FREEBSD__)
#include <stdlib.h>
#include <unistd.h>
#elif defined(__OS_WINDOWS__)
#include <windows.h>
#else
#error Configuration not supported.
#endif

/*!
 * Simple structure to keep some statistics about the memory operations.
 */
struct allocator_internals {
  atomic32_t  ai_total_commits;
  atomic32_t  ai_total_releases;
};

/*!
 * @brief Simple routine to allocate memory.
 * @return Pointer to memory on success , NULL on failure.
 */
static void* default_mem_alloc(struct allocator* al, size_t size) {
  BUGSTOP_IF((!al), "No allocator passed to allocation routine.");
  BUGSTOP_IF((!size), "Zero sized allocation request.");

  atomic32_increment(
      &((struct allocator_internals*) al->al_internals)->ai_total_commits,
      (atomic32_t) 1);
#if defined(__OS_WINDOWS__)
  return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else 
  return malloc(size);
#endif 
}

/*!
 * @brief Frees a block of memory previously allocated with a call
 * to default_mem_alloc() function.
 */
static void default_mem_release(struct allocator* al, void* block) {
  BUGSTOP_IF((!al), "No allocator passed to allocation routine.");
  BUGSTOP_IF((!block), "Null block passed to free routine.");

  atomic32_increment(
      &((struct allocator_internals*) al->al_internals)->ai_total_releases,
      (atomic32_t) 1);
#if defined(__OS_WINDOWS__)
  VirtualFree(block, 0, MEM_RELEASE);
#else
  free(block);
#endif
}

/*!
 * @brief Simple function to dump statistics from the allocator.
 * @param HANDLE to a file.
 */
static int default_stats_dumper(struct allocator* al, void* stream) {
  atomic32_t total_commits;
  atomic32_t total_releases;
  char buff_stats[1024];
#if defined(__OS_WINDOWS__)
  size_t charcnt;
  DWORD bytes_out = 0;
#endif

  BUGSTOP_IF((!stream), "Invalid parameter, expected a file HANDLE.");
  BUGSTOP_IF((!al), "Invalid paramater, expected a valid struct "
             "allocator pointer.");

  total_commits = atomic32_load(
      &((struct allocator_internals*) al->al_internals)->ai_total_commits);
  total_releases = atomic32_load(
      &((struct allocator_internals*) al->al_internals)->ai_total_releases);

#if defined(__OS_WINDOWS__)
  if (FAILED(StringCchPrintfEx(buff_stats,
                               sizeof(buff_stats),
                               NULL,
                               NULL,
                               STRSAFE_IGNORE_NULLS,
                               "\nTotal operations %d\nAllocations %d\nReleases%d\n",
                               total_commits + total_releases,
                               total_commits,
                               total_releases))) {
    return -1;
  }

  if (FAILED(StringCbLength(buff_stats, sizeof(buff_stats), &charcnt))) {
    return -1;
  }

  WriteFile(stream, buff_stats, (DWORD) charcnt, &bytes_out, NULL);
  return (DWORD) charcnt == bytes_out;
#else
  int bytecount = snprintf(buff_stats,
                           _countof(buff_stats),
                           "\nTotal operations %d\nAllocations %d\nReleases%d\n",
                           total_commits + total_releases,
                           total_commits,
                           total_releases);
  ssize_t bytes_out = write((int) stream, buff_stats, bytecount);
  return bytes_out == (ssize_t) bytecount;
#endif
}

static struct allocator_internals def_all_stats = { 0, 0 };

/*!
 * The default allocator.
 */
static struct allocator default_allocator = {
  default_mem_alloc,
  default_mem_release,
  &def_all_stats,
  default_stats_dumper 
};

/*!
 * A pointer to the default allocator so clients can use it.
 */
struct allocator* allocator_handle = &default_allocator;
