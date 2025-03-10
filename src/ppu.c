#include <SDL3/SDL_stdinc.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "cpu.h"
#include "mem.h"
#include "ppu.h"
#include "utils.h"


//uint8_t g_ppu_regs[8] = { 0 };

static uint8_t vram[0x800];
//static uint8_t vram[0x800];
static uint8_t name_table[32 * 30];
// Pointers to handle mirroring
uint8_t *nametables[4];

// OAM
static Sprite sprites[64];
// OAM Secondary
static Sprite sprites_secondary[8];

static uint8_t palette_table[32];

typedef union
{
    uint16_t addr;
    uint8_t low;
    uint8_t high;
} PpuAddr;

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

static const uint8_t ntsc_palette[] =
{
    0x80, 0x80, 0x80,
    0x00, 0x3D, 0xA6,
    0x00, 0x12, 0xB0, 
    0x44, 0x00, 0x96,
    0xA1, 0x00, 0x5E,
    0xC7, 0x00, 0x28,
    0xBA, 0x06, 0x00, 
    0x8C, 0x17, 0x00,
    0x5C, 0x2F, 0x00,
    0x10, 0x45, 0x00,
    0x05, 0x4A, 0x00, 
    0x00, 0x47, 0x2E, 
    0x00, 0x41, 0x66,
    0x00, 0x00, 0x00,
    0x05, 0x05, 0x05,
    0x05, 0x05, 0x05,
    0xC7, 0xC7, 0xC7,
    0x00, 0x77, 0xFF,
    0x21, 0x55, 0xFF,
    0x82, 0x37, 0xFA,
    0xEB, 0x2F, 0xB5,
    0xFF, 0x29, 0x50,
    0xFF, 0x22, 0x00,
    0xD6, 0x32, 0x00,
    0xC4, 0x62, 0x00,
    0x35, 0x80, 0x00,
    0x05, 0x8F, 0x00,
    0x00, 0x8A, 0x55,
    0x00, 0x99, 0xCC,
    0x21, 0x21, 0x21,
    0x09, 0x09, 0x09,
    0x09, 0x09, 0x09,
    0xFF, 0xFF, 0xFF,
    0x0F, 0xD7, 0xFF,
    0x69, 0xA2, 0xFF,
    0xD4, 0x80, 0xFF,
    0xFF, 0x45, 0xF3, 
    0xFF, 0x61, 0x8B,
    0xFF, 0x88, 0x33, 
    0xFF, 0x9C, 0x12,
    0xFA, 0xBC, 0x20,
    0x9F, 0xE3, 0x0E,
    0x2B, 0xF0, 0x35,
    0x0C, 0xF0, 0xA4,
    0x05, 0xFB, 0xFF,
    0x5E, 0x5E, 0x5E,
    0x0D, 0x0D, 0x0D,
    0x0D, 0x0D, 0x0D,
    0xFF, 0xFF, 0xFF,
    0xA6, 0xFC, 0xFF,
    0xB3, 0xEC, 0xFF,
    0xDA, 0xAB, 0xEB,
    0xFF, 0xA8, 0xF9,
    0xFF, 0xAB, 0xB3,
    0xFF, 0xD2, 0xB0,
    0xFF, 0xEF, 0xA6,
    0xFF, 0xF7, 0x9C,
    0xD7, 0xE8, 0x95,
    0xA6, 0xED, 0xAF,
    0xA2, 0xF2, 0xDA,
    0x99, 0xFF, 0xFC,
    0xDD, 0xDD, 0xDD, 
    0x11, 0x11, 0x11,
    0x11, 0x11, 0x11
};

static Ppu *ppu_ptr = NULL;
static uint64_t prev_ppu_cycles = 0;

typedef union {
    uint16_t raw_addr;
    struct {
        // Fine Y offset, the row number within a tile
        uint8_t y_offset : 3;// Bits 0-2
        // Bit plane (0: less significant bit; 1: more significant bit)
        bool bit_plane_msb : 1; // Bit 3
        // Tile number from name table
        uint16_t tile_num  : 8; // Bits 4-12
        // Half of pattern table (0: "left"; 1: "right")
        bool pattern_table_half : 1; // Bit 13
        // Pattern table is at $0000-$1FFF
        bool pattern_table_low_addr : 1; // Bit 14 
    };

} PpuAddrReg;

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

    return 0;

}

static void UpdateCtrlReg(uint8_t data)
{
    //ppu_ctrl
}

static void UpdateAddrReg(Ppu *ppu, uint16_t data)
{
    uint8_t high_addr = (data >> 8) & 0xFF;
    uint8_t low_addr = data & 0xFF;
    uint16_t ret = (uint16_t)low_addr << 8 | high_addr;

        //// Push high first
        //StackPush(state, (state->pc >> 8) & 0xFF);
        //// Push low next
}

static void PPU_UpdateVramAddr(Ppu *ppu)
{
    ppu->v += ppu->ctrl.vram_addr_inc ? 32 : 1;
}

void PPU_WriteAddrReg(const uint8_t value)
{
    if (!ppu_ptr->w)
    {
        ppu_ptr->t = (ppu_ptr->t & 0x00FF) | ((value & 0x3F) << 8); // Set high byte of `t`
    }
    else
    {
        ppu_ptr->t = (ppu_ptr->t & 0xFF00) | value; // Set low byte of `t`
        // Transfer `t` to `v`
        ppu_ptr->v = ppu_ptr->t;
    }
    ppu_ptr->w = !ppu_ptr->w;
}


void WriteToNametable(uint16_t addr, uint8_t data)
{
    addr &= 0x2FFF;  // Force address within 0x2000-0x2FFF range
    uint8_t table_index = (addr >> 10) & 3;  // Select which nametable
    nametables[table_index][addr & 0x3FF] = data;
}

void PPU_WriteData(const uint8_t data)
{
    // Extract A13, A12, A11 for region decoding
    uint8_t ppu_region = (ppu_ptr->v >> 11) & 0x7;
    switch (ppu_region)
    {
        case 0x0:
        case 0x1:
        case 0x2:
        case 0x3:
        {
            // chr rom is actually chr ram
            g_chr_rom[ppu_ptr->v % g_chr_rom_size] = data;
            break;
        }
        case 0x4:
        case 0x5:
            //vram[ppu_ptr->v & 0x1FFF] = data;
            WriteToNametable(ppu_ptr->v, data);
            break;
        case 0x7:
            palette_table[ppu_ptr->v & 0x1F] = data;
        default:
            break;
    }

    // Auto-increment address
    ppu_ptr->v += ppu_ptr->ctrl.vram_addr_inc ? 32 : 1;
}

void write_ppu2005(const uint8_t value)
{
    if (!ppu_ptr->w)
    {
        // First write: X scroll (fine X + coarse X)
        ppu_ptr->t = (ppu_ptr->t & 0xFFE0) | (value >> 3);
        ppu_ptr->x = value & 0x7; // Fine X scroll
    } else {
        // Second write: Y scroll (fine Y + coarse Y)
        ppu_ptr->t = (ppu_ptr->t & 0x8C1F) | ((value & 0x7) << 12) | ((value & 0xF8) << 2);
    }
    ppu_ptr->w = !ppu_ptr->w;
}

uint8_t PPU_ReadStatus(/*Ppu *ppu*/)
{
    uint8_t status_value = ppu_ptr->status.raw;

    // **Clear VBlank flag and reset write toggle**
    ppu_ptr->status.vblank = 0;
    ppu_ptr->w = 0;

    return status_value;
}

uint8_t PPU_ReadData(/*Ppu *ppu*/)
{
    uint16_t addr = ppu_ptr->v & 0x3FFF;
    uint8_t data;

    if (addr >= 0x3F00)
    {  
        // Palette memory (no buffering)
        data = vram[addr & 0x1F];  // Mirror palette range
    }
    else if (addr <= 0x1FFF)
    {  
        // VRAM/CHR memory (buffered)
        //data = ppu_ptr->buffered_data;  // Return stale buffer value
        //ppu_ptr->buffered_data = vram[addr];  // Load new data into buffer
        data = ppu_ptr->buffered_data;  // Return stale buffer value
        ppu_ptr->buffered_data = g_chr_rom[addr];  // Load new data into buffer
    }
    else if (addr <= 0x2FFF)
    {  
        // VRAM/CHR memory (buffered)
        data = ppu_ptr->buffered_data;  // Return stale buffer value
        ppu_ptr->buffered_data = vram[addr];  // Load new data into buffer
    }

    // Auto-increment address
    ppu_ptr->v += ppu_ptr->ctrl.vram_addr_inc ? 32 : 1;

    return data;
}


uint8_t ReadPPURegister(const uint16_t address)
{
    switch (address) {
        case PPU_STATUS:  // PPUSTATUS
            return PPU_ReadStatus();
        case OAM_DATA:  // OAMDATA (reads from OAM memory)
            return sprites[ppu_ptr->oam_addr & 63].raw[ppu_ptr->oam_addr & 3];
        case PPU_DATA:  // PPUDATA (reads from VRAM)
            return PPU_ReadData();
        default:
            return 0;  // Unhandled read (usually open bus behavior)
    }
}

void WritePPURegister(const uint16_t addr, const uint8_t data)
{
    //if ((prev_ppu_cycles < 88974) && (addr == PPU_CTRL))
    //    return;
    //if ((prev_ppu_cycles < 88974) && (addr == PPU_MASK))
    //    return;
    //if ((prev_ppu_cycles < 88974) && (addr == PPU_SCROLL))
    //    return;
    //if ((prev_ppu_cycles < 88974) && (addr == PPU_ADDR))
    //    return;

    switch (addr)
    {
        case PPU_CTRL:
            ppu_ptr->ctrl.raw = data;
            ppu_ptr->t = (ppu_ptr->t & 0xF3FF) | ((data & 0x03) << 10);  // Update t (Name table bits)
            break;
        case PPU_MASK:
            ppu_ptr->mask = data;
            break;
        case OAM_ADDR:
            ppu_ptr->oam_addr = data;
            break;
        case OAM_DATA:  // OAMDATA (writes to OAM memory)
        {
            //const int oam_n = ppu_ptr->oam_addr & 63;
            const int oam_m = ppu_ptr->oam_addr & 3;
            
            sprites[ppu_ptr->oam_addr++ & 63].raw[oam_m] = data;
            break;
        }
        case PPU_SCROLL:  // PPUSCROLL
            write_ppu2005(data);
            break;
        case PPU_ADDR:
            PPU_WriteAddrReg(data);
            break;
        case PPU_DATA:
            PPU_WriteData(data);
            break;
    }
}

void OAM_Dma(const uint16_t addr)
{
    uint16_t base_addr = addr * 0x100;
    uint8_t *data_ptr = CpuGetPtr(base_addr);
    memcpy(sprites, data_ptr, sizeof(Sprite) * 64);
}

void PPU_Write8(uint16_t addr, uint8_t data)
{
    vram[addr & 0x1FFF] = data;
}

uint8_t PPU_Read8(uint16_t addr)
{
    uint8_t region = (addr >> 13) & 0x7;
     switch (region) {
    
    }
    //uint16_t decoded_addr = addr & 0x3FFF;
    return vram[addr & 0x3FFF];
}

uint8_t *GetPPUMemPtr(uint16_t addr)
{
    return &vram[addr & 0x3FFF];
}

uint8_t ReadNametable(uint16_t addr) {
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


void PPU_Init(Ppu *ppu, int name_table_layout, uint32_t *pixels)
{
    memset(ppu, 0, sizeof(*ppu));
    ppu->nt_mirror_mode = name_table_layout;
    NametableMirroringInit(ppu->nt_mirror_mode);
    ppu->buffer = pixels;
    ppu_ptr = ppu;
}

uint64_t prev_vblank_cycles = 0;
uint64_t next_vblank_cycles = 27384;
bool nmi_triggered = false;
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

const uint32_t dots_per_frame_odd = 341 * 261 + 340;
const uint32_t dots_per_frame_even = 341 * 261 + 341;
const uint32_t cpu_cycles_per_frame = dots_per_frame_even / 3; 

static void DrawSprite(uint32_t *buffer, int xpos, int ypos, int h, int w, uint8_t *rgb)
{
    //buffer[y * SCREEN_WIDTH + x] = 0;
    //buffer[y * SCREEN_WIDTH + x + 1] = 0;
    //buffer[y * SCREEN_WIDTH + x + 2] = 0;
    //buffer[y * SCREEN_WIDTH + x + 3] = 0;

    for (int x = xpos; x < xpos + w; x++)
    {
        for (int y = ypos; y < ypos + h; y++)
        {
            uint8_t a = 255;
            buffer[y * 340 + x] = (a << 24) | (rgb[2] << 16) | (rgb[1] << 8) | rgb[0];
        }
    }
}

// Function to set a pixel in the frame
static void SetPixel(uint32_t *buffer, int x, int y, uint8_t* rgb)
{
    buffer[y * 340 + x] = (0 << 24) | (rgb[2] << 16) | (rgb[1] << 8) | rgb[0];
}

 void Render(Ppu *ppu)
 {
    uint16_t bank = ppu->ctrl.bg_pat_table_addr ? 0x1000: 0;
 
    for (uint16_t i = 0; i < 0x03c0; i++)
    {
        // just for now, lets use the first nametable
        uint16_t tile = vram[i];
        int tile_x = i % 32;
        int tile_y = i / 32;

        size_t tile_offset = (bank + tile * 16);
        const uint8_t *tile_data = &g_chr_rom[tile_offset];
 
        for (int y = 0; y < 8; y++)
        {
            uint8_t upper = tile_data[y];
            uint8_t lower = tile_data[y + 8];
 
            for (int x = 8; x > 0; x--)  // Reverse order
            {
                int value = (1 & upper) << 1 | (1 & lower);
                upper = upper >> 1;
                lower = lower >> 1;
                uint8_t *rgb_ptr = NULL;
                switch (value)
                {
                    case 0:
                    {
                        uint8_t rgb[] = {ntsc_palette[0], ntsc_palette[1], ntsc_palette[2]};
                        rgb_ptr = rgb;
                        break;
                    }
                    case 1:
                    {
                        uint8_t rgb[] = {ntsc_palette[3], ntsc_palette[4], ntsc_palette[5]};
                        rgb_ptr = rgb;
                        break;
                    }
                    case 2:
                    {
                        uint8_t rgb[] = {ntsc_palette[6], ntsc_palette[7], ntsc_palette[8]};
                        rgb_ptr = rgb;
                        break;
                    }
                    case 3:
                    {
                        uint8_t rgb[] = {ntsc_palette[9], ntsc_palette[10], ntsc_palette[11]};
                        rgb_ptr = rgb;
                        break;
                    }
                    default: 
                        printf("Unexpected tile value!\n");
                        return;
                }
                SetPixel(ppu->buffer, tile_x * 8 + x, tile_y * 8 + y, rgb_ptr);
            }
        }
    }
 }

static void PPU_IsFrameDone(Ppu *ppu)
{
    // NES PPU has ~29780 CPU cycles per frame
    uint32_t num_dots = dots_per_frame_even;

    if (ppu->cycles & 1)
        num_dots = dots_per_frame_odd;

    // is frame done?
    if (ppu->cycles >= prev_ppu_cycles + num_dots)
    {
        prev_ppu_cycles = ppu->cycles;
        ppu->frame_finished = true;
        //Render(ppu);
    }
}

static void ResetSecondaryOAMSprites(void)
{
    memset(sprites_secondary, 0xFF, sizeof(Sprite) * 8);
}

static int sprite_count = 0;

static uint8_t sprite_y;
static uint8_t sprite_y_m;
static int sprite_index = 0;
static int m = 0;

static int GetSpriteHeight(void)
{
    return ppu_ptr->ctrl.sprite_size ? 16 : 8;
}

// 192 cycles
// 65 -> 256 cycles
static void SpriteEval(int scanline, int cycle)
{
    if (cycle < 65 || cycle > 257)
        return;

    if (cycle == 65)
    {
        sprite_index = 0;
        sprite_count = 0;
    }

    if (sprite_index < 64)
    {
        if (cycle & 1)
        {
            sprite_y = sprites[sprite_index].y;
        }
        else if (scanline >= sprite_y && scanline < sprite_y + (ppu_ptr->ctrl.sprite_size ? 16 : 8))
        {
            if (sprite_count < 8)
            {
                // Copy full sprite data into secondary OAM
                sprites_secondary[sprite_count] = sprites[sprite_index];
                sprite_count++;
            }
            else
            {
                ppu_ptr->status.sprite_overflow = 1;
            }
        }

        if (++sprite_index == 64)
        {
            sprite_index = 0;
        }
    }

//    if (cycle == 256)
//    {
//        sprite_index = 0;
//    }
} 

static void SpriteFetch(int scanline, int cycle)
{

}

static void PrepareSpriteData(int scanline, int cycle)
{
    //FOR each scanline DO
    // Step 1: Clear secondary OAM
    //FOR i = 0 TO 31 DO

    if (cycle == 64)
    {
        ResetSecondaryOAMSprites();
    }
    //END FOR

    // Step 2: Sprite evaluation
    SpriteEval(scanline, cycle);
/*
    if (cycle > 256 && cycle < 321)
    {
        for (int i = 0; i < 7; i++)
        {
            if (i < sprite_count)
            {
                sprite_y = sprites_secondary[i].y
                sprite_tile = sprites_secondary[i].tile_id;
                sprite_attr = sprites_secondary[i * 4 + 2]
                sprite_X = secondary_OAM[i * 4 + 3]
                // Fetch tile data (handled in PPU internals)
            }
            else
            {
                // Fill remaining slots with $FF
                secondary_OAM[i] = $FF
                secondary_OAM[i] = $FF
                secondary_OAM[i] = $FF
                secondary_OAM[i * 4 + 3] = $FF
            }
        }
    }
*/
/*
    sprite_count = 0
    n = 0
    WHILE n < 64 DO
        // Read Y-coordinate of sprite n
        sprite_Y = OAM[n][0]

        IF sprite_Y is within scanline range THEN
            IF sprite_count < 8 THEN
                // Copy full sprite data into secondary OAM
                secondary_OAM[sprite_count * 4 + 0] = sprite_Y
                secondary_OAM[sprite_count * 4 + 1] = OAM[n][1] // Tile number
                secondary_OAM[sprite_count * 4 + 2] = OAM[n][2] // Attributes
                secondary_OAM[sprite_count * 4 + 3] = OAM[n][3] // X-coordinate
                sprite_count += 1
            ELSE
                // If 8 sprites are already found, check for overflow condition
                sprite_overflow_flag = 1
            END IF
        END IF

        n += 1
    END WHILE
*/
/*
    // Step 3: Sprite fetches (Load 8 chosen sprites for rendering)
    FOR i = 0 TO 7 DO
        IF i < sprite_count THEN
            sprite_Y = oam_secondary[i].
            sprite_tile = secondary_OAM[i * 4 + 1]
            sprite_attr = secondary_OAM[i * 4 + 2]
            sprite_X = secondary_OAM[i * 4 + 3]
            // Fetch tile data (handled in PPU internals)
        ELSE
            // Fill remaining slots with $FF
            secondary_OAM[i * 4 + 0] = $FF
            secondary_OAM[i * 4 + 1] = $FF
            secondary_OAM[i * 4 + 2] = $FF
            secondary_OAM[i * 4 + 3] = $FF
        END IF
    END FOR

    // Step 4: Background render pipeline setup
    Fetch first byte from secondary OAM
    Initialize background tile fetching for next scanline

END FOR
*/
}

// We need to catch up to the cpu
void PPU_Update(Ppu *ppu, uint64_t cpu_cycles)
{
    // Get the delta of cpu cycles since the last cpu instruction
    uint64_t cpu_cycles_delta = cpu_cycles - ppu->prev_cpu_cycles;
    // Update prev cpu cycles to current amount for next update
    ppu->prev_cpu_cycles = cpu_cycles;
    // Calculate how many ppu ticks we need to run (1 CPU cycle = 3 PPU cycles)
    uint64_t ppu_cycles_to_run = cpu_cycles_delta * 3;

    while (ppu_cycles_to_run != 0)
    {
        int prev_scan_line = 0;
        int ppu_cycle_counter = (ppu->cycles % 341);
        int scanline = (ppu->cycles / 341) % 262; // 1 scanline = 341 PPU cycles
        ppu->cycles++;
        ppu_cycles_to_run--;

        if (scanline <= 240)
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

            // During all visible scanlines, the PPU scans through OAM to determine which sprites to render on the next scanline.
            // Sprites found to be within range are copied into the secondary OAM, which is then used to initialize eight internal sprite output units.
            // OAM[n][m] below refers to the byte at offset 4*n + m within OAM, i.e. OAM byte m (0-3) of sprite n (0-63).
            // 
            // During each pixel clock (341 total per scanline), the PPU accesses OAM in the following pattern:
            // Cycles 1-64: Secondary OAM (32-byte buffer for current sprites on scanline) is initialized to $FF - attempting to read $2004 will return $FF.
            // Internally, the clear operation is implemented by reading from the OAM and writing into the secondary OAM as usual, only a signal is active that makes the read always return $FF.
            // Cycles 65-256: Sprite evaluation
            // On odd cycles, data is read from (primary) OAM
            // On even cycles, data is written to secondary OAM (unless secondary OAM is full, in which case it will read the value in secondary OAM instead)
            // 1. Starting at n = 0, read a sprite's Y-coordinate (OAM[n][0], copying it to the next open slot in secondary OAM (unless 8 sprites have been found, in which case the write is ignored).
            // 1a. If Y-coordinate is in range, copy remaining bytes of sprite data (OAM[n][1] thru OAM[n][3]) into secondary OAM.
            // 2. Increment n
            // 2a. If n has overflowed back to zero (all 64 sprites evaluated), go to 4
            // 2b. If less than 8 sprites have been found, go to 1
            // 2c. If exactly 8 sprites have been found, disable writes to secondary OAM because it is full. This causes sprites in back to drop out.
            // 3. Starting at m = 0, evaluate OAM[n][m] as a Y-coordinate.
            // 3a. If the value is in range, set the sprite overflow flag in $2002 and read the next 3 entries of OAM
            // (incrementing 'm' after each byte and incrementing 'n' when 'm' overflows); if m = 3, increment n
            // 3b. If the value is not in range, increment n and m (without carry). If n overflows to 0, go to 4; otherwise go to 3
            // The m increment is a hardware bug - if only n was incremented, the overflow flag would be set whenever more than 8 sprites were present on the same scanline, as expected.
            // 4. Attempt (and fail) to copy OAM[n][0] into the next free slot in secondary OAM, and increment n (repeat until HBLANK is reached)
            // Cycles 257-320: Sprite fetches (8 sprites total, 8 cycles per sprite)
            // 1-4: Read the Y-coordinate, tile number, attributes, and X-coordinate of the selected sprite from secondary OAM
            // 5-8: Read the X-coordinate of the selected sprite from secondary OAM 4 times (while the PPU fetches the sprite tile data)
            // For the first empty sprite slot, this will consist of sprite #63's Y-coordinate followed by 3 $FF bytes; for subsequent empty sprite slots, this will be four $FF bytes
            // Cycles 321-340+0: Background render pipeline initialization
            // Read the first byte in secondary OAM (while the PPU fetches the first two background tiles for the next scanline)
            PrepareSpriteData(scanline, ppu_cycle_counter);
            if (scanline == 0 && ppu_cycle_counter == 1)
            {
                Render(ppu);
            }
        }
        else if (scanline == 241 && ppu_cycle_counter == 1)
        {
            // VBlank starts at scanline 241
            ppu->status.vblank = 1;
            // If NMI is enabled
            if (ppu->ctrl.vblank_nmi)
            {
                nmi_triggered = true;
            }
        }
        // Clear VBlank flag at scanline 261, dot 1
        // This might be a hack, should prob set it when the register is read?
        else if (scanline == 261 && ppu_cycle_counter == 1)
        {
            ppu->status.vblank = 0;
            //ppu_status->sprite_hit = 0;
            ppu->status.sprite_overflow = 0;
            // Hack for smb
            //ppu_status->sprite_hit = !ppu_status->sprite_hit;

        }
        else if (scanline == 261 && ppu_cycle_counter == 340)
        {
            //ppu_status->vblank = 0;
            // Hack for smb
            ppu->status.sprite_hit = !ppu->status.sprite_hit;

        }
    }
    PPU_IsFrameDone(ppu);
}

static int FetchBackgroundPixel(int scanline, int pixel_x)
{
    int tile_x = pixel_x / 8;
    int tile_y = scanline / 8;

/*
    int tile_index = ReadNametable(tile_x, tile_y);
    uint32_t attribute_data = ReadAttributeTable(tile_x, tile_y);
    
    int tile_pattern = ReadPatternTable(tile_index)
    uint32_t pixel_value = DecodeTilePixel(tile_pattern, pixel_x % 8, scanline % 8);

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