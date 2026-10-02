#ifndef _PORT_H_
#define _PORT_H_

// Type-compatibility header for the scale effect library (MAMEPlus origin).
// Rewritten for 0.287u: the legacy fixed-width types are no longer provided
// by the OSD headers, and the old Windows/SNES9X-platform spaghetti
// (windows.h include, bool/true/false macros) has been removed.

#include <limits.h>
#include <string.h>
#include <stdint.h>

#include "osdcore.h"

/* #define PIXEL_FORMAT RGB565 */
#define GFX_MULTI_FORMAT

#ifndef NOASM
//#define USE_X86_ASM
#endif

// legacy fixed-width types (0.287u compatibility)
typedef uint8_t  UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef int8_t   INT8;
typedef int16_t  INT16;
typedef int32_t  INT32;
typedef int64_t  INT64;

#ifndef snes9x_types_defined
#define snes9x_types_defined

typedef int8_t   int8;
typedef uint8_t  uint8;
typedef int16_t  int16;
typedef uint16_t uint16;
typedef int32_t  int32;
typedef uint32_t uint32;
typedef int64_t  int64;
typedef uint64_t uint64;
typedef intptr_t pint;
typedef unsigned char bool8;

#endif /* snes9x_types_defined */

typedef UINT8  u8;
typedef UINT16 u16;
typedef UINT32 u32;
typedef UINT64 u64;
typedef INT8   s8;
typedef INT16  s16;
typedef INT32  s32;
typedef INT64  s64;

// from advmame
typedef UINT16 interp_uint16;
typedef UINT32 interp_uint32;

typedef UINT8 scale2x_uint8;
typedef UINT16 scale2x_uint16;
typedef UINT32 scale2x_uint32;

typedef UINT8 scale3x_uint8;
typedef UINT16 scale3x_uint16;
typedef UINT32 scale3x_uint32;

typedef UINT32 xbrz_uint32;

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#endif
