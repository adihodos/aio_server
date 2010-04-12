#ifndef _ATOMIC_H
#define _ATOMIC_H

#if defined(__OS_LINUX__) || defined(__OS_FREEBSD__)
#include "atomic_gcc.h"
#elif defined(__OS_WINDOWS__)
#include "atomic_msvc.h"
#else
#error Configuration not supported!
#endif

#endif
