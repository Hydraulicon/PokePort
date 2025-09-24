// gba_hw_redirect.h - Pokemon Emerald includes this instead of gba/io_reg.h
#ifndef GBA_HW_REDIRECT_H
#define GBA_HW_REDIRECT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

	// Forward declare our redirect functions
	extern volatile uint16_t* gba_io_base(void);
	extern uint8_t* gba_vram_base(void);
	extern uint8_t* gba_oam_base(void);
	extern uint16_t* gba_bg_palette(void);
	extern uint16_t* gba_obj_palette(void);

	// Base addresses (matching Pokemon Emerald's expectations)
#define REG_BASE        0x04000000
#define PLTT            0x05000000
#define VRAM            0x06000000
#define OAM             0x07000000

// Palette subdivisions
#define BG_PLTT         PLTT
#define OBJ_PLTT        (PLTT + 0x200)

// I/O Register offsets (from pokeemerald's gba_constants.inc)
#define OFFSET_REG_DISPCNT      0x000
#define OFFSET_REG_DISPSTAT     0x004
#define OFFSET_REG_VCOUNT       0x006
#define OFFSET_REG_BG0CNT       0x008
#define OFFSET_REG_BG1CNT       0x00A
#define OFFSET_REG_BG2CNT       0x00C
#define OFFSET_REG_BG3CNT       0x00E
#define OFFSET_REG_BG0HOFS      0x010
#define OFFSET_REG_BG0VOFS      0x012
#define OFFSET_REG_BG1HOFS      0x014
#define OFFSET_REG_BG1VOFS      0x016
#define OFFSET_REG_BG2HOFS      0x018
#define OFFSET_REG_BG2VOFS      0x01A
#define OFFSET_REG_BG3HOFS      0x01C
#define OFFSET_REG_BG3VOFS      0x01E
#define OFFSET_REG_WIN0H        0x040
#define OFFSET_REG_WIN1H        0x042
#define OFFSET_REG_WIN0V        0x044
#define OFFSET_REG_WIN1V        0x046
#define OFFSET_REG_WININ        0x048
#define OFFSET_REG_WINOUT       0x04A
#define OFFSET_REG_MOSAIC       0x04C
#define OFFSET_REG_BLDCNT       0x050
#define OFFSET_REG_BLDALPHA     0x052
#define OFFSET_REG_BLDY         0x054

// Affine BG registers
#define OFFSET_REG_BG2PA        0x020
#define OFFSET_REG_BG2PB        0x022
#define OFFSET_REG_BG2PC        0x024
#define OFFSET_REG_BG2PD        0x026
#define OFFSET_REG_BG2X         0x028
#define OFFSET_REG_BG2Y         0x02C
#define OFFSET_REG_BG3PA        0x030
#define OFFSET_REG_BG3PB        0x032
#define OFFSET_REG_BG3PC        0x034
#define OFFSET_REG_BG3PD        0x036
#define OFFSET_REG_BG3X         0x038
#define OFFSET_REG_BG3Y         0x03C

// Redirect I/O registers to our functions
#define REG_ADDR(offset)        (gba_io_base() + ((offset) / 2))

#define REG_DISPCNT             (*REG_ADDR(OFFSET_REG_DISPCNT))
#define REG_DISPSTAT            (*REG_ADDR(OFFSET_REG_DISPSTAT))
#define REG_VCOUNT              (*REG_ADDR(OFFSET_REG_VCOUNT))
#define REG_BG0CNT              (*REG_ADDR(OFFSET_REG_BG0CNT))
#define REG_BG1CNT              (*REG_ADDR(OFFSET_REG_BG1CNT))
#define REG_BG2CNT              (*REG_ADDR(OFFSET_REG_BG2CNT))
#define REG_BG3CNT              (*REG_ADDR(OFFSET_REG_BG3CNT))
#define REG_BG0HOFS             (*REG_ADDR(OFFSET_REG_BG0HOFS))
#define REG_BG0VOFS             (*REG_ADDR(OFFSET_REG_BG0VOFS))
#define REG_BG1HOFS             (*REG_ADDR(OFFSET_REG_BG1HOFS))
#define REG_BG1VOFS             (*REG_ADDR(OFFSET_REG_BG1VOFS))
#define REG_BG2HOFS             (*REG_ADDR(OFFSET_REG_BG2HOFS))
#define REG_BG2VOFS             (*REG_ADDR(OFFSET_REG_BG2VOFS))
#define REG_BG3HOFS             (*REG_ADDR(OFFSET_REG_BG3HOFS))
#define REG_BG3VOFS             (*REG_ADDR(OFFSET_REG_BG3VOFS))
#define REG_WIN0H               (*REG_ADDR(OFFSET_REG_WIN0H))
#define REG_WIN1H               (*REG_ADDR(OFFSET_REG_WIN1H))
#define REG_WIN0V               (*REG_ADDR(OFFSET_REG_WIN0V))
#define REG_WIN1V               (*REG_ADDR(OFFSET_REG_WIN1V))
#define REG_WININ               (*REG_ADDR(OFFSET_REG_WININ))
#define REG_WINOUT              (*REG_ADDR(OFFSET_REG_WINOUT))
#define REG_MOSAIC              (*REG_ADDR(OFFSET_REG_MOSAIC))
#define REG_BLDCNT              (*REG_ADDR(OFFSET_REG_BLDCNT))
#define REG_BLDALPHA            (*REG_ADDR(OFFSET_REG_BLDALPHA))
#define REG_BLDY                (*REG_ADDR(OFFSET_REG_BLDY))

// Affine registers
#define REG_BG2PA               (*((volatile int16_t*)REG_ADDR(OFFSET_REG_BG2PA)))
#define REG_BG2PB               (*((volatile int16_t*)REG_ADDR(OFFSET_REG_BG2PB)))
#define REG_BG2PC               (*((volatile int16_t*)REG_ADDR(OFFSET_REG_BG2PC)))
#define REG_BG2PD               (*((volatile int16_t*)REG_ADDR(OFFSET_REG_BG2PD)))
#define REG_BG2X                (*((volatile int32_t*)REG_ADDR(OFFSET_REG_BG2X)))
#define REG_BG2Y                (*((volatile int32_t*)REG_ADDR(OFFSET_REG_BG2Y)))

// DMA functions
	void DmaCopy16(uint32_t channel, const void* src, void* dst, uint32_t halfwords);
	void DmaCopy32(uint32_t channel, const void* src, void* dst, uint32_t words);
	void DmaFill16(uint16_t value, void* dst, uint32_t halfwords);
	void DmaFill32(uint32_t value, void* dst, uint32_t words);

	// Pokemon Emerald expects these macros
#define DMA3COPY(src, dst, count) DmaCopy16(3, src, dst, count)
#define DMA3FILL(value, dst, count) DmaFill16(value, dst, count)

#ifdef __cplusplus
}
#endif

#endif