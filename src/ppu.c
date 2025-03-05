#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cpu.h"
#include "mem.h"
#include "ppu.h"


uint8_t g_ppu_regs[8] = { 0 };

static uint8_t vram[0x800];
//static uint8_t vram[0x800];
static uint8_t name_table[32 * 30];
// Pointers to handle mirroring
uint8_t *nametables[4];

typedef union {
    uint8_t raw;
    struct {
        uint8_t palette : 2;
        uint8_t padding : 3;
        uint8_t priority : 1;
        uint8_t horz_flip : 1;
        uint8_t vert_flip : 1;
    };
} Attribs;

typedef struct
{
    uint8_t y;
    uint8_t tile_id;
    Attribs attribs;
    uint8_t x;
} Sprite;

static Sprite oam_table[64];

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

static PpuCtrl *ppu_ctrl = NULL;
static uint8_t *ppu_mask = NULL;
static PpuStatus *ppu_status = NULL;
static uint8_t *oam_addr = NULL;
static uint8_t *oam_data = NULL;
static uint8_t *ppu_scroll = NULL;
static uint8_t *ppu_addr = NULL;
static uint8_t *ppu_data = NULL;
static uint8_t *oam_dma = NULL;

static uint16_t PatternTableDecodeAddress(uint16_t addr)
{
    // Fine Y offset, the row number within a tile
    uint8_t y_offset = addr & 0x7;// Bits 0-2
    // Bit plane (0: less significant bit; 1: more significant bit)
    bool bit_plane_msb = (addr >> 3) & 1; // Bit 3
    // Tile number from name table
    uint16_t tile_num = (addr >> 4) & 0xFF; // Bits 4-12
    // Half of pattern table (0: "left"; 1: "right")
    bool pattern_table_half = (addr >> 13) & 1; // Bit 13
    // Pattern table is at $0000-$1FFF
    bool pattern_table_low_addr = (addr >> 15) & 1; // Bit 14 

}

void PPU_Write8(uint16_t addr, uint8_t data)
{
    vram[addr & 0x1FFF] = data;
}

uint8_t PPU_Read8(uint16_t addr)
{
    //uint16_t decoded_addr = addr & 0x3FFF;
    return vram[addr & 0x3FFF];
}

uint8_t *GetPPUMemPtr(uint16_t addr)
{
    return &vram[addr & 0x3FFF];
}

uint8_t ppu_read(uint16_t addr) {
    addr &= 0x2FFF;  // Force address within 0x2000-0x2FFF range
    uint8_t table_index = (addr >> 10) & 3;  // Select which nametable
    return nametables[table_index][addr & 0x3FF];
}

uint8_t PPU_ReadChrRom(uint16_t addr)
{
    return g_chr_rom[addr & 0x1FFF];
}


// Configure the mirroring type
void NametableMirroringInit(NameTableMirror mode)
{
    switch (mode) {
        case NAMETABLE_HORIZONTAL:
            nametables[0] = &vram[0x0000];      // NT0 (0x2000)
            nametables[1] = &vram[0x0000];      // NT0 (Mirrored at 0x2400)
            nametables[2] = &vram[0x0800];  // NT1 (0x2800)
            nametables[3] = &vram[0x0800];  // NT1 (Mirrored at 0x2C00)
            break;
        
        case NAMETABLE_VERTICAL:
            nametables[0] = &vram[0x0000];      // NT0 (0x2000)
            nametables[1] = &vram[0x0400];  // NT1 (0x2400)
            nametables[2] = &vram[0x0000];      // NT0 (Mirrored at 0x2800)
            nametables[3] = &vram[0x0400];  // NT1 (Mirrored at 0x2C00)
            break;

        default:
            printf("Unimplemented Nametable mirroring mode %d detected!\n", mode);
            break;
    }
}


void PPU_Init(Ppu *ppu, int name_table_layout)
{
    memset(ppu, 0, sizeof(*ppu));
    ppu->nt_mirror_mode = name_table_layout;
    NametableMirroringInit(ppu->nt_mirror_mode);

    g_ppu_regs[0] = 0;
    g_ppu_regs[2] = 0xA0;
    //MemWrite8(PPU_CTRL, 0);
    //MemWrite8(PPU_MASK, 0);
    //MemWrite8(PPU_STATUS, 0xA0);
    //MemWrite8(0x2003, 0);
    //MemWrite8(0x2004, 0);
    //MemWrite8(PPU_SCROLL, 0);
    //MemWrite8(PPU_ADDR, 0);
    //MemWrite8(0x2007, 0);
    //MemWrite8(0x2008, 0);
    ppu_ctrl = (PpuCtrl*)&g_ppu_regs[0];
    ppu_status = (PpuStatus*)&g_ppu_regs[2];
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


bool vblank_set = false; // Prevents multiple VBlank triggers in one frame

//int scanline = 0; // Track current scanline
//int dot = 0;      // Track the dot (pixel) position within a scanline

const float CPU_FREQ  = 1789773.0;  // 1.789 Mhz
const float FRAME_RATE = 60.0;
const float CYCLES_PER_FRAME  = CPU_FREQ / FRAME_RATE;
const float CPU_AVG_CYCLES = 3.0;
const uint32_t CPU_INSTR_PER_FRAME  = (CYCLES_PER_FRAME / CPU_AVG_CYCLES);
const uint32_t CPU_INSTR_PER_FRAME_ACTIVE = CPU_INSTR_PER_FRAME * 240 / 262;
const uint32_t CPU_INSTR_PER_FRAME_VBLANK = CPU_INSTR_PER_FRAME - CPU_INSTR_PER_FRAME_ACTIVE;

static void PPU_FrameDone(Ppu *ppu)
{
    // NES PPU has ~29780 CPU cycles per frame
    uint32_t num_dots = dots_per_frame_even;

    if (ppu->cycles % 2)
        num_dots = dots_per_frame_odd;

    // is frame done?
    if (ppu->cycles >= prev_ppu_cycles + num_dots)
    {
        prev_ppu_cycles = ppu->cycles;
        ppu->frame_finished = true;
    }
}

static int prev_scan_line = 0;
// We need to catch up to the cpu
void PPU_Update(Ppu *ppu, uint64_t cpu_cycles)
{
    //PPU_Write8(addr, data);
    // Get the delta of cpu cycles since the last cpu instruction
    uint64_t cpu_cycles_delta = cpu_cycles - ppu->prev_cpu_cycles;
    // Update prev cpu cycles to current amount for next update
    ppu->prev_cpu_cycles = cpu_cycles;
    // Calculate how many ppu ticks we need to run
    // 1 CPU cycle = 3 PPU cycles
    uint64_t ppu_cycles_to_run = cpu_cycles_delta * 3;

    while (ppu_cycles_to_run != 0)
    {
        int scanline = (ppu->cycles / 341) % 262; // 1 scanline = 341 PPU cycles
        ppu->cycles++;
        ppu_cycles_to_run--;

        //if (scanline == prev_scan_line && !ppu_ctrl->vblank_nmi)
        //    continue;

        prev_scan_line = scanline;

        if (scanline < 240)
        {
            // Render stuff in here

            // Nametables
            // Conceptually, the PPU does this 33 times for each scanline:
            //Fetch a nametable entry from $2000-$2FFF.
            //Fetch the corresponding attribute table entry from $23C0-$2FFF and increment the current VRAM address within the same row.
            //Fetch the low-order byte of an 8x1 pixel sliver of pattern table from $0000-$0FF7 or $1000-$1FF7.
            //Fetch the high-order byte of this sliver from an address 8 bytes higher.
            //Turn the attribute data and the pattern table data into palette indices, and combine them with data from sprite data using priority.
            //It also does a fetch of a 34th (nametable, attribute, pattern) tuple that is never used, but some mappers rely on this fetch for timing purposes.

            // PPU sprite evaluation is an operation done by the PPU once each scanline. It prepares the set of sprites and fetches their data to be rendered on the next scanline.
            // This is a separate step from sprite rendering.

            // First, it clears the list of sprites to draw.
            // Second, it reads through OAM, checking which sprites will be on this scanline. It chooses the first eight it finds that do.
            // Third, if eight sprites were found, it checks (in a wrongly-implemented fashion) for further sprites on the scanline to see if the sprite overflow flag should be set.
            // Fourth, using the details for the eight (or fewer) sprites chosen, it determines which pixels each has on the scanline and where to draw them.
    
                
        }
        else if (scanline == 241)
        {
            // VBlank starts at scanline 241
            // Get the ppu status reg
            //PpuStatus ppu_status = (PpuStatus)MemRead8(PPU_STATUS);
            ppu_status->vblank = 1;
            //MemWrite8(PPU_STATUS, ppu_status.raw); // Set VBlank bit
            // If NMI is enabled
            if (ppu_ctrl->vblank_nmi)
            {
                nmi_triggered = true;
                ppu->cycles += ppu_cycles_to_run;
                break;
            }
        }
        // VBlank ENDS: Clear VBlank flag at scanline 261, dot 1
        else if (scanline == 261)
        {
            ppu_status->vblank = 0;
            //ppu->frame_finished = 1;

        }
        //else if (roundf(scanline) == 262)
        //{
        //    ppu->frame_finished = true;
        //}
    }
    PPU_FrameDone(ppu);
}

static int FetchBackgroundPixel(int scanline, int pixel_x)
{
    int tile_x = pixel_x / 8;
    int tile_y = scanline / 8;

    /*
    tile_index = ReadNametable(tile_x, tile_y);
    attribute_data = ReadAttributeTable(tile_x, tile_y);
    
    tile_pattern = ReadPatternTable(tile_index)
    pixel_value = DecodeTilePixel(tile_pattern, pixel_x % 8, scanline % 8);

    return MapPixelToPalette(pixel_value, attribute_data);
*/
    return 0;
}

static void PPU_DrawScanline(int scanline)
{
    for (int x = 0; x < 260; x++)
    {

    }

}

void PPU_Update1(uint64_t cpu_cycles)
{
    // 1 CPU cycle = 3 PPU cycles
    ppu_cycle_counter += cpu_cycles * 3;

    // NES PPU has ~29780 CPU cycles per frame
    //bool odd_frame = ppu_cycle_counter % 2;
    uint32_t num_dots = dots_per_frame_even;

    if (ppu_cycles % 2)
        num_dots = dots_per_frame_odd;

    // is frame done?
    if (ppu_cycles >= prev_ppu_cycles + num_dots)
    {

        //// Get the delta (number of ppu cycles that have passed)
        //uint64_t ppu_cycles_delta = (ppu_cycles - prev_ppu_cycles);
        //// How far off(in ppu cycles) are we?
        //uint32_t dsynched_cycles = ppu_cycles_delta - num_dots;
        //if (dsynched_cycles <= 6)
        //{
        //    
        //}
        //prev_ppu_cycles = ppu_cycles;
        //ppu_cycle_counter += ppu_cycles;
    }

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
        //MemWrite8(PPU_STATUS, MemRead8(PPU_STATUS) | 0x80); // Set VBlank bit
        //vblank_set = true;
        //if (MemRead8(PPU_CTRL) & 0x80) // If NMI is enabled
        //{
        //    nmi_triggered = true;
        //}
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
    //MemWrite8(0x2000, 0);
    //MemWrite8(0x2001, 0);
    //MemWrite8(0x2002, 0);
    //MemWrite8(0x2003, 0);
    //MemWrite8(0x2004, 0);
    //MemWrite8(0x2007, 0);
    //MemWrite8(0x2008, 0);
}