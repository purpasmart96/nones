#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "cpu.h"
#include "mem.h"
#include "ppu.h"

static uint8_t ppu_mem[0x4000];
static uint8_t vram[0x800];
static uint8_t name_table[32 * 30];
static uint8_t oam[64 * 4];

#define CART_ADDR_START 0
#define CART_ADDR_SIZE 0x2000

#define PPU_START_ADDR 0x2000
#define PPU_RAM_SIZE 0x1000

#define MISC_START_ADDR 0x3000
#define MISC_SIZE 0xF00

#define PALETTE_START_ADDR 0x3F00

// NTSC
// Clock devider: * 2 / 12 / 4
static float ppu_freq = 21.47727;

const uint8_t PaletteLUT_2C04_0001[64] =
{
    0x35,0x23,0x16,0x22,0x1C,0x09,0x1D,0x15,0x20,0x00,0x27,0x05,0x04,0x28,0x08,0x20,
    0x21,0x3E,0x1F,0x29,0x3C,0x32,0x36,0x12,0x3F,0x2B,0x2E,0x1E,0x3D,0x2D,0x24,0x01,
    0x0E,0x31,0x33,0x2A,0x2C,0x0C,0x1B,0x14,0x2E,0x07,0x34,0x06,0x13,0x02,0x26,0x2E,
    0x2E,0x19,0x10,0x0A,0x39,0x03,0x37,0x17,0x0F,0x11,0x0B,0x0D,0x38,0x25,0x18,0x3A
};

const uint8_t PaletteLUT_2C04_0002[64] =
{
    0x2E,0x27,0x18,0x39,0x3A,0x25,0x1C,0x31,0x16,0x13,0x38,0x34,0x20,0x23,0x3C,0x0B,
    0x0F,0x21,0x06,0x3D,0x1B,0x29,0x1E,0x22,0x1D,0x24,0x0E,0x2B,0x32,0x08,0x2E,0x03,
    0x04,0x36,0x26,0x33,0x11,0x1F,0x10,0x02,0x14,0x3F,0x00,0x09,0x12,0x2E,0x28,0x20,
    0x3E,0x0D,0x2A,0x17,0x0C,0x01,0x15,0x19,0x2E,0x2C,0x07,0x37,0x35,0x05,0x0A,0x2D
};

const uint8_t PaletteLUT_2C04_0003[64] =
{
    0x14,0x25,0x3A,0x10,0x0B,0x20,0x31,0x09,0x01,0x2E,0x36,0x08,0x15,0x3D,0x3E,0x3C,
    0x22,0x1C,0x05,0x12,0x19,0x18,0x17,0x1B,0x00,0x03,0x2E,0x02,0x16,0x06,0x34,0x35,
    0x23,0x0F,0x0E,0x37,0x0D,0x27,0x26,0x20,0x29,0x04,0x21,0x24,0x11,0x2D,0x2E,0x1F,
    0x2C,0x1E,0x39,0x33,0x07,0x2A,0x28,0x1D,0x0A,0x2E,0x32,0x38,0x13,0x2B,0x3F,0x0C
};

const uint8_t PaletteLUT_2C04_0004[64] =
{
    0x18,0x03,0x1C,0x28,0x2E,0x35,0x01,0x17,0x10,0x1F,0x2A,0x0E,0x36,0x37,0x0B,0x39,
    0x25,0x1E,0x12,0x34,0x2E,0x1D,0x06,0x26,0x3E,0x1B,0x22,0x19,0x04,0x2E,0x3A,0x21,
    0x05,0x0A,0x07,0x02,0x13,0x14,0x00,0x15,0x0C,0x3D,0x11,0x0F,0x0D,0x38,0x2D,0x24,
    0x33,0x20,0x08,0x16,0x3F,0x2B,0x20,0x3C,0x2E,0x27,0x23,0x31,0x29,0x32,0x2C,0x09
};


typedef struct
{
    union {
        uint8_t ctrl;
        struct {
            uint8_t base_name_table_addr : 2;
            uint8_t vram_addr_inc : 1;
            uint8_t sprite_pat_table_addr : 1;
            uint8_t bg_pat_table_addr : 1;
            uint8_t sprite_size : 1;
        };
    };

} PPU;

typedef union
{
    uint8_t raw;
    struct {
        uint8_t base_name_table_addr : 2;
        uint8_t vram_addr_inc : 1;
        uint8_t sprite_pat_table_addr : 1;
        uint8_t bg_pat_table_addr : 1;
        uint8_t sprite_size : 1;
        uint8_t master_slave : 1;
        uint8_t vblank_nmi : 1;
    };

} PPU_CTRL;

typedef union
{
    uint8_t raw;
    struct {
        uint8_t open_bus : 5;
        uint8_t sprite_overflow : 1;
        uint8_t sprite_hit : 1;
        uint8_t vblank : 1;
    };

} PPU_Status;

#define PPU_CTRL 0x2000
#define PPU_MASK 0x2001
#define PPU_STATUS 0x2002

#define PPU_SCROLL 0x2005
#define PPU_ADDR 0x2006

/*
static PPU_CTRL *ppu_ctrl = NULL;
static uint8_t *ppu_mask = NULL;
static uint8_t *ppu_status = NULL;
static uint8_t *oam_addr = NULL;
static uint8_t *oam_data = NULL;
static uint8_t *ppu_scroll = NULL;
static uint8_t *ppu_addr = NULL;
static uint8_t *ppu_data = NULL;
static uint8_t *oam_dma = NULL;
*/

void PPU_Write8(uint16_t addr, uint8_t data)
{
    ppu_mem[addr] = data;
}

void PPU_Init(void)
{
    MemWrite8(PPU_CTRL, 0);
    MemWrite8(PPU_MASK, 0);
    MemWrite8(PPU_STATUS, 0xA0);
    MemWrite8(0x2003, 0);
    MemWrite8(0x2004, 0);
    MemWrite8(PPU_SCROLL, 0);
    MemWrite8(PPU_ADDR, 0);
    MemWrite8(0x2007, 0);
    //MemWrite8(0x2008, 0);
}

uint64_t prev_vblank_cycles = 0;
uint64_t next_vblank_cycles = 27384;
uint64_t ppu_cycles = 0;

const uint32_t dots_per_frame_odd = 341 * 261 + 340;
const uint32_t dots_per_frame_even = 341 * 261 + 341;
const uint32_t cpu_cycles_per_frame = dots_per_frame_even / 3; 

uint64_t ppu_cycle_counter = 0;
bool nmi_triggered = false;

static uint64_t prev_ppu_cycles = 0;
static uint64_t prev_cpu_cycles = 0;
/*
void PPU_Update(uint64_t cycles)
{
    uint64_t ppu_cycles = cycles * 3;
    uint32_t num_dots = dots_per_frame_even;

    //if (ppu_cycles % 2)
    //    num_dots = dots_per_frame_odd;

    if (ppu_cycles >= prev_ppu_cycles + num_dots)
    {        
        // Get the delta (number of ppu cycles that have passed)
        uint64_t ppu_cycles_delta = (ppu_cycles - prev_ppu_cycles);
        // How far off(in ppu cycles) are we?
        uint32_t dsynched_cycles = ppu_cycles_delta - num_dots;
        if (dsynched_cycles <= 6)
        {
            
        }
        prev_ppu_cycles = ppu_cycles;
        ppu_cycle_counter += ppu_cycles;
    }

    // Compute current scanline
    int scanline = (ppu_cycle_counter / 341); // 1 scanline = 341 PPU cycles
    if (scanline == 241) // VBlank starts at scanline 241
    {
        MemWrite8(PPU_STATUS, MemRead8(PPU_STATUS) | 0x80); // Set VBlank bit
        if (MemRead8(PPU_CTRL) & 0x80) // If NMI is enabled
        {
            nmi_triggered = true;
        }
    }
    else if (scanline == 261) // VBlank ends at scanline 261
    {
        MemWrite8(PPU_STATUS, MemRead8(PPU_STATUS) & ~0x80); // Clear VBlank bit
    }
}
*/
/*
void PPU_Update(uint64_t cpu_cycles)
{
    // Accumulate PPU cycles instead of resetting
    ppu_cycle_counter += cpu_cycles * 3; // 1 CPU cycle = 3 PPU cycles

    // Wrap the cycle counter to prevent overflow beyond a frame
    if (ppu_cycle_counter >= 89342)
    {
        ppu_cycle_counter %= 89342; // Use modulo instead of subtracting
    }

    // Compute the current scanline and pixel position
    int scanline = (ppu_cycle_counter / 341) % 262; // NES has 262 scanlines per frame
    int dot = ppu_cycle_counter % 341; // Position within scanline

    // VBlank starts at scanline 241, dot 1
    if (scanline == 241 && dot == 1)
    {
        MemWrite8(PPU_STATUS, MemRead8(PPU_STATUS) | 0x80); // Set VBlank bit

        if (MemRead8(PPU_CTRL) & 0x80) // If NMI is enabled
        {
            nmi_triggered = true;
        }
    }

    // VBlank flag should be cleared at the start of the new frame (scanline 261, dot 1)
    if (scanline == 261 && dot == 1)
    {
        MemWrite8(PPU_STATUS, MemRead8(PPU_STATUS) & ~0x80); // Clear VBlank bit
    }
}
*/

bool vblank_set = false; // Prevents multiple VBlank triggers in one frame

void PPU_Update(uint64_t cpu_cycles)
{
    // 1 CPU cycle = 3 PPU cycles
    ppu_cycle_counter += cpu_cycles * 3;

    // NES PPU has ~29780 CPU cycles per frame
    //bool odd_frame = ppu_cycle_counter % 2;
    uint32_t num_dots = dots_per_frame_even;

    if (ppu_cycles % 2)
        num_dots = dots_per_frame_odd;

    //while (ppu_cycle_counter >= num_dots) // 29780 * 3 PPU cycles per frame
    //{
    //    ppu_cycle_counter -= num_dots; // Reset cycle counter at end of frame
    //}
    if (ppu_cycle_counter >= num_dots)
    {
        ppu_cycle_counter %= num_dots; // Correct frame wrapping
        vblank_set = false;
    }

    // Compute current scanline
    int scanline = (ppu_cycle_counter / 341) % 262; // 1 scanline = 341 PPU cycles

    if (scanline == 241 && !vblank_set) // VBlank starts at scanline 241
    {
        MemWrite8(PPU_STATUS, MemRead8(PPU_STATUS) | 0x80); // Set VBlank bit
        vblank_set = true;
        if (MemRead8(PPU_CTRL) & 0x80) // If NMI is enabled
        {
            nmi_triggered = true;
        }
    }
/*
    else if (scanline == 261) // VBlank ends at scanline 261
    {
        MemWrite8(PPU_STATUS, MemRead8(PPU_STATUS) & ~0x80); // Clear VBlank bit
    }
*/
}

/*
bool PPU_NmiTriggered(void)
{
    // Clear the flag after reading
    return nmi_triggered ? !(nmi_triggered = false) : false;
}
*/
bool PPU_NmiTriggered(void)
{
    if (nmi_triggered)
    {
        nmi_triggered = false; // Clear the flag after reading
        return true;
    }
    return false;
}

void PPU_Reset(void)
{
    MemWrite8(0x2000, 0);
    MemWrite8(0x2001, 0);
    MemWrite8(0x2002, 0);
    MemWrite8(0x2003, 0);
    MemWrite8(0x2004, 0);
    MemWrite8(0x2007, 0);
    //MemWrite8(0x2008, 0);
}