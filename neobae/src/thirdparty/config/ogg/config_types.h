#ifndef __CONFIG_TYPES_H__
#define __CONFIG_TYPES_H__

/* Direct final config (Autoconf/CMake bypassed) */
#define INCLUDE_INTTYPES_H 0
#define INCLUDE_STDINT_H 1
#define INCLUDE_SYS_TYPES_H 0

#include <stdint.h>

typedef int16_t   ogg_int16_t;
typedef uint16_t  ogg_uint16_t;
typedef int32_t   ogg_int32_t;
typedef uint32_t  ogg_uint32_t;
typedef int64_t   ogg_int64_t;
typedef uint64_t  ogg_uint64_t;

#endif