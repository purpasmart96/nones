
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "nes.h"
#include "ppu.h"


static uint8_t sram[0x2000];

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

typedef enum {
    NROM_INVALID,
    NROM_128,
    NROM_256
} NromType;

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

static MemMap mappings[10];
static int num_mappings;

static bool MapperOverlaps(MemMap *a, MemMap *b)
{
    if (a->base <= b->base && b->base < (a->base + a->size))
        return true;
    if (a->base < (b->base+b->size) && (b->base+b->size) < (a->base+a->size))
        return true;
    return false;
}

static bool MapperContains(MemMap *map, uint16_t addr, uint16_t size)
{
    return (map->base <= addr && (addr + size) <= (map->base + map->size));
}

static int MapperAdd(uint16_t base, uint16_t size)
{
    if (!size)
        return 0;

    mappings[num_mappings].base = base;
    mappings[num_mappings].size = size;
    mappings[num_mappings].mirror = false;
    mappings[num_mappings].ppu_mirror_regs = false;

    for (int j = 0; j < num_mappings; j++) 
    {
        if (MapperOverlaps(&mappings[j], &mappings[num_mappings]))
        {
            printf("Mapping already exists! [0x%04X->0x%04X]\n", base, base + size);
            return 2;
        }
    }

    num_mappings++;
    return 0;
}

static int MapperAddMirror(uint16_t base, uint16_t size, uint16_t mirrored_addr, bool ppu_mirror_regs)
{
    if (!size)
        return 0;

    mappings[num_mappings].base = base;
    mappings[num_mappings].size = size;
    mappings[num_mappings].mirror = true;
    mappings[num_mappings].ppu_mirror_regs = ppu_mirror_regs;

    for (int j = 0; j < num_mappings; j++) 
    {
        if (MapperOverlaps(&mappings[j], &mappings[num_mappings]))
        {
            printf("Mapping already exists! [0x%04X->0x%04X]\n", base, base + size);
            return 2;
        }
    }

    num_mappings++;
    return 0;
}

static uint16_t MapperMirrorAddrToRealAddr(MemMap *map, uint16_t addr)
{
    if (map->ppu_mirror_regs)
        return map->mirrored_addr + (addr & 7);

    return map->mirrored_addr + (addr - map->base);
}

uint8_t MapperRead8(uint16_t addr)
{
    for (int i = 0; i < num_mappings; i++)
    {
        MemMap *map = &mappings[i];
        if (MapperContains(map, addr, 1))
        { 
            if (map->mirror)
            {
                const uint16_t real_addr = MapperMirrorAddrToRealAddr(map, addr);
                printf("Reading from mirrored addr 0x%04X\n", addr);
                return MemRead16(real_addr);
            }
            return MemRead8(addr);
        }
    }

    printf("Invalid read at 0x%04X\n", addr);
    return 0;
}

uint8_t MapperRead16(uint16_t addr)
{
    for (int i = 0; i < num_mappings; i++)
    {
        MemMap *map = &mappings[i];
        if (MapperContains(map, addr, 2))
        { 
            if (map->mirror)
            {
                const uint16_t real_addr = MapperMirrorAddrToRealAddr(map, addr);
                return MemRead16(real_addr);
            }
            return MemRead16(addr);
        }
    }

    printf("Invalid read at 0x%04X\n", addr);
    return 0;
}

void MapperWrite8(uint16_t addr, uint8_t data)
{
    for (int i = 0; i < num_mappings; i++)
    {
        MemMap *map = &mappings[i];
        if (MapperContains(map, addr, 1))
        { 
            if (map->mirror)
            {
                const uint16_t real_addr = MapperMirrorAddrToRealAddr(map, addr);
                MemWrite8(real_addr, data);
                return;
            }
            MemWrite16(addr, data);
            return;
        }
    }

    printf("Invalid write at 0x%04X\n", addr);
}

void MapperInit(void)
{
    MapperAdd(CPU_RAM_START_ADDR, CPU_RAM_SIZE);
    MapperAddMirror(CPU_RAM_MIRROR0_START_ADDR, CPU_RAM_SIZE, CPU_RAM_START_ADDR, false);
    MapperAddMirror(CPU_RAM_MIRROR1_START_ADDR, CPU_RAM_SIZE, CPU_RAM_START_ADDR, false);
    MapperAddMirror(CPU_RAM_MIRROR2_START_ADDR, CPU_RAM_SIZE, CPU_RAM_START_ADDR, false);
    MapperAdd(PPU_REGS_START_ADDR,PPU_REGS_SIZE);
    // $3456 is the same as a write to $2006. 
    MapperAddMirror(PPU_REGS_MIRROR_START_ADDR, PPU_REGS_MIRROR_SIZE, PPU_REGS_START_ADDR, true);
    MapperAdd(PPU_REGS_START_ADDR,PPU_REGS_SIZE);
    MapperAdd(0, 0x800);
    MapperAdd(0, 0x800);
}
