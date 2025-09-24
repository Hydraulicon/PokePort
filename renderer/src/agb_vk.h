#pragma once

#if defined(__cplusplus)
#include <cstddef>
#include <cstdint>
extern "C" {
#else
#include <stddef.h>
#include <stdint.h>
#endif

typedef struct AgbVkCtx AgbVkCtx;            // Opaque renderer context

// ---- Lifecycle ----
AgbVkCtx* agbvk_create(void);
void      agbvk_destroy(AgbVkCtx* ctx);

// ---- Upload endpoints (mirror the 11 SSBOs) ----
// Byte-stream inputs are in the GBA/native layout; the implementation expands to
// the SSBO layout (e.g., "uint-per-byte").

void agbvk_upload_vram(AgbVkCtx*, const void* bytes, size_t countBytes);   // 96 KB bytes
void agbvk_upload_pal_bg(AgbVkCtx*, const void* bytes, size_t countBytes);   // 1 KB  bytes
void agbvk_upload_bg_params(AgbVkCtx*, const uint32_t* u32, size_t countU32);     // 4*8 = 32 dwords
void agbvk_upload_pal_obj(AgbVkCtx*, const void* bytes, size_t countBytes);   // 512  bytes
void agbvk_upload_oam(AgbVkCtx*, const void* bytes, size_t countBytes);   // 1 KB  bytes
void agbvk_upload_win(AgbVkCtx*, const void* bytes, size_t countBytes);   // ~3264 bytes
void agbvk_upload_fx(AgbVkCtx*, const void* bytes, size_t countBytes);   // 16 bytes (padded)
void agbvk_upload_scanline(AgbVkCtx*, const void* bytes, size_t countBytes);   // 160*80 bytes
void agbvk_upload_bg_aff(AgbVkCtx*, const int32_t* i32, size_t countI32);     // 4*6  ints
void agbvk_upload_obj_aff(AgbVkCtx*, const int32_t* i32, size_t countI32);     // 32*4 ints

// ---- Dispatch + readback ----
// Push-constants = {fbW, fbH, mapW, mapH, objCharBase, objMapMode(0=2D,1=1D)}
void agbvk_dispatch_frame(AgbVkCtx*, uint32_t fbW, uint32_t fbH,
    uint32_t mapW, uint32_t mapH,
    uint32_t objCharBase, uint32_t objMapMode);

// Read back FB as RGBA8; pixelCount = fbW * fbH
void agbvk_readback_rgba(AgbVkCtx*, uint32_t* dstRGBA, size_t pixelCount);

#if defined(__cplusplus)
} // extern "C"
#endif
