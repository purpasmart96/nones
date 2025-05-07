#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>

#include "apu.h"
#include "ppu.h"
#include "cpu.h"
#include "joypad.h"
#include "arena.h"
#include "cart.h"
#include "bus.h"
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
    {0x00, 0x00, 0x00, 255},
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
        sprite0_pixels[x][y] = pixel;
}

static bool PpuSprite0Hit(Ppu *ppu, int cycle)
{
    return (ppu->scanline >= sprites[0].y + 1 &&
            cycle <= (sprites[0].x + 8) && cycle >= sprites[0].x);
}

static Color GetBGColor(const uint8_t palette_index, const uint8_t pixel)
{
    // Compute palette memory address
    const uint16_t palette_addr = 0x3F00 + (palette_index * 4) + pixel;

    // Read the color index from PPU palette memory
    uint16_t color_index = palette_table[palette_addr & 0x1F];

    if (!pixel)
    {
        color_index = palette_table[0];
    }

    return sys_palette[color_index & 0x3F];

}

static Color GetSpriteColor(const uint8_t palette_index, const uint8_t pixel)
{
    const uint16_t palette_addr = 0x10 + (palette_index * 4) + pixel;

    const uint16_t color_index = palette_table[palette_addr];

    return sys_palette[color_index & 0x3F];
}

static uint16_t prev_v;
static int prev_scanline = 0;

static void Mmc3UpdateIrqCounter(Ppu *ppu)
{
    const uint16_t v = (ppu->v.raw >> 12) & 1;
    if (v && !prev_v)
    {
        PpuClockMMC3();
        printf("mmc3 clock at scnaline: %d cycle: %d\n", ppu->scanline, ppu->cycle_counter);
    }
    prev_scanline = ppu->scanline;
    prev_v = v;
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
        // Set low byte of t
        ppu->t.writing.low = value;
        // Transfer t to v
        ppu->v.raw = ppu->t.raw;
    }
    ppu->w = !ppu->w;
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
static void IncX(Ppu *ppu)
{
    if (ppu->v.scrolling.coarse_x == 31) // if coarse X == 31
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
        ppu->v.scrolling.coarse_x++;
    }
}

// Vertical Scroll
static void IncY(Ppu *ppu)
{
    //printf("PPU v addr: 0x%04X\n", ppu_ptr->v.raw);
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
            ppu->v.scrolling.coarse_y++;
        }
    }
}

static void WriteToPaletteTable(uint16_t addr, const uint8_t data)
{
    if ((addr & 0x3) == 0)
    {
        addr &= ~0x10;
    }
    palette_table[addr] = data;
}

void PPU_WriteData(Ppu *ppu, const uint8_t data)
{
    // Extract A13, A12, A11 for region decoding
    uint8_t ppu_region = (ppu->v.raw >> 11) & 0x7;
    switch (ppu_region)
    {
        case 0x0:
        case 0x1:
        case 0x2:
        case 0x3:
        {
            // chr rom is actually chr ram
            PpuBusWriteChrRam(ppu->v.raw, data);
            break;
        }
        case 0x4:
        case 0x5:
            //vram[ppu->v & 0x1FFF] = data;
            WriteToNametable(ppu->v.raw, data);
            break;
        case 0x6:
            printf("ppu v: 0x%04X\n", ppu->v.raw);
            break;
        case 0x7:
            WriteToPaletteTable(ppu->v.raw & 0x1F, data);
            //palette_table[ppu->v.raw & 0x1F] = data;
            break;
        default:
            break;
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

static bool nmi_triggered = false;

uint8_t PPU_ReadStatus(Ppu *ppu)
{
    PpuStatus status_value = ppu->status;
    //status_value.sprite_hit = ppu->status.sprite_hit;
    //status_value.sprite_overflow = ppu->status.sprite_overflow;
    //status_value.vblank = ppu->status.vblank;

    // Clear vblank and write toggle
    ppu->status.vblank = 0;
    ppu->w = 0;
    nmi_triggered = false;

    //if (ppu->status.sprite_overflow == 1)
    //{
    //    //if (ppu->scanline == 241 && (ppu->cycle_counter == 7 || ppu->cycle_counter == 20))
    //    //    status_value.raw = 0x20;
    //    //printf("Sprite overflow is set (scanline:%d cycle: %d)\n", ppu->scanline, ppu->cycle_counter);
    //}

    //printf("$2002 value: 0x%x (scanline:%d cycle: %d)\n", status_value.raw, ppu->scanline, ppu->cycle_counter);

    //if (status_value.raw == 0xA0)
    //{
    //    printf("$2002 status is 0xA0 (vblank:%d sprite_hit:%d)\n", ppu->status.vblank, ppu->status.sprite_hit);
    //}

    return status_value.raw;
}

uint8_t PPU_ReadData(Ppu *ppu)
{
    uint16_t addr = ppu->v.raw & 0x3FFF;

    uint8_t data = 0;

    if (addr >= 0x3F00)
    {  
        // Palette memory (no buffering)
        data = palette_table[addr & 0x1F];  // Mirror palette range
    }
    else if (addr <= 0x1FFF)
    {
        // Return stale buffer value
        data = ppu->buffered_data;
        ppu->buffered_data = PpuBusReadChrRom(addr);  // Load new data into buffer
    }
    else if (addr <= 0x2FFF)
    {
        // Return stale buffer value
        data = ppu->buffered_data;
        ppu->buffered_data = vram[addr & 0x7FF];
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
        //Mmc3UpdateIrqCounter(ppu);
    }

    return data;
}

uint8_t ReadPPURegister(Ppu *ppu, const uint16_t address)
{
    switch (address)
    {
        case PPU_STATUS:
            return PPU_ReadStatus(ppu);
        case OAM_DATA:
            return sprites[ppu->oam_addr & 63].raw[ppu->oam_addr & 3];
        case PPU_DATA:
            return PPU_ReadData(ppu);
    }
    // Read from open bus;
    return 0;
}

void WritePPURegister(Ppu *ppu, const uint16_t addr, const uint8_t data)
{
    if ((ppu->cycles < 88974) && (addr == PPU_CTRL))
        return;
    if ((ppu->cycles < 88974) && (addr == PPU_MASK))
        return;
    if ((ppu->cycles < 88974) && (addr == PPU_SCROLL))
        return;
    if ((ppu->cycles < 88974) && (addr == PPU_ADDR))
        return;

    switch (addr)
    {
        case PPU_CTRL:
            ppu->ctrl.raw = data;
            ppu->t.scrolling.name_table_sel = data & 0x3;
            break;
        case PPU_MASK:
            ppu->mask.raw = data;
            break;
        case OAM_ADDR:
            ppu->oam_addr = data;
            break;
        case OAM_DATA:
        {
            //const int oam_n = ppu->oam_addr & 63;
            const int oam_m = ppu->oam_addr & 3;
            sprites[ppu->oam_addr++ & 63].raw[oam_m] = data;
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

        default:
            printf("Unimplemented Nametable mirroring mode %d detected!\n", mode);
            break;
    }
}

void PPU_Init(Ppu *ppu, int name_table_layout, uint32_t **buffers)
{
    memset(ppu, 0, sizeof(*ppu));
    ppu->nt_mirror_mode = name_table_layout;
    NametableMirroringInit(ppu->nt_mirror_mode);
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
    buffer[y * SCREEN_WIDTH + x] = (uint32_t)((color.r << 24) | (color.g << 16) | (color.b << 8) | color.a);
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

        uint8_t upper = PpuBusReadChrRom(tile_offset + row);
        uint8_t lower = PpuBusReadChrRom(tile_offset + row + 8);

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

            if (ppu->mask.show_sprites_left_corner || ((tile_x + x) > 7))
            {
                Color color = GetSpriteColor(palette, sprite_pixel);
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

        uint8_t upper = PpuBusReadChrRom(tile_offset + row); 
        uint8_t lower = PpuBusReadChrRom(tile_offset + row + 8);

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

            if ((ppu->mask.show_sprites_left_corner || ((tile_x + x) > 7)))
            {
                if (sprite_pixel && (!priority || !bg_pixel))
                {
                    Color color = GetSpriteColor(palette, sprite_pixel);
                    DrawPixel(ppu->buffers[0], tile_x + x, tile_y + y, color);
                }
            }
        }
    }
}

static void DrawSpritesPlaceholder(Ppu *ppu)
{
    if (!ppu->mask.sprites_rendering)
        return;

    const uint16_t bank = ppu->ctrl.sprite_pat_table_addr ? 0x1000: 0;
    bool sprite0 = false;

    for (int n = 7; n >= 0; n--)
    {
        Sprite *curr_sprite = &sprites_secondary[n];

        uint16_t palette = curr_sprite->attribs.palette;
        size_t tile_offset = bank + (curr_sprite->tile_id * 16);
        if (!n && ppu->sprite0_loaded)
            sprite0 = true;

        if (ppu->ctrl.sprite_size)
        {
            PpuDrawSprite8x16(ppu, curr_sprite->tile_id , curr_sprite->x, curr_sprite->y + 1,
                              palette,curr_sprite->attribs.horz_flip, curr_sprite->attribs.vert_flip, curr_sprite->attribs.priority, sprite0);
        }
        else
        {
            PpuDrawSprite8x8(ppu, tile_offset, curr_sprite->x, curr_sprite->y + 1, palette,
               curr_sprite->attribs.horz_flip, curr_sprite->attribs.vert_flip, curr_sprite->attribs.priority, sprite0);
        }
    }
}

static void ResetSecondaryOAMSprites(void)
{
    memset(sprites_secondary, 0xFF, sizeof(Sprite) * 8);
}

static void PpuUpdateSprites(Ppu *ppu)
{
    int found_sprites = 0;
    ppu->sprite0_loaded = false;

    for (int n = 0; n < 64; n++)
    {
        Sprite curr_sprite = sprites[n];
        if (ppu->scanline >= curr_sprite.y + 1 && ppu->scanline < curr_sprite.y + (ppu->ctrl.sprite_size ? 16 : 8) + 1)
        {
            if (found_sprites == 8)
            {
                if (ppu->rendering)
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
            sprites_secondary[found_sprites++] = curr_sprite;
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
    const uint8_t *nametable = nametables[ppu->v.scrolling.name_table_sel];
    const uint8_t *attribute_table = &nametable[0x3C0];
    const uint16_t bank = ppu->ctrl.bg_pat_table_addr ? 0x1000: 0;

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
        if (ppu->rendering && (ppu->mask.show_bg_left_corner || cycle - 1 > 7))
        {
            PpuSetBgPixel(cycle - 1, scanline, pixel);
            Color color = GetBGColor(palette, pixel);
            DrawPixel(ppu->buffers[0], cycle - 1, scanline, color);
        }
        else
        {
            PpuSetBgPixel(cycle - 1, scanline, 0);
            Color color = GetBGColor(palette, 0);
            DrawPixel(ppu->buffers[0], cycle - 1, scanline, color);
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
        ppu->cycle_counter = (++ppu->cycle_counter) % 341;

        if (!ppu->cycle_counter)
        {
            // 1 scanline = 341 PPU cycles
            ppu->scanline = (++ppu->scanline) % 262;
        }

        if (!ppu->cycle_counter && !ppu->scanline)
        {
            ppu->frame_finished = true;
            ppu->frames++;
        }

        if (ppu->rendering && ppu->cycle_counter == 340 && ppu->scanline == 261 && ppu->frames & 1)
        {
            continue;
        }

        ppu->cycles++;
        ppu_cycles_to_run--;

        if (ppu->cycle_counter == 0 && ppu->scanline == 0 && ppu->frames & 1)
        {
            //const uint8_t *nametable = nametables[ppu->v.scrolling.name_table_sel];
            //const uint8_t *attribute_table = &nametable[0x3C0];
            const uint16_t bank = ppu->ctrl.bg_pat_table_addr ? 0x1000 : 0;
            // Get pattern table address for this tile
            size_t tile_offset = bank + (ppu->tile_id * 16) + ppu->v.scrolling.fine_y;
            // Bitplane 0
            ppu->bg_lsb = PpuBusReadChrRom(tile_offset);
        }

        if (ppu->cycle_counter && (ppu->scanline < 240 || ppu->scanline == 261) && (ppu->cycle_counter <= 257 || (ppu->cycle_counter >= 321 && ppu->cycle_counter <= 336)))
            PpuRender(ppu, ppu->scanline, ppu->cycle_counter);

        if (ppu->cycle_counter == 260 && !ppu->ctrl.sprite_size && ppu->ctrl.sprite_pat_table_addr && !ppu->ctrl.bg_pat_table_addr)
            PpuClockMMC3v2(ppu->scanline);
        else if (ppu->cycle_counter == 320 && !ppu->ctrl.sprite_size && !ppu->ctrl.sprite_pat_table_addr && ppu->ctrl.bg_pat_table_addr)
            PpuClockMMC3v2(ppu->scanline - 1);
        else if (ppu->cycle_counter == 260 && ppu->ctrl.sprite_size)
            PpuClockMMC3v2(ppu->scanline);

        if (ppu->cycle_counter == 64 && ppu->scanline < 240)
            ResetSecondaryOAMSprites();

        if (ppu->cycle_counter == 256 && (ppu->scanline < 240))
        {
            PpuUpdateSprites(ppu);
        }

        //if ((ppu->cycle_counter > 256 && ppu->cycle_counter < 321) && (ppu->scanline < 240 || ppu->scanline == 261))
        //{
        //    const uint16_t bank = ppu->ctrl.sprite_pat_table_addr ? 0x1000 : 0;
        //    int sprite_index = (320 - ppu->cycle_counter) & 7;
        //    Sprite *curr_sprite = &sprites_secondary[sprite_index];
        //    //printf("Sprite %d fetch at cycle:%d \n", sprite_index, ppu->cycle_counter);
//
        //    uint16_t palette = curr_sprite->attribs.palette;

            //printf("Sprite %d fetch at cycle:%d \n", ppu->cycle_counter & 7, ppu->cycle_counter);
            //const uint16_t sprite_bank = ppu->ctrl.sprite_pat_table_addr ? 0x1000 : 0;
            //size_t tile_offset = sprite_bank + (curr_sprite->tile_id * 16);
            //ppu->sprite_lsb = PpuBusReadChrRom(tile_offset);
            //switch (ppu->cycle_counter & 7)
            //{
            //    case 0:
            //    case 1:
            //        break;
            //    case 6:
            //    {
            //        printf("Sprite %d fetch at cycle:%d \n", sprite_index, ppu->cycle_counter);
            //        size_t tile_offset = bank + (curr_sprite->tile_id * 16);
            //        ppu->sprite_lsb = PpuBusReadChrRom(tile_offset);
            //        break;
            //    }
//
            //    case 7:
            //        printf("Sprite %d fetch at cycle:%d \n", sprite_index, ppu->cycle_counter);
            //        break;
            //    //case 1:
            //    //    //printf("Sprite %d fetch at cycle:%d \n", sprite_index, ppu->cycle_counter);
            //    //    break;
            //    default:
            //        break;
            //}

            //if (ppu->ctrl.sprite_size)
            //{
            //    int bank = (curr_sprite->tile_id & 1) ? 0x1000 : 0x0000;
            //    int tile_id = curr_sprite->tile_id & 0xFE;
            //    printf("Sprite %d tile_id:%d \n", sprite_index, tile_id);
            //    //int tile_part = (y < 8) ^ flip_vert ? 0 : 1;
            //    //size_t tile_offset = bank + (tile_id + tile_part) * 16;
            //    //int row = y & 7;
            //    //if (flip_vert)
            //    //    row = 7 - (y & 7);
            //}
            //else {
            //    size_t tile_offset = bank + (curr_sprite->tile_id * 16);
            //    ppu->sprite_lsb = PpuBusReadChrRom(tile_offset);
            //}
    
            //switch ((257 - ppu->cycle_counter) & 7)
            //{
            //    case 1:
            //    {
            //        // Get pattern table address for this tile
            //        size_t tile_offset = 0x1000 + (ppu->tile_id * 16) + ppu->v.scrolling.fine_y;
            //        // Bitplane 0
            //        ppu->sprite_lsb = PpuBusReadChrRom(tile_offset);
            //        break;
            //    }
            //    case 3:
            //    {
            //        // Get pattern table address for this tile
            //        size_t tile_offset = 0x1000 + (ppu->tile_id * 16) + ppu->v.scrolling.fine_y;
            //        // Bitplane 1
            //        ppu->sprite_msb = PpuBusReadChrRom(tile_offset + 8);
            //        break;
            //    }
            //}
        //}
    
        if (ppu->cycle_counter == 320 && ppu->scanline < 240)
        {
            DrawSpritesPlaceholder(ppu);
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
            ppu->status.vblank = 1;
            // If NMI is enabled
            if (ppu->ctrl.vblank_nmi)
            {
                nmi_triggered = true;
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

        if (ppu->scanline == 261 && (ppu->cycle_counter >= 280 && ppu->cycle_counter < 305))
        {
            // reset scroll
            if (ppu->rendering)
            {
                ppu->v.scrolling.coarse_y = ppu->t.scrolling.coarse_y;
                ppu->v.scrolling.fine_y = ppu->t.scrolling.fine_y;
                ppu->v.raw_bits.bit11 = ppu->t.raw_bits.bit11;
            }
        }
    }
    ppu->rendering = ppu->mask.bg_rendering || ppu->mask.sprites_rendering;
}

bool PPU_NmiTriggered(void)
{
    if (nmi_triggered)
    {
        // Clear the flag after reading
        nmi_triggered = false;
        //printf("NMI:(scanline:%d cycle: %d)\n", ppu_ptr->scanline, ppu_ptr->cycle_counter);
        return true;
    }
    return false;
}

void PPU_Reset(void)
{

}
