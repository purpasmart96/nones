#ifndef PPU_H
#define PPU_H

// PPU mem map
#include <stdint.h>
#define PPU_MM_MASK 0x3FFF
#define CART_ADDR_START 0
#define CART_ADDR_SIZE 0x2000
#define PPU_START_ADDR 0x2000
#define PPU_RAM_SIZE 0x1000
#define MISC_START_ADDR 0x3000
#define MISC_SIZE 0xF00
#define PALETTE_START_ADDR 0x3F00

// PPU regs
#define PPU_CTRL_REG   0x2000
#define PPU_MASK_REG   0x2001
#define PPU_STATUS_REG 0x2002
#define OAM_ADDR_REG   0x2003
#define OAM_DATA_REG   0x2004
#define PPU_SCROLL_REG 0x2005
#define PPU_ADDR_REG   0x2006
#define PPU_DATA_REG   0x2007
#define OAM_DMA_REG    0x4014

typedef enum
{
    PPU_CTRL   = 0,
    PPU_MASK   = 1,
    PPU_STATUS = 2,
    OAM_ADDR   = 3,
    OAM_DATA   = 4,
    PPU_SCROLL = 5,
    PPU_ADDR   = 6,
    PPU_DATA   = 7
} PpuIoReg;

typedef enum {
    NAMETABLE_HORIZONTAL,
    NAMETABLE_VERTICAL,
    NAMETABLE_SINGLE_SCREEN,
    NAMETABLE_FOUR_SCREEN,
} NameTableMirror;

typedef enum
{
    PPU_CLEARING_SECONDARY_OAM,
    PPU_SPRITE_EVAL,
    PPU_SPRITE_FETCH
} PpuState;

typedef enum
{
    PPU_RENDER_BEGIN = 1,
    PPU_RENDER_END = 239,
    PPU_POST_RENDER = 241,
    PPU_PRE_RENDER = 261
} PpuStages;

typedef union
{
    uint8_t raw;
    struct {
        // Base nametable address
        // (0 = $2000; 1 = $2400; 2 = $2800; 3 = $2C00)
        uint8_t base_name_table_addr : 2;
        // (0: add 1, going across; 1: add 32, going down)
        uint8_t vram_addr_inc : 1;
        // Sprite pattern table address for 8x8 sprite
        // (0: $0000; 1: $1000; ignored in 8x16 mode)
        uint8_t sprite_pat_table_addr : 1;
        // Background pattern table address (0: $0000; 1: $1000)
        uint8_t bg_pat_table_addr : 1;
        // Sprite size (0: 8x8 pixels; 1: 8x16 pixels – see PPU OAM#Byte 1)
        uint8_t sprite_size : 1;
        uint8_t master_slave : 1;
        uint8_t vblank_nmi : 1;
    };

} PpuCtrl;

typedef union
{
    uint8_t raw;
    struct {
        // Greyscale (0: normal color, 1: greyscale)
        uint8_t grey_scale : 1;
        // 1: Show background in leftmost 8 pixels of screen, 0: Hide
        uint8_t show_bg_left_corner : 1;
        // 1: Show sprites in leftmost 8 pixels of screen, 0: Hide
        uint8_t show_sprites_left_corner : 1;
        // 1: Enable background rendering
        uint8_t bg_rendering : 1;
        // 1: Enable sprite rendering
        uint8_t sprites_rendering : 1;
        // (Red / Blue / Green) Emphasize
        uint8_t emphasize_red : 1;
        uint8_t emphasize_green : 1;
        uint8_t emphasize_blue : 1;
    };

} PpuMask;

typedef union
{
    uint8_t raw;
    struct
    {
        uint8_t open_bus : 5;
        uint8_t sprite_overflow : 1;
        uint8_t sprite_hit : 1;
        uint8_t vblank : 1;
    };

} PpuStatus;

typedef union
{
    uint16_t raw : 15;

    struct
    {
        // Coarse X (tile column) (Bits 0-4)
        uint16_t coarse_x : 5;

        // Coarse Y (tile row) (Bits 5-9)
        uint16_t coarse_y : 5;

        // Nametable select (Bits 10-11)
        uint16_t name_table_sel : 2;

        // Fine Y offset (Vertical offset within a tile) (Bits 12-14)
        uint16_t fine_y : 3;
    } scrolling;

    struct
    {
        // Fine Y offset, the row number within a tile
        uint16_t fine_y : 3;// Bits 0-2
        // Bit plane (0: less significant bit; 1: more significant bit)
        uint16_t bit_plane_msb : 1; // Bit 3
        // Tile number from name table
        uint16_t tile_num  : 8; // Bits 4-12
        // Half of pattern table (0: "left"; 1: "right")
        uint16_t pattern_table_half : 1; // Bit 13
        // Pattern table is at $0000-$1FFF
        uint16_t pattern_table_low_addr : 1; // Bit 14 
    } pattern_table;

    struct
    {
        //NN 1111 YYY XXX
        //|| |||| ||| +++-- high 3 bits of coarse X (x/4)
        //|| |||| +++------ high 3 bits of coarse Y (y/4)
        //|| ++++---------- attribute offset (960 bytes)
        //++--------------- nametable select
        uint16_t coarse_x : 3;
        uint16_t coarse_y : 3;
        uint16_t attrib_offset : 4;
        uint16_t name_table_sel : 2;
    } fetching;

    // The 16-bit address is written to PPUADDR one byte at a time, high byte first.
    // Whether this is the first or second write is tracked by the PPU's internal w register, which is shared with PPUSCROLL.
    // If w is not 0 or its state is not known, it must be cleared by reading PPUSTATUS before writing the address.
    // For example, to set the VRAM address to $2108 after w is known to be 0: 
    struct
    {
        uint16_t low : 8;
        uint16_t high : 6;
        uint16_t  bit_z: 1;
    } writing;

    struct {
        uint16_t bit0 : 1;
        uint16_t bit1 : 1;
        uint16_t bit2 : 1;
        uint16_t bit3 : 1;
        uint16_t bit4 : 1;
        uint16_t bit5 : 1;
        uint16_t bit6 : 1;
        uint16_t bit7 : 1;
        uint16_t bit8 : 1;
        uint16_t bit9 : 1;
        uint16_t bit10 : 1;
        uint16_t bit11 : 1;
        uint16_t bit12 : 1;
        uint16_t bit13 : 1;
        uint16_t bit14 : 1;
    } raw_bits;

} PpuAddrReg;

typedef union
{
    uint16_t raw;

    struct
    {
        uint16_t low : 8;
        uint16_t high : 8;
    };

} ShiftReg;

typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} Color;

typedef union
{
    uint8_t raw;
    struct {
        uint8_t palette : 2;
        uint8_t padding : 3;
        uint8_t priority : 1;
        uint8_t horz_flip : 1;
        uint8_t vert_flip : 1;
    };
} Attribs;

typedef union
{
    uint8_t raw[4];
    struct
    {
        uint8_t y;
        uint8_t tile_id;
        Attribs attribs;
        uint8_t x;
    };
} Sprite;

typedef struct
{
    Sprite sprites[64];
    int64_t cycles;
    uint64_t frames;
    int32_t cycles_to_run;
    int32_t cycle_counter;
    int scanline;

    // Double buffer for SDL
    // buffer 0 is the backbuffer
    // buffer 1 is the frontbuffer
    uint32_t *buffers[2];

    // PPU internel regs
    struct {
        // vram addr or scroll position
        PpuAddrReg v;
        // When rendering, the coarse-x scroll for the next scanline and the starting y scroll for the screen.
        // Outside of rendering, holds the scroll or VRAM address before transferring it to v
        PpuAddrReg t;
        // Fine-x position of the current scroll, used during rendering alongside v.
        uint8_t x;
        // write toggle
        bool w;
    };

    NameTableMirror nt_mirror_mode;
    int ext_input;
    uint8_t attrib_data;
    uint8_t tile_id;
    uint8_t bg_lsb;
    uint8_t bg_msb;
    ShiftReg bg_shift_low;
    ShiftReg bg_shift_high;
    ShiftReg attrib_shift_low;
    ShiftReg attrib_shift_high;
    uint8_t sprite_lsb;
    uint8_t sprite_msb;
    //ShiftReg sprite_shift_low;
    //ShiftReg sprite_shift_high;

    // Per scanline
    int found_sprites;
    bool sprite0_loaded;
    
    uint32_t bus_addr;
    bool rendering;
    bool clear_vblank;
    bool frame_finished;

    // External io regs for cpu
    PpuCtrl ctrl;
    PpuMask mask;
    PpuStatus status;
    uint8_t oam_addr;
    // Read buffer for $2007
    uint8_t buffered_data;
    // io data bus
    uint8_t io_bus;
} Ppu;

void PPU_Init(Ppu *ppu, int name_table_layout, uint32_t **buffers);
void PPU_Update(Ppu *ppu, uint64_t cpu_cycles);
void PPU_Tick(Ppu *ppu);
void PPU_Reset(Ppu *ppu);
uint8_t ReadPPURegister(Ppu *ppu, const uint16_t addr);
void WritePPURegister(Ppu *ppu, const uint16_t addr, const uint8_t data);
void PpuSetMirroring(NameTableMirror mode, int page);

#endif
