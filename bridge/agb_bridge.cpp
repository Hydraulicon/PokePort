#include "agb_bridge.h"
#include "agb_vk.h"

#include <cmath>
#include <cstring>

// --- small helpers (host-side, Vulkan-free) ---------------------------------

static inline void put16LE(uint8_t* dstBytes, size_t byteOffset, uint16_t v) {
    dstBytes[byteOffset + 0] = static_cast<uint8_t>(v & 0xFF);
    dstBytes[byteOffset + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

// Write a 4bpp tile row byte (two nibbles) into VRAM at a byte offset.
static inline void putByte(uint8_t* vram, size_t byteOffset, uint8_t b) {
    vram[byteOffset] = b;
}

// Fixed-point 8.8 converter used for affine params
static inline int32_t fx8(float f) {
    return static_cast<int32_t>(std::lround(f * 256.0f));
}

// ---------------------------------------------------------------------------

void agb_init_hw(AgbHwState* hw) {
    if (!hw) return;

    std::memset(hw, 0, sizeof(*hw));

    // --- VRAM layout (byte offsets), matching the original sample -----------
    const uint32_t charBase0 = 0;
    const uint32_t charBase1 = 8 * 1024;
    const uint32_t charBase2 = 16 * 1024;
    const uint32_t charBase3 = 24 * 1024;
    const uint32_t screenBase0 = 64 * 1024;
    const uint32_t screenBase1 = 72 * 1024;
    const uint32_t screenBase2 = 80 * 1024;
    const uint32_t screenBase3 = 88 * 1024; // reserved
    const uint32_t objCharBase = 32 * 1024; // OBJ tiles region (push-constant in dispatch)  :contentReference[oaicite:1]{index=1}

    const uint32_t mapWidth = 32;
    const uint32_t mapHeight = 32;

    // Scroll baselines (parity with the original program)
    const uint32_t hofs0 = 12, vofs0 = 7;   // BG0
    const uint32_t hofs1 = 100, vofs1 = 32;  // BG1  :contentReference[oaicite:2]{index=2}

    // Initialize BG parameters
    hw->bg_params[0] = { charBase0, screenBase0, hofs0, vofs0, 2, 1, 0, 0 };  // BG0: priority=2, enabled=1
    hw->bg_params[1] = { charBase1, screenBase1, hofs1, vofs1, 1, 1, AGB_BG_FLAG_MOSAIC, 0 };  // BG1: priority=1, enabled=1, mosaic
    hw->bg_params[2] = { charBase2, screenBase2, 0, 0, 1, 1, AGB_BG_FLAG_AFFINE | AGB_BG_FLAG_WRAP, 0 };  // BG2: affine+wrap
    hw->bg_params[3] = { charBase3, screenBase3, 0, 0, 3, 0, 0, 0 };  // BG3: disabled

    // --- BG0 4bpp tiles: tile0 nibble=1, tile1 nibble=2 ---------------------
    {
        for (uint32_t row = 0; row < 8; ++row) {
            for (uint32_t col = 0; col < 4; ++col) {
                putByte(hw->vram, charBase0 + row * 4 + col, 0x11); // tile 0
            }
        }
        const uint32_t tile1 = charBase0 + 32; // 4bpp tile size = 32 bytes
        for (uint32_t row = 0; row < 8; ++row) {
            for (uint32_t col = 0; col < 4; ++col) {
                putByte(hw->vram, tile1 + row * 4 + col, 0x22);     // tile 1
            }
        }
    }

    // --- BG1 4bpp tiles: tile0 nibble=3 (red), tile1 nibble=0 (transparent) -
    {
        for (uint32_t row = 0; row < 8; ++row) {
            for (uint32_t col = 0; col < 4; ++col) {
                putByte(hw->vram, charBase1 + row * 4 + col, 0x33); // red
            }
        }
        const uint32_t tile1 = charBase1 + 32;
        for (uint32_t row = 0; row < 8; ++row) {
            for (uint32_t col = 0; col < 4; ++col) {
                putByte(hw->vram, tile1 + row * 4 + col, 0x00);     // transparent
            }
        }
    }

    // --- BG2 8bpp tile #0: coarse checker with indices {1,4} ----------------
    {
        for (uint32_t y = 0; y < 8; ++y) {
            for (uint32_t x = 0; x < 8; ++x) {
                bool blk = ((y / 2) ^ (x / 2)) & 1;
                hw->vram[charBase2 + y * 8 + x] = static_cast<uint8_t>(blk ? 1 : 4);
            }
        }
    }

    // --- OBJ char: a few tiles for tests (we'll use only 4bpp tile 0 here) ---
    {
        // 4bpp tiles 0..3 -> nibble 1 (magenta when pal idx 1 is magenta)
        for (uint32_t t = 0; t < 4; ++t) {
            uint32_t base = objCharBase + t * 32u; // 32 bytes per 4bpp tile
            for (uint32_t row = 0; row < 8; ++row)
                for (uint32_t col = 0; col < 4; ++col)
                    hw->vram[base + row * 4 + col] = 0x11;
        }
        // 8bpp tiles 16..19 -> value 2 (cyan) if you enable an 8bpp OBJ later
        const uint32_t baseTile = 16;
        for (uint32_t t = 0; t < 4; ++t) {
            uint32_t base = objCharBase + (baseTile + t) * 64u; // 64 bytes per 8bpp tile
            for (uint32_t row = 0; row < 8; ++row)
                for (uint32_t col = 0; col < 8; ++col)
                    hw->vram[base + row * 8 + col] = 2;
        }
    }

    // --- BG0 screenblock: 32x32 checker, toggling pal bank 0/1 ---------------
    {
        for (uint32_t ty = 0; ty < mapHeight; ++ty) {
            for (uint32_t tx = 0; tx < mapWidth; ++tx) {
                uint16_t tile = ((tx + ty) & 1) ? 1 : 0;
                uint16_t palBank = (tx & 1);
                uint16_t attrs = tile | (palBank << 12);
                size_t off = screenBase0 + 2u * (ty * mapWidth + tx);
                put16LE(hw->vram, off, attrs);
            }
        }
    }

    // --- BG1 screenblock: transparent (tile 1) except a 10x10 red patch ------
    {
        for (uint32_t ty = 0; ty < mapHeight; ++ty) {
            for (uint32_t tx = 0; tx < mapWidth; ++tx) {
                uint16_t attrs = 1; // tile 1 = fully transparent tile
                size_t off = screenBase1 + 2u * (ty * mapWidth + tx);
                put16LE(hw->vram, off, attrs);
            }
        }
        const uint32_t startTx = 10, startTy = 5;
        for (uint32_t ty = 0; ty < 10; ++ty)
            for (uint32_t tx = 0; tx < 10; ++tx) {
                uint16_t attrs = 0; // tile 0 (red)
                if (tx & 1) attrs |= (1u << 10); // HFLIP
                if (ty & 1) attrs |= (1u << 11); // VFLIP
                size_t off = screenBase1 + 2u * ((startTy + ty) * mapWidth + (startTx + tx));
                put16LE(hw->vram, off, attrs);
            }
    }

    // --- BG2 affine map: one byte per entry -> fill with tile 0 ---------------
    {
        for (uint32_t ty = 0; ty < mapHeight; ++ty)
            for (uint32_t tx = 0; tx < mapWidth; ++tx)
                hw->vram[screenBase2 + ty * mapWidth + tx] = 0;
        (void)screenBase3; // reserved
    }

    // --- Palettes (BGR555) ---------------------------------------------------
    auto setBGPal = [&](uint32_t idx, uint16_t bgr) { put16LE(hw->pal_bg, idx * 2, bgr); };
    auto setOBJPal = [&](uint32_t idx, uint16_t bgr) { put16LE(hw->pal_obj, idx * 2, bgr); };

    setBGPal(0, 0x4210);       // backdrop gray
    setBGPal(1, 0x0000);       // bank0 idx1 = black
    setBGPal(2, 0x7FFF);       // bank0 idx2 = white
    setBGPal(3, 0x001F);       // bank0 idx3 = red
    setBGPal(4, 0x03FF);       // bank0 idx4 = yellow
    setBGPal(16 + 1, 0x03E0);    // bank1 idx1 = green
    setBGPal(16 + 2, 0x7C00);    // bank1 idx2 = blue
    setOBJPal(0, 0x0000);      // OBJ idx0 transparent
    setOBJPal(1, 0x7C1F);      // OBJ idx1 magenta
    setOBJPal(2, 0x7FE0);      // OBJ idx2 cyan  :contentReference[oaicite:3]{index=3}

    // --- One simple OBJ (16x16, 4bpp, magenta) to match the early milestone --
    {
        auto w16 = [&](size_t off, uint16_t v) { put16LE(hw->oam, off, v); };

        // Hide all first
        for (uint32_t i = 0; i < 128; ++i) w16(i * 8 + 0, 0x0200); // attr0 hidden

        uint16_t y = 12, x = 12;
        uint16_t attr0 = (y & 0x00FF) | /*normal*/0 | /*4bpp*/0 | /*square*/(0 << 14);
        uint16_t attr1 = (x & 0x01FF) | /*size=1 (16x16)*/(1 << 14);
        uint16_t attr2 = /*tileIndex*/0 | /*pri=1*/(1 << 10) | /*palBank*/0;
        w16(0, attr0); w16(2, attr1); w16(4, attr2);
    }

    // Entry 1: OBJ-window (from lines 364-386 in main.cpp)
    {
        auto w16 = [&](size_t off, uint16_t v) { put16LE(hw->oam, off, v); };

        uint16_t y = 18;
        uint16_t x = 18;
        uint16_t shape_square = 0 << 14;
        uint16_t four_bpp = 0 << 13;
        uint16_t objModeWin = 2 << 10; // OBJ-window mode
        uint16_t affine_off = 0 << 8;
        uint16_t attr0 = (y & 0x00FF) | affine_off | objModeWin | four_bpp | shape_square;

        uint16_t size_16 = 1 << 14;
        uint16_t attr1 = (x & 0x01FF) | size_16;

        uint16_t tileIndex = 0;
        uint16_t objPri = 1 << 10;
        uint16_t palBank = 0 << 12;
        uint16_t attr2 = tileIndex | objPri | palBank;

        w16(8 + 0, attr0); w16(8 + 2, attr1); w16(8 + 4, attr2);
    }

    // Entry 2: 8bpp cyan sprite with affine + mosaic (from lines 389-416)
    {
        auto w16 = [&](size_t off, uint16_t v) { put16LE(hw->oam, off, v); };

        uint16_t y = 24;
        uint16_t x = 44;
        uint16_t shape_square = 0 << 14;
        uint16_t color_8bpp = 1 << 13;
        uint16_t affine_on = 1 << 8;
        uint16_t double_size = 1 << 9;
        uint16_t objSemi = 1 << 10;
        uint16_t objMosaic = 1 << 12;

        uint16_t attr0 = (y & 0x00FF) | affine_on | double_size | color_8bpp | objMosaic | objSemi | shape_square;

        uint16_t size_16 = 1 << 14;
        uint16_t affIndex = 0;
        uint16_t attr1 = (x & 0x01FF) | size_16 | (affIndex << 9);

        uint16_t baseTile = 16;
        uint16_t objPri = 1 << 10;
        uint16_t attr2 = baseTile | objPri;

        w16(16 + 0, attr0); w16(16 + 2, attr1); w16(16 + 4, attr2);
    }

    // Entry 3: 32x16 wide sprite (from lines 418-429)
    {
        auto w16 = [&](size_t off, uint16_t v) { put16LE(hw->oam, off, v); };

        uint16_t y = 40, x = 24;
        uint16_t shape_wide = 1u << 14;
        uint16_t size_32x16 = 1u << 14;
        uint16_t attr0 = (y & 0xFF) | shape_wide;
        uint16_t attr1 = (x & 0x1FF) | size_32x16;
        uint16_t attr2 = 0 | (1u << 10);

        w16(24 + 0, attr0); w16(24 + 2, attr1); w16(24 + 4, attr2);
    }

    // --- Window rectangles + masks (WIN0 brighten box over BG1) --------------
    {
        hw->win.win0[0] = 8;  hw->win.win0[1] = 8;  hw->win.win0[2] = 112; hw->win.win0[3] = 56;
        hw->win.win1[0] = 0;  hw->win.win1[1] = 0;  hw->win.win1[2] = 0;   hw->win.win1[3] = 0;

        // bit: 0=BG0,1=BG1,2=BG2,3=BG3,4=OBJ,5=ColorEffect
        hw->win.winIn0 = (1u << 0) | (1u << 1) | (1u << 4) | (1u << 5);
        hw->win.winIn1 = 0u;
        hw->win.winOut = 0x1Fu;                   // BG0..BG3 + OBJ outside, no ColorEffect
        hw->win.winObj = (1u << 0) | (1u << 5);       // OBJ-window allows BG0 + ColorEffect
    }

    // --- Color math & mosaic (global regs) -----------------------------------
    {
        hw->fx.bldcnt = (1u << 1) | (2u << 6);   // A-target=BG1, mode=brighten
        hw->fx.bldalpha = (8u) | (8u << 8);      // not used by brighten, harmless
        hw->fx.bldy = 8u;                  // brightness strength
        uint32_t bgH = 3, bgV = 3, objH = 3, objV = 3;  // 4x4 mosaic if enabled
        hw->fx.mosaic = (bgH & 0xF) | ((bgV & 0xF) << 4) | ((objH & 0xF) << 8) | ((objV & 0xF) << 12);
    }

    // --- Per-scanline: only BG0 small sine X scroll overrides ----------------
    for (int y = 0; y < static_cast<int>(AGB_SCANLINES); ++y) {
        float phase = float(y) * 3.14159265f / 16.0f;
        hw->scan[y].hofs[0] = hofs0 + int(4.0f * std::sin(phase));
        hw->scan[y].vofs[0] = vofs0;

        hw->scan[y].hofs[1] = hofs1;  hw->scan[y].vofs[1] = vofs1;
        hw->scan[y].hofs[2] = 0;      hw->scan[y].vofs[2] = 0;
        hw->scan[y].hofs[3] = 0;      hw->scan[y].vofs[3] = 0;

        hw->scan[y].win0x1 = 8; hw->scan[y].win0x2 = 112;
        hw->scan[y].win1x1 = 0; hw->scan[y].win1x2 = 0;

        // Let global FX stand (we only override scroll here)
        hw->scan[y].bldcnt = 0; hw->scan[y].bldalpha = 0; hw->scan[y].bldy = 0;
        hw->scan[y].flags = 1u; // bit0: scroll override enabled
    }

    // --- BG2 affine params: rotate 30 deg at 0.75 scale, centered ----------------
    {
        float deg = 30.f, scale = 0.75f;
        float rad = deg * 3.14159265358979323846f / 180.f;
        float cs = std::cos(rad) * scale, sn = std::sin(rad) * scale;

        int32_t pa = fx8(cs), pb = fx8(-sn), pc = fx8(sn), pd = fx8(cs);
        int x0 = 120, y0 = 80;                               // screen pivot
        int u0 = 32 * 8 / 2, v0 = 32 * 8 / 2;                        // map center
        int32_t refX = (u0 << 8) - pa * x0 - pb * y0;
        int32_t refY = (v0 << 8) - pc * x0 - pd * y0;

        hw->bgAff[2] = { refX, refY, pa, pb, pc, pd };    // BG2 only
    }

    // OBJ affine set 0 should be rotated
    {
        float degO = 30.0f, scaleO = 1.0f;
        float radO = degO * 3.14159265358979323846f / 180.0f;
        float csO = std::cos(radO) * scaleO;
        float snO = std::sin(radO) * scaleO;
        hw->objAff[0] = { fx8(csO), fx8(-snO), fx8(snO), fx8(csO) };
    }
}

// Copy host state into the renderer's SSBOs (descriptor order: 1..10)
void agb_sync_to_renderer(const AgbHwState* hw, AgbVkCtx* ctx) {
    if (!hw || !ctx) return;

    // 1) VRAM / 2) PAL BG / 3) BG params / 4) PAL OBJ / 5) OAM
    agbvk_upload_vram(ctx, hw->vram, AGB_VRAM_SIZE);
    agbvk_upload_pal_bg(ctx, hw->pal_bg, AGB_PAL_BG_SIZE);
    agbvk_upload_bg_params(ctx, reinterpret_cast<const uint32_t*>(hw->bg_params),
        AGB_BG_COUNT * AGB_BG_PARAM_DWORDS);
    agbvk_upload_pal_obj(ctx, hw->pal_obj, AGB_PAL_OBJ_SIZE);
    agbvk_upload_oam(ctx, hw->oam, AGB_OAM_SIZE);

    // 6) WIN / 7) FX / 8) Scanline overrides
    agbvk_upload_win(ctx, &hw->win, sizeof(hw->win));
    agbvk_upload_fx(ctx, &hw->fx, sizeof(hw->fx));
    agbvk_upload_scanline(ctx, hw->scan, AGB_SCANLINES * sizeof(Scanline));

    // 9) BG affine / 10) OBJ affine
    agbvk_upload_bg_aff(ctx, reinterpret_cast<const int32_t*>(hw->bgAff),
        AGB_BG_AFF_COUNT * 6);
    agbvk_upload_obj_aff(ctx, reinterpret_cast<const int32_t*>(hw->objAff),
        AGB_OBJ_AFF_COUNT * 4);
}
