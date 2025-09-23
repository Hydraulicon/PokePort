// bridge/agb_bridge.h  — GBA-shaped host state + bridge API (no Vulkan)

#pragma once
#include <cstdint>
#include <cstddef>
#include <array>

// Forward-declare the renderer context (from renderer/agb_vk.h) to avoid coupling
struct AgbVkCtx;

// --------------------------- Fixed sizes (match shader SSBOs) ---------------------------
constexpr size_t AGB_VRAM_SIZE = 96 * 1024;   // bytes  (expanded to uint-per-byte in SSBO)
constexpr size_t AGB_PAL_BG_SIZE = 1024;        // bytes  (uint-per-byte in SSBO)
constexpr size_t AGB_PAL_OBJ_SIZE = 512;         // bytes  (uint-per-byte in SSBO)
constexpr size_t AGB_OAM_SIZE = 1024;        // bytes  (uint-per-byte in SSBO)
constexpr size_t AGB_SCANLINES = 160;         // lines
constexpr size_t AGB_BG_COUNT = 4;
constexpr size_t AGB_BG_PARAM_DWORDS = 8;           // 8 u32 per BG (total 32 u32)
constexpr size_t AGB_BG_AFF_COUNT = 4;           // BG0..BG3
constexpr size_t AGB_OBJ_AFF_COUNT = 32;          // 32 OBJ affine sets

// --------------------------- BG param flags (matches your host code) --------------------
enum : uint32_t {
    AGB_BG_FLAG_AFFINE = 1u,   // BG2/3 affine enable
    AGB_BG_FLAG_WRAP = 2u,   // wrap affine sampling
    AGB_BG_FLAG_MOSAIC = 4u,   // mosaic enable
};

// --------------------------- Host-side structs (std430-friendly) -----------------------
// 1) BGParam: exactly 8 u32 per BG (total 32 u32 across 4 BGs)
struct BGParam {
    uint32_t charBase;   // byte offset into VRAM for char/tile base
    uint32_t screenBase; // byte offset into VRAM for screen/map base
    uint32_t hofs;       // scroll X (text) or dx for affine origin
    uint32_t vofs;       // scroll Y (text) or dy for affine origin
    uint32_t pri;        // priority (0=front..3=back)
    uint32_t enabled;    // 0/1
    uint32_t flags;      // AGB_BG_FLAG_* bitfield
    uint32_t _pad;       // keep 8 dwords per BG
};
static_assert(sizeof(BGParam) == 8 * sizeof(uint32_t), "BGParam must be 8 u32");

// 2) Window registers (WIN0/WIN1 rectangles + masks)
// bit layout per mask: 0=BG0,1=BG1,2=BG2,3=BG3,4=OBJ,5=ColorEffect
struct WinState {
    uint32_t win0[4]; // x1,y1,x2,y2 (exclusive)
    uint32_t win1[4];
    uint32_t winIn0, winIn1, winOut, winObj;
};
static_assert(sizeof(WinState) % 4 == 0, "WinState is 32-bit aligned");

// 3) Color math + mosaic registers (packed as 4 u32 = 16 bytes)
struct FxRegs {
    uint32_t bldcnt;   // BLDCNT
    uint32_t bldalpha; // EVA | (EVB<<8)
    uint32_t bldy;     // brightness factor (for brighten/darken)
    uint32_t mosaic;   // BG/OBJ mosaic params
};
static_assert(sizeof(FxRegs) == 16, "FxRegs must be 16 bytes");

// 4) Per-scanline overrides (80 bytes/line = 160*80 total, matching your allocation)
// flags bit 0 = scroll override enabled; window x1/x2 wired for WIN0 slit if needed.
struct Scanline {
    uint32_t hofs[4], vofs[4];    // 8*4 = 32 bytes
    uint32_t win0x1, win0x2, _p0, _p1; // 16 bytes
    uint32_t win1x1, win1x2, _p2, _p3; // 16 bytes
    uint32_t bldcnt, bldalpha, bldy, flags; // 16 bytes
};
static_assert(sizeof(Scanline) == 80, "Scanline must be 80 bytes");

// 5) Affine params for BGx (6 int32 each = 24 bytes)
struct AffineParam { int32_t refX, refY, pa, pb, pc, pd; };
static_assert(sizeof(AffineParam) == 24, "AffineParam must be 24 bytes");

// 6) OBJ affine set (4 int32 = 16 bytes)
struct ObjAff { int32_t pa, pb, pc, pd; };
static_assert(sizeof(ObjAff) == 16, "ObjAff must be 16 bytes");

// --------------------------- Aggregated host state -------------------------------------
struct AgbHwState {
    // Byte-addressable storages (caller writes native GBA-style bytes)
    std::array<uint8_t, AGB_VRAM_SIZE>     vram{};     // BG/OBJ char + screen blocks
    std::array<uint8_t, AGB_PAL_BG_SIZE>   pal_bg{};   // BG palettes (BGR555 as little-endian bytes)
    std::array<uint8_t, AGB_PAL_OBJ_SIZE>  pal_obj{};  // OBJ palettes (index 0 = transparent)
    std::array<uint8_t, AGB_OAM_SIZE>      oam{};      // OAM entries (little-endian 16-bit fields)

    // Registers / structured state
    std::array<BGParam, AGB_BG_COUNT>      bg_params{};        // 4 * 8 u32
    WinState                               win{};              // WIN* + masks
    FxRegs                                 fx{};               // BLDCNT/BLDALPHA/BLDY + MOSAIC
    std::array<Scanline, AGB_SCANLINES>    scan{};             // 160 lines @ 80 bytes
    std::array<AffineParam, AGB_BG_AFF_COUNT> bgAff{};         // BG0..BG3 affine
    std::array<ObjAff, AGB_OBJ_AFF_COUNT>  objAff{};           // 32 OBJ affine sets
};

// --------------------------- Bridge API -------------------------------------------------
// Build the exact demo scene you had in hello_frame (tiles, maps, palettes, OAM,
// windows, color math, per-line scroll, BG2 affine), all in host memory.
// No Vulkan calls here; purely populates AgbHwState.
void agb_init_hw(AgbHwState& hw);

// Copy host state into the renderer's 11 SSBOs using agb_vk.* upload calls
// (VRAM/palettes/OAM are expanded to "uint-per-byte" internally).
void agb_sync_to_renderer(const AgbHwState& hw, AgbVkCtx* ctx);

