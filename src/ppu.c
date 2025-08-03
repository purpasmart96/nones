#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
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
// OAM Secondary
static Sprite sprites_secondary[8];
static uint8_t palette_table[32];

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
    //printf("PPU_WriteCtrl: NMI: %d scanline:%d cycle: %d\n", ppu->ctrl.vblank_nmi, ppu->scanline, ppu->cycle_counter);
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
        ppu->v.raw += ppu->ctrl.vram_addr_inc ? 32 : 1;
    }
    if (~prev_a12 & ppu->v.raw_bits.bit12)
        PpuClockMMC3();
}

static void PPU_WriteScroll(Ppu *ppu, const uint8_t value)
{
    if (!ppu->w)
    {
        // First write: X scroll (fine X + coarse X)
        ppu->t.scrolling.coarse_x = value >> 3;
        ppu->x = value & 0x7;
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
    PpuStatus ret_status = ppu->status;
    ret_status.open_bus = ppu->io_bus & 0x1F;

    //printf("PPU_ReadStatus : %d scanline:%d cycle: %d\n", ppu->status.vblank, ppu->scanline, ppu->cycle_counter);

    // Clear vblank and write toggle
    ppu->clear_vblank = true;
    ppu->w = 0;

    return ret_status.raw;
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
            ppu->buffered_data = PpuBusReadChrRom(addr);
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
                data = (palette_table[addr & 0x1F] & 0x3F) | (ppu->io_bus & 0xC0);
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
            ppu->io_bus = PPU_ReadStatus(ppu);
            break;
        case OAM_DATA:
            ppu->io_bus = ppu->sprites[ppu->oam_addr >> 2].raw[ppu->oam_addr & 3];
            break;
        case PPU_DATA:
            ppu->io_bus = PPU_ReadData(ppu);
            break;
    }

    // Read value from the io bus
    return ppu->io_bus;
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
                ppu->sprites[ppu->oam_addr >> 2].raw[ppu->oam_addr & 3] = data;
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
    ppu->io_bus = data;
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
    ppu->buffers[0] = buffers[0];
    ppu->buffers[1] = buffers[1];
    ppu->ext_input = 0;
    //ppu->status.open_bus = 0x1c;
}

static void DrawPixel(uint32_t *buffer, int x, int y, Color color)
{
    if (x < 0 || y < 0 || x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT)
        return;

    buffer[y * SCREEN_WIDTH + x] = (uint32_t)((color.r << 24) | (color.g << 16) | (color.b << 8) | 255);
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
        Sprite curr_sprite = ppu->sprites[n];
        if (ppu->scanline >= curr_sprite.y && ppu->scanline < curr_sprite.y + (ppu->ctrl.sprite_size ? 16 : 8))
        {
            if (ppu->found_sprites == 8)
            {
                ppu->status.sprite_overflow = 1;
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

static void PpuHandleSprite0Hit(Ppu *ppu, const int xpos, const int fifo_lane, const uint8_t bg_pixel, const uint8_t sprite_pixel)
{
    if (!bg_pixel || !sprite_pixel)
        return;

    if (fifo_lane != 0 || !ppu->sprite0_loaded)
        return;

    if (!ppu->mask.bg_rendering || ppu->status.sprite_hit)
        return;

    const bool corner_renderering = ppu->mask.show_bg_left_corner && ppu->mask.show_sprites_left_corner;
    const bool sprite_0_hit_inrange = xpos != 255 && (xpos > 7 || corner_renderering);

    // Check if we are currently inside of sprite 0
    const bool inside_sprite0 = ppu->scanline >= ppu->sprites[0].y &&
                                xpos <= (ppu->sprites[0].x + 7) &&
                                xpos >= ppu->sprites[0].x;

    ppu->status.sprite_hit = inside_sprite0 && sprite_0_hit_inrange;
}

static void PpuRenderSpritePixel(Ppu *ppu, const int xpos, const uint8_t bg_pixel)
{
    if (!ppu->mask.sprites_rendering)
        return;

    const int scanline = ppu->scanline;
    const bool sprite_inrange = (ppu->mask.show_sprites_left_corner || xpos > 7);

    // Go through each fifo lane backwards so the sprite's pixels are drawn in the right order
    for (int i = ppu->found_sprites - 1; i >= 0; i--)
    {
        SpriteFifo *fifo_lane = &ppu->fifo[i];
        if (fifo_lane->x > 0)
            --fifo_lane->x;
        else
        {
            const uint8_t bit = !fifo_lane->attribs.horz_flip * 7;
            uint8_t spixel_low  = (fifo_lane->shift.low >> bit) & 1;
            uint8_t spixel_high = (fifo_lane->shift.high >> bit) & 1;
            uint8_t sprite_pixel = (spixel_high << 1) | spixel_low;

            PpuHandleSprite0Hit(ppu, xpos, i, bg_pixel, sprite_pixel);

            if (scanline && sprite_inrange)
            {
                if (sprite_pixel && (!fifo_lane->attribs.priority || !bg_pixel))
                {
                    Color color = GetSpriteColor(ppu, fifo_lane->attribs.palette, sprite_pixel);
                    DrawPixel(ppu->buffers[0], xpos, scanline, color);
                }
            }

            if (fifo_lane->attribs.horz_flip)
            {
                fifo_lane->shift.low >>= 1;
                fifo_lane->shift.high >>= 1;
            }
            else
            {
                fifo_lane->shift.low <<= 1;
                fifo_lane->shift.high <<= 1;
            }
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
            // TODO: should set the ppu bus addr here
            const uint16_t tile_addr = 0x2000 | (ppu->v.raw & 0x0FFF);
            ppu->tile_id = PpuNametableRead(ppu, tile_addr);
            break;
        }
        case 4:
        {
            // TODO: should set the ppu bus addr here
            const uint16_t attrib_addr = 0x23C0 | (ppu->v.raw & 0x0C00) | ((ppu->v.raw >> 4) & 0x38) | ((ppu->v.raw >> 2) & 0x07);
            uint8_t attrib_data = PpuNametableRead(ppu, attrib_addr);
            uint8_t shift = ((ppu->v.scrolling.coarse_y & 2) << 1) | (ppu->v.scrolling.coarse_x & 2);
            ppu->attrib_data = (attrib_data >> shift) & 0x3;
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
        // The effective x positon is the current cycle - 1 since cycle 0 is a dummy cycle
        const int xpos = ppu->cycle_counter - 1;
        // Fine X tells us which bit from the shift regs we want to use
        const int bit = 15 - ppu->x;

        uint8_t bg_pixel_low  = (ppu->bg_shift_low.raw >> bit) & 1;
        uint8_t bg_pixel_high = (ppu->bg_shift_high.raw >> bit) & 1;
        uint8_t bg_palette_low  = (ppu->attrib_shift_low.raw >> bit) & 1;
        uint8_t bg_palette_high = (ppu->attrib_shift_high.raw >> bit) & 1;

        const uint8_t bg_pixel = (bg_pixel_high << 1) | bg_pixel_low;
        const uint8_t bg_palette = (bg_palette_high << 1) | bg_palette_low;

        const bool draw_bg = ppu->mask.bg_rendering && (ppu->mask.show_bg_left_corner || xpos > 7);

        if (draw_bg)
        {
            Color color = GetBGColor(ppu, bg_palette, bg_pixel);
            DrawPixel(ppu->buffers[0], xpos, scanline, color);
        }
        else
        {
            Color color = GetBGColor(ppu, bg_palette, 0);
            DrawPixel(ppu->buffers[0], xpos, scanline, color);
        }

        PpuRenderSpritePixel(ppu, xpos, bg_pixel);
    }
}

static uint16_t PpuGetSpriteAddr(Ppu *ppu, Sprite *curr_sprite)
{
    const int y_offset = ppu->scanline - curr_sprite->y;
    const bool flip_vert = curr_sprite->attribs.vert_flip;
    const uint8_t tile_row = (flip_vert ? 7 - y_offset : y_offset) & 7; 

    if (ppu->ctrl.sprite_size)
    {
        int bank = (curr_sprite->tile_id & 1) ? 0x1000 : 0x0;
        int tile_id = curr_sprite->tile_id & 0xFE;
        int tile_part = (y_offset < 8) ^ !flip_vert;

        return (bank + (tile_id + (tile_part)) * 16) | tile_row;
    }
    else
    {
        int bank = ppu->ctrl.sprite_pat_table_addr ? 0x1000 : 0x0;
        return (bank + (curr_sprite->tile_id * 16)) | tile_row;
    }
}

static void PpuFetchSprite(Ppu *ppu, int sprite_num)
{
    Sprite *curr_sprite = &sprites_secondary[sprite_num];
    const int effective_cycle = ppu->cycle_counter - 257;

    switch (effective_cycle & 7)
    {
        case 3:
        {
            ppu->fifo[sprite_num].attribs = curr_sprite->attribs;
            ppu->fifo[sprite_num].x = curr_sprite->x;
            break;
        }
        case 5:
        {
            // Bitplane 0
            ppu->fifo[sprite_num].shift.low = PpuReadChr(ppu, PpuGetSpriteAddr(ppu, curr_sprite));
            break;
        }
        case 7:
        {
            // Bitplane 1
            ppu->fifo[sprite_num].shift.high = PpuReadChr(ppu, PpuGetSpriteAddr(ppu, curr_sprite) + 8);
            break;
        }
    }
}

void PPU_Tick(Ppu *ppu)
{
    ppu->cycles_to_run += 3;

    while (ppu->cycles_to_run > 0)
    {
        if (ppu->cycles_to_run == 2)
        {
            //printf("Nmi polled: frame:%ld scanline:%d cycle:%d\n", ppu->frames, ppu->scanline, ppu->cycle_counter);
            SystemPollNmi();
        }

        if (ppu->cycle_counter && (ppu->scanline < 240 || ppu->scanline == 261) && (ppu->cycle_counter <= 257 || (ppu->cycle_counter >= 321 && ppu->cycle_counter <= 336)))
            PpuRender(ppu, ppu->scanline, ppu->cycle_counter);

        if (ppu->cycle_counter == 64 && ppu->scanline < 240)
        {
            ResetSecondaryOAMSprites();
        }

        if (ppu->rendering && ppu->cycle_counter == 256 && (ppu->scanline < 240))
        {
            PpuUpdateSprites(ppu);
        }

        if ((ppu->cycle_counter <= 320 && ppu->cycle_counter >= 257) && (ppu->scanline < 240 || ppu->scanline == 261))
        {
            ppu->oam_addr *= !ppu->rendering;
            PpuFetchSprite(ppu, (ppu->cycle_counter - 257) >> 3);
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
            ppu->bus_addr = ppu->v.raw & 0x3FFF;
            // Vblank starts at scanline 241
            ppu->status.vblank = 1;
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

        ppu->cycle_counter = (ppu->cycle_counter + 1) % 341;
        ++ppu->cycles;
        --ppu->cycles_to_run;

        if (!ppu->cycle_counter)
        {
            // 1 scanline = 341 PPU cycles
            ppu->scanline = (ppu->scanline + 1) % 262;
        }

        if (!ppu->cycle_counter && !ppu->scanline)
        {
            ppu->frame_finished = true;
            // Clear io bus at the end of each frame
            // (Actually random on real hardware and can be up to a 30 frame delay)
            ppu->io_bus = 0;
            ++ppu->frames;
        }

        // Seems like the vblank flag side effect from reading PpuStatus is delayed by one dot/cycle
        if (ppu->clear_vblank)
        {
            // Clear vblank
            ppu->status.vblank = 0;
            ppu->clear_vblank = false;
        }

        if (ppu->mask.bg_rendering && ppu->cycle_counter == 339 && ppu->scanline == 261 && ppu->frames & 1)
        {
            ++ppu->cycle_counter;
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
