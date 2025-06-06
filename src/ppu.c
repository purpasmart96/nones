#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>

#include <SDL3/SDL.h>

#include "apu.h"
#include "ppu.h"
#include "cpu.h"
#include "joypad.h"
#include "arena.h"
#include "cart.h"
#include "system.h"
#include "nones.h"
#include "utils.h"

static uint8_t vram[0x800];
// Pointers to handle mirroring
static uint8_t *nametables[4];
// OAM
static Sprite sprites[64];
// OAM Secondary
static Sprite sprites_secondary[8];
static uint8_t palette_table[32];

// For sprite priority and sprite 0 hit
static uint8_t bg_pixels[SCREEN_WIDTH][SCREEN_HEIGHT];
static uint8_t sprite0_pixels[SCREEN_WIDTH][SCREEN_HEIGHT];

static Color sys_palette[64] =
{
    {0x66, 0x66, 0x66},
    {0x00, 0x2A, 0x88}, 
    {0x14, 0x12, 0xA7},
    {0x3B, 0x00, 0xA4}, 
    {0x5C, 0x00, 0x7E},
    {0x6E, 0x00, 0x40},
    {0x6C, 0x07, 0x00},
    {0x56, 0x1D, 0x00},
    {0x33, 0x35, 0x00},
    {0x0B, 0x48, 0x00},
    {0x00, 0x52, 0x00},
    {0x00, 0x4F, 0x08},
    {0x00, 0x40, 0x4D}, 
    {0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00},
    {0xAD, 0xAD, 0xAD}, 
    {0x15, 0x5F, 0xD9},
    {0x42, 0x40, 0xFF}, 
    {0x75, 0x27, 0xFE},
    {0xA0, 0x1A, 0xCC},
    {0xB7, 0x1E, 0x7B},
    {0xB5, 0x31, 0x20}, 
    {0x99, 0x4E, 0x00},
    {0x6B, 0x6D, 0x00},
    {0x38, 0x87, 0x00},
    {0x0C, 0x93, 0x00}, 
    {0x00, 0x8F, 0x32},
    {0x00, 0x7C, 0x8D}, 
    {0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00}, 
    {0x00, 0x00, 0x00},
    {0xFF, 0xFE, 0xFF}, 
    {0x64, 0xB0, 0xFF},
    {0x92, 0x90, 0xFF}, 
    {0xC6, 0x76, 0xFF},
    {0xF3, 0x6A, 0xFF}, 
    {0xFE, 0x6E, 0xCC},
    {0xFE, 0x81, 0x70}, 
    {0xEA, 0x9E, 0x22},
    {0xBC, 0xBE, 0x00}, 
    {0x88, 0xD8, 0x00},
    {0x5C, 0xE4, 0x30}, 
    {0x45, 0xE0, 0x82},
    {0x48, 0xCD, 0xDE}, 
    {0x4F, 0x4F, 0x4F},
    {0x00, 0x00, 0x00}, 
    {0x00, 0x00, 0x00},
    {0xFF, 0xFE, 0xFF}, 
    {0xC0, 0xDF, 0xFF},
    {0xD3, 0xD2, 0xFF}, 
    {0xE8, 0xC8, 0xFF},
    {0xFB, 0xC2, 0xFF}, 
    {0xFF, 0xC4, 0xEA},
    {0xFF, 0xCC, 0xB3}, 
    {0xF4, 0xD8, 0x8E},
    {0xE0, 0xE6, 0x7C}, 
    {0xC8, 0xF0, 0x7E},
    {0xAD, 0xEF, 0x8E}, 
    {0x9D, 0xE8, 0xC5},
    {0xA4, 0xE2, 0xEA}, 
    {0xA8, 0xA8, 0xA8},
    {0x00, 0x00, 0x00}, 
    {0x00, 0x00, 0x00}
};

static Ppu *ppu_ptr = NULL;

static uint8_t PpuGetBgPixel(const int x, const int y)
{
    if (x < SCREEN_WIDTH && y < SCREEN_HEIGHT)
        return bg_pixels[x][y];

    return 0;
}

static void PpuSetBgPixel(const int x, const int y, const uint8_t pixel)
{
    if (x < SCREEN_WIDTH && y < SCREEN_HEIGHT)
        bg_pixels[x][y] = pixel;
}

static uint8_t PpuGetSprite0Pixel(const int x, const int y)
{
    if (x < SCREEN_WIDTH && y < SCREEN_HEIGHT)
        return sprite0_pixels[x][y];

    return 0;
}

static void PpuSetSprite0Pixel(const int x, const int y, const uint8_t pixel)
{
    if (x < SCREEN_WIDTH && y < SCREEN_HEIGHT)
    {
        //printf("sprite0.y: %d sprite0.x: %d\n", y, x);
        sprite0_pixels[x][y] = pixel;
    }
}

static bool PpuSprite0Hit(Ppu *ppu, int cycle)
{
    return (ppu->scanline >= sprites[0].y + 1 &&
            cycle <= (sprites[0].x + 8) && cycle >= sprites[0].x);
}

static Color GetBGColor(Ppu *ppu, const uint8_t palette_index, const uint8_t pixel)
{
    // Compute palette memory address
    const uint16_t palette_addr = 0x3F00 + (palette_index * 4) + pixel;

    // Read the color index from PPU palette memory
    uint16_t color_index = palette_table[palette_addr & 0x1F];

    if (!pixel)
    {
        color_index = palette_table[0];
    }

    if (ppu->mask.grey_scale)
    {
        color_index &= 0x30;
    }

    return sys_palette[color_index & 0x3F];

}

static Color GetSpriteColor(Ppu *ppu, const uint8_t palette_index, const uint8_t pixel)
{
    const uint16_t palette_addr = 0x10 + (palette_index * 4) + pixel;
    uint16_t color_index = palette_table[palette_addr];

    if (ppu->mask.grey_scale)
    {
        color_index &= 0x30;
    }

    return sys_palette[color_index & 0x3F];
}

void PPU_WriteAddrReg(Ppu *ppu, const uint8_t value)
{
    if (!ppu->w)
    {
        // Set high byte of t
        ppu->t.writing.high = value & 0x3F;
        ppu->t.writing.bit_z = 0;
    }
    else
    {
        const uint8_t prev_a12 = ppu->v.raw_bits.bit12;
        // Set low byte of t
        ppu->t.writing.low = value;
        // Transfer t to v
        ppu->v.raw = ppu->t.raw;
        if (~prev_a12 & ppu->v.raw_bits.bit12)
            PpuClockMMC3();
    }
    ppu->w = !ppu->w;
}

static void PpuNametableWrite(Ppu *ppu, uint16_t addr, uint8_t data)
{
    nametables[ppu->v.scrolling.name_table_sel][addr & 0x3FF] = data;
}

static uint8_t PpuNametableRead(Ppu *ppu, uint16_t addr)
{
    return nametables[ppu->v.scrolling.name_table_sel][addr & 0x3FF];
}

// Horizontal scrolling
static void IncX(Ppu *ppu)
{
    if (ppu->v.scrolling.coarse_x == 31)
    {
        //printf("PPU v addr before: 0x%04X\n", ppu->v.raw);
        ppu->v.scrolling.coarse_x = 0;
        //printf("PPU v addr after coarse_x reset : 0x%04X\n", ppu->v.raw);
        // Switch horizontal nametable
        ppu->v.scrolling.name_table_sel ^= 0x1;
        //printf("PPU v addr after nt switch: 0x%04X\n", ppu->v.raw);
    }
    else
    {
        ++ppu->v.scrolling.coarse_x;
    }
}

// Vertical Scroll
static void IncY(Ppu *ppu)
{
    if (ppu->v.scrolling.fine_y < 7)
        ppu->v.scrolling.fine_y++;
    else
    {
        ppu->v.scrolling.fine_y = 0;
        if (ppu->v.scrolling.coarse_y == 29)
        {
            ppu->v.scrolling.coarse_y = 0;
            // Flip vertical nametable bit
            ppu->v.scrolling.name_table_sel ^= 0x2;
        }
        else if (ppu->v.scrolling.coarse_y == 31)
        {
            //printf("PPU v addr before: 0x%04X\n", ppu_ptr->v.raw);
            // coarse Y = 0, nametable not switched
            ppu->v.scrolling.coarse_y = 0;
            //printf("PPU v addr after: 0x%04X\n", ppu_ptr->v.raw);
        }
        else
        {
            // increment coarse Y
            ++ppu->v.scrolling.coarse_y;
        }
    }
}

static void WriteToPaletteTable(const uint16_t addr, const uint8_t data)
{
    uint16_t effective_addr = addr;
    if ((effective_addr & 0x3) == 0)
    {
        effective_addr &= ~0x10;
    }
    palette_table[effective_addr] = data;
}

static void PPU_WriteCtrl(Ppu *ppu, const uint8_t data)
{
    ppu->ctrl.raw = data;
    ppu->t.scrolling.name_table_sel = data & 0x3;

    // TODO: Delay by one instruction
    //if (ppu->ctrl.vblank_nmi & ppu->status.vblank)
    //    SystemSendNmiToCpu();
}

void PPU_WriteData(Ppu *ppu, const uint8_t data)
{
    const uint8_t prev_a12 = ppu->v.raw_bits.bit12;
    const uint16_t addr = ppu->v.raw & 0x3FFF;

    // Extract A13, A12, A11 for region decoding
    switch (addr >> 12)
    {
        case 0x0:
        case 0x1:
        {
            // chr rom is actually chr ram
            PpuBusWriteChrRam(ppu->v.raw, data);
            //printf("ppu v: 0x%X\n", ppu->v.raw);
            break;
        }
        case 0x2:
        case 0x3:
        {
            if (addr < 0x3F00)
                PpuNametableWrite(ppu, ppu->v.raw, data);
            else
                WriteToPaletteTable(ppu->v.raw & 0x1F, data);
            break;
        }
    }

    // Outside of rendering, reads from or writes to $2007 will add either 1 or 32 to v depending on the VRAM increment bit set via $2000.
    // During rendering (on the pre-render line and the visible lines 0-239, provided either background or sprite rendering is enabled),
    // it will update v in an odd way, triggering a coarse X increment and a Y increment simultaneously (with normal wrapping behavior).
    if (ppu->rendering && (ppu->scanline < 240 || ppu->scanline == 261))
    {
        IncX(ppu);
        IncY(ppu);
    }
    else
    {
        ppu->v.raw += ppu_ptr->ctrl.vram_addr_inc ? 32 : 1;
    }
    if (~prev_a12 & ppu->v.raw_bits.bit12)
        PpuClockMMC3();
}

static void PPU_WriteScroll(Ppu *ppu, const uint8_t value)
{
    if (!ppu->w)
    {
        // First write: x scroll (fine X + coarse X)
        ppu->t.scrolling.coarse_x = value >> 3;
        ppu->x = value & 0x7; // Fine X scroll
    }
    else
    {
        // Second write: Y scroll (fine Y + coarse Y)
        ppu->t.scrolling.coarse_y = value >> 3;
        ppu->t.scrolling.fine_y = value & 0x7;
    }
    ppu->w = !ppu->w;
}

uint8_t PPU_ReadStatus(Ppu *ppu)
{
    PpuStatus status_value = ppu->status;
    //printf("PPU_ReadStatus : %d scanline:%d cycle: %d\n", ppu->ctrl.vblank_nmi, ppu->scanline, ppu->cycle_counter);
    // I think this is a hack, prob shouldn't be done normally
    // Allows the vblank timing tests to pass
    if (ppu->scanline == 241 && ppu->cycle_counter == 1)
        ppu->ignore_vblank = true;

    // Clear vblank and write toggle
    ppu->status.vblank = 0;
    ppu->w = 0;

    return status_value.raw;
}

static uint8_t PpuReadChr(Ppu *ppu, const uint16_t addr)
{
    uint8_t prev_a12 = (ppu->bus_addr >> 12) & 1;
    uint8_t new_a12 = (addr >> 12) & 1;
    if (~prev_a12 & new_a12)
    {
        //printf("PPU A12: %d scanline:%d cycle: %d\n", new_a12, ppu->scanline, ppu->cycle_counter);
        PpuClockMMC3();
    }
    ppu->bus_addr = addr;
    return PpuBusReadChrRom(addr);
}

uint8_t PPU_ReadData(Ppu *ppu)
{
    const uint16_t prev_a12 = ppu->v.raw_bits.bit12;
    uint16_t addr = ppu->v.raw & 0x3FFF;

    uint8_t data = 0;

    switch (addr >> 12)
    {
        case 0:
        case 1:
        {
            // Grab the stale buffer value
            data = ppu->buffered_data;
            // Load new data into buffer
            ppu->buffered_data = PpuReadChr(ppu, addr);
            break;
        }

        case 2:
        case 3:
        {
            if (addr < 0x3F00)
            {
                // Return stale buffer value
                data = ppu->buffered_data;
                ppu->buffered_data = PpuNametableRead(ppu, addr);
            }
            else
            {
                data = palette_table[addr & 0x1F];
            }
            break;
        }
    }

    if (ppu->rendering && (ppu->scanline < 240 || ppu->scanline == 261))
    {
        IncX(ppu);
        IncY(ppu);
    }
    else
    {
        // Auto-increment address
        ppu->v.raw += ppu->ctrl.vram_addr_inc ? 32 : 1;
    }

    if (~prev_a12 & ppu->v.raw_bits.bit12)
        PpuClockMMC3();
    return data;
}

uint8_t ReadPPURegister(Ppu *ppu, const uint16_t addr)
{
    switch (addr & 7)
    {
        case PPU_STATUS:
            return PPU_ReadStatus(ppu);
        case OAM_DATA:
            return sprites[ppu->oam_addr >> 2].raw[ppu->oam_addr & 3];
        case PPU_DATA:
            return PPU_ReadData(ppu);
    }

    // Read from open bus;
    return ppu->bus_latch;
}

void WritePPURegister(Ppu *ppu, const uint16_t addr, const uint8_t data)
{
    const uint16_t reg = addr & 7;

    if ((ppu->cycles < 88974) && (reg == PPU_CTRL || reg == PPU_MASK || reg == PPU_SCROLL || reg == PPU_ADDR))
        return;

    switch (reg)
    {
        case PPU_CTRL:
            PPU_WriteCtrl(ppu, data);
            break;
        case PPU_MASK:
            ppu->mask.raw = data;
            break;
        case OAM_ADDR:
            ppu->oam_addr = data;
            break;
        case OAM_DATA:
        {
            if (!ppu->rendering || (ppu->scanline > 239 && ppu->scanline < 261))
            {
                sprites[ppu->oam_addr >> 2].raw[ppu->oam_addr & 3] = data;
                ++ppu->oam_addr;
            }
            else
            {
                ppu->oam_addr += 4;
            }
            break;
        }
        case PPU_SCROLL:
            PPU_WriteScroll(ppu, data);
            break;
        case PPU_ADDR:
            PPU_WriteAddrReg(ppu, data);
            break;
        case PPU_DATA:
            PPU_WriteData(ppu, data);
            break;
    }
    ppu->bus_latch = data;
}

// Set the mirroring mode for the nametables
// Note that mirroring is the opposite of arrangement
void PpuSetMirroring(NameTableMirror mode, int page)
{
    switch (mode)
    {
        case NAMETABLE_HORIZONTAL:
            nametables[0] = &vram[0x000];  // NT0 (0x2000)
            nametables[1] = &vram[0x000];  // NT0 (Mirrored at 0x2400)
            nametables[2] = &vram[0x400];  // NT1 (0x2800)
            nametables[3] = &vram[0x400];  // NT1 (Mirrored at 0x2C00)
            break;
        
        case NAMETABLE_VERTICAL:
            nametables[0] = &vram[0x000];  // NT0 (0x2000)
            nametables[1] = &vram[0x400];  // NT1 (0x2400)
            nametables[2] = &vram[0x000];  // NT0 (Mirrored at 0x2800)
            nametables[3] = &vram[0x400];  // NT1 (Mirrored at 0x2C00)
            break;
        case NAMETABLE_SINGLE_SCREEN:
            nametables[0] = &vram[0x400 * page];
            nametables[1] = &vram[0x400 * page];
            nametables[2] = &vram[0x400 * page];
            nametables[3] = &vram[0x400 * page];
            break;

        default:
            printf("Unimplemented Nametable mirroring mode %d detected!\n", mode);
            break;
    }
}

void PPU_Init(Ppu *ppu, int name_table_layout, uint32_t **buffers)
{
    memset(ppu, 0, sizeof(*ppu));
    ppu->nt_mirror_mode = name_table_layout;
    PpuSetMirroring(ppu->nt_mirror_mode, 0);
    ppu->rendering = false;
    ppu->prev_rendering = false;
    ppu->buffers[0] = buffers[0];
    ppu->buffers[1] = buffers[1];
    ppu->ext_input = 0;
    //ppu->status.open_bus = 0x1c;
    ppu_ptr = ppu;
}

const uint32_t dots_per_frame_odd = 341 * 261 + 340;
const uint32_t dots_per_frame_even = 341 * 261 + 341;
const uint32_t cpu_cycles_per_frame = dots_per_frame_even / 3; 

static void DrawPixel(uint32_t *buffer, int x, int y, Color color)
{
    if (x < 0 || y < 0 || x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT)
        return;
    //assert(x >= 0 && x <= SCREEN_WIDTH);
    //assert(y >= 0 && y <= SCREEN_HEIGHT);
    buffer[y * SCREEN_WIDTH + x] = (uint32_t)((color.r << 24) | (color.g << 16) | (color.b << 8) | 255);
}

static void PpuDrawSprite8x8(Ppu *ppu, int tile_offset, int tile_x, int tile_y, int palette,
                             bool flip_horz, bool flip_vert, bool priority, bool sprite0)
{
    int bit = 0;
    uint8_t sprite_pixel = 0;
    uint8_t bg_pixel = 0;

    for (int y = 0; y < 8; y++)
    {
        int row = y;
        // Vertical flip
        if (flip_vert)
            row = 7 - y;

        uint8_t upper = PpuReadChr(ppu, tile_offset + row);
        uint8_t lower = PpuReadChr(ppu, tile_offset + row + 8);
        if (ppu->scanline == 261)
            return;

        for (int x = 7; x >= 0; x--)
        {
            // Apply horizontal flip
            bit = flip_horz ? x : 7 - x;
            sprite_pixel = ((upper >> bit) & 1) | (((lower >> bit) & 1) << 1);
            bg_pixel = PpuGetBgPixel(tile_x + x, tile_y + y);

            if (sprite0)
            {
                PpuSetSprite0Pixel(tile_x + x, tile_y + y, sprite_pixel);
            }

            if (!sprite_pixel || (priority && bg_pixel))
                continue;

            if (ppu->mask.sprites_rendering && (ppu->mask.show_sprites_left_corner || ((tile_x + x) > 7)))
            {
                Color color = GetSpriteColor(ppu, palette, sprite_pixel);
                DrawPixel(ppu->buffers[0], tile_x + x, tile_y + y, color);
            }
        }
    }
}

static void PpuDrawSprite8x16(Ppu *ppu, int tile_index, int tile_x, int tile_y, int palette,
                              bool flip_horz, bool flip_vert, bool priority, bool sprite0)
{
    int bank = (tile_index & 1) ? 0x1000 : 0x0000;
    int tile_id = tile_index & 0xFE;

    int bit = 0;
    uint8_t sprite_pixel = 0;
    uint8_t bg_pixel = 0;

    for (int y = 0; y < 16; y++)
    {
        //int tile_part = (y < 8) ? 0 : 1;
        // Swap tile part if flipping vertically
        int tile_part = (y < 8) ^ flip_vert ? 0 : 1;
        size_t tile_offset = bank + (tile_id + tile_part) * 16;
        int row = y & 7;
        if (flip_vert)
            row = 7 - (y & 7); 
        uint8_t upper = PpuReadChr(ppu, tile_offset + row); 
        uint8_t lower = PpuReadChr(ppu, tile_offset + row + 8);
        if (ppu->scanline == 261)
            return;

        for (int x = 7; x >= 0; x--)
        {
            // Apply horizontal flip
            bit = flip_horz ? x : 7 - x;
            sprite_pixel = ((upper >> bit) & 1) | (((lower >> bit) & 1) << 1);
            bg_pixel = PpuGetBgPixel(tile_x + x, tile_y + y);

            if (sprite0)
            {
                PpuSetSprite0Pixel(tile_x + x, tile_y + y, sprite_pixel);
            }

            if (!sprite_pixel || (priority && bg_pixel))
                continue;

            if (ppu->mask.sprites_rendering && (ppu->mask.show_sprites_left_corner || ((tile_x + x) > 7)))
            {
                Color color = GetSpriteColor(ppu, palette, sprite_pixel);
                DrawPixel(ppu->buffers[0], tile_x + x, tile_y + y, color);
            }
        }
    }
}

static void DrawSprite(Ppu *ppu, int sprite_num)
{
    const uint16_t bank = ppu->ctrl.sprite_pat_table_addr ? 0x1000 : 0;

    Sprite *curr_sprite = &sprites_secondary[sprite_num];
    uint16_t palette = curr_sprite->attribs.palette;
    size_t tile_offset = bank + (curr_sprite->tile_id * 16);
    bool sprite0 = !sprite_num && ppu->sprite0_loaded;

    if (ppu->ctrl.sprite_size)
    {
        PpuDrawSprite8x16(ppu, curr_sprite->tile_id , curr_sprite->x, curr_sprite->y + 1,
                          palette,curr_sprite->attribs.horz_flip, curr_sprite->attribs.vert_flip,
                          curr_sprite->attribs.priority, sprite0);
    }
    else
    {
        PpuDrawSprite8x8(ppu, tile_offset, curr_sprite->x, curr_sprite->y + 1, palette,
                        curr_sprite->attribs.horz_flip, curr_sprite->attribs.vert_flip,
                        curr_sprite->attribs.priority, sprite0);
    }
}

static void ResetSecondaryOAMSprites(void)
{
    memset(sprites_secondary, 0xFF, sizeof(Sprite) * 8);
}

static void PpuUpdateSprites(Ppu *ppu)
{
    ppu->found_sprites = 0;
    ppu->sprite0_loaded = false;

    for (int n = 0; n < 64; n++)
    {
        Sprite curr_sprite = sprites[n];
        if (ppu->scanline >= curr_sprite.y + 1 && ppu->scanline < curr_sprite.y + (ppu->ctrl.sprite_size ? 16 : 8) + 1)
        {
            if (ppu->found_sprites == 8)
            {
                ppu->status.sprite_overflow = ppu->rendering;
                break;
            }

            if (!n)
            {
                //printf("Found sprite 0 at y:%d\n", ppu->scanline);
                //printf("sprite0.y: %d sprite0.x: %d\n", curr_sprite.y, curr_sprite.x);
                ppu->sprite0_loaded = true;
            }

            //printf("Found sprite %d at y:%d\n", found_sprites, ppu->scanline);
            sprites_secondary[ppu->found_sprites++] = curr_sprite;
        }
    }
}

static inline void PpuShiftRegsUpdate(Ppu *ppu)
{
    ppu->bg_shift_low.raw <<= 1;
    ppu->bg_shift_high.raw <<= 1;
    ppu->attrib_shift_low.raw <<= 1;
    ppu->attrib_shift_high.raw <<= 1;
}

static inline void PpuFetchShifters(Ppu *ppu)
{
    ppu->bg_shift_low.low = ppu->bg_lsb;
    ppu->bg_shift_high.low = ppu->bg_msb;

    const bool latch_low = ppu->attrib_data & 1;
    const bool latch_high = (ppu->attrib_data >> 1) & 1;

    ppu->attrib_shift_low.low = latch_low ? 0xFF : 0x00;
    ppu->attrib_shift_high.low = latch_high ? 0xFF : 0x00;
}

static void PpuRender(Ppu *ppu, int scanline, int cycle)
{
    const uint16_t bank = ppu->ctrl.bg_pat_table_addr ? 0x1000 : 0;

    if (ppu->mask.bg_rendering)
        PpuShiftRegsUpdate(ppu);

    switch (cycle & 7)
    {
        case 0:
        {
            if (ppu->rendering)
                IncX(ppu);
            break;
        }
        case 1:
        {
            PpuFetchShifters(ppu);
            break;
        }
        case 2:
        {
            ppu->bus_addr = 0x2000 | (ppu->v.raw & 0x0FFF);
            ppu->tile_id = PpuNametableRead(ppu, ppu->bus_addr);
            break;
        }
        case 4:
        {
            ppu->bus_addr = 0x23C0 | (ppu->v.raw & 0x0C00) | ((ppu->v.raw >> 4) & 0x38) | ((ppu->v.raw >> 2) & 0x07);
            uint8_t attrib_data = PpuNametableRead(ppu, ppu->bus_addr);
            if (ppu->v.scrolling.coarse_y & 0x02)
                attrib_data >>= 4;
            if (ppu->v.scrolling.coarse_x & 0x02)
                attrib_data >>= 2;
            ppu->attrib_data = attrib_data & 0x3;
            break;
        }
        case 6:
        {
            // Get pattern table address for this tile
            size_t tile_offset = bank + (ppu->tile_id * 16) + ppu->v.scrolling.fine_y;
            // Bitplane 0
            ppu->bg_lsb = PpuReadChr(ppu, tile_offset);
            break;
        }
        case 7:
        {
            // Get pattern table address for this tile
            size_t tile_offset = bank + (ppu->tile_id * 16) + ppu->v.scrolling.fine_y;
            // Bitplane 1
            ppu->bg_msb = PpuReadChr(ppu, tile_offset + 8);
            break;
        }
    }

    if (scanline < 240 && cycle < 257)
    {
        // Rendering a pixel (every dot)
        int bit = 15 - ppu->x;
        uint8_t pixel_low  = (ppu->bg_shift_low.raw >> bit) & 1;
        uint8_t pixel_high = (ppu->bg_shift_high.raw >> bit) & 1;
        uint8_t palette_low  = (ppu->attrib_shift_low.raw >> bit) & 1;
        uint8_t palette_high = (ppu->attrib_shift_high.raw >> bit) & 1;

        uint8_t pixel = (pixel_high << 1) | pixel_low;
        uint8_t palette = (palette_high << 1) | palette_low;

        if (ppu->mask.bg_rendering && ppu->mask.sprites_rendering && !ppu->status.sprite_hit && cycle != 256 && 
            (cycle - 1 > 7 || (ppu->mask.show_bg_left_corner && ppu->mask.show_sprites_left_corner)))
        {
            if (PpuSprite0Hit(ppu, cycle - 1) && pixel && PpuGetSprite0Pixel(cycle - 1, scanline))
            {
                ppu->status.sprite_hit = 1;
                //printf("Sprite 0 hit at cycle:%d scanline:%d\n", cycle, scanline);
                //memset(sprite0_pixels, 0, sizeof(sprite0_pixels));
            }
        }

        // Draw pixel here
        if (ppu->mask.bg_rendering && (ppu->mask.show_bg_left_corner || cycle - 1 > 7))
        {
            PpuSetBgPixel(cycle - 1, scanline, pixel);
            Color color = GetBGColor(ppu, palette, pixel);
            DrawPixel(ppu->buffers[0], cycle - 1, scanline, color);
        }
        else
        {
            PpuSetBgPixel(cycle - 1, scanline, 0);
            Color color = GetBGColor(ppu, palette, 0);
            DrawPixel(ppu->buffers[0], cycle - 1, scanline, color);
        }
    }
}

void PPU_Tick(Ppu *ppu)
{
    ppu->cycles_to_run += 3;

    while (ppu->cycles_to_run != 0)
    {
        if (ppu->cycle_counter && (ppu->scanline < 240 || ppu->scanline == 261) && (ppu->cycle_counter <= 257 || (ppu->cycle_counter >= 321 && ppu->cycle_counter <= 336)))
            PpuRender(ppu, ppu->scanline, ppu->cycle_counter);

        if (ppu->cycle_counter == 64 && ppu->scanline < 240)
        {
            ResetSecondaryOAMSprites();
        }

        if (ppu->cycle_counter == 256 && (ppu->scanline < 240))
        {
            PpuUpdateSprites(ppu);
        }

        if ((ppu->cycle_counter <= 320 && ppu->cycle_counter >= 257) && (ppu->scanline < 240 || ppu->scanline == 261))
        {
            ppu->oam_addr *= !ppu->rendering;
            switch (ppu->cycle_counter)
            {
                case 260:
                    DrawSprite(ppu, 7);
                    break;
                case 268:
                    DrawSprite(ppu, 6);
                    break;
                case 276:
                    DrawSprite(ppu, 5);
                    break;
                case 284:
                    DrawSprite(ppu, 4);
                    break;
                case 292:
                    DrawSprite(ppu, 3);
                    break;
                case 300:
                    DrawSprite(ppu, 2);
                    break;
                case 308:
                    DrawSprite(ppu, 1);
                    break;
                case 316:
                    DrawSprite(ppu, 0);
                    break;
            }
        }

        if (ppu->rendering && ppu->cycle_counter == 256 && (ppu->scanline < 240 || ppu->scanline == 261))
            IncY(ppu);

        if (ppu->rendering && ppu->cycle_counter == 257 && (ppu->scanline < 240 || ppu->scanline == 261))
        {
            ppu->v.scrolling.coarse_x = ppu->t.scrolling.coarse_x;
            ppu->v.raw_bits.bit10 = ppu->t.raw_bits.bit10;
        }

        if (ppu->scanline == 241 && ppu->cycle_counter == 1)
        {
            //printf("PPU v addr: 0x%04X\n", ppu->v.raw);
            //assert(ppu->v.raw != 0);
            ppu->v.raw = ppu->v.raw & 0x3FFF;
            // VBlank starts at scanline 241
            ppu->status.vblank = !ppu->ignore_vblank;
            // If NMI is enabled
            if (ppu->ctrl.vblank_nmi)
            {
                SystemSendNmiToCpu();
                //ppu->finish_early = true;
            }
            // Copy the finished image in the back buffer to the front buffer
            memcpy(ppu->buffers[1], ppu->buffers[0], sizeof(uint32_t) * SCREEN_WIDTH * SCREEN_HEIGHT);
        }

        // Clear VBlank flag at scanline 261, dot 1
        if (ppu->scanline == 261 && ppu->cycle_counter == 1)
        {
            ppu->status.vblank = 0;
            ppu->status.sprite_hit = 0;
            ppu->status.sprite_overflow = 0;
        }

        if (ppu->rendering && ppu->scanline == 261 && (ppu->cycle_counter >= 280 && ppu->cycle_counter < 305))
        {
            // reset scroll
            ppu->v.scrolling.coarse_y = ppu->t.scrolling.coarse_y;
            ppu->v.scrolling.fine_y = ppu->t.scrolling.fine_y;
            ppu->v.raw_bits.bit11 = ppu->t.raw_bits.bit11;
        }

        if (ppu->rendering && ppu->cycle_counter == 339 && ppu->scanline == 261 && ppu->frames & 1)
        {
            ppu->cycle_counter = 340;
        }

        ppu->cycle_counter = (ppu->cycle_counter + 1) % 341;
        ++ppu->cycles;
        --ppu->cycles_to_run;

        if (!ppu->cycle_counter)
        {
            // 1 scanline = 341 PPU cycles
            ppu->scanline = (ppu->scanline + 1) % 262;
            ppu->ignore_vblank = false;
        }

        if (!ppu->cycle_counter && !ppu->scanline)
        {
            ppu->frame_finished = true;
            ++ppu->frames;
        }
    }
    ppu->rendering = ppu->mask.bg_rendering || ppu->mask.sprites_rendering;
}

// We need to catch up to the cpu
void PPU_Update(Ppu *ppu, uint64_t cpu_cycles)
{
    // Get the delta of ppu cycles that need to be run to catch up
    int64_t cycles_delta = (cpu_cycles * 3) - ppu->cycles;
    // Calculate how many ppu ticks we need to run (1 CPU cycle = 3 PPU cycles)
    ppu->cycles_to_run = MAX(-3, (cycles_delta + ppu->cycles_to_run) - 3);
    //if (ppu->cycles_to_run > -3)
    //    printf("Syncing of %d Ppu cycles\n", ppu->cycles_to_run + 3);
    PPU_Tick(ppu);
    ppu->finish_early = false;
}

void PPU_Reset(Ppu *ppu)
{
    ppu->cycle_counter = 0;
    ppu->cycles = 0;
    ppu->cycles_to_run = 0;
    ppu->frames = 0;
    ppu->frame_finished = 0;
    ppu->w = false;
    ppu->ctrl.raw = 0;
    ppu->mask.raw = 0;
    ppu->buffered_data = 0;
}
