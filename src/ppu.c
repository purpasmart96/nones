#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>

#include "cpu.h"
#include "arena.h"
#include "nones.h"

#include "loader.h"
#include "arena.h"
#include "bus.h"
#include "ppu.h"
#include "utils.h"

static uint8_t vram[0x800];
// Pointers to handle mirroring
static uint8_t *nametables[4];
// OAM
static Sprite sprites[64];
// OAM Secondary
static Sprite sprites_secondary[8];
static uint8_t palette_table[32];

typedef struct
{
    //uint8_t raw[4];
    //struct {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;
    //};

} Color;

static Color sys_palette2[64] =
{
    
    {0x66, 0x66, 0x66, 255},
    {0x00, 0x2A, 0x88, 255}, 
    {0x14, 0x12, 0xA7, 255},
    {0x3B, 0x00, 0xA4, 255}, 
    {0x5C, 0x00, 0x7E, 255},
    {0x6E, 0x00, 0x40, 255},
    {0x6C, 0x07, 0x00, 255},
    {0x56, 0x1D, 0x00, 255},
    {0x33, 0x35, 0x00, 255},
    {0x0B, 0x48, 0x00, 255},
    {0x00, 0x52, 0x00, 255},
    {0x00, 0x4F, 0x08, 255},
    {0x00, 0x40, 0x4D, 255}, 
    {0x00, 0x00, 0x00, 255},
    {0x00, 0x00, 0x00, 255},
    {0x00, 0x00, 0x0, 255},
    {0xAD, 0xAD, 0xAD, 255}, 
    {0x15, 0x5F, 0xD9, 255},
    {0x42, 0x40, 0xFF, 255}, 
    {0x75, 0x27, 0xFE, 255},
    {0xA0, 0x1A, 0xCC, 255},
    {0xB7, 0x1E, 0x7B, 255},
    {0xB5, 0x31, 0x20, 255}, 
    {0x99, 0x4E, 0x00, 255},
    {0x6B, 0x6D, 0x00, 255},
    {0x38, 0x87, 0x00, 255},
    {0x0C, 0x93, 0x00, 255}, 
    {0x00, 0x8F, 0x32, 255},
    {0x00, 0x7C, 0x8D, 255}, 
    {0x00, 0x00, 0x00, 255},
    {0x00, 0x00, 0x00, 255}, 
    {0x00, 0x00, 0x00, 255},
    {0xFF, 0xFE, 0xFF, 255}, 
    {0x64, 0xB0, 0xFF, 255},
    {0x92, 0x90, 0xFF, 255}, 
    {0xC6, 0x76, 0xFF, 255},
    {0xF3, 0x6A, 0xFF, 255}, 
    {0xFE, 0x6E, 0xCC, 255},
    {0xFE, 0x81, 0x70, 255}, 
    {0xEA, 0x9E, 0x22, 255},
    {0xBC, 0xBE, 0x00, 255}, 
    {0x88, 0xD8, 0x00, 255},
    {0x5C, 0xE4, 0x30, 255}, 
    {0x45, 0xE0, 0x82, 255},
    {0x48, 0xCD, 0xDE, 255}, 
    {0x4F, 0x4F, 0x4F, 255},
    {0x00, 0x00, 0x00, 255}, 
    {0x00, 0x00, 0x00, 255},
    {0xFF, 0xFE, 0xFF, 255}, 
    {0xC0, 0xDF, 0xFF, 255},
    {0xD3, 0xD2, 0xFF, 255}, 
    {0xE8, 0xC8, 0xFF, 255},
    {0xFB, 0xC2, 0xFF, 255}, 
    {0xFF, 0xC4, 0xEA, 255},
    {0xFF, 0xCC, 0xB3, 255}, 
    {0xF4, 0xD8, 0x8E, 255},
    {0xE0, 0xE6, 0x7C, 255}, 
    {0xC8, 0xF0, 0x7E, 255},
    {0xAD, 0xEF, 0x8E, 255}, 
    {0x9D, 0xE8, 0xC5, 255},
    {0xA4, 0xE2, 0xEA, 255}, 
    {0xA8, 0xA8, 0xA8, 255},
    {0x00, 0x00, 0x00, 255}, 
    {0x00, 0x00, 0x00, 255}
};

static Ppu *ppu_ptr = NULL;
static uint64_t prev_ppu_cycles = 0;

static Color GetColor(uint8_t palette_index, uint8_t pixel)
{
    // Compute palette memory address
    uint16_t palette_addr = 0x3F00 + (palette_index * 4) + pixel;

    // Read the color index from PPU palette memory
    uint16_t color_index = palette_table[palette_addr & 0x1F];

    if (!pixel)
    {
        color_index = palette_table[0];
    }

    return sys_palette2[color_index & 0x3F];

}

static Color GetspriteColor(uint8_t palette_index, uint8_t pixel)
{
    uint16_t palette_addr = 0x10 + (palette_index * 4) + pixel;

    uint16_t color_index = palette_table[palette_addr];

    return sys_palette2[color_index & 0x3F];
}

void PPU_WriteAddrReg(const uint8_t value)
{
    if (!ppu_ptr->w)
    {
        //ppu_ptr->t.raw = (ppu_ptr->t.raw & 0x00FF) | ((value & 0x3F) << 8);
        // Set high byte of t
        ppu_ptr->t.writing.high = value & 0x3F;
        ppu_ptr->t.writing.bit_z = 0;
    }
    else
    {
        // Set low byte of t
        ppu_ptr->t.writing.low = value;
        // Transfer t to v
        ppu_ptr->v.raw = ppu_ptr->t.raw;
    }
    ppu_ptr->w = !ppu_ptr->w;
}

static inline int GetNameTableIndex(uint16_t addr)
{
    return (addr >> 10) & 0x3;
}

void WriteToNametable(uint16_t addr, uint8_t data)
{
    addr &= 0x2FFF;  // Force address within 0x2000-0x2FFF range
    uint8_t table_index = (addr >> 10) & 3;  // Select which nametable
    nametables[table_index][addr & 0x3FF] = data;
}

uint8_t ReadNametable(uint16_t addr)
{
    //addr &= 0x2FFF;  // Force address within 0x2000-0x2FFF range
    uint8_t table_index = (addr >> 10) & 3;  // Select which nametable
    return nametables[table_index][addr & 0x3FF];
}

// Horizontal scrolling
static void IncX(void)
{
    if (ppu_ptr->v.scrolling.coarse_x == 31) // if coarse X == 31
    {
        //printf("PPU v addr before: 0x%04X\n", ppu_ptr->v.raw);
        ppu_ptr->v.scrolling.coarse_x = 0; // coarse X = 0
        //printf("PPU v addr after coarse_x reset : 0x%04X\n", ppu_ptr->v.raw);
        ppu_ptr->v.scrolling.name_table_sel ^= 0x1; // switch horizontal nametable
        //printf("PPU v addr after nt switch: 0x%04X\n", ppu_ptr->v.raw);
    }
    else
    {
        ppu_ptr->v.scrolling.coarse_x++; // increment coarse X
    }
}

// Vertical Scroll
static void IncY(void)
{
    //printf("PPU v addr: 0x%04X\n", ppu_ptr->v.raw);
    if (ppu_ptr->v.scrolling.fine_y < 7)        // if fine Y < 7
        ppu_ptr->v.scrolling.fine_y++;                      // increment fine Y
    else
    {
        ppu_ptr->v.scrolling.fine_y = 0;                  // fine Y = 0
        if (ppu_ptr->v.scrolling.coarse_y == 29)
        {
            ppu_ptr->v.scrolling.coarse_y = 0;
            ppu_ptr->v.scrolling.name_table_sel ^= 0x2; // Flip vertical nametable bit
        }
        else if (ppu_ptr->v.scrolling.coarse_y == 31)
        {
            //printf("PPU v addr before: 0x%04X\n", ppu_ptr->v.raw);
            ppu_ptr->v.scrolling.coarse_y = 0;// coarse Y = 0, nametable not switched
            //printf("PPU v addr after: 0x%04X\n", ppu_ptr->v.raw);
        }
        else
        {
            // increment coarse Y
            ppu_ptr->v.scrolling.coarse_y++;
        }
    }
}

static uint8_t *background_palettes[4];
static uint8_t *sprite_palettes[4];

static void WriteToPaletteTable(uint16_t addr, const uint8_t data)
{
    //if ((addr & 0x3) == 0) {
    //    addr &= ~0x10;
    //}
    //palette_table[addr] = data;

    if ((addr & 0x3) == 0)
    {
        if (addr < 0x10)
        {
            background_palettes[addr >> 3][0] = data;
            sprite_palettes[addr >> 3][0] = data;
        }
        else
        {
            background_palettes[(addr - 0x10) >> 3][0] = data;
            sprite_palettes[(addr - 0x10) >> 3][0] = data;
        }
    }
    else
    {
        palette_table[addr] = data;
    }
}

void PPU_WriteData(const uint8_t data)
{
    // Extract A13, A12, A11 for region decoding
    uint8_t ppu_region = (ppu_ptr->v.raw >> 11) & 0x7;
    switch (ppu_region)
    {
        case 0x0:
        case 0x1:
        case 0x2:
        case 0x3:
        {
            // chr rom is actually chr ram
            PpuBusWriteChrRam(ppu_ptr->v.raw, data);
            break;
        }
        case 0x4:
        case 0x5:
            //vram[ppu_ptr->v & 0x1FFF] = data;
            WriteToNametable(ppu_ptr->v.raw, data);
            break;
        case 0x6:
            printf("ppu v: 0x%04X\n", ppu_ptr->v.raw);
            break;
        case 0x7:
            WriteToPaletteTable(ppu_ptr->v.raw & 0x1F, data);
            //palette_table[ppu_ptr->v.raw & 0x1F] = data;
            break;
        default:
            break;
    }

    // Outside of rendering, reads from or writes to $2007 will add either 1 or 32 to v depending on the VRAM increment bit set via $2000.
    // During rendering (on the pre-render line and the visible lines 0-239, provided either background or sprite rendering is enabled),
    // it will update v in an odd way, triggering a coarse X increment and a Y increment simultaneously (with normal wrapping behavior).
    if (ppu_ptr->rendering && (ppu_ptr->scanline < 240 || ppu_ptr->scanline == 261))
    {
        IncX();
        IncY();
    }
    else
    {
        ppu_ptr->v.raw += ppu_ptr->ctrl.vram_addr_inc ? 32 : 1;
    }
}

static void PPU_WriteScroll(const uint8_t value)
{
    if (!ppu_ptr->w)
    {
        // First write: X scroll (fine X + coarse X)
        //ppu_ptr->t.raw = (ppu_ptr->t.raw & 0xFFE0) | (value >> 3);
        ppu_ptr->t.scrolling.coarse_x = value >> 3;
        ppu_ptr->x = value & 0x7; // Fine X scroll
    }
    else
    {
        // Second write: Y scroll (fine Y + coarse Y)
        //ppu_ptr->t.raw = (ppu_ptr->t.raw & 0x8C1F) | ((value & 0x7) << 12) | ((value & 0xF8) << 2);
        ppu_ptr->t.scrolling.coarse_y = value >> 3;
        ppu_ptr->t.scrolling.fine_y = value & 0x7;
    }
    ppu_ptr->w = !ppu_ptr->w;
}

uint8_t PPU_ReadStatus(/*Ppu *ppu*/)
{
    uint8_t status_value = ppu_ptr->status.raw;

    // Clear VBlank and write toggle
    ppu_ptr->status.vblank = 0;
    ppu_ptr->w = 0;

    return status_value;
}

uint8_t PPU_ReadData(/*Ppu *ppu*/)
{
    uint16_t addr = ppu_ptr->v.raw & 0x3FFF;
    uint8_t data = 0;

    if (addr >= 0x3F00)
    {  
        // Palette memory (no buffering)
        data = palette_table[addr & 0x1F];  // Mirror palette range
    }
    else if (addr <= 0x1FFF)
    {  
        // VRAM/CHR memory (buffered)
        //data = ppu_ptr->buffered_data;  // Return stale buffer value
        //ppu_ptr->buffered_data = vram[addr];  // Load new data into buffer
        data = ppu_ptr->buffered_data;  // Return stale buffer value
        ppu_ptr->buffered_data = PpuBusReadChrRom(addr);  // Load new data into buffer
    }
    else if (addr <= 0x2FFF)
    {  
        // VRAM/CHR memory (buffered)
        data = ppu_ptr->buffered_data;  // Return stale buffer value
        ppu_ptr->buffered_data = vram[addr & 0x7FF];
    }

    if (ppu_ptr->rendering && (ppu_ptr->scanline < 240 || ppu_ptr->scanline == 261))
    {
        IncX();
        IncY();
    }
    else
    {
        // Auto-increment address
        ppu_ptr->v.raw += ppu_ptr->ctrl.vram_addr_inc ? 32 : 1;
    }

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
    if ((ppu_ptr->cycles < 88974) && (addr == PPU_CTRL))
        return;
    if ((ppu_ptr->cycles < 88974) && (addr == PPU_MASK))
        return;
    if ((ppu_ptr->cycles < 88974) && (addr == PPU_SCROLL))
        return;
    if ((ppu_ptr->cycles < 88974) && (addr == PPU_ADDR))
        return;

    switch (addr)
    {
        case PPU_CTRL:
            ppu_ptr->ctrl.raw = data;
            //ppu_ptr->t.raw = (ppu_ptr->t.raw & 0xF3FF) | ((data & 0x03) << 10);  // Update t (Name table bits)
            ppu_ptr->t.scrolling.name_table_sel = data & 0x3;
            break;
        case PPU_MASK:
            ppu_ptr->mask.raw = data;
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
            PPU_WriteScroll(data);
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
    uint8_t *data_ptr = BusGetPtr(base_addr);
    memcpy(sprites, data_ptr, sizeof(Sprite) * 64);
}

// Configure the mirroring type
void NametableMirroringInit(NameTableMirror mode)
{
    switch (mode) {
        case NAMETABLE_HORIZONTAL:
            nametables[0] = &vram[0x000];      // NT0 (0x2000)
            nametables[1] = &vram[0x000];      // NT0 (Mirrored at 0x2400)
            nametables[2] = &vram[0x400];  // NT1 (0x2800)
            nametables[3] = &vram[0x400];  // NT1 (Mirrored at 0x2C00)
            break;
        
        case NAMETABLE_VERTICAL:
            nametables[0] = &vram[0x000];      // NT0 (0x2000)
            nametables[1] = &vram[0x400];  // NT1 (0x2400)
            nametables[2] = &vram[0x000];      // NT0 (Mirrored at 0x2800)
            nametables[3] = &vram[0x400];  // NT1 (Mirrored at 0x2C00)
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
    background_palettes[0] = &palette_table[0];
    background_palettes[1] = &palette_table[0x4];
    background_palettes[2] = &palette_table[0x8];
    background_palettes[3] = &palette_table[0xC];

    sprite_palettes[0] = &palette_table[0x10];
    sprite_palettes[1] = &palette_table[0x14];
    sprite_palettes[2] = &palette_table[0x18];
    sprite_palettes[3] = &palette_table[0x1C];

    ppu->rendering = false;
    ppu->prev_rendering = false;
    ppu->buffer = pixels;
    ppu->ext_input = 0;
    ppu_ptr = ppu;
}

uint64_t prev_vblank_cycles = 0;
bool nmi_triggered = false;
bool vblank_set = false; // Prevents multiple VBlank triggers in one frame

const uint32_t dots_per_frame_odd = 341 * 261 + 340;
const uint32_t dots_per_frame_even = 341 * 261 + 341;
const uint32_t cpu_cycles_per_frame = dots_per_frame_even / 3; 


static void DrawPixel(uint32_t *buffer, int x, int y, Color color)
{
    if (x < 0 || y < 0 || x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT)
        return;
    //assert(x >= 0 && x <= SCREEN_WIDTH);
    //assert(y >= 0 && y <= SCREEN_HEIGHT);
    buffer[y * SCREEN_WIDTH + x] = (uint32_t)((color.r << 24) | (color.g << 16) | (color.b << 8) | color.a);
}

static void DrawSprite(uint32_t *buffer, int xpos, int ypos, int size, Color color)
{
    for (int x = xpos; x < xpos + 8; x++)
    {
        for (int y = ypos; y < ypos + size; y++)
        {
            DrawPixel(buffer, x, y, color);
        }
    }
}

static inline uint8_t ReverseBits(uint8_t b)
{
    uint8_t r = 0;
    uint8_t byte_len = 2;

    while (byte_len--)
    {
        r = (r << 1) | (b & 1);
        b >>= 1;
    }
    return r;
}

static void PpuDrawSprite(Ppu *ppu, int tile_offset, int tile_x, int tile_y, int palette, bool flip_horz, bool flip_vert)
{
    //const uint8_t *tile_data = &PpuBusReadChrRom[tile_offset];

    for (int y = 0; y < 8; y++)
    {
        uint8_t upper = PpuBusReadChrRom(tile_offset + y); //tile_data[y];
        uint8_t lower = PpuBusReadChrRom(tile_offset + y + 8); //tile_data[y + 8];

        for (int x = 7; x >= 0; x--)
        {
            uint8_t pixel = (1 & lower) << 1 | (1 & upper);
            upper >>= 1;
            lower >>= 1;

            if (!pixel)
                continue;
    
            Color color = GetspriteColor(palette, pixel);

            if (flip_horz && flip_vert)
                DrawPixel(ppu->buffer, tile_x + 7 - x, tile_y + 7 - y, color);
            else if (flip_horz)
                DrawPixel(ppu->buffer, tile_x + 7 - x, tile_y + y, color);
            else if (flip_vert)
                DrawPixel(ppu->buffer, tile_x + x, tile_y + 7 - y, color);
            else
                DrawPixel(ppu->buffer, tile_x + x, tile_y + y, color);
        }
    }
}

static void PpuDrawSprite16(Ppu *ppu, int tile_index, int tile_x, int tile_y, int palette, bool flip_horz, bool flip_vert)
{
    int bank = (tile_index & 1) ? 0x1000 : 0x0000;
    int tile_id = tile_index & 0xFE; 

    for (int y = 0; y < 16; y++)
    {
        //int tile_part = (y < 8) ? 0 : 1;
        int tile_part = (y < 8) ^ flip_vert ? 0 : 1;  // Swap tile part if flipping vertically
        size_t tile_offset = bank + (tile_id + tile_part) * 16;
        int row = y & 7;
        if (flip_vert)
            row = 7 - (y & 7); 

        uint8_t upper = PpuBusReadChrRom(tile_offset + row); 
        uint8_t lower = PpuBusReadChrRom(tile_offset + row + 8);

        for (int x = 7; x >= 0; x--)
        {
            // Apply horizontal flip
            int bit = flip_horz ? x : 7 - x;
            uint8_t pixel = ((upper >> bit) & 1) | (((lower >> bit) & 1) << 1);

            if (!pixel)
                continue;
    
            Color color = GetspriteColor(palette, pixel);

            DrawPixel(ppu->buffer, tile_x + x, tile_y + y, color);
        }
    }
}

static void PpuDrawSprite8x16(Ppu *ppu, int tile_index, int tile_x, int tile_y, int palette, bool flip_horz, bool flip_vert)
{
    int bank = (tile_index & 1) ? 0x1000 : 0x0000;  
    int tile_id = tile_index & 0xFE; 

    for (int y = 0; y < 16; y++)
    {
        int tile_part = (y < 8) ^ flip_vert ? 0 : 1;  // Swap tile part if flipping vertically
        int row = flip_vert ? (7 - (y & 7)) : (y & 7);  

        int tile_offset = bank + (tile_id + tile_part) * 16; 
        uint8_t upper = PpuBusReadChrRom(tile_offset + row); //tile_data[y];
        uint8_t lower = PpuBusReadChrRom(tile_offset + row + 8); //tile_data[y + 8];

        for (int x = 7; x >= 0; x--)
        {
            int col = flip_horz ? x : 7 - x;  // Apply horizontal flip

            uint16_t pixel = ((lower >> col) & 1) << 1 | ((upper >> col) & 1);
            if (!pixel)
                continue;

            Color color = GetspriteColor(palette, pixel);
            DrawPixel(ppu->buffer, tile_x + x, tile_y + y, color);
        }
    }
}

static void DrawSpritePlaceholder(Ppu *ppu, int scanline, int cycle)
{
    if (!ppu->mask.sprites_rendering)
        return;

    uint16_t bank = ppu->ctrl.sprite_pat_table_addr ? 0x1000: 0;

    for (int n = 0; n < 64; n++)
    {
        Sprite *curr_sprite =  &sprites[n];

        uint16_t palette = curr_sprite->attribs.palette;
        size_t tile_offset = bank + (curr_sprite->tile_id * 16);
        if (curr_sprite->y < 241 && curr_sprite->x < 255)
        {
            if (ppu->ctrl.sprite_size)
            {
                //PpuDrawSprite8x16(ppu, curr_sprite->tile_id, curr_sprite->x, curr_sprite->y + 1,
                //    palette,curr_sprite->attribs.horz_flip, curr_sprite->attribs.vert_flip);
                //bank = (curr_sprite->tile_id & 1) ? 0x1000: 0;
                PpuDrawSprite16(ppu, curr_sprite->tile_id , curr_sprite->x, curr_sprite->y + 1,
                                  palette,curr_sprite->attribs.horz_flip, curr_sprite->attribs.vert_flip);
                //PpuDrawSprite(ppu, tile_offset, curr_sprite->x, curr_sprite->y + 1, palette,
                //    curr_sprite->attribs.horz_flip, curr_sprite->attribs.vert_flip);
                //    tile_offset = bank + (curr_sprite->tile_id + 1 * 16);
                //PpuDrawSprite(ppu, tile_offset, curr_sprite->x, curr_sprite->y + 9, palette,
                //    curr_sprite->attribs.horz_flip, curr_sprite->attribs.vert_flip);
            }
            else
            {
                PpuDrawSprite(ppu, tile_offset, curr_sprite->x, curr_sprite->y + 1, palette,
                   curr_sprite->attribs.horz_flip, curr_sprite->attribs.vert_flip);
            }

            //PpuDrawSprite(ppu, tile_offset + 8, curr_sprite->x, curr_sprite->y + 9, palette,
            //        curr_sprite->attribs.horz_flip, curr_sprite->attribs.vert_flip);
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
        DrawSpritePlaceholder(ppu, 240, 256);
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

static void SpriteEval2(int scanline, int cycle)
{
    int n;
    int m;
step1:
    for (n = 0; n < 64; n++)
    {
        int y_coord = sprites[n].raw[0];
        if (scanline >= y_coord && scanline < y_coord + (ppu_ptr->ctrl.sprite_size ? 16 : 8))
        {
            if (sprite_count < 8)
            {
                // Copy full sprite data into secondary OAM
                sprites_secondary[sprite_count] = sprites[n];
                sprite_count++;
            }
            else
            {
                ppu_ptr->status.sprite_overflow = 1;
            }
        }
    }
    if (sprite_count == 8)
        return;
    else if (sprite_count < 8)
        goto step1;
    else
        goto step4;
step3:
    m = 0;
    for (m = 0; m < 4; m++)
    {
        int y_coord = sprites[n].raw[m];
        if (scanline >= y_coord && scanline < y_coord + (ppu_ptr->ctrl.sprite_size ? 16 : 8))
        {
    
        }
    }
    int y_coord = sprites[n].raw[m];
    if (scanline >= y_coord && scanline < y_coord + (ppu_ptr->ctrl.sprite_size ? 16 : 8))
    {

    }
    else 
    {

    }
step4:
    n++;
    return;
}
static void Step3(int *n, int scanline, int cycle)
{
    //int m = 0;

    for (int m = 0; m < 4; m++)
    {
        int y_coord = sprites[*n].raw[m];
        if (scanline >= y_coord && scanline < y_coord + (ppu_ptr->ctrl.sprite_size ? 16 : 8))
        {
            ppu_ptr->status.sprite_overflow = 1;
        }
        else 
        {
            *n = *n += 1;
        }
    }

    if (scanline >= sprite_y && scanline < sprite_y + (ppu_ptr->ctrl.sprite_size ? 16 : 8))
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
}

static int Step4(int *n, int scanline, int cycle)
{

}


// 192 cycles
// 65 -> 256 cycles
static void SpriteEval(int scanline, int cycle)
{
    //if (cycle < 65 || cycle > 257)
    //    return;
//
    //if (cycle == 65)
    //{
    //    sprite_index = 0;
    //    sprite_count = 0;
    //}

    if (cycle != 256)
        return;

    //while (sprite_index < 64)
    //int n = 0;
    //int m = 0;
    //for (int i = 1; i <= 192; i++)
    //{
    //    while (n < 64)
    //    if (i & 1)
    //    {
    //        sprite_y = sprites[sprite_index].y;
    //    }
    //    else
    //    {
    //    
    //    }
    //}

    SpriteEval2(scanline, cycle);

    //if (sprite_index < 64)
    //{
    //    if (cycle & 1)
    //    {
    //        sprite_y = sprites[sprite_index].y;
    //    }
    //    else if (scanline >= sprite_y && scanline < sprite_y + (ppu_ptr->ctrl.sprite_size ? 16 : 8))
    //    {
    //        if (sprite_count < 8)
    //        {
    //            // Copy full sprite data into secondary OAM
    //            sprites_secondary[sprite_count] = sprites[sprite_index];
    //            sprite_count++;
    //        }
    //        else
    //        {
    //            ppu_ptr->status.sprite_overflow = 1;
    //        }
    //    }
    //
    //    if (++sprite_index == 64)
    //    {
    //        sprite_index = 0;
    //    }
    //}

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

static void PPU_DrawScanline(Ppu *ppu, int scanline)
{
    // Y max = 240
    // X max = 256
    int row = scanline / 8;
    uint8_t *nametable = nametables[GetNameTableIndex(ppu->v.raw)];
    uint8_t *attribute_table = &nametable[0x3C0];
    uint16_t bank = ppu->ctrl.bg_pat_table_addr ? 0x1000: 0;

    for (int x = 0; x < 256; x++)
    {
        int column = x / 8;
        int fine_x = x & 7;  // Pixel within tile (0-7)
        int fine_y = scanline & 7; // Row within tile
        //uint16_t tile_addr = row * column;
        uint16_t tile_addr = row * 32 + column;
        uint16_t tile_index = nametable[tile_addr];
        uint16_t attrib_addr = (row / 4) * 8 + (column / 4);
        uint16_t attrib_data = attribute_table[attrib_addr & 0x03F];

        // Select the 2-bit palette for this tile
        int shift = ((row & 3) / 2) * 4 + ((column & 3) / 2) * 2;
        uint8_t palette_select = (attrib_data >> shift) & 0x03;

        // Get pattern table address for this tile
        size_t tile_offset = bank + (tile_index * 16) + fine_y;
        uint8_t upper = PpuBusReadChrRom(tile_offset);       // Bitplane 0
        uint8_t lower = PpuBusReadChrRom(tile_offset + 8);   // Bitplane 1

        // Extract the correct pixel from bitplanes
        uint8_t bit = 7 - fine_x; // NES tiles are stored MSB first
        uint8_t value = ((upper >> bit) & 1) | (((lower >> bit) & 1) << 1);

        Color color = GetColor(palette_select, value);
        DrawPixel(ppu->buffer, x, scanline, color);
        //if (!(x & 7))
        //{
        //    if (ppu->v.scrolling.coarse_x == 31) // if coarse X == 31
        //    {
        //        ppu->v.scrolling.coarse_x = 0; // coarse X = 0
        //        ppu->v.scrolling.name_table_sel ^= 0b01; // switch horizontal nametable
        //    }
        //    else
        //    {
        //        ppu->v.scrolling.coarse_x++;             // increment coarse X
        //    }
        //}
    }
}

static void PPU_DrawDot(Ppu *ppu, int scanline, int cycle)
{
    // Y max = 240
    // X max = 256
    int row = scanline / 8;
    uint8_t *nametable = nametables[GetNameTableIndex(ppu->v.raw)];
    uint8_t *attribute_table = &nametable[0x3C0];
    uint16_t bank = ppu->ctrl.bg_pat_table_addr ? 0x1000: 0;

    int column = cycle / 8;
    int fine_x = cycle & 7;  // Pixel within tile (0-7)
    int fine_y = scanline & 7; // Row within tile
    //uint16_t tile_addr = row * column;
    uint16_t tile_addr = row * 32 + column;
    uint16_t tile_index = nametable[tile_addr];
    uint16_t attrib_addr = (row / 4) * 8 + (column / 4);
    uint16_t attrib_data = attribute_table[attrib_addr & 0x03F];

    // Select the 2-bit palette for this tile
    int shift = ((row & 3) / 2) * 4 + ((column & 3) / 2) * 2;
    uint8_t palette_select = (attrib_data >> shift) & 0x03;

    // Get pattern table address for this tile
    size_t tile_offset = bank + (tile_index * 16) + fine_y;
    uint8_t upper = PpuBusReadChrRom(tile_offset);       // Bitplane 0
    uint8_t lower = PpuBusReadChrRom(tile_offset + 8);   // Bitplane 1

    // Extract the correct pixel from bitplanes
    uint8_t bit = 7 - fine_x; // NES tiles are stored MSB first
    uint8_t value = ((upper >> bit) & 1) | (((lower >> bit) & 1) << 1);
    Color color = GetColor(palette_select, value);
    DrawPixel(ppu->buffer, cycle, scanline, color);
    //if (!(cycle & 7))
    //{
    //    if (ppu->v.scrolling.coarse_x == 31) // if coarse X == 31
    //    {
    //        ppu->v.scrolling.coarse_x = 0; // coarse X = 0
    //        ppu->v.scrolling.name_table_sel ^= 0b01; // switch horizontal nametable
    //    }
    //    else
    //    {
    //        ppu->v.scrolling.coarse_x++;             // increment coarse X
    //    }
    //}
}

static void PPU_DrawTile(Ppu *ppu, int scanline)
{
    int row = scanline / 8;
    uint8_t *nametable = nametables[GetNameTableIndex(ppu->v.raw)];
    uint8_t *attribute_table = &nametable[0x3C0];
    uint16_t bank = ppu->ctrl.bg_pat_table_addr ? 0x1000: 0;
    for (int x = 0; x < 8; x++)
    {
        int column = x / 8;
        int fine_x = x & 7;  // Pixel within tile (0-7)
        int fine_y = scanline & 7; // Row within tile
        //uint16_t tile_addr = row * column;
        //uint16_t tile_addr = row * 32 + column;
        //uint16_t tile_index = nametable[tile_addr];
        uint16_t tile_addr = 0x2000 | (ppu->v.raw & 0x0FFF);
        uint16_t tile_index = vram[tile_addr & 0x7FF];
        //uint16_t attrib_addr = (row / 4) * 8 + (column / 4);
        uint16_t attrib_addr = (ppu->v.raw & 0x0C00) | ((ppu->v.raw >> 4) & 0x38) | ((ppu->v.raw >> 2) & 0x07);
        uint16_t attrib_data = attribute_table[attrib_addr & 0x3F];

        // Select the 2-bit palette for this tile
        int shift = ((row & 3) / 2) * 4 + ((column & 3) / 2) * 2;
        uint8_t palette_select = (attrib_data >> shift) & 0x03;
        //int shift = ((ppu->v.scrolling.coarse_y & 2) << 1) | (ppu->v.scrolling.coarse_x & 2);
        //uint8_t palette_select = (attrib_data >> shift) & 0x03;

        // Get pattern table address for this tile
        size_t tile_offset = bank + (tile_index * 16) + fine_y;
        uint8_t upper = PpuBusReadChrRom(tile_offset);       // Bitplane 0
        uint8_t lower = PpuBusReadChrRom(tile_offset + 8);   // Bitplane 1

        // Extract the correct pixel from bitplanes
        uint8_t bit = 7 - fine_x; // NES tiles are stored MSB first
        uint8_t value = ((upper >> bit) & 1) | (((lower >> bit) & 1) << 1);

        Color color = GetColor(palette_select, value);
        DrawPixel(ppu->buffer, x, scanline, color);
        if (x && ppu->prev_rendering && !(x & 7))
            IncX();
    }
    //IncY();
    //IncX();
}

/*
static void PPU_DrawTile2(Ppu *ppu, int cycle)
{
    //uint16_t pattern_shift_low  <<= 1;
    //uint16_t pattern_shift_high <<= 1;
    //uint16_t attrib_shift_low  <<= 1;
    //uint16_t attrib_shift_high <<= 1;

    uint8_t *nametable = nametables[ppu->v.scrolling.name_table_sel];
    uint8_t *attribute_table = &nametable[0x3C0];
    uint16_t bank = ppu->ctrl.bg_pat_table_addr ? 0x1000: 0;

    // At specific cycles (every 8), reload shift registers with next tile data
    switch (cycle & 7)
    {
        case 1:
        {
            // Fetch tile index from nametable
            uint16_t addr = 0x2000 | (ppu->v.raw & 0x0FFF);
            ppu->next_tile_index = vram[addr & 0x7FF];
            break;
        }
    
        case 3:
        {
            // Fetch attribute byte
            uint16_t attr_addr = 0x23C0 | (ppu->v.raw & 0x0C00) | ((ppu->v.scrolling.coarse_y >> 2) << 3) | (ppu->v.scrolling.coarse_x >> 2);
            ppu->next_tile_attrib = vram[attr_addr & 0x7FF];
            break;
        }

        case 5:
        {
            // Fetch pattern table LSB
            uint16_t tile_addr = (ppu->ctrl.bg_pat_table_addr << 12) + (ppu->next_tile_index * 16) + ppu->v.scrolling.fine_y;
            ppu->next_tile_lsb = PpuBusReadChrRom[tile_addr];
            break;
        }

        case 7:
            // Fetch pattern table MSB
            tile_addr = (ppu->ctrl.bg_pattern_table << 12) + (ppu->next_tile_index * 16) + ppu->v.scrolling.fine_y;
            ppu->next_tile_msb = PpuBusReadChrRom[tile_addr + 8];
            break;

        case 0:
            // Load fetched data into shift registers
            ppu->pattern_shift_low  = (ppu->pattern_shift_low  & 0xFF00) | ppu->next_tile_lsb;
            ppu->pattern_shift_high = (ppu->pattern_shift_high & 0xFF00) | ppu->next_tile_msb;

            // Decode attribute bits for the tile
            uint8_t quadrant = ((ppu->v.scrolling.coarse_y & 2) << 1) | (ppu->v.scrolling.coarse_x & 2);
            uint8_t attrib_bits = (ppu->next_tile_attrib >> quadrant) & 0x03;
            ppu->attrib_shift_low  = (ppu->attrib_shift_low  & 0xFF00) | ((attrib_bits & 1) ? 0xFF : 0x00);
            ppu->attrib_shift_high = (ppu->attrib_shift_high & 0xFF00) | ((attrib_bits & 2) ? 0xFF : 0x00);

            // Increment coarse X (scrolling)
            IncX();
            break;
    }

    // Rendering a pixel (every dot)
    uint8_t bit_mux = 0x8000 >> ppu->x;  // fine X scroll masks shift registers
    uint8_t pixel_low  = (ppu->pattern_shift_low  & bit_mux) ? 1 : 0;
    uint8_t pixel_high = (ppu->pattern_shift_high & bit_mux) ? 1 : 0;
    uint8_t palette_low  = (ppu->attrib_shift_low  & bit_mux) ? 1 : 0;
    uint8_t palette_high = (ppu->attrib_shift_high & bit_mux) ? 1 : 0;

    uint8_t color_id = (palette_high << 3) | (palette_low << 2) | (pixel_high << 1) | pixel_low;

    // Final color = get_color(color_id)
    DrawPixel(x, y, color_id);
}
*/

static void PPU_DrawDotv2(Ppu *ppu, int scanline, int cycle)
{
    // Y max = 240
    // X max = 256
    int row = scanline / 8;
    int name_table_sel = GetNameTableIndex(ppu->v.raw);
    uint8_t *nametable = nametables[ppu->v.scrolling.name_table_sel];
    uint8_t *attribute_table = &nametable[0x3C0];
    uint16_t bank = ppu->ctrl.bg_pat_table_addr ? 0x1000: 0;

    int column = cycle / 8;
    //int fine_x = ppu->x;  // Pixel within tile (0-7)
    int fine_y = ppu->v.scrolling.fine_y; // Row within tile
    int fine_x = (cycle - 1) & 7;  // Pixel within tile (0-7)
    //int fine_y = scanline & 7; // Row within tile

    //uint16_t attrib_addr = 0x23C0 | (ppu->v.raw & 0x0C00) | ((ppu->v.raw >> 4) & 0x38) | ((ppu->v.raw >> 2) & 0x07);
    uint16_t attrib_addr = (ppu->v.raw & 0x0C00) | ((ppu->v.raw >> 4) & 0x38) | ((ppu->v.raw >> 2) & 0x07);
    //uint16_t tile_addr = 0x2000 | (ppu->v.raw & 0x0FFF);
    uint16_t tile_addr = (ppu->v.raw & 0x0FFF);

    // Correct
    uint16_t tile_index = nametable[tile_addr & 0x3FF];
    uint16_t attrib_data = attribute_table[attrib_addr & 0x3f];

    // Select the 2-bit palette for this tile
    //int shift = ((row & 3) / 2) * 4 + ((column & 3) / 2) * 2;
    //int shift = 7 - (ppu->v.raw & 0x07);
    //uint8_t palette_select = (attrib_data >> shift) & 0x03;
    int shift = ((ppu->v.scrolling.coarse_y & 2) << 1) | (ppu->v.scrolling.coarse_x & 2);
    uint8_t palette_select = (attrib_data >> shift) & 0x3;

    // Get pattern table address for this tile
    size_t tile_offset = bank + (tile_index * 16) + fine_y;
    uint8_t upper = PpuBusReadChrRom(tile_offset);       // Bitplane 0
    uint8_t lower = PpuBusReadChrRom(tile_offset + 8);   // Bitplane 1

    // Fetch current and next tile index
    uint16_t tile_index1 = nametable[tile_addr & 0x3FF];
    uint16_t tile_index2 = nametable[(tile_addr + 1) & 0x3FF];  // Next tile in scanline

    // Pattern addresses
    size_t tile_offset1 = bank + (tile_index1 * 16) + fine_y;
    size_t tile_offset2 = bank + (tile_index2 * 16) + fine_y;

    // Fetch pattern bytes for both tiles
    uint8_t upper1 = PpuBusReadChrRom(tile_offset1);
    uint8_t lower1 = PpuBusReadChrRom(tile_offset1 + 8);

    uint8_t upper2 = PpuBusReadChrRom(tile_offset2);
    uint8_t lower2 = PpuBusReadChrRom(tile_offset2 + 8);

    // Merge into 16-bit shift buffers (simulate shift registers)
    uint16_t upper_shift = (upper1 << 8) | upper2;
    uint16_t lower_shift = (lower1 << 8) | lower2;

    // Calculate bit index with fine X shift
    int bit = 15 - (ppu->x + ((cycle - 1) & 7));  // Start at leftmost bit after fine X shift

    // Extract pixel
    uint8_t value = ((upper_shift >> bit) & 1) | (((lower_shift >> bit) & 1) << 1);

    // Extract the correct pixel from bitplanes
    //int bit = 7 - ((cycle - 1 + ppu->x) & 7);
    //int tile_pixel = ((cycle - 1 + ppu->x) % 8);
    //int bit = 7 - tile_pixel;

    //int bit = 15 - ppu->x;
    ////if (!ppu->x)
    ////{
    //    uint8_t value = ((upper >> bit) & 1) | (((lower >> bit) & 1) << 1);

        Color color = GetColor(palette_select, value);
    
        DrawPixel(ppu->buffer, cycle - 1, scanline, color);
    //}
    //else {
    //    printf("Fine X: %d\n", ppu->x);
    //    printf("Coarse X: %d\n", ppu->v.scrolling.coarse_x);
    //}

    if (ppu->rendering && !(cycle & 7))
        IncX();
}

static inline void PpuShiftRegsUpdate(Ppu *ppu)
{
    ppu->bg_shift_low.raw <<= 1;
    ppu->bg_shift_high.raw <<= 1;
    ppu->attrib_shift_low.raw <<= 1;
    ppu->attrib_shift_high.raw <<= 1;
    //ppu->attrib_shift_low.low <<= 1;
    //ppu->attrib_shift_high.high <<= 1;
}

static inline void PpuFetchShifters(Ppu *ppu)
{
    //ppu->bg_shift_low.high = ppu->bg_shift_low.low;
    //ppu->bg_shift_low.low = ppu->bg_lsb;
//
    //ppu->bg_shift_high.low = ppu->bg_shift_high.high;
    //ppu->bg_shift_high.high = ppu->bg_msb;
    //ppu->bg_shift_low.raw = ppu->bg_shift_low.high | ppu->bg_lsb;
    //ppu->bg_shift_high.raw = ppu->bg_shift_high.high | ppu->bg_msb;

    ppu->bg_shift_low.raw = (ppu->bg_shift_low.raw & 0xFF00) | ppu->bg_lsb;
    ppu->bg_shift_high.raw = (ppu->bg_shift_high.raw & 0xFF00) | ppu->bg_msb;

    //if (ppu->bg_shift_low.raw != 0)
    //{
    //    printf("bg shift low low: %d\n", ppu->bg_shift_low.low);
    //    printf("bg shift low high: %d\n", ppu->bg_shift_low.high);
    //}
//
    //if (ppu->bg_shift_high.raw != 0)
    //{
    //    printf("bg shift high low: %d\n", ppu->bg_shift_high.low);
    //    printf("bg shift high high: %d\n", ppu->bg_shift_high.high);
    //}
    uint8_t latch_low = ppu->attrib_data & 0x01;
    uint8_t latch_high = (ppu->attrib_data >> 1) & 0x01;

    //ppu->attrib_shift_low.raw = ppu->attrib_shift_low.high | latch_low ? 0xFF : 0x00;
    //ppu->attrib_shift_high.raw = ppu->attrib_shift_high.high | latch_high ? 0xFF : 0x00;
    //uint8_t quadrant = ((ppu->v.scrolling.coarse_y & 2) << 1) | (ppu->v.scrolling.coarse_x & 2);
    //uint8_t attrib_bits = (ppu->attrib_data >> quadrant) & 0x03;
    //ppu->attrib_shift_low.raw  = (ppu->attrib_shift_low.raw  & 0xFF00) | ((ppu->attrib_data & 0b01) ? 0xFF : 0x00);
    //ppu->attrib_shift_high.raw = (ppu->attrib_shift_high.raw & 0xFF00) | ((ppu->attrib_data & 0b10) ? 0xFF : 0x00);
    ppu->attrib_shift_low.raw = (ppu->attrib_shift_low.raw & 0xFF00) | ((ppu->attrib_data & 0x1) ? 0xFF : 0x00);
    ppu->attrib_shift_high.raw = (ppu->attrib_shift_high.raw & 0xFF00) | ((ppu->attrib_data & 0x2) ? 0xFF : 0x00);
}

static void PpuRender(Ppu *ppu, int scanline, int cycle)
{
    uint8_t *nametable = nametables[ppu->v.scrolling.name_table_sel];
    uint8_t *attribute_table = &nametable[0x3C0];
    uint16_t bank = ppu->ctrl.bg_pat_table_addr ? 0x1000: 0;

    if (ppu->mask.bg_rendering)
        PpuShiftRegsUpdate(ppu);

    switch (cycle & 7)
    {
        case 0:
        {
            //ppu->bg_shift_low.high = ppu->bg_lsb;
            //ppu->bg_shift_high.high = ppu->bg_msb;

            // Extract 2-bit attribute for tile quadrant
            //uint8_t quadrant = ((ppu->v.scrolling.coarse_y & 2) << 1) | (ppu->v.scrolling.coarse_x & 2);
            //uint8_t attrib_bits = (ppu->attrib_data >> quadrant) & 0x03;
            if (ppu->rendering)
                IncX();
            break;
        }
        case 1:
        {
            PpuFetchShifters(ppu);
            break;
        }
        case 2:
        {
            const uint16_t tile_addr = (ppu->v.raw & 0x0FFF);
            ppu->tile_id = nametable[tile_addr & 0x3FF];
            break;
        }
        case 4:
        {
            uint16_t attrib_addr = (ppu->v.raw & 0x0C00) | ((ppu->v.raw >> 4) & 0x38) | ((ppu->v.raw >> 2) & 0x07);
            uint8_t attrib_data = attribute_table[attrib_addr & 0x3f];
            if (ppu->v.scrolling.coarse_y & 0x02)
                attrib_data >>= 4;
            if (ppu->v.scrolling.coarse_x & 0x02)
                attrib_data >>= 2;
            //uint8_t quadrant = ((ppu->v.scrolling.coarse_y & 2) << 1) | (ppu->v.scrolling.coarse_x & 2);
            //uint8_t attrib_bits = (ppu->attrib_data >> quadrant) & 0x03;
            ppu->attrib_data = attrib_data & 0x3;
            //uint8_t attrib_data = attribute_table[attrib_addr & 0x3f];
            //uint8_t quadrant = ((ppu->v.scrolling.coarse_y & 2) << 1) | (ppu->v.scrolling.coarse_x & 2);
            //ppu->next_palette = (ppu->attrib_data >> quadrant) & 0x3;
            //uint8_t attrib_bits = (ppu->attrib_data >> quadrant) & 0x03;
            break;
        }
        case 6:
        {
            // Get pattern table address for this tile
            size_t tile_offset = bank + (ppu->tile_id * 16) + ppu->v.scrolling.fine_y;
            // Bitplane 0
            ppu->bg_lsb = PpuBusReadChrRom(tile_offset);
            break;
        }
        case 7:
        {
            // Get pattern table address for this tile
            size_t tile_offset = bank + (ppu->tile_id * 16) + ppu->v.scrolling.fine_y;
            // Bitplane 1
            ppu->bg_msb = PpuBusReadChrRom(tile_offset + 8);
            break;
        }
    }

    if (ppu->rendering && scanline < 240 && cycle < 257)
    {
        // Rendering a pixel (every dot)
        int bit = 15 - ppu->x;
        uint16_t bit_mux = 0x8000 >> ppu->x;  // fine X scroll masks shift registers
        //uint8_t pixel_low  = (ppu->bg_shift_low.raw & bit_mux) > 0;
        //uint8_t pixel_high = (ppu->bg_shift_high.raw & bit_mux) > 0;
        uint8_t palette_low  = (ppu->attrib_shift_low.raw & bit_mux) > 0;
        uint8_t palette_high = (ppu->attrib_shift_high.raw & bit_mux) > 0;
        uint8_t pixel_low  = (ppu->bg_shift_low.raw >> bit) & 1;
        uint8_t pixel_high = (ppu->bg_shift_high.raw >> bit) & 1;
        //uint8_t palette_low  = (ppu->attrib_shift_low.raw & bit_mux) > 0;
        //uint8_t palette_high = (ppu->attrib_shift_high.raw & bit_mux) > 0;
        //uint8_t palette_low  = (ppu->attrib_shift_low.raw >> bit) & 1;
        //uint8_t palette_high = (ppu->attrib_shift_high.low >> bit) & 1;
        //uint8_t palette = (palette_high << 1) | palette_low;

        uint8_t pixel = (pixel_high << 1) | pixel_low;
        uint8_t palette = (palette_high << 1) | palette_low;

        // Draw pixel here
        Color color = GetColor(palette, pixel);
        DrawPixel(ppu->buffer, cycle - 1, scanline, color);
    }
}

static bool PpuSprite0Hit(Ppu *ppu, int cycle, int scanline)
{
    return sprites[0].y == scanline - 6 && cycle <= sprites[0].x && ppu->mask.sprites_rendering;
}

static void PpuPreRenderLine(Ppu *ppu, int cycle, int scanline)
{
    if (scanline != 261)
        return;

    // Clear VBlank flag at scanline 261, dot 1
    if (cycle == 1)
    {
        ppu->status.vblank = 0;
        ppu->status.sprite_overflow = 0;
        ppu->status.sprite_hit = 0;
    }    

    if (cycle > 279 && cycle < 305)
    {
        // reset scroll
        if (ppu->rendering)
        {
            //ppu->v.raw = (ppu->v.raw & ~0x7BE0) | (ppu->t.raw & 0x7BE0);
            //ppu->v.raw = (ppu->v.raw & ~0x041F) | (ppu->t.raw & 0x041F);
            ppu->v.scrolling.coarse_y = ppu->t.scrolling.coarse_y;
            ppu->v.scrolling.fine_y = ppu->t.scrolling.fine_y;
            ppu->v.raw_bits.bit11 = ppu->t.raw_bits.bit11;
        }
    }
}

static void PpuPostRenderLine(Ppu *ppu, int cycle, int scanline)
{
    if (scanline == 241 && cycle == 1)
    {
        //printf("PPU v addr: 0x%04X\n", ppu->v.raw);
        //assert(ppu->v.raw != 0);
        //ppu->v.raw = ppu->v.raw & 0x3FFF;
        // VBlank starts at scanline 241
        ppu->status.vblank = 1;
        // If NMI is enabled
        if (ppu->ctrl.vblank_nmi)
        {
            nmi_triggered = true;
        }
    }
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
        int ppu_cycle_counter = (ppu->cycles % 341);
        int scanline = (ppu->cycles / 341) % 262; // 1 scanline = 341 PPU cycles
        ppu->cycles++;
        ppu_cycles_to_run--;

        //if (scanline <= 240)
        //{
            //if (scanline && scanline < 240 && ppu_cycle_counter == 256)
            //    PPU_DrawScanline(ppu, scanline);
            //if (ppu_cycle_counter && (scanline < 240) && ppu_cycle_counter < 257)
            //    PPU_DrawDotv2(ppu, scanline, ppu_cycle_counter);
            if (ppu_cycle_counter && (scanline < 240 || scanline == 261) && (ppu_cycle_counter < 257 || (ppu_cycle_counter >= 321 && ppu_cycle_counter <= 336)))
                PpuRender(ppu, scanline, ppu_cycle_counter);

            
            //if (scanline < 240 && ppu_cycle_counter == 256)
            //    DrawSpritePlaceholder(ppu, scanline, ppu_cycle_counter);
            //if (ppu_cycle_counter && scanline < 240 && ppu_cycle_counter < 257)
            //    PPU_DrawSprite(ppu, ppu_cycle_counter, scanline);
            //Color color = {
            //    .r = 255,
            //    .g = 0,
            //    .b = 0,
            //    .a = 255
            //};
            //DrawSprite(ppu->buffer, 100, 100, 8,  color);

            //if ((scanline < 240 || scanline == 261) && ppu->prev_rendering && (ppu_cycle_counter < 257) && !(ppu_cycle_counter & 7))
            //{
            //    IncX();
            //    //if (ppu_cycle_counter == 256)
            //    //    IncY();
            //}

            //if (scanline < 240 && ppu_cycle_counter < 256)
            //    PPU_DrawDotv3(ppu, scanline, ppu_cycle_counter);

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
            //PrepareSpriteData(scanline, ppu_cycle_counter);
            //if (scanline > 0 && scanline < 240 && ppu_cycle_counter == 256)
            //{
            //    Render(ppu);
            //}
        //}

        //if (scanline < 240)
        //    PrepareSpriteData(scanline, ppu_cycle_counter);
        //if (scanline == 30 && ppu_cycle_counter != 255 /*&& ppu_cycle_counter == 96*/)
        ////if (scanline == sprites[0].y /*&& ppu_cycle_counter == 100*/)
        //{
        //    if (ppu->rendering && !ppu->status.sprite_hit && (ppu_cycle_counter > 7 || !(ppu->mask.raw & 0x3)))
        //    {
        //        ppu->status.sprite_hit = 1; // !ppu->status.sprite_hit;
        //    }
        //}
        if (PpuSprite0Hit(ppu, ppu_cycle_counter, scanline))
        {
            if (ppu->rendering && !ppu->status.sprite_hit && (ppu_cycle_counter > 7 || !(ppu->mask.raw & 0x3)))
            {
                ppu->status.sprite_hit = 1;
            }
        }
        if (ppu->rendering && ppu_cycle_counter == 256 && (scanline < 240 || scanline == 261))
            IncY();
        if (ppu->rendering && ppu_cycle_counter == 257 && (scanline < 240 || scanline == 261))
        {
            ppu->v.scrolling.coarse_x = ppu->t.scrolling.coarse_x;
            ppu->v.raw_bits.bit10 = ppu->t.raw_bits.bit10;
        }
        if (scanline == 241 && ppu_cycle_counter == 1)
        {
            //printf("PPU v addr: 0x%04X\n", ppu->v.raw);
            //assert(ppu->v.raw != 0);
            ppu->v.raw = ppu->v.raw & 0x3FFF;
            // VBlank starts at scanline 241
            ppu->status.vblank = 1;
            // If NMI is enabled
            if (ppu->ctrl.vblank_nmi)
            {
                nmi_triggered = true;
            }
        }
        // Clear VBlank flag at scanline 261, dot 1
        if (scanline == 261 && ppu_cycle_counter == 1)
        {
            ppu->status.vblank = 0;
            ppu->status.sprite_hit = 0;
            ppu->status.sprite_overflow = 0;

        }
        if (scanline == 261 && (ppu_cycle_counter >= 280 && ppu_cycle_counter < 305))
        {
            // reset scroll
            if (ppu->rendering)
            {
                ppu->v.scrolling.coarse_y = ppu->t.scrolling.coarse_y;
                ppu->v.scrolling.fine_y = ppu->t.scrolling.fine_y;
                ppu->v.raw_bits.bit11 = ppu->t.raw_bits.bit11;
            }
        }

        ppu->scanline = scanline;
    }
    ppu->rendering = ppu->mask.bg_rendering || ppu->mask.sprites_rendering;
    PPU_IsFrameDone(ppu);
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

}
