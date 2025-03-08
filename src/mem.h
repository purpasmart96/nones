#ifndef MEM_H
#define MEM_H

typedef enum {
    NONE,
    READ,
    WRITE,
    READ_WRITE,
    EXECUTE,
    EXECUTE_READ,
    EXECUTE_WRITE,
    EXECUTE_READ_WRITE,
} MemPermissions;

typedef struct {
    uint16_t base;
    uint16_t size;
    uint16_t mirrored_addr;
    bool ro;
    bool mirror;
    bool ppu_mirror_regs;
} MemMap;

#define CPU_RAM_START_ADDR 0
#define CPU_RAM_SIZE 0x800
#define CPU_RAM_MIRROR0_START_ADDR 0x800
#define CPU_RAM_MIRROR1_START_ADDR 0x1000
#define CPU_RAM_MIRROR2_START_ADDR 0x1800
#define PPU_REGS_START_ADDR 0x2000
#define PPU_REGS_SIZE 0x8
#define PPU_REGS_MIRROR_START_ADDR 0x2008
#define PPU_REGS_MIRROR_SIZE 0x1FF8
#define APU_IO_REGS_START_ADDR 0x4000
#define APU_IO_REGS_SIZE 0x18

extern uint8_t g_ppu_regs[PPU_REGS_SIZE];
extern uint8_t g_apu_regs[APU_IO_REGS_SIZE];

extern uint8_t *g_sys_ram;
extern uint8_t *g_sram;
extern uint8_t *g_prg_rom;
extern uint32_t g_prg_rom_size;
extern uint8_t *g_chr_rom;
extern uint32_t g_chr_rom_size;

//void MemInit(void);

int MemAddMap(uint16_t base, uint16_t size);
int MemAddMirror(uint16_t base, uint16_t size, uint16_t mirrored_addr, bool ppu_mirror_regs);
uint16_t MemGetNonMirroredAddr(uint16_t addr);

//uint8_t MemRead8(uint16_t addr);
//uint8_t MemRead16(uint16_t addr);
//void MemWrite8(uint16_t addr, uint8_t data);

#endif
