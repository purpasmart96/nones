#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "ppu.h"
#include "arena.h"
#include "loader.h"
#include "bus.h"
#include "mapper.h"
#include "utils.h"

typedef enum {
    NROM_INVALID,
    NROM_128,
    NROM_256
} NromType;

static uint8_t NromRead(PrgRom *prg_rom, uint16_t addr)
{
    return prg_rom->data[addr & (prg_rom->size - 1)];
}

static uint8_t Read16kBank(Cart *cart, int bank, const uint16_t addr)
{
    switch ((addr >> 13) & 0x3)
    {
        case 0:
        case 1:
        {
            uint16_t offset = addr - 0x8000;
            uint32_t bank_addr_end = (bank * 0x4000);
            uint32_t final_addr = bank_addr_end + offset;
            //printf("Reading from addr: 0x%X\n", final_addr);
            return cart->prg_rom.data[final_addr];
        }
        case 2:
        case 3:
        {
            uint16_t offset = 0x10000 - addr;
            return cart->prg_rom.data[cart->prg_rom.size - offset];
        }

    }

    return 0;
}

static uint8_t Mmc1Read(Cart *cart, const uint16_t addr)
{
    Mmc1 *mmc1 = &cart->mmc1;

    return Read16kBank(cart, mmc1->prg_bank.mmc1a.select, addr);
}

static uint8_t UxRomRead(Cart *cart, const uint16_t addr)
{
    UxRom *ux_rom = &cart->ux_rom;

    return Read16kBank(cart, ux_rom->bank & 0x7, addr);
}

static uint8_t Mmc1ReadChrRom(Cart *cart, uint16_t addr)
{
    uint16_t bank_size = cart->mmc1.control.chr_rom_bank_mode ? 0x1000 : 0x2000;
    
    // Select chr bank (5-bit value, max 32 banks)
    uint32_t bank;
    if (cart->mmc1.control.chr_rom_bank_mode)  
    {
        // 4 KB mode: separate banks for $0000-$0FFF and $1000-$1FFF
        if (addr < 0x1000)
            bank = cart->mmc1.chr_bank0;
        else
            bank = cart->mmc1.chr_bank1;
    }
    else
    {
        // 8 KB mode: only chr_bank0 is used, but low bit is ignored?
        bank = (cart->mmc1.chr_bank0 & 0x1E);
    }

    // If CHR is RAM and only 8 KB, the bank number is ANDed with 1
    if (cart->chr_rom.is_ram && cart->chr_rom.size == CART_RAM_SIZE)
        bank &= 1;

    // Compute CHR-ROM address
    uint32_t final_addr = (bank * 0x1000) + (addr & (bank_size - 1));

    return cart->chr_rom.data[final_addr];
}

static void MapperUpdatePPUMirroring(int nt_mirror_mode)
{
    switch (nt_mirror_mode) {
        case 0:
            NametableMirroringInit(4);
            break;
        case 1:
            NametableMirroringInit(3);
            break;
        case 2:
            NametableMirroringInit(NAMETABLE_VERTICAL);
            break;
        case 3:
            NametableMirroringInit(NAMETABLE_HORIZONTAL);
            break;
    }
}

static void Mmc1Write(Cart *cart, const uint16_t addr, const uint8_t data)
{
    Mmc1 *mmc1 = &cart->mmc1;

    if ((data >> 7) & 1)
    {
        printf("Mmc1 reset request from addr: 0x%04X\n", addr);

        // Mmc1 reset
        mmc1->shift.raw = 0x10;
        mmc1->shift_count = 0;
        // Set last bank at $C000 and switch 16 KB bank at $8000
        mmc1->control.prg_rom_bank_mode = 0x3;
        return;
    }

    mmc1->shift.raw >>= 1;
    mmc1->shift.bit4 = data & 1;
    mmc1->shift_count++;

    if (mmc1->shift_count == 5)
    {
        const uint8_t reg = mmc1->shift.raw;
        switch ((addr >> 13) & 0x3)
        {
            case 0:
                mmc1->control.raw = reg;
                MapperUpdatePPUMirroring(mmc1->control.name_table_setup);
                //printf("Set nametable mode to: %d\n", mmc1->control.name_table_setup);
                //printf("Set prg rom bank mode to: %d\n", mmc1->control.prg_rom_bank_mode);
                printf("Set chr bank mode to %d\n", mmc1->control.chr_rom_bank_mode);
                break;
            case 1:
                mmc1->chr_bank0 = reg;
                printf("Set chr rom bank0 index to %d\n", mmc1->chr_bank0);
                break;
            case 2:
                mmc1->chr_bank1 = reg;
                printf("Set chr rom bank1 index to %d\n", mmc1->chr_bank1);
                break;
            case 3:
                mmc1->prg_bank.raw = reg;
                //printf("Set prg rom bank index to %d\n", mmc1->prg_bank.mmc1a.select);
                break;
            default:
                break;
        
        }
        mmc1->shift.raw = 0x10;
        mmc1->shift_count = 0;
    }
}

static void UxRomWrite(Cart *cart, const uint16_t addr, const uint8_t data)
{
    UxRom *ux_rom = &cart->ux_rom;

    ux_rom->bank = data;

    printf("Set prg rom bank index to %d\n", data & 0x7);
}

uint8_t MapperReadPrgRom(Cart *cart, const uint16_t addr)
{
    switch (cart->mapper_type)
    {
        case 0:
            return NromRead(&cart->prg_rom, addr);
        case 1:
            return Mmc1Read(cart, addr);
        case 2:
            return UxRomRead(cart, addr);
        default:
            printf("Mapper %d is not implemented! (Read at addr 0x%04X)\n",
                    cart->mapper_type, addr);
    }

    return 0;
}

uint8_t MapperReadChrRom(Cart *cart, const uint16_t addr)
{
    switch (cart->mapper_type)
    {
        case 0:
            return cart->chr_rom.data[addr & (cart->chr_rom.size - 1)];
        case 1:
            return Mmc1ReadChrRom(cart, addr);
        case 2:
            return cart->chr_rom.data[addr & (cart->chr_rom.size - 1)];
    }

    return 0;
}

void MapperWrite(Cart *cart, const uint16_t addr, uint8_t data)
{
    switch (cart->mapper_type)
    {
        case 1:
            Mmc1Write(cart, addr, data);
            break;
        case 2:
            UxRomWrite(cart, addr, data);
            break;
        default:
            printf("Uknown mapper %d!\n", cart->mapper_type);
            break;
    }
}

// TODO
//static const Mapper mappers[] = {
//    {NULL, NULL },
//    {Mmc1Read, NULL }
//};

