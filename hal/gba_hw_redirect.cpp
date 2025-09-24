// gba_hw_redirect.cpp
#include "gba_port.h"
#include "gba_hw_redirect.h"
#include <cstring>

// The redirect header exports VRAM/PLTT/OAM macros so C code can keep the
// handheld-style names.  Those collide with the C++ mirror objects defined in
// gba:: (e.g. gba::VRAM), so immediately drop the macros in this translation
// unit after we get the register offset constants we need.
#if defined(__cplusplus)
#  undef VRAM
#  undef PLTT
#  undef BG_PLTT
#  undef OBJ_PLTT
#  undef OAM
#endif

// Create a mock I/O register space
static uint16_t io_registers[0x200]; // 0x400 bytes / 2

extern "C" {

    // Return base of our mock I/O register array
    volatile uint16_t* gba_io_base(void) {
        return io_registers;
    }

    // Direct memory region access
    uint8_t* gba_vram_base(void) {
        return gba::VRAM.data();
    }

    uint8_t* gba_oam_base(void) {
        return gba::OAM.data();
    }

    uint16_t* gba_bg_palette(void) {
        return reinterpret_cast<uint16_t*>(gba::PAL_BG.data());
    }

    uint16_t* gba_obj_palette(void) {
        return reinterpret_cast<uint16_t*>(gba::PAL_OBJ.data());
    }

    // DMA emulation
    void DmaCopy16(uint32_t channel, const void* src, void* dst, uint32_t halfwords) {
        (void)channel; // We don't need channel info for simple copying
        std::memcpy(dst, src, halfwords * 2);
    }

    void DmaCopy32(uint32_t channel, const void* src, void* dst, uint32_t words) {
        (void)channel;
        std::memcpy(dst, src, words * 4);
    }

    void DmaFill16(uint16_t value, void* dst, uint32_t halfwords) {
        auto* p = static_cast<uint16_t*>(dst);
        for (uint32_t i = 0; i < halfwords; i++) {
            p[i] = value;
        }
    }

    void DmaFill32(uint32_t value, void* dst, uint32_t words) {
        auto* p = static_cast<uint32_t*>(dst);
        for (uint32_t i = 0; i < words; i++) {
            p[i] = value;
        }
    }

} // extern "C"

// Synchronization hook - call this before rendering
void sync_io_to_gba_state() {
    // Copy I/O registers to gba::REG structure
    gba::REG.DISPCNT = io_registers[OFFSET_REG_DISPCNT / 2];

    for (int i = 0; i < 4; i++) {
        gba::REG.BG_CNT[i] = io_registers[(OFFSET_REG_BG0CNT + i * 2) / 2];
        gba::REG.BG_HOFS[i] = io_registers[(OFFSET_REG_BG0HOFS + i * 4) / 2];
        gba::REG.BG_VOFS[i] = io_registers[(OFFSET_REG_BG0VOFS + i * 4) / 2];
    }

    // Windows
    uint16_t win0h = io_registers[OFFSET_REG_WIN0H / 2];
    uint16_t win0v = io_registers[OFFSET_REG_WIN0V / 2];
    gba::REG.WIN0H_x1 = win0h & 0xFF;
    gba::REG.WIN0H_x2 = (win0h >> 8) & 0xFF;
    gba::REG.WIN0V_y1 = win0v & 0xFF;
    gba::REG.WIN0V_y2 = (win0v >> 8) & 0xFF;

    gba::REG.WININ = io_registers[OFFSET_REG_WININ / 2];
    gba::REG.WINOUT = io_registers[OFFSET_REG_WINOUT / 2];

    // Color effects
    gba::REG.BLDCNT = io_registers[OFFSET_REG_BLDCNT / 2];
    gba::REG.BLDALPHA = io_registers[OFFSET_REG_BLDALPHA / 2];
    gba::REG.BLDY = io_registers[OFFSET_REG_BLDY / 2] & 0xFF;
    gba::REG.MOSAIC = io_registers[OFFSET_REG_MOSAIC / 2];

    // Affine params
    gba::REG.BG2PA = *reinterpret_cast<int16_t*>(&io_registers[OFFSET_REG_BG2PA / 2]);
    gba::REG.BG2PB = *reinterpret_cast<int16_t*>(&io_registers[OFFSET_REG_BG2PB / 2]);
    gba::REG.BG2PC = *reinterpret_cast<int16_t*>(&io_registers[OFFSET_REG_BG2PC / 2]);
    gba::REG.BG2PD = *reinterpret_cast<int16_t*>(&io_registers[OFFSET_REG_BG2PD / 2]);
    gba::REG.BG2X = *reinterpret_cast<int32_t*>(&io_registers[OFFSET_REG_BG2X / 2]);
    gba::REG.BG2Y = *reinterpret_cast<int32_t*>(&io_registers[OFFSET_REG_BG2Y / 2]);
}