
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "nes.h"
#include "ppu.h"
#include "mem.h"
#include "utils.h"

static uint8_t sram[0x2000];
static MemMap mappings[10];
static int num_mappings;

static bool MemOverlaps(MemMap *a, MemMap *b)
{
    if (a->base <= b->base && b->base < (a->base + a->size))
        return true;
    if (a->base < (b->base+b->size) && (b->base+b->size) < (a->base+a->size))
        return true;
    return false;
}

static bool MemContains(MemMap *map, uint16_t addr, uint16_t size)
{
    return (map->base <= addr && (addr + size) <= (map->base + map->size));
}

int MemAddMap(uint16_t base, uint16_t size)
{
    if (!size)
        return 0;

    mappings[num_mappings].base = base;
    mappings[num_mappings].size = size;
    mappings[num_mappings].mirror = false;
    mappings[num_mappings].ppu_mirror_regs = false;

    for (int j = 0; j < num_mappings; j++) 
    {
        if (MemOverlaps(&mappings[j], &mappings[num_mappings]))
        {
            printf("Mapping already exists! [0x%04X->0x%04X]\n", base, base + size);
            return 2;
        }
    }

    num_mappings++;
    return 0;
}

int MemAddMirror(uint16_t base, uint16_t size, uint16_t mirrored_addr, bool ppu_mirror_regs)
{
    if (!size)
        return 0;

    mappings[num_mappings].base = base;
    mappings[num_mappings].size = size;
    mappings[num_mappings].mirror = true;
    mappings[num_mappings].mirrored_addr = mirrored_addr;
    mappings[num_mappings].ppu_mirror_regs = ppu_mirror_regs;

    for (int i = 0; i < num_mappings; i++) 
    {
        if (MemOverlaps(&mappings[i], &mappings[num_mappings]))
        {
            printf("Mapping already exists! [0x%04X->0x%04X]\n", base, base + size);
            return 2;
        }
    }

    num_mappings++;
    return 0;
}

static uint16_t MemMirrorAddrToRealAddr(MemMap *map, uint16_t addr)
{
    if (map->ppu_mirror_regs)
        return map->mirrored_addr + (addr & 7);

    return map->mirrored_addr + (addr - map->base);
}

uint8_t MemRead8(uint16_t addr)
{
    for (int i = 0; i < num_mappings; i++)
    {
        MemMap *map = &mappings[i];
        if (MemContains(map, addr, 1))
        { 
            if (map->mirror)
            {
                const uint16_t real_addr = MemMirrorAddrToRealAddr(map, addr);
                DEBUG_LOG("Reading from mirrored addr 0x%04X\n", addr);
                return CPURead8(real_addr);
            }
            return CPURead8(addr);
        }
    }

    printf("Invalid read at 0x%04X\n", addr);
    return 0;
}

uint16_t MemGetNonMirroredAddr(uint16_t addr)
{
    for (int i = 0; i < num_mappings; i++)
    {
        MemMap *map = &mappings[i];
        if (MemContains(map, addr, 1))
        { 
            if (map->mirror)
            {
                const uint16_t real_addr = MemMirrorAddrToRealAddr(map, addr);
                printf("0x%04X is a mirror\n", addr);
                return real_addr;
            }
            return addr;
        }
    }
    return addr;
}

uint8_t MemRead16(uint16_t addr)
{
    for (int i = 0; i < num_mappings; i++)
    {
        MemMap *map = &mappings[i];
        if (MemContains(map, addr, 2))
        { 
            if (map->mirror)
            {
                const uint16_t real_addr = MemMirrorAddrToRealAddr(map, addr);
                return CPURead16(real_addr);
            }
            return CPURead16(addr);
        }
    }

    printf("Invalid read at 0x%04X\n", addr);
    return 0;
}

void MemWrite8(uint16_t addr, uint8_t data)
{
    for (int i = 0; i < num_mappings; i++)
    {
        MemMap *map = &mappings[i];
        if (!MemContains(map, addr, 1))
            continue;

        if (map->mirror)
        {
            const uint16_t real_addr = MemMirrorAddrToRealAddr(map, addr);
            CPUWrite8(real_addr, data);
            return;
        }

        CPUWrite8(addr, data);
        return;
    }

    printf("Invalid write at 0x%04X\n", addr);
}

void MemInit(void)
{
    MemAddMap(CPU_RAM_START_ADDR, CPU_RAM_SIZE);
    MemAddMirror(CPU_RAM_MIRROR0_START_ADDR, CPU_RAM_SIZE, CPU_RAM_START_ADDR, false);
    MemAddMirror(CPU_RAM_MIRROR1_START_ADDR, CPU_RAM_SIZE, CPU_RAM_START_ADDR, false);
    MemAddMirror(CPU_RAM_MIRROR2_START_ADDR, CPU_RAM_SIZE, CPU_RAM_START_ADDR, false);
    MemAddMap(PPU_REGS_START_ADDR,PPU_REGS_SIZE);
    // $3456 is the same as a write to $2006. 
    MemAddMirror(PPU_REGS_MIRROR_START_ADDR, PPU_REGS_MIRROR_SIZE, PPU_REGS_START_ADDR, true);
    //MemAddMap(PPU_REGS_START_ADDR,PPU_REGS_SIZE);
    //MemAddMap(0, 0x800);
}
