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

void SystemInit(System *system, bool ppu_warmup, bool swap_duty_cycles, uint32_t **buffers, const uint32_t buffer_size)
{
    PPU_Init(system->ppu, system->cart->mirroring, ppu_warmup, buffers, buffer_size);
    APU_Init(system->apu, swap_duty_cycles);
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

static bool ApuRegsActivated(System *system)
{
    return system->cpu_addr >= 0x4000 && system->cpu_addr < 0x4020;
}

static bool ExplicitAbortDmcDma(System *system)
{
    return !system->apu->status.dmc;
}

static void SystemStartOamDma(System *system, const uint8_t page_num, const uint16_t addr)
{
    system->oam_dma_triggered = false;
    uint16_t base_addr = (page_num * 0x100);
    // Add cpu halt cycle
    SystemTick();
    BusRead(addr);

    bool single_dma_cycle = false;
    system->oam_dma_bytes_remaining = 256;
    while (system->oam_dma_bytes_remaining > 0)
    {
        // OAM Alignment cycle if needed
        if (system->cpu->cycles & 1)
        {
            SystemTick();
            BusRead(addr);
        }

        SystemTick();
        // OAM DMA uses Ppu reg $2004 (OAM_DATA) internally
        // Get
        const uint8_t data = BusRead(base_addr++);
        SystemTick();
        // Put
        BusWrite(OAM_DATA_REG, data);
        if (system->dmc_dma_triggered && system->oam_dma_bytes_remaining > 2)
        {
            SystemTick();
            ApuDmcDmaUpdate(system->apu);
            system->dmc_dma_triggered = false;
        }
        else if (system->dmc_dma_triggered && system->oam_dma_bytes_remaining == 2)
        {
            single_dma_cycle = true;
        }
        --system->oam_dma_bytes_remaining;
    }

    if (system->dmc_dma_triggered && !single_dma_cycle)
    {
        // DMC DMA dummy cycle
        SystemTick();
        BusRead(addr);

        // DMC Dma Alignment cycle if needed
        if (system->cpu->cycles & 1)
        {
            SystemTick();
            BusRead(addr);
        }

        SystemTick();
        ApuDmcDmaUpdate(system->apu);
        system->dmc_dma_triggered = false;
    }
    else if (system->dmc_dma_triggered && single_dma_cycle)
    {
        SystemTick();
        ApuDmcDmaUpdate(system->apu);
        system->dmc_dma_triggered = false;
    }
}

static void SystemStartDmcDma(System *system, const uint16_t addr)
{
    // Add cpu halt cycle
    SystemTick();
    BusRead(addr);

    if (ExplicitAbortDmcDma(system))
    {
        system->dmc_dma_triggered = false;
        return;
    }

    // Add cpu dummy cycle
    SystemTick();
    BusRead(addr);

    // Alignment cycle if needed
    if (system->cpu->cycles & 1)
    {
        SystemTick();
        BusRead(addr);
    }

    SystemTick();
    ApuDmcDmaUpdate(system->apu);
    system->dmc_dma_triggered = false;
}

void SystemSignalDmcDma(void)
{
    system_ptr->dma_pending = true;
    system_ptr->dmc_dma_triggered = true;
}

static uint8_t SystemMemMappedRead(System *system, MemOperation op, const uint16_t addr)
{
    switch (op)
    {
        case MEM_PRG_READ:
            return MapperReadPrgRom(system->cart, addr);
        case MEM_REG_READ:
            return MapperReadReg(system->cart, addr);
        case MEM_SWRAM_READ:
            return SWramRead(system, addr);
        default:
            return 0;
    }
}

static void SystemMemMappedWrite(System *system, MemOperation op, const uint16_t addr, uint8_t data)
{
    switch (op)
    {
        case MEM_SWRAM_WRITE:
            SWramWrite(system, addr, data);
            break;
        case MEM_REG_WRITE:
            MapperWriteReg(system->cart, addr, data);
            break;
        default:
            break;
    }
}

void SystemAddMemMapRead(const uint16_t start_addr, const uint16_t end_addr, MemOperation op)
{
    System *system = system_ptr;
    MemMap *mem_map = &system->mem_map_r[system->mem_maps_r++];
    mem_map->start_addr = start_addr;
    mem_map->end_addr = end_addr;
    mem_map->op = op;
}

void SystemAddMemMapWrite(const uint16_t start_addr, const uint16_t end_addr, MemOperation op)
{
    System *system = system_ptr;
    MemMap *mem_map = &system->mem_map_w[system->mem_maps_w++];
    mem_map->start_addr = start_addr;
    mem_map->end_addr = end_addr;
    mem_map->op = op;
}

uint8_t SystemRead(const uint16_t addr)
{
    system_ptr->cpu_addr = addr;
    if (system_ptr->dma_pending)
    {
        if (system_ptr->dmc_dma_triggered && !system_ptr->oam_dma_triggered)
        {
            SystemStartDmcDma(system_ptr, addr);
        }
        else if (system_ptr->oam_dma_triggered)
        {
            SystemStartOamDma(system_ptr, system_ptr->bus_data, addr);
        }
        system_ptr->dma_pending = false;
    }

    SystemTick();
    return BusRead(addr);
}

uint8_t BusRead(const uint16_t addr)
{
    ++system_ptr->cpu->cycles;
    // Extract A15, A14, and A13
    uint8_t region = (addr >> 13) & 0x7;

    if (ApuRegsActivated(system_ptr))
    {
        uint8_t val = addr & 0x1F;
        if (val == 0x15)
        {
            return ApuReadStatus(system_ptr->apu, system_ptr->bus_data);
        }
        else if (val == 0x16)
        {
            // Clear bits 0–4
            system_ptr->bus_data &= 0xE0;
            // Update bits 0–4
            system_ptr->bus_data |= (ReadJoyPadReg(system_ptr->joy_pad1) & 0x1F);
        }
        else if (val == 0x17)
        {
            // Clear bits 0–4
            system_ptr->bus_data &= 0xE0;
            // Update bits 0–4
            system_ptr->bus_data |= (ReadJoyPadReg(system_ptr->joy_pad2) & 0x1F);
        }
        else if (addr >= 0x4000 && addr < 0x6000)
        {
            DEBUG_LOG("Open bus read! addr: 0x%04X bus: %X\n", addr, system_ptr->bus_data);
        }
    }

    switch (region)
    {
        // $0000 - $1FFF
        case 0x0:
            system_ptr->bus_data = SystemRamRead(system_ptr, addr);
            break;

        // $2000 - $3FFF
        case 0x1:
            system_ptr->bus_data = ReadPPURegister(system_ptr->ppu, addr);
            break;
        default:
        {
            for (int i = 0; i < system_ptr->mem_maps_r; i++)
            {
                MemMap *mem_map = &system_ptr->mem_map_r[i];
            
                if (addr >= mem_map->start_addr && addr <= mem_map->end_addr)
                {
                    system_ptr->bus_data = SystemMemMappedRead(system_ptr, mem_map->op, addr);
                }
            }
            break;
        }
    }

    // Finally read the data from the bus
    return system_ptr->bus_data;
}

void SystemWrite(const uint16_t addr, const uint8_t data)
{
    system_ptr->cpu_addr = addr;
    SystemTick();
    BusWrite(addr, data);
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
                system_ptr->oam_dma_triggered = true;
                system_ptr->dma_pending = true;
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
            break;
        }
    }

    for (int i = 0; i < system_ptr->mem_maps_w; i++)
    {
        MemMap *mem_map = &system_ptr->mem_map_w[i];

        if (addr >= mem_map->start_addr && addr <= mem_map->end_addr)
        {
            SystemMemMappedWrite(system_ptr, mem_map->op, addr, data);
        }
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

uint8_t SystemGetPpuA9(void)
{
    return system_ptr->ppu->v.raw_bits.bit9;
}

// TODO: The Ppu struct should have a ptr to the chr rom / chr ram
// The PPU only exposes the io regs on the main bus, it has its own bus
uint8_t PpuBusReadChrRom(const uint16_t addr)
{
    return MapperReadChrRom(system_ptr->cart, addr);
}

void PpuBusWriteChrRam(const uint16_t addr, const uint8_t data)
{
    Cart *cart = system_ptr->cart;
    if (!cart->chr_rom.ram)
        return;

    MapperWriteChrRam(cart, addr, data);
}

void PpuClockMMC3(void)
{
    if (system_ptr->cart->mapper_num != MAPPER_MMC3)
        return;

    Mmc3ClockIrqCounter(system_ptr->cart);
}

void PpuClockMMC5(const uint16_t addr)
{
    if (system_ptr->cart->mapper_num != MAPPER_MMC5)
        return;

    Mmc5ClockIrqCounter(system_ptr->cart, addr);
}

void SystemUpdateState(System *system, SystemState state)
{
    if (system->state == PAUSED && state == PAUSED)
        system->state ^= PAUSED;
    else
    {
        system->state = state;
    }
}

void SystemRun(System *system, bool debug_info)
{
    if (system->state == PAUSED)
        return;

    system->ppu->frame_finished = false;

    do {
        CPU_Update(system->cpu, debug_info);
    } while (!system->ppu->frame_finished && system->state != STEP_INSTR);

    if ((system->state == STEP_FRAME && system->ppu->frame_finished) || system->state == STEP_INSTR)
    {
        system->state = PAUSED;
    }
}

bool SystemPollAllIrqs(void)
{
    return PollApuIrqs(system_ptr->apu) || PollMapperIrq();
}

// The PPU pulls /NMI low if and only if both vblank_flag and NMI_output are true.
static uint8_t SystemReadNmiPin(System *system)
{
    return ~(system->ppu->ctrl.vblank_nmi & system->ppu->status.vblank);
}

static void SystemPollNmi(System *system)
{
    uint8_t current_nmi_pin = SystemReadNmiPin(system);
    if (~current_nmi_pin & system->cpu->nmi_pin)
    {
        system->cpu->nmi_pending = true;
        //printf("NMI falling edge at frame: %ld ppu cycle: %d scanline:%d\n",
        //        system_ptr->ppu->frames, system_ptr->ppu->cycle_counter, system_ptr->ppu->scanline);
    }
    system->cpu->nmi_pin = current_nmi_pin;
}

void SystemTick(void)
{
    APU_Tick(system_ptr->apu);

    PPU_Tick(system_ptr->ppu);
    PpuUpdateRenderingState(system_ptr->ppu);
    SystemPollNmi(system_ptr);
    PPU_Tick(system_ptr->ppu);
    PPU_Tick(system_ptr->ppu);
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
    MapperReset(system->cart);
    APU_Reset(system->apu);
    PPU_Reset(system->ppu);
    CPU_Reset(system->cpu);
}

void SystemShutdown(System *system)
{
    CartSaveSram(system->cart);
}
