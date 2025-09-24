#pragma once
// PC-side GBA surface (header-only). Minimal enough for early Emerald modules.

#include <cstdint>
#include <cstddef>
#include <array>
#include <cstring>
#include <cmath>

// PokePort types
#include "agb_bridge.h"   // AgbHwState + BGParam/WinState/FxRegs/...  (SSBO ABI)
                                 // Keep this ABI in lock-step with the renderer.  // :contentReference[oaicite:2]{index=2}

namespace gba {

    // ------------------- Device memory mirrors (byte-accurate) -------------------
    inline std::array<uint8_t, AGB_VRAM_SIZE>   VRAM{}; // 96 KB
    inline std::array<uint8_t, AGB_PAL_BG_SIZE> PAL_BG{}; // 1 KB (BGR555 little-endian bytes)
    inline std::array<uint8_t, AGB_PAL_OBJ_SIZE>PAL_OBJ{}; // 512 B
    inline std::array<uint8_t, AGB_OAM_SIZE>    OAM{}; // 1 KB

    // ------------------- Registers (minimal set for first scenes) ----------------
    struct Regs {
        // Display control (only fields we use now)
        uint16_t DISPCNT = 0; // BG enables, OBJ enable, OBJ map mode, windows, mode

        // BG control for BG0..BG3 (text/affine)
        uint16_t BG_CNT[4] = { 0,0,0,0 }; // priority/charBase/screenBase/mosaic/wrap/size

        // Text BG scroll
        uint16_t BG_HOFS[4] = { 0,0,0,0 };
        uint16_t BG_VOFS[4] = { 0,0,0,0 };

        // Windows
        uint8_t  WIN0H_x1 = 0, WIN0H_x2 = 0;
        uint8_t  WIN0V_y1 = 0, WIN0V_y2 = 0;
        uint8_t  WIN1H_x1 = 0, WIN1H_x2 = 0;
        uint8_t  WIN1V_y1 = 0, WIN1V_y2 = 0;
        uint16_t WININ = 0;  // masks
        uint16_t WINOUT = 0;  // outside/objwin masks

        // Color effects
        uint16_t BLDCNT = 0;
        uint16_t BLDALPHA = 0;
        uint8_t  BLDY = 0;

        // Mosaic
        uint16_t MOSAIC = 0; // BG/OBJ mosaic params

        // Affine BG2/BG3
        int16_t  BG2PA = 256, BG2PB = 0, BG2PC = 0, BG2PD = 256;
        int32_t  BG2X = 0, BG2Y = 0;   // 28.8 fixed in HW; we’ll downshift to 8.8 for the shader
        int16_t  BG3PA = 256, BG3PB = 0, BG3PC = 0, BG3PD = 256;
        int32_t  BG3X = 0, BG3Y = 0;

        // OBJ affine sets (32) — 8.8 fixed like our shader uses
        struct ObjAff { int16_t pa = 256, pb = 0, pc = 0, pd = 256; };
        std::array<ObjAff, 32> OBJ_AFF{};
    };
    inline Regs REG{};

    // ------------------- Small MMIO-like helpers used by decomp code -------------
    inline void DmaCopy16(const void* src, void* dst, size_t halfwords) {
        std::memcpy(dst, src, halfwords * 2);
    }
    inline void DmaCopy32(const void* src, void* dst, size_t words) {
        std::memcpy(dst, src, words * 4);
    }
    inline void DmaFill16(uint16_t value, void* dst, size_t halfwords) {
        auto* p = static_cast<uint16_t*>(dst);
        for (size_t i = 0; i < halfwords; i++) p[i] = value;
    }
    inline void DmaFill32(uint32_t value, void* dst, size_t words) {
        auto* p = static_cast<uint32_t*>(dst);
        for (size_t i = 0; i < words; i++) p[i] = value;
    }

    // Minimal “GPU reg” interface commonly used by projects like pokeemerald.
    // Offsets follow the decomp’s REG_OFFSET_* constants.
    inline void SetGpuReg(uint16_t /*offset*/, uint16_t /*val*/) {
        // We’ll populate per-site shims as we import each module.
        // For early bringup we prefer writing directly to gba::REG fields.
    }

    // ------------------- Snapshot HAL → AgbHwState (renderer ABI) ----------------
    inline int32_t fx8(float f) { return int32_t(std::lround(f * 256.0f)); }

    inline void snapshot_to(AgbHwState& hw) {
        // 1) copy raw memories (host → SSBO byte streams)
        std::memcpy(hw.vram, VRAM.data(), VRAM.size());
        std::memcpy(hw.pal_bg, PAL_BG.data(), PAL_BG.size());
        std::memcpy(hw.pal_obj, PAL_OBJ.data(), PAL_OBJ.size());
        std::memcpy(hw.oam, OAM.data(), OAM.size());

        // 2) BG params (charBase/screenBase in BYTES; priority; enabled; flags)
        auto charBaseBytes = [](uint16_t bgcnt)->uint32_t {
            // Spec: 2 bits (2..3) select block in 16 KB units (for BG charblocks).
            return ((bgcnt >> 2) & 0x3u) * 16u * 1024u;
            };
        auto screenBaseBytes = [](uint16_t bgcnt)->uint32_t {
            // Spec: 5 bits (8..12) select block in 2 KB units.
            return ((bgcnt >> 8) & 0x1Fu) * 2u * 1024u;
            };
        auto pri = [](uint16_t bgcnt)->uint32_t { return (bgcnt & 3u); };
        auto mosaicFlag = [](uint16_t bgcnt)->uint32_t { return (bgcnt & (1u << 6)) ? AGB_BG_FLAG_MOSAIC : 0u; };

        for (int i = 0; i < 4; i++) {
            uint32_t flags = mosaicFlag(REG.BG_CNT[i]);
            // Treat BG2/3 as affine if their PA/PD aren’t identity or if DISPCNT selects affine mode later.
            if (i >= 2) flags |= AGB_BG_FLAG_AFFINE; // safe default; refine as needed with DISPCNT.
            hw.bg_params[i] = {
                charBaseBytes(REG.BG_CNT[i]),
                screenBaseBytes(REG.BG_CNT[i]),
                REG.BG_HOFS[i],
                REG.BG_VOFS[i],
                pri(REG.BG_CNT[i]),
                /*enabled*/1u,
                flags,
                0u
            };
        }

        // 3) Windows
        hw.win.win0[0] = REG.WIN0H_x1; hw.win.win0[1] = REG.WIN0V_y1;
        hw.win.win0[2] = REG.WIN0H_x2; hw.win.win0[3] = REG.WIN0V_y2;
        hw.win.win1[0] = REG.WIN1H_x1; hw.win.win1[1] = REG.WIN1V_y1;
        hw.win.win1[2] = REG.WIN1H_x2; hw.win.win1[3] = REG.WIN1V_y2;
        hw.win.winIn0 = REG.WININ & 0x3F;
        hw.win.winIn1 = (REG.WININ >> 8) & 0x3F;
        hw.win.winOut = REG.WINOUT & 0x3F;
        hw.win.winObj = (REG.WINOUT >> 8) & 0x3F;

        // 4) Color math + mosaic
        hw.fx.bldcnt = REG.BLDCNT;
        hw.fx.bldalpha = REG.BLDALPHA;
        hw.fx.bldy = REG.BLDY;
        hw.fx.mosaic = REG.MOSAIC;

        // 5) Per-scanline: none yet (engine will populate when we hook HBlank effects)
        std::memset(hw.scan, 0, sizeof(hw.scan));

        // 6) BG affine (our shader expects 8.8; HW BGxX/Y are 28.8; BGxPA.. are 8.8 already)
        auto packBG = [&](int idx, int32_t X, int32_t Y, int16_t pa, int16_t pb, int16_t pc, int16_t pd) {
            hw.bgAff[idx].refX = (X >> 20); // 28.8 → 8.8 = shift by 20
            hw.bgAff[idx].refY = (Y >> 20);
            hw.bgAff[idx].pa = pa; hw.bgAff[idx].pb = pb; hw.bgAff[idx].pc = pc; hw.bgAff[idx].pd = pd;
            };
        packBG(2, REG.BG2X, REG.BG2Y, REG.BG2PA, REG.BG2PB, REG.BG2PC, REG.BG2PD);
        packBG(3, REG.BG3X, REG.BG3Y, REG.BG3PA, REG.BG3PB, REG.BG3PC, REG.BG3PD);

        // 7) OBJ affine sets
        for (size_t i = 0; i < AGB_OBJ_AFF_COUNT; ++i) {
            hw.objAff[i].pa = REG.OBJ_AFF[i].pa;
            hw.objAff[i].pb = REG.OBJ_AFF[i].pb;
            hw.objAff[i].pc = REG.OBJ_AFF[i].pc;
            hw.objAff[i].pd = REG.OBJ_AFF[i].pd;
        }
    }

} // namespace gba
