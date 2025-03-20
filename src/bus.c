#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "cpu.h"
#include "ppu.h"
#include "loader.h"
#include "mapper.h"
#include "arena.h"
#include "bus.h"

//static Bus bus;
static Bus ppu_bus;

uint8_t BusRead(Bus *bus, const uint16_t addr)
{
    // Extract A15, A14, and A13
    uint8_t region = (addr >> 13) & 0x7;

    switch (region)
    {
        case 0x0:  // $0000 - $1FFF
            return bus->sys_ram[addr & 0x7FF];

        case 0x1:  // $2000 - $3FFF
            return ReadPPURegister(addr & 7);

        case 0x2:  // $4000 - $5FFF
            if (addr < 0x4018)
            {
                return g_apu_regs[(addr - APU_IO_REGS_START_ADDR) & 0x17];
                //return apu_io_read(addr);  // APU & I/O
            }
            else
            {
                printf("Trying to read unknown (Mapper reg?) value at 0x%04X\n", addr);
                break;
            }


        case 0x3:  // $6000 - $7FFF
        {
            return bus->cart->sram[addr & 0x1FFF];
        }
    
        case 0x4:  // $8000 - $9FFF
        case 0x5:  // $A000 - $BFFF
        case 0x6:  // $C000 - $DFFF
        case 0x7:  // $E000 - $FFFF
        {
            return MapperRead(bus->cart, addr);
        }
    }

    return 0;  // open bus
}

void BusInit(Bus *bus, Cart *cart)
{
    //Bus *bus = malloc(sizeof(*bus));
    memset(bus, 0, sizeof(*bus));
    bus->sys_ram = malloc(0x800);
    bus->cart = cart;
}

uint8_t BusRead2(Bus *bus, const uint16_t addr)
{
    uint16_t ret = 0;
    // Extract A15, A14, and A13
    uint8_t region = (addr >> 13) & 0x7;

    switch (region)
    {
        case 0x0:  // $0000 - $1FFF
            return bus->sys_ram[addr & 0x7FF];

        case 0x1:  // $2000 - $3FFF
            return ReadPPURegister(addr);

        case 0x2:  // $4000 - $5FFF
            if (addr < 0x4018)
            {
                if (addr == 0x4016)
                {
                    return 0; //ReadJoyPadReg();
                }
                //printf("Trying to read APU/IO reg at 0x%04X\n", addr);
                //break;
                return g_apu_regs[addr & 0x17];
                //return apu_io_read(addr);  // APU & I/O
            }
            else
            {
                printf("Trying to read unknown (Mapper reg?) value at 0x%04X\n", addr);
                break;
            }

            case 0x3:  // $6000 - $7FFF
                return bus->cart->sram[addr & 0x1FFF];
        
            case 0x4:  // $8000 - $9FFF
            case 0x5:  // $A000 - $BFFF
            case 0x6:  // $C000 - $DFFF
            case 0x7:  // $E000 - $FFFF
            {
                //return bus->cart->prg_rom.data[addr % g_prg_rom_size];
                return MapperRead(bus->cart, addr);
            }
    }

    return 0;  // open bus
}

/*
static uint8_t BusRead1(Bus *bus, BusType bus_type, const uint16_t addr)
{
    switch (bus_type) {
        case CPU_BUS:
            return BusReadCpu(bus, addr);
        case PPU_BUS:
            return BusReadCpu(bus, addr);
    }
}
*/