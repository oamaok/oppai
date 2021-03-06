#pragma once
#include "common.h"
#include <inttypes.h>

typedef float 			f32;
typedef double 			f64;

typedef int_least64_t	i64;
typedef int_least32_t	i32;
typedef int_least16_t	i16;
typedef int_least8_t	i8;

typedef uint_least64_t	u64;
typedef uint_least32_t	u32;
typedef uint_least16_t	u16;
typedef uint_least8_t	u8;

#define fi64	PRIdLEAST64
#define fi32	PRIdLEAST32

#if NEEDS_TO_INSTALL_GENTOO
#define fi16	PRIdLEAST16
#define fi8		PRIdLEAST8
#else
// TODO: figure out how to get my inttypes.h to be c99 compliant
#define fi16	"hd"
#define fi8		"hhd"
#endif

#define fu64	PRIuLEAST64
#define fu32	PRIuLEAST32

#if NEEDS_TO_INSTALL_GENTOO
#define fu16	PRIuLEAST16
#define fu8		PRIuLEAST8
#else
#define fu16	"hu"
#define fu8		"hhu"
#endif
