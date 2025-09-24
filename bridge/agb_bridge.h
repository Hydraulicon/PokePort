// bridge/agb_bridge.h   GBA-shaped host state + bridge API (no Vulkan)

#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

// Forward-declare the renderer context (from renderer/agb_vk.h) to avoid coupling
typedef struct AgbVkCtx AgbVkCtx;

// --------------------------- Fixed sizes (match shader SSBOs) ---------------------------
#define AGB_VRAM_SIZE         (96u * 1024u)
#define AGB_PAL_BG_SIZE       (1024u)
#define AGB_PAL_OBJ_SIZE      (512u)
#define AGB_OAM_SIZE          (1024u)
#define AGB_SCANLINES         (160u)
#define AGB_BG_COUNT          (4u)
#define AGB_BG_PARAM_DWORDS   (8u)
#define AGB_BG_AFF_COUNT      (4u)
#define AGB_OBJ_AFF_COUNT     (32u)

#if defined(__cplusplus)
#  define AGB_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#  define AGB_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

// --------------------------- BG param flags (matches your host code) --------------------
enum {
    AGB_BG_FLAG_AFFINE = 1u,   // BG2/3 affine enable
    AGB_BG_FLAG_WRAP   = 2u,   // wrap affine sampling
    AGB_BG_FLAG_MOSAIC = 4u,   // mosaic enable
};

// --------------------------- Host-side structs (std430-friendly) -----------------------
// 1) BGParam: exactly 8 u32 per BG (total 32 u32 across 4 BGs)
typedef struct BGParam {
    uint32_t charBase;   // byte offset into VRAM for char/tile base
    uint32_t screenBase; // byte offset into VRAM for screen/map base
    uint32_t hofs;       // scroll X (text) or dx for affine origin
    uint32_t vofs;       // scroll Y (text) or dy for affine origin
    uint32_t pri;        // priority (0=front..3=back)
    uint32_t enabled;    // 0/1
    uint32_t flags;      // AGB_BG_FLAG_* bitfield
    uint32_t _pad;       // keep 8 dwords per BG
} BGParam;
AGB_STATIC_ASSERT(sizeof(BGParam) == 8u * sizeof(uint32_t), "BGParam must be 8 u32");

// 2) Window registers (WIN0/WIN1 rectangles + masks)
// bit layout per mask: 0=BG0,1=BG1,2=BG2,3=BG3,4=OBJ,5=ColorEffect
typedef struct WinState {
    uint32_t win0[4]; // x1,y1,x2,y2 (exclusive)
    uint32_t win1[4];
    uint32_t winIn0, winIn1, winOut, winObj;
} WinState;
AGB_STATIC_ASSERT(sizeof(WinState) % 4u == 0u, "WinState is 32-bit aligned");

// 3) Color math + mosaic registers (packed as 4 u32 = 16 bytes)
typedef struct FxRegs {
    uint32_t bldcnt;   // BLDCNT
    uint32_t bldalpha; // EVA | (EVB<<8)
    uint32_t bldy;     // brightness factor (for brighten/darken)
    uint32_t mosaic;   // BG/OBJ mosaic params
} FxRegs;
AGB_STATIC_ASSERT(sizeof(FxRegs) == 16u, "FxRegs must be 16 bytes");

// 4) Per-scanline overrides (80 bytes/line = 160*80 total, matching your allocation)
// flags bit 0 = scroll override enabled; window x1/x2 wired for WIN0 slit if needed.
typedef struct Scanline {
    uint32_t hofs[4], vofs[4];    // 8*4 = 32 bytes
    uint32_t win0x1, win0x2, _p0, _p1; // 16 bytes
    uint32_t win1x1, win1x2, _p2, _p3; // 16 bytes
    uint32_t bldcnt, bldalpha, bldy, flags; // 16 bytes
} Scanline;
AGB_STATIC_ASSERT(sizeof(Scanline) == 80u, "Scanline must be 80 bytes");

// 5) Affine params for BGx (6 int32 each = 24 bytes)
typedef struct AffineParam {
    int32_t refX;
    int32_t refY;
    int32_t pa;
    int32_t pb;
    int32_t pc;
    int32_t pd;
} AffineParam;
AGB_STATIC_ASSERT(sizeof(AffineParam) == 24u, "AffineParam must be 24 bytes");

// 6) OBJ affine set (4 int32 = 16 bytes)
typedef struct ObjAff {
    int32_t pa;
    int32_t pb;
    int32_t pc;
    int32_t pd;
} ObjAff;
AGB_STATIC_ASSERT(sizeof(ObjAff) == 16u, "ObjAff must be 16 bytes");

// --------------------------- Aggregated host state -------------------------------------
typedef struct AgbHwState {
    // Byte-addressable storages (caller writes native GBA-style bytes)
    uint8_t vram[AGB_VRAM_SIZE];     // BG/OBJ char + screen blocks
    uint8_t pal_bg[AGB_PAL_BG_SIZE];   // BG palettes (BGR555 as little-endian bytes)
    uint8_t pal_obj[AGB_PAL_OBJ_SIZE];  // OBJ palettes (index 0 = transparent)
    uint8_t oam[AGB_OAM_SIZE];      // OAM entries (little-endian 16-bit fields)

    // Registers / structured state
    BGParam   bg_params[AGB_BG_COUNT];      // 4 * 8 u32
    WinState  win;                          // WIN* + masks
    FxRegs    fx;                           // BLDCNT/BLDALPHA/BLDY + MOSAIC
    Scanline  scan[AGB_SCANLINES];          // 160 lines @ 80 bytes
    AffineParam bgAff[AGB_BG_AFF_COUNT];    // BG0..BG3 affine
    ObjAff    objAff[AGB_OBJ_AFF_COUNT];    // 32 OBJ affine sets
} AgbHwState;

// --------------------------- Bridge API -------------------------------------------------
// Build the exact demo scene you had in hello_frame (tiles, maps, palettes, OAM,
// windows, color math, per-line scroll, BG2 affine), all in host memory.
// No Vulkan calls here; purely populates AgbHwState.
void agb_init_hw(AgbHwState* hw);

// Copy host state into the renderer's 11 SSBOs using agb_vk.* upload calls
// (VRAM/palettes/OAM are expanded to "uint-per-byte" internally).
void agb_sync_to_renderer(const AgbHwState* hw, AgbVkCtx* ctx);

#if defined(__cplusplus)
} // extern "C"
#endif

#undef AGB_STATIC_ASSERT
