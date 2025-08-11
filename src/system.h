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
uint8_t BusRead(const uint16_t addr);
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
