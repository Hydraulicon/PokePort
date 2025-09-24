#pragma once

// ---- Make GCC attributes harmless on MSVC (C-only target) ----
#ifdef _MSC_VER
#ifndef __attribute__
#define __attribute__(x)
#endif
#ifndef __asm__
#define __asm__(x)
#endif
#ifndef asm
#define asm(...)
#endif
#ifndef __builtin_expect
#define __builtin_expect(x,y) (x)
#endif

// Common Emerald section/placement macros become no-ops in host builds
#ifndef IWRAM_DATA
#define IWRAM_DATA
#endif
#ifndef EWRAM_DATA
#define EWRAM_DATA
#endif
#ifndef IWRAM_CODE
#define IWRAM_CODE
#endif
#ifndef EWRAM_CODE
#define EWRAM_CODE
#endif
#ifndef NAKED
#define NAKED
#endif
#ifndef NOINLINE
#define NOINLINE __declspec(noinline)
#endif
#ifndef ALIGNED
#define ALIGNED(x)
#endif
#ifndef PACKED
#define PACKED
#endif
#endif

// If Emerald typedefs aren’t seen yet, define fallback types.
// (global.h usually defines these, so guard to avoid redefinition.)
#include <stdint.h>
#ifndef u8
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
#endif
