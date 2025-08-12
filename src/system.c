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
    system->joy_pad1 = ArenaPush(arena, sizeof(JoyPad));
    system->joy_pad2 = ArenaPush(arena, sizeof(JoyPad));
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
    PPU_Init(system->ppu, system->cart->mirroring, buffers);
    APU_Init(system->apu);
    CPU_Init(system->cpu);
}

uint8_t SystemReadOpenBus(void)
{
    return system_ptr->bus_data;
}

static void SystemRamWrite(System *system, const uint16_t addr, const uint8_t data)
{
    system->sys_ram[addr & 0x7FF] = data;
}

static uint8_t SystemRamRead(System *system, const uint16_t addr)
{
    return system->sys_ram[addr & 0x7FF];
}

static void SWramWrite(System *system, const uint16_t addr, const uint8_t data)
{
    system->cart->ram[addr & 0x1FFF] = data;
}

static uint8_t SWramRead(System *system, const uint16_t addr)
{
    return system->cart->ram[addr & 0x1FFF];
}

static void NinjaWrite(System *system, const uint16_t addr, const uint8_t data)
{
    SWramWrite(system, addr, data);
    MapperWrite(system->cart, addr, data);
}

static void SystemStartOamDma(const uint8_t page_num)
{
    uint16_t base_addr = (page_num * 0x100);

    // Add cpu halt cycle
    SystemAddCpuCycles(1);
    SystemTick();
    if (system_ptr->cpu->cycles & 1)
    {
        SystemAddCpuCycles(1);
        SystemTick();
    }
    for (int i = 0; i < 256; i++)
    {
        SystemTick();
        // OAM DMA uses Ppu reg $2004 (OAM_DATA) internally
        const uint8_t data = BusRead(base_addr++);
        SystemTick();
        BusWrite(OAM_DATA_REG, data);
    }
}

typedef void (*MemMap6k)(System *system, const uint16_t addr, const uint8_t data);
// TODO: This method of handling mem maps should be redone
static const MemMap6k mem_map_6k[] =
{
    [0] = SWramWrite,
    [1] = NinjaWrite,
};

uint8_t BusRead(const uint16_t addr)
{
    ++system_ptr->cpu->cycles;
    // Extract A15, A14, and A13
    uint8_t region = (addr >> 13) & 0x7;

    switch (region)
    {
        // $0000 - $1FFF
        case 0x0:
            system_ptr->bus_data = SystemRamRead(system_ptr, addr);
            break;

        // $2000 - $3FFF
        case 0x1:
        {
            system_ptr->bus_data = ReadPPURegister(system_ptr->ppu, addr);
            break;
        }
        // $4000 - $5FFF
        case 0x2:
            if (addr < 0x4018)
            {
                if (addr == 0x4016)
                {
                    // Clear bits 0–4
                    system_ptr->bus_data &= 0xE0;
                    // Update bits 0–4
                    system_ptr->bus_data |= (ReadJoyPadReg(system_ptr->joy_pad1) & 0x1F);
                }
                else if (addr == 0x4017)
                {
                    // Clear bits 0–4
                    system_ptr->bus_data &= 0xE0;
                    // Update bits 0–4
                    system_ptr->bus_data |= (ReadJoyPadReg(system_ptr->joy_pad2) & 0x1F);
                }
                else if (addr == 0x4015)
                {
                    return ReadAPURegister(system_ptr->apu, addr);
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
    ++system_ptr->cpu->cycles;
    // Extract A15, A14, and A13
    uint8_t region = (addr >> 13) & 0x7;

    switch (region)
    {
        // $0000 - $1FFF
        case 0x0:
            // Internal RAM (mirrored)
            SystemRamWrite(system_ptr, addr, data);
            break;

        // $2000 - $3FFF
        case 0x1:
            WritePPURegister(system_ptr->ppu, addr, data);
            break;

        // $4000 - $5FFF
        case 0x2:
        {
            if (addr == 0x4014)
            {
                DEBUG_LOG("Requested OAM DMA 0x%04X\n", addr);
                SystemStartOamDma(data);
            }
            else if (addr == 0x4016)
            {
                WriteJoyPadReg(system_ptr->joy_pad1, data);
                WriteJoyPadReg(system_ptr->joy_pad2, data);
            }
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
        // SRAM / WRAM / Mapper
        // $6000 - $7FFF
        case 0x3:
            mem_map_6k[system_ptr->cart->mem_map](system_ptr, addr, data);
            break;

        case 0x4:  // $8000 - $9FFF
        case 0x5:  // $A000 - $BFFF
        case 0x6:  // $C000 - $DFFF
        case 0x7:  // $E000 - $FFFF
            MapperWrite(system_ptr->cart, addr, data);
            break;
    }

    system_ptr->bus_data = data;
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
    if (system_ptr->cart->mapper_num != MAPPER_MMC3)
        return;

    Mmc3ClockIrqCounter(system_ptr->cart);
}

void SystemRun(System *system, SystemState state, bool debug_info)
{
    if (state == PAUSED)
        return;

    if (state == STEP_FRAME && system->ppu->frame_finished)
    {
        system->ppu->frame_finished = false;
        return;
    }

    system->ppu->frame_finished = false;

    do {
        CPU_Update(system->cpu, debug_info);
    } while (!system->ppu->frame_finished && state != STEP_INSTR);
}

bool SystemPollAllIrqs(void)
{
    return PollApuIrqs(system_ptr->apu) || PollMapperIrq();
}

void SystemSetNmiPin(void)
{
    system_ptr->cpu->nmi_pin = ~(system_ptr->ppu->ctrl.vblank_nmi & system_ptr->ppu->status.vblank);
}

// The PPU pulls /NMI low if and only if both vblank_flag and NMI_output are true.
static uint8_t SystemReadNmiPin(void)
{
    return ~(system_ptr->ppu->ctrl.vblank_nmi & system_ptr->ppu->status.vblank);
}

void SystemPollNmi(void)
{
    uint8_t current_nmi_pin = SystemReadNmiPin();
    if (~current_nmi_pin & system_ptr->cpu->nmi_pin)
    {
        system_ptr->cpu->nmi_pending = true;
        //printf("NMI falling edge at frame: %ld ppu cycle: %d scanline:%d\n",
        //        system_ptr->ppu->frames, system_ptr->ppu->cycle_counter, system_ptr->ppu->scanline);
    }
    system_ptr->cpu->nmi_pin = current_nmi_pin;
}

void SystemTick(void)
{
    APU_Tick(system_ptr->apu);

    PPU_Tick(system_ptr->ppu);
    SystemPollNmi();
    PPU_Tick(system_ptr->ppu);
    PPU_Tick(system_ptr->ppu);
    PpuUpdateRenderingState(system_ptr->ppu);
}

void SystemAddCpuCycles(uint32_t cycles)
{
    system_ptr->cpu->cycles += cycles;
}

void SystemUpdateJPButtons(System *system, const bool *buttons)
{
    JoyPadSetButton(system->joy_pad1, JOYPAD_A, buttons[0]);
    JoyPadSetButton(system->joy_pad1, JOYPAD_B, buttons[1]);
    JoyPadSetButton(system->joy_pad1, JOYPAD_UP, buttons[2]);
    JoyPadSetButton(system->joy_pad1, JOYPAD_DOWN, buttons[3]);
    JoyPadSetButton(system->joy_pad1, JOYPAD_LEFT, buttons[4]);
    JoyPadSetButton(system->joy_pad1, JOYPAD_RIGHT, buttons[5]);
    JoyPadSetButton(system->joy_pad1, JOYPAD_START, buttons[6]);
    JoyPadSetButton(system->joy_pad1, JOYPAD_SELECT, buttons[7]);

    JoyPadSetButton(system->joy_pad2, JOYPAD_A, buttons[8]);
    JoyPadSetButton(system->joy_pad2, JOYPAD_B, buttons[9]);
    JoyPadSetButton(system->joy_pad2, JOYPAD_UP, buttons[10]);
    JoyPadSetButton(system->joy_pad2, JOYPAD_DOWN, buttons[11]);
    JoyPadSetButton(system->joy_pad2, JOYPAD_LEFT, buttons[12]);
    JoyPadSetButton(system->joy_pad2, JOYPAD_RIGHT, buttons[13]);
    JoyPadSetButton(system->joy_pad2, JOYPAD_START, buttons[14]);
    JoyPadSetButton(system->joy_pad2, JOYPAD_SELECT, buttons[15]);
}

void SystemReset(System *system)
{
    APU_Reset(system->apu);
    PPU_Reset(system->ppu);
    CPU_Reset(system->cpu);
}

void SystemShutdown(System *system)
{
    CartSaveSram(system->cart);
}
