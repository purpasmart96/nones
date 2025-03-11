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
#define PPU_CTRL   0x2000
#define PPU_MASK   0x2001
#define PPU_STATUS 0x2002
#define OAM_ADDR   0x2003
#define OAM_DATA   0x2004
#define PPU_SCROLL 0x2005
#define PPU_ADDR   0x2006
#define PPU_DATA   0x2007
#define OAM_DMA    0x4014

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
        uint8_t open_bus : 5;
        uint8_t sprite_overflow : 1;
        uint8_t sprite_hit : 1;
        uint8_t vblank : 1;
    };

} PpuStatus;

typedef union
{
    uint16_t raw : 15;
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
    } pattern_table;

    struct
    {
        //NN 1111 YYY XXX
        //|| |||| ||| +++-- high 3 bits of coarse X (x/4)
        //|| |||| +++------ high 3 bits of coarse Y (y/4)
        //|| ++++---------- attribute offset (960 bytes)
        //++--------------- nametable select
        uint8_t coarse_x : 3;
        uint8_t coarse_y : 3;
        uint8_t attrib_offset : 4;
        uint8_t name_table_sel : 2;
    } fetching;
} PpuAddrReg;

typedef struct
{
    uint64_t cycles;
    uint64_t prev_cpu_cycles;

    // PPU internel regs
    struct {
        // vram addr or scroll position
        PpuAddrReg v;
        // When rendering, the coarse-x scroll for the next scanline and the starting y scroll for the screen.
        // Outside of rendering, holds the scroll or VRAM address before transferring it to v
        uint16_t t;
        // Fine-x position of the current scroll, used during rendering alongside v.
        uint8_t x : 3;
        // write toggle
        bool w;
    };

    NameTableMirror nt_mirror_mode;
    int prev_scanline;
    bool frame_finished;
    uint8_t buffered_data; // Read buffer for $2007

    // External regs for cpu
    PpuCtrl ctrl;
    uint8_t mask;
    uint8_t oam_addr;
    PpuStatus status;
    uint32_t *buffer;
} Ppu;

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

void PPU_Init(Ppu *ppu, int name_table_layout, uint32_t *pixels);
void PPU_Update(Ppu *ppu, uint64_t cpu_cycles);
void PPU_Reset(void);
void PPU_Write8(uint16_t addr, uint8_t data);
uint8_t ReadPPURegister(const uint16_t addr);
void WritePPURegister(const uint16_t addr, const uint8_t data);
uint8_t *GetPPUMemPtr(uint16_t addr);
void OAM_Dma(const uint16_t addr);

bool PPU_NmiTriggered(void);

#endif
