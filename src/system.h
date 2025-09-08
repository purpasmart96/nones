#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "arena.h"
#include "cpu.h"
#include "apu.h"
#include "ppu.h"
#include "joypad.h"
#include "cart.h"
#include "mapper.h"

typedef enum
{
    RUNNING,
    PAUSED,
    STEP_INSTR,
    STEP_FRAME
} SystemState;

typedef struct System
{
    Cpu *cpu;
    Apu *apu;
    Ppu *ppu;

    Cart *cart;
    JoyPad *joy_pad1;
    JoyPad *joy_pad2;
    uint8_t *sys_ram;
    int oam_dma_bytes_remaining;

    uint16_t cpu_addr;
    //uint16_t oam_addr;
    //uint16_t dmc_addr;
    //uint16_t addr_bus;

    bool oam_dma_triggered;
    bool dmc_dma_triggered;
    bool dma_pending;

    uint8_t bus_data;
} System;

#define CPU_RAM_SIZE 0x800

System *SystemCreate(Arena *arena);
void SystemInit(System *system, uint32_t **buffers);
void SystemRun(System *system, SystemState state, bool debug_info);
void SystemSync(uint64_t cycles);
void SystemTick(void);
void SystemSetNmiPin(void);
void SystemPollNmi(void);
void SystemPrePollAllIrqs(void);
bool SystemPollAllIrqs(void);
void SystemReset(System *system);
void SystemShutdown(System *system);

uint8_t SystemReadOpenBus(void);
void SystemSignalDmcDma(void);
uint8_t SystemRead(const uint16_t addr);
uint8_t BusRead(const uint16_t addr);
void SystemWrite(const uint16_t addr, const uint8_t data);
void BusWrite(const uint16_t addr, const uint8_t data);
int SystemLoadCart(Arena *arena, System *System, const char *path);

uint8_t PpuBusReadChrRom(const uint16_t addr);
void PpuBusWriteChrRam(const uint16_t addr, const uint8_t data);
void PpuClockMMC3(void);

void SystemAddCpuCycles(uint32_t cycles);
void SystemUpdateJPButtons(System *system, const bool *buttons);

Cart *SystemGetCart(void);
Apu *SystemGetApu(void);
Ppu *SystemGetPpu(void);
Cpu *SystemGetCpu(void);

#endif
