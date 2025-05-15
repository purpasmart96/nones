#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "apu.h"
#include "bus.h"
#include "mapper.h"
#include "ppu.h"
#include "utils.h"

static Bus *bus_ptr = NULL;

Bus *BusCreate(Arena *arena)
{
    Bus *bus = ArenaPush(arena, sizeof(Bus));
    bus->cpu = ArenaPush(arena, sizeof(Cpu));
    bus->apu = ArenaPush(arena, sizeof(Apu));
    bus->ppu = ArenaPush(arena, sizeof(Ppu));
    bus->cart = ArenaPush(arena, sizeof(Cart));
    bus->joy_pad = ArenaPush(arena, sizeof(JoyPad));
    bus->sys_ram = ArenaPush(arena, CPU_RAM_SIZE);

    bus_ptr = bus;
    return bus;
}

int BusLoadCart(Arena *arena, Bus *bus, const char *path)
{
    return CartLoad(arena, bus->cart, path);
}

uint8_t BusRead(const uint16_t addr)
{
    // Extract A15, A14, and A13
    uint8_t region = (addr >> 13) & 0x7;

    switch (region)
    {
        // $0000 - $1FFF
        case 0x0:
            return bus_ptr->sys_ram[addr & 0x7FF];

        // $2000 - $3FFF
        case 0x1:
            return ReadPPURegister(bus_ptr->ppu, addr);

        // $4000 - $5FFF
        case 0x2:
            if (addr < 0x4018)
            {
                if (addr == 0x4016)
                {
                    //DEBUG_LOG("Requested Joypad reg 0x%04X\n", addr);
                    return ReadJoyPadReg(bus_ptr->joy_pad);
                }
                if (addr == 0x4017)
                {
                    //DEBUG_LOG("Requested Joypad reg 0x%04X\n", addr);
                    return 0;
                }
                return ReadAPURegister(bus_ptr->apu, addr);
            }
            else
            {
                printf("Trying to read unknown (Mapper reg?) value at 0x%04X\n", addr);
                break;
            }

        case 0x3:  // $6000 - $7FFF
            return bus_ptr->cart->ram[addr & 0x1FFF];
    
        case 0x4:  // $8000 - $9FFF
        case 0x5:  // $A000 - $BFFF
        case 0x6:  // $C000 - $DFFF
        case 0x7:  // $E000 - $FFFF
            return MapperReadPrgRom(bus_ptr->cart, addr);
    }

    // open bus
    return 0;
}

void BusWrite(const uint16_t addr, const uint8_t data)
{
    // Extract A15, A14, and A13
    uint8_t region = (addr >> 13) & 0x7;

    switch (region)
    {
        // $0000 - $1FFF
        case 0x0:
            // Internal RAM (mirrored)
            bus_ptr->sys_ram[addr & 0x7FF] = data;
            break;

        // $2000 - $3FFF
        case 0x1:
            WritePPURegister(bus_ptr->ppu, addr, data);
            break;

        // $4000 - $5FFF
        case 0x2:
            if (addr < 0x4018)
            {
                if (addr == 0x4014)
                {
                    DEBUG_LOG("Requested OAM DMA 0x%04X\n", addr);
                    bus_ptr->cpu->cycles += 513;
                    OAM_Dma(data);
                    break;
                }
                else if (addr == 0x4016)
                {
                    //DEBUG_LOG("Requested Joypad reg 0x%04X\n", addr);
                    WriteJoyPadReg(bus_ptr->joy_pad, data);
                    break;
                }
                //else if (addr == 0x4017)
                //{
                //    //DEBUG_LOG("Requested Joypad reg 0x%04X\n", addr);
                //    break;
                //}
                WriteAPURegister(bus_ptr->apu, addr, data);
                break;
            }
            else
            {
                printf("Trying to write unknown (Mapper reg?) value at 0x%04X\n", addr);
                break;  // $4018-$5FFF might be mapper-controlled
            }

        // SRAM / WRAM
        // $6000 - $7FFF
        case 0x3:
            bus_ptr->cart->ram[addr & 0x1FFF] = data;
            break;

        case 0x4:  // $8000 - $9FFF
        case 0x5:  // $A000 - $BFFF
        case 0x6:  // $C000 - $DFFF
        case 0x7:  // $E000 - $FFFF
            MapperWrite(bus_ptr->cart, addr, data);
            break;
    }
}

// TODO: Remove
uint8_t *BusGetPtr(const uint16_t addr)
{
    // Extract A15, A14, and A13
    uint8_t region = (addr >> 13) & 0x7;

    switch (region)
    {
        case 0x0:  // $0000 - $1FFF
        {
            return &bus_ptr->sys_ram[addr & 0x7FF];
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
            return &bus_ptr->cart->ram[addr & 0x1FFF];
        }
    
        case 0x4:  // $8000 - $9FFF
        case 0x5:  // $A000 - $BFFF
        case 0x6:  // $C000 - $DFFF
        case 0x7:  // $E000 - $FFFF
        {
            // Temp
            PrgRom *prg_rom = &bus_ptr->cart->prg_rom;
            return &prg_rom->data[addr & (prg_rom->size - 1)];
        }
    }

    return NULL; 
}


Cart *BusGetCart(void)
{
    return bus_ptr->cart;
}

Apu *BusGetApu(void)
{
    return bus_ptr->apu;
}

Ppu *BusGetPpu(void)
{
    return bus_ptr->ppu;
}

// TODO: The Ppu struct should have a ptr to the chr rom / chr ram
// The PPU only exposes the io regs on the main bus, it has its own bus
uint8_t PpuBusReadChrRom(const uint16_t addr)
{
    return MapperReadChrRom(bus_ptr->cart, addr);
}

void PpuBusWriteChrRam(const uint16_t addr, const uint8_t data)
{
    ChrRom *chr_rom = &bus_ptr->cart->chr_rom;
    chr_rom->data[addr & (chr_rom->size - 1)] = data;
}

void PpuClockMMC3(void)
{
    if (bus_ptr->cart->mapper_type == 4)
        Mmc3ClockIrqCounter(bus_ptr->cart);
}

void BusUpdate(uint64_t cycles)
{
    APU_Update(bus_ptr->apu, cycles);
    bus_ptr->cpu->irq_pending = bus_ptr->apu->status.dmc_interrupt | bus_ptr->apu->status.frame_interrupt | MapperIrqTriggered();
    PPU_Update(bus_ptr->ppu, cycles);
}

void ClearIrq(void)
{
    if (MapperIrqTriggered() && !bus_ptr->apu->status.dmc_interrupt && !bus_ptr->apu->status.frame_interrupt)
    {
        MapperIrqClear();
    }
}

void BusAddCpuCycles(uint32_t cycles)
{
    bus_ptr->cpu->cycles += cycles;
}
