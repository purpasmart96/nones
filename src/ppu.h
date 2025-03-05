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

typedef struct
{
    uint64_t cycles;
    uint64_t prev_cpu_cycles;
    int prev_scanline;
    bool frame_finished;
    NameTableMirror nt_mirror_mode;
    uint16_t vram_addr;
    uint16_t temp_vram_addr;
    uint8_t x : 3;
    bool write;
} Ppu;

typedef union
{
    uint8_t raw;
    struct {
        // Base nametable address
        // (0 = $2000; 1 = $2400; 2 = $2800; 3 = $2C00)
        uint8_t base_name_table_addr : 2;
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

void PPU_Init(Ppu *ppu, int name_table_layout);
void PPU_Update(Ppu *ppu, uint64_t cpu_cycles);
void PPU_Reset(void);
void PPU_Write8(uint16_t addr, uint8_t data);
uint8_t *GetPPUMemPtr(uint16_t addr);

bool PPU_NmiTriggered(void);

#endif
