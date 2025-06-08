#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "arena.h"
#include "cart.h"
#include "ppu.h"
#include "cpu.h"
#include "mapper.h"

#include "utils.h"

Mmc1 mmc1;
Mmc3 mmc3;
UxRom ux_rom;
AxRom ax_rom;
CnRom cn_rom;

static uint8_t NromReadPrgRom(Cart *cart, uint16_t addr)
{
    return cart->prg_rom.data[addr & cart->prg_rom.mask];
}

static inline int GetNumPrgRomBanks(const uint32_t prg_rom_size, const uint16_t bank_size)
{
    return prg_rom_size / bank_size;
}

static inline uint32_t GetBankAddr(const int bank, const uint16_t addr, const uint16_t bank_size, const uint32_t prg_rom_mask)
{
    return ((bank * bank_size) + (addr & (bank_size - 1))) & prg_rom_mask;
}

static uint8_t Read16kBank(Cart *cart, int bank, const uint16_t addr)
{
    switch ((addr >> 13) & 0x3)
    {
        case 0:
        case 1:
        {
            uint32_t final_addr = GetBankAddr(bank, addr, 0x4000, cart->prg_rom.mask);
            //printf("Reading from addr: 0x%X\n", final_addr);
            return cart->prg_rom.data[final_addr];
        }
        case 2:
        case 3:
        {
            uint32_t final_addr = GetBankAddr(cart->prg_rom.num_banks - 1, addr, 0x4000, cart->prg_rom.mask); 
            //printf("3, Reading from addr: 0x%X\n", final_addr);
            return cart->prg_rom.data[final_addr];
        }
    }

    return 0;
}

static uint8_t Mmc3ReadPrgRom(Cart *cart, const uint16_t addr)
{
    if (mmc3.bank_sel.prg_rom_bank_mode)
    {
        switch ((addr >> 13) & 0x3)
        {
            case 0:
            {
                // Read from second to last bank
                uint32_t final_addr = GetBankAddr(cart->prg_rom.num_banks - 2, addr, 0x2000, cart->prg_rom.mask);
                //printf("0i, Reading from addr: 0x%X\n", final_addr);
                return cart->prg_rom.data[final_addr];
            }
            case 1:
            {
                uint32_t final_addr = GetBankAddr(mmc3.regs[7], addr, 0x2000, cart->prg_rom.mask);
                //printf("1i, Reading from addr: 0x%X\n", final_addr);
                return cart->prg_rom.data[final_addr];
            }

            case 2:
            {
                uint32_t final_addr = GetBankAddr(mmc3.regs[6], addr, 0x2000, cart->prg_rom.mask);
                //printf("2i, Reading from addr: 0x%X\n", final_addr);
                return cart->prg_rom.data[final_addr];
            }
            case 3:
            {
                // Read from the last bank
                uint32_t final_addr = GetBankAddr(cart->prg_rom.num_banks - 1, addr, 0x2000, cart->prg_rom.mask);
                //printf("3i, Reading from addr: 0x%X\n", final_addr);
                return cart->prg_rom.data[final_addr];
            }
        }
    }

    switch ((addr >> 13) & 0x3)
    {
        case 0:
        {
            uint32_t final_addr = GetBankAddr(mmc3.regs[6], addr, 0x2000, cart->prg_rom.mask);
            //printf("0, Reading from addr: 0x%X\n", final_addr);
            return cart->prg_rom.data[final_addr];
        }
        case 1:
        {
            uint32_t final_addr = GetBankAddr(mmc3.regs[7], addr, 0x2000, cart->prg_rom.mask);
            //printf("1, Reading from addr: 0x%X\n", final_addr);
            return cart->prg_rom.data[final_addr];
        }

        case 2:
        {
            uint32_t final_addr = GetBankAddr(cart->prg_rom.num_banks - 2, addr, 0x2000, cart->prg_rom.mask);
            //printf("2, Reading from addr: 0x%X\n", final_addr);
            return cart->prg_rom.data[final_addr];
        }
        case 3:
        {
            uint32_t final_addr = GetBankAddr(cart->prg_rom.num_banks - 1, addr, 0x2000, cart->prg_rom.mask);
            //printf("3, Reading from addr: 0x%X\n", final_addr);
            return cart->prg_rom.data[final_addr];
        }
    }

    return 0;
}

static uint8_t Mmc1ReadPrgRom(Cart *cart, const uint16_t addr)
{
    //Mmc1 *mmc1 = &cart->mmc1;

    return Read16kBank(cart, mmc1.prg_bank.mmc1a.select, addr);
}

static uint8_t UxRomReadPrgRom(Cart *cart, const uint16_t addr)
{
    //UxRom *ux_rom = &cart->ux_rom;

    return Read16kBank(cart, ux_rom.bank & 0x7, addr);
}

static uint8_t AxRomReadPrgRom(Cart *cart, const uint16_t addr)
{
    uint32_t final_addr = GetBankAddr(ax_rom.reg.bank, addr, 0x8000, cart->prg_rom.mask);
    return cart->prg_rom.data[final_addr];
}

static uint8_t NromReadChrRom(Cart *cart, const uint16_t addr)
{
    return cart->chr_rom.data[addr & (cart->chr_rom.size - 1)];
}

static uint8_t Mmc1ReadChrRom(Cart *cart, const uint16_t addr)
{
    uint16_t bank_size = mmc1.control.chr_rom_bank_mode ? 0x1000 : 0x2000;
    
    // Select chr bank (5-bit value, max 32 banks)
    uint32_t bank;
    if (mmc1.control.chr_rom_bank_mode)  
    {
        // 4 KB mode: separate banks for $0000-$0FFF and $1000-$1FFF
        if (addr < 0x1000)
            bank = mmc1.chr_bank0;
        else
            bank = mmc1.chr_bank1;
    }
    else
    {
        // 8 KB mode: only chr_bank0 is used, but low bit is ignored?
        bank = (mmc1.chr_bank0 & 0x1E);
    }

    // If CHR is RAM and only 8 KB, the bank number is ANDed with 1
    if (cart->chr_rom.is_ram && cart->chr_rom.size == CART_RAM_SIZE)
        bank &= 1;

    // Compute CHR-ROM address
    uint32_t final_addr = (bank * 0x1000) + (addr & (bank_size - 1));

    return cart->chr_rom.data[final_addr];
}

static uint8_t Mmc3ReadChrRom(Cart *cart, const uint16_t addr)
{
    const uint32_t effective_addr = addr ^ (mmc3.bank_sel.chr_a12_invert * 0x1000);

    // Branchless
    uint32_t region = effective_addr >> 12;
    uint32_t shift = 10 + (region ^ 1);
    uint32_t offset = region * 2;
    uint32_t bank_size = 1 << shift;
    
    uint32_t index = (effective_addr >> shift) - offset;
    uint32_t bank_base = mmc3.regs[index] << shift;
    uint32_t final_addr = bank_base | (effective_addr & (bank_size - 1));

    return cart->chr_rom.data[final_addr];
}

static uint8_t CnromReadChrRom(Cart *cart, const uint16_t addr)
{
    return cart->chr_rom.data[(cn_rom.reg.chr_bank * 0x2000) + (addr & 0x1FFF)];
}

static const int mmc1_mirror_map[4] =
{
    NAMETABLE_FOUR_SCREEN,
    NAMETABLE_SINGLE_SCREEN,
    NAMETABLE_VERTICAL,
    NAMETABLE_HORIZONTAL
};

static void Mmc1RegWrite(Cart *cart, const uint16_t addr, const uint8_t data)
{
    //Mmc1 *mmc1 = &cart->mmc1;

    if ((data >> 7) & 1)
    {
        DEBUG_LOG("Mmc1 reset request from addr: 0x%04X\n", addr);

        // Mmc1 reset
        mmc1.shift.raw = 0x10;
        mmc1.shift_count = 0;
        // Set last bank at $C000 and switch 16 KB bank at $8000
        mmc1.control.prg_rom_bank_mode = 0x3;
        return;
    }

    mmc1.shift.raw >>= 1;
    mmc1.shift.bit4 = data & 1;
    mmc1.shift_count++;

    if (mmc1.shift_count != 5)
        return;

    const uint8_t reg = mmc1.shift.raw;
    switch ((addr >> 13) & 0x3)
    {
        case 0:
            mmc1.control.raw = reg;
            PpuSetMirroring(mmc1_mirror_map[mmc1.control.name_table_setup], 0);
            //printf("Set nametable mode to: %d\n", mmc1->control.name_table_setup);
            //printf("Set prg rom bank mode to: %d\n", mmc1->control.prg_rom_bank_mode);
            DEBUG_LOG("Set chr bank mode to %d\n", mmc1.control.chr_rom_bank_mode);
            break;
        case 1:
            mmc1.chr_bank0 = reg;
            DEBUG_LOG("Set chr rom bank0 index to %d\n", mmc1.chr_bank0);
            break;
        case 2:
            mmc1.chr_bank1 = reg;
            DEBUG_LOG("Set chr rom bank1 index to %d\n", mmc1.chr_bank1);
            break;
        case 3:
            mmc1.prg_bank.raw = reg;
            DEBUG_LOG("Set prg rom bank index to %d\n", mmc1.prg_bank.mmc1a.select);
            //DEBUG_LOG("Bypass 16k logic? %d\n", mmc1->prg_bank.mmc1a.bypass_16k_logic);
            break;
    }
    mmc1.shift.raw = 0x10;
    mmc1.shift_count = 0;
}

static void Mmc3RegWrite(Cart *cart, const uint16_t addr, const uint8_t data)
{
    // Odd address
    if (addr & 1)
    {
        switch ((addr >> 13) & 0x3)
        {
            // Bank data ($8001-$9FFF, odd)
            case 0:
            {
                uint8_t effective_data = data;
                if (mmc3.bank_sel.reg == 0x6 || mmc3.bank_sel.reg == 0x7)
                {
                    effective_data = data & 0x3F;
                }
                else if (mmc3.bank_sel.reg == 0x0 || mmc3.bank_sel.reg == 0x1)
                {
                    effective_data = data >> 1;
                }

                mmc3.regs[mmc3.bank_sel.reg] = effective_data;
            
                //printf("Set MMC3 reg %d bank value 0x%X\n", mmc3.bank_sel.reg, effective_data);
                break;
            }
            // PRG RAM protect ($A001-$BFFF, odd)
            case 1:
                mmc3.prg_ram_protect.raw = data;
                break;
            // IRQ reload ($C001-$DFFF, odd)
            case 2:
                mmc3.irq_counter = 0;
                mmc3.irq_reload = true;
                break;
            // IRQ enable ($E001-$FFFF, odd)
            case 3:
                mmc3.irq_enable = true;
                break;
            default:
                printf("Unknown MMC3 Write from odd addr: 0x%X data: 0x%X\n", addr, data);
                break;
        }
        return;
    }

    // Even address
    switch ((addr >> 13) & 0x3)
    {
        // Bank select ($8000-$9FFE, even)
        case 0:
            mmc3.bank_sel.raw = data;
            //printf("MMC3 Set bank selection: reg %d, prg_rom_bank_mode:%d, chr_a12_invert: %d\n",
            //        mmc3.bank_sel.reg, mmc3.bank_sel.prg_rom_bank_mode, mmc3.bank_sel.chr_a12_invert);
            break;
        // Nametable arrangement ($A000-$BFFE, even)
        case 1:
            mmc3.name_table_arrgmnt = data & 1;
            PpuSetMirroring(mmc3.name_table_arrgmnt ^ 1, 0);
            //printf("Set MMC3 nametable mirroring mode: %d\n", !cart->mmc3.name_table_setup);
            break;
        // IRQ latch ($C000-$DFFE, even)
        case 2:
            mmc3.irq_latch = data;
            //printf("Set MMC3 irq_latch: %d\n", data);
            break;
        // IRQ disable ($E000-$FFFE, even)
        case 3:
            mmc3.irq_enable = false;
            mmc3.irq_pending = false;
            //printf("Set MMC3 interrupts off: 0x%X\n", data);
            break;
        default:
            printf("Unknown MMC3 write from even addr: 0x%X data: 0x%X\n", addr, data);
            break;
    }
}

static void UxRomRegWrite(Cart *cart, const uint16_t addr, const uint8_t data)
{
    //UxRom *ux_rom = &cart->ux_rom;

    ux_rom.bank = data;

    DEBUG_LOG("Set prg rom bank index to %d\n", data & 0x7);
}

static void AxRomRegWrite(Cart *cart, const uint16_t addr, const uint8_t data)
{
    ax_rom.reg.raw = data;
    PpuSetMirroring(2, ax_rom.reg.page);
}

static void CnRomRegWrite(Cart *cart, const uint16_t addr, const uint8_t data)
{
    cn_rom.reg.raw = data;
}

uint8_t MapperReadPrgRom(Cart *cart, const uint16_t addr)
{
    return cart->ReadPrgFn(cart, addr);
}

uint8_t MapperReadChrRom(Cart *cart, const uint16_t addr)
{
    return cart->ReadChrFn(cart, addr);
}

void MapperWrite(Cart *cart, const uint16_t addr, uint8_t data)
{
    if (cart->mapper_num != 0)
        cart->WriteFn(cart, addr, data);
}

bool Mmc3ClockIrqCounter(Cart *cart)
{
    if (!mmc3.irq_counter || mmc3.irq_reload)
    {
        mmc3.irq_counter = mmc3.irq_latch;
    }
    else
    {
        mmc3.irq_counter--;
    }

    if (!mmc3.irq_counter && mmc3.irq_enable)
    {
        mmc3.irq_pending = true;
    }

    if (mmc3.irq_reload)
    {
        mmc3.irq_reload = false;
    }

    return mmc3.irq_pending;
}

bool PollMapperIrq(void)
{
    return mmc3.irq_pending;
}

void MapperInit(Cart *cart)
{
    switch (cart->mapper_num)
    {
        case 0:
            cart->ReadPrgFn = NromReadPrgRom;
            cart->ReadChrFn = NromReadChrRom;
            break;
        case 1:
            mmc1.control.prg_rom_bank_mode = 3;
            cart->ReadPrgFn = Mmc1ReadPrgRom;
            cart->ReadChrFn = Mmc1ReadChrRom;
            cart->WriteFn = Mmc1RegWrite;
            cart->prg_rom.num_banks = GetNumPrgRomBanks(cart->prg_rom.size, 0x4000);
            break;
        case 2:
            cart->ReadPrgFn = UxRomReadPrgRom;
            cart->ReadChrFn = NromReadChrRom;
            cart->WriteFn = UxRomRegWrite;
            cart->prg_rom.num_banks = GetNumPrgRomBanks(cart->prg_rom.size, 0x4000);
            break;
        case 3:
            cart->ReadPrgFn = NromReadPrgRom;
            cart->ReadChrFn = CnromReadChrRom;
            cart->WriteFn = CnRomRegWrite;
            break;
        case 4:
            cart->ReadPrgFn = Mmc3ReadPrgRom;
            cart->ReadChrFn = Mmc3ReadChrRom;
            cart->WriteFn = Mmc3RegWrite;
            cart->prg_rom.num_banks = GetNumPrgRomBanks(cart->prg_rom.size, 0x2000);
            break;
        case 7:
            cart->ReadPrgFn = AxRomReadPrgRom;
            cart->ReadChrFn = NromReadChrRom;
            cart->WriteFn = AxRomRegWrite;
            cart->prg_rom.num_banks = GetNumPrgRomBanks(cart->prg_rom.size, 0x8000);
            break;
        default:
            printf("Bad Mapper type!: %d\n", cart->mapper_num);
            break;
    }
}
