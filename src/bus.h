#ifndef BUS_H
#define BUS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "arena.h"
#include "cpu.h"
#include "apu.h"
#include "ppu.h"
#include "joypad.h"
#include "cart.h"

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

typedef struct
{
    Cpu *cpu;
    Apu *apu;
    Ppu *ppu;

    Cart *cart;
    JoyPad *joy_pad;
    uint8_t *sys_ram;
} Bus;

Bus *BusCreate(Arena *arena);
uint8_t BusRead(const uint16_t addr);
void BusWrite(const uint16_t addr, const uint8_t data);
uint8_t *BusGetPtr(const uint16_t addr);
int BusLoadCart(Arena *arena, Bus *bus, const char *path);

uint8_t PpuBusReadChrRom(const uint16_t addr);
void PpuBusWriteChrRam(const uint16_t addr, const uint8_t data);
void PpuClockMMC3(void);
void PpuClockMMC3v2(int scanline);
void BusUpdate(uint64_t cycles);
void BusAddCpuCycles(uint32_t cycles);
void ClearIrq(void);

Cart *BusGetCart(void);
Apu *BusGetApu(void);
Ppu *BusGetPpu(void);

#endif
