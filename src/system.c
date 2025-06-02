#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "arena.h"
#include "apu.h"
#include "cart.h"
#include "system.h"
#include "mapper.h"
#include "ppu.h"
#include "utils.h"

static System *system_ptr = NULL;

System *SystemCreate(Arena *arena)
{
    System *system = ArenaPush(arena, sizeof(System));
    system->cpu = ArenaPush(arena, sizeof(Cpu));
    system->apu = ArenaPush(arena, sizeof(Apu));
    system->ppu = ArenaPush(arena, sizeof(Ppu));
    system->cart = ArenaPush(arena, sizeof(Cart));
    system->joy_pad = ArenaPush(arena, sizeof(JoyPad));
    system->sys_ram = ArenaPush(arena, CPU_RAM_SIZE);

    system_ptr = system;
    return system;
}

int SystemLoadCart(Arena *arena, System *system, const char *path)
{
    return CartLoad(arena, system->cart, path);
}

void SystemInit(System *system, uint32_t **buffers)
{
    CPU_Init(system->cpu);
    APU_Init(system->apu);
    PPU_Init(system->ppu, system->cart->mirroring, buffers);
}

uint8_t SystemReadOpenBus(void)
{
    return system_ptr->bus_data;
}

static void SWramWrite(System *system, const uint16_t addr, const uint8_t data)
{
    system->cart->ram[addr & 0x1FFF] = data;
}

static uint8_t SWramRead(System *system, const uint16_t addr)
{
    return system->cart->ram[addr & 0x1FFF];
}

uint8_t BusRead(const uint16_t addr)
{
    // Extract A15, A14, and A13
    uint8_t region = (addr >> 13) & 0x7;
    //SystemSync(system_ptr->cpu->cycles);

    switch (region)
    {
        // $0000 - $1FFF
        case 0x0:
            system_ptr->bus_data = system_ptr->sys_ram[addr & 0x7FF];
            break;

        // $2000 - $3FFF
        case 0x1:
        {
            //SystemSync(system_ptr->cpu->cycles + 1);
            system_ptr->bus_data = ReadPPURegister(system_ptr->ppu, addr);
            break;
        }
        // $4000 - $5FFF
        case 0x2:
            if (addr < 0x4018)
            {
                if (addr == 0x4016)
                {
                    //DEBUG_LOG("Requested Joypad reg 0x%04X\n", addr);
                    system_ptr->bus_data = ReadJoyPadReg(system_ptr->joy_pad);
                }
                else if (addr == 0x4017)
                {
                    //DEBUG_LOG("Requested Joypad reg 0x%04X\n", addr);
                    system_ptr->bus_data = 0;
                }
                else
                {
                    system_ptr->bus_data = ReadAPURegister(system_ptr->apu, addr);
                }
                break;
            }
            else
            {
                DEBUG_LOG("Trying to read value at 0x%04X\n", addr);
                break;
            }

        case 0x3:  // $6000 - $7FFF
            system_ptr->bus_data = SWramRead(system_ptr, addr);
            break;
    
        case 0x4:  // $8000 - $9FFF
        case 0x5:  // $A000 - $BFFF
        case 0x6:  // $C000 - $DFFF
        case 0x7:  // $E000 - $FFFF
            system_ptr->bus_data = MapperReadPrgRom(system_ptr->cart, addr);
        break;
    }

    // Finally read the data from the bus
    return system_ptr->bus_data;
}

void BusWrite(const uint16_t addr, const uint8_t data)
{
    // Extract A15, A14, and A13
    uint8_t region = (addr >> 13) & 0x7;
    //SystemSync(system_ptr->cpu->cycles);

    switch (region)
    {
        // $0000 - $1FFF
        case 0x0:
            // Internal RAM (mirrored)
            system_ptr->sys_ram[addr & 0x7FF] = data;
            break;

        // $2000 - $3FFF
        case 0x1:
            //SystemSync(system_ptr->cpu->cycles);
            WritePPURegister(system_ptr->ppu, addr, data);
            break;

        // $4000 - $5FFF
        case 0x2:
        {
            if (addr == 0x4014)
            {
                DEBUG_LOG("Requested OAM DMA 0x%04X\n", addr);
                system_ptr->cpu->cycles += 513;
                OAM_Dma(data);
            }
            else if (addr == 0x4016)
            {
                //DEBUG_LOG("Requested Joypad reg 0x%04X\n", addr);
                WriteJoyPadReg(system_ptr->joy_pad, data);
            }
            //else if (addr == 0x4017)
            //{
            //    //DEBUG_LOG("Requested Joypad reg 0x%04X\n", addr);
            //}
            else if (addr < 0x4018)
            {
                WriteAPURegister(system_ptr->apu, addr, data);
            }
            else
            {
                DEBUG_LOG("Trying to write %d at 0x%04X\n", data, addr);
            }
            break;
        }
        // SRAM / WRAM
        // $6000 - $7FFF
        case 0x3:
            SWramWrite(system_ptr, addr, data);
            break;

        case 0x4:  // $8000 - $9FFF
        case 0x5:  // $A000 - $BFFF
        case 0x6:  // $C000 - $DFFF
        case 0x7:  // $E000 - $FFFF
            MapperWrite(system_ptr->cart, addr, data);
            break;
    }
}

// TODO: Remove
uint8_t *SystemGetPtr(const uint16_t addr)
{
    // Extract A15, A14, and A13
    uint8_t region = (addr >> 13) & 0x7;

    switch (region)
    {
        case 0x0:  // $0000 - $1FFF
        {
            return &system_ptr->sys_ram[addr & 0x7FF];
        }

        case 0x2:  // $4000 - $5FFF
            if (addr < 0x4018)
            {
                //return &g_apu_regs[addr % 0x18];
                printf("Trying to read APU/IO reg at 0x%04X\n", addr);
                break;
            }
            else
            {
                printf("Trying to read unknown (Mapper reg?) value at 0x%04X\n", addr);
                break;  // $4018-$5FFF might be mapper-controlled
            }


        case 0x3:  // $6000 - $7FFF
        {
            return &system_ptr->cart->ram[addr & 0x1FFF];
        }
    
        case 0x4:  // $8000 - $9FFF
        case 0x5:  // $A000 - $BFFF
        case 0x6:  // $C000 - $DFFF
        case 0x7:  // $E000 - $FFFF
        {
            // Temp
            PrgRom *prg_rom = &system_ptr->cart->prg_rom;
            return &prg_rom->data[addr & (prg_rom->size - 1)];
        }
    }

    return NULL; 
}


Cart *SystemGetCart(void)
{
    return system_ptr->cart;
}

Apu *SystemGetApu(void)
{
    return system_ptr->apu;
}

Ppu *SystemGetPpu(void)
{
    return system_ptr->ppu;
}

Cpu *SystemGetCpu(void)
{
    return system_ptr->cpu;
}

// TODO: The Ppu struct should have a ptr to the chr rom / chr ram
// The PPU only exposes the io regs on the main bus, it has its own bus
uint8_t PpuBusReadChrRom(const uint16_t addr)
{
    return MapperReadChrRom(system_ptr->cart, addr);
}

void PpuBusWriteChrRam(const uint16_t addr, const uint8_t data)
{
    ChrRom *chr_rom = &system_ptr->cart->chr_rom;
    chr_rom->data[addr & (chr_rom->size - 1)] = data;
}

void PpuClockMMC3(void)
{
    if (system_ptr->cart->mapper_num != 4)
        return;

    if (Mmc3ClockIrqCounter(system_ptr->cart))
        system_ptr->ppu->finish_early = true;
}

void SystemRun(System *system, bool paused, bool step_instr, bool step_frame)
{
    if (paused && !step_instr && !step_frame)
        return;

    if (step_frame && system->ppu->frame_finished)
    {
        system->ppu->frame_finished = false;
        return;
    }

    system->ppu->frame_finished = false;

    do {
        CPU_Update(system->cpu);
    } while (!system->ppu->frame_finished && !step_instr);
}


bool SystemPollAllIrqs(void)
{
    return PollApuIrqs(system_ptr->apu) || PollMapperIrq();
}

void SystemSendNmiToCpu(void)
{
    system_ptr->cpu->nmi_pending = true;
}

void SystemSync(uint64_t cycles)
{
    APU_Update(system_ptr->apu, cycles);
    PPU_Update(system_ptr->ppu, cycles);
}

void SystemAddCpuCycles(uint32_t cycles)
{
    system_ptr->cpu->cycles += cycles;
}

void SystemUpdateJPButtons(System *system, const bool *buttons)
{
    JoyPadSetButton(system->joy_pad, JOYPAD_A, buttons[0]);
    JoyPadSetButton(system->joy_pad, JOYPAD_B, buttons[1]);
    JoyPadSetButton(system->joy_pad, JOYPAD_UP, buttons[2]);
    JoyPadSetButton(system->joy_pad, JOYPAD_DOWN, buttons[3]);
    JoyPadSetButton(system->joy_pad, JOYPAD_LEFT, buttons[4]);
    JoyPadSetButton(system->joy_pad, JOYPAD_RIGHT, buttons[5]);
    JoyPadSetButton(system->joy_pad, JOYPAD_START, buttons[6]);
    JoyPadSetButton(system->joy_pad, JOYPAD_SELECT, buttons[7]);
}

void SystemReset(System *system)
{
    CPU_Reset(system->cpu);
    APU_Reset(system->apu);
    PPU_Reset(system->ppu);
}

void SystemShutdown(System *system)
{
    CartSaveSram(system->cart);
}
