#ifndef _BASE_TYPES_H
#define _BASE_TYPES_H

#if defined(__OS_LINUX__) || defined(__OS_FREEBSD__)

#define UNUSED_POST(param) param __attribute__((unused))
#define UNUSED_PRE(param)

#include <stdint.h>
typedef intptr_t  atomic32_t;

#define _countof(data)	(sizeof(data) / sizeof(data[0]))

#elif defined(__OS_WINDOWS__)

#define UNUSED_POST(param) param
#define UNUSED_PRE(param) param

#include <stddef.h>
typedef intptr_t  atomic32_t;

#else
#error "Configuration not supported!!"
#endif

#endif
