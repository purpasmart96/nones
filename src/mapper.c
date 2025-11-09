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
#include "system.h"

#include "utils.h"

Mmc1 mmc1;
Mmc3 mmc3;
Mmc5 mmc5;
UxRom ux_rom;
AxRom ax_rom;
CnRom cn_rom;
ColorDreams color_dreams;
Ninja ninja;
BnRom bn_rom;
Nanjing nanjing;

static const uint16_t mmc1_chr_bank_sizes[2] = 
{
    0x2000, 0x1000
};

static uint8_t NromReadPrgRom(Cart *cart, uint16_t addr)
{
    return cart->prg_rom.data[addr & cart->prg_rom.mask];
}

static void ChrWriteGeneric(Cart *cart, const uint16_t addr, const uint8_t data)
{
    cart->chr_rom.data[addr & cart->chr_rom.mask] = data;
}

static inline int GetNumPrgRomBanks(const uint32_t prg_rom_size, const uint16_t bank_size)
{
    return prg_rom_size / bank_size;
}

static inline uint32_t GetPrgBankAddr(const int bank, const uint16_t addr, const uint16_t bank_size, const uint32_t prg_rom_mask)
{
    return ((bank * bank_size) + (addr & (bank_size - 1))) & prg_rom_mask;
}

// Prg bank mode 0 & 1: switch 32 KB at $8000, ignoring low bit of bank number;
static uint8_t Mmc1PrgReadMode01(Cart *cart, int bank, const uint16_t addr)
{
    uint32_t final_addr = GetPrgBankAddr(bank, addr, PRG_BANK_SIZE_32KIB, cart->prg_rom.mask);
    //printf("0, 1, Reading from addr: 0x%X\n", final_addr);
    return cart->prg_rom.data[final_addr];
}

// Prg bank mode 2: fix first bank at $8000 and switch 16 KB bank at $C000;
static uint8_t Mmc1PrgReadMode2(Cart *cart, int bank, const uint16_t addr)
{
    switch ((addr >> 13) & 0x3)
    {
        case 0:
        case 1:
        {
            uint32_t final_addr = GetPrgBankAddr(0, addr, PRG_BANK_SIZE_16KIB, cart->prg_rom.mask);
            //printf("Mmc1 mode 2: 0, 1, Reading from addr: 0x%X\n", final_addr);
            return cart->prg_rom.data[final_addr];
        }
        case 2:
        case 3:
        {
            uint32_t final_addr = GetPrgBankAddr(bank, addr, PRG_BANK_SIZE_16KIB, cart->prg_rom.mask);
            //printf("Mmc1 mode 2: 2, 3, Reading from addr: 0x%X\n", final_addr);
            return cart->prg_rom.data[final_addr];
        }
    }

    return 0;
}

// Prg bank mode 3: fix last bank at $C000 and switch 16 KB bank at $8000);
static uint8_t Mmc1PrgReadMode3(Cart *cart, int bank, const uint16_t addr)
{
    switch ((addr >> 13) & 0x3)
    {
        case 0:
        case 1:
        {
            uint32_t final_addr = GetPrgBankAddr(bank, addr, PRG_BANK_SIZE_16KIB, cart->prg_rom.mask);
            //printf("0, 1, Reading from addr: 0x%X\n", final_addr);
            return cart->prg_rom.data[final_addr];
        }
        case 2:
        case 3:
        {
            uint32_t final_addr = GetPrgBankAddr(cart->prg_rom.num_banks - 1, addr, PRG_BANK_SIZE_16KIB, cart->prg_rom.mask); 
            //printf("2, 3, Reading from addr: 0x%X\n", final_addr);
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
                uint32_t final_addr = GetPrgBankAddr(cart->prg_rom.num_banks - 2, addr, PRG_BANK_SIZE_8KIB, cart->prg_rom.mask);
                //printf("0i, Reading from addr: 0x%X\n", final_addr);
                return cart->prg_rom.data[final_addr];
            }
            case 1:
            {
                uint32_t final_addr = GetPrgBankAddr(mmc3.regs[7], addr, PRG_BANK_SIZE_8KIB, cart->prg_rom.mask);
                //printf("1i, Reading from addr: 0x%X\n", final_addr);
                return cart->prg_rom.data[final_addr];
            }

            case 2:
            {
                uint32_t final_addr = GetPrgBankAddr(mmc3.regs[6], addr, PRG_BANK_SIZE_8KIB, cart->prg_rom.mask);
                //printf("2i, Reading from addr: 0x%X\n", final_addr);
                return cart->prg_rom.data[final_addr];
            }
            case 3:
            {
                // Read from the last bank
                uint32_t final_addr = GetPrgBankAddr(cart->prg_rom.num_banks - 1, addr, PRG_BANK_SIZE_8KIB, cart->prg_rom.mask);
                //printf("3i, Reading from addr: 0x%X\n", final_addr);
                return cart->prg_rom.data[final_addr];
            }
        }
    }

    switch ((addr >> 13) & 0x3)
    {
        case 0:
        {
            uint32_t final_addr = GetPrgBankAddr(mmc3.regs[6], addr, PRG_BANK_SIZE_8KIB, cart->prg_rom.mask);
            //printf("0, Reading from addr: 0x%X\n", final_addr);
            return cart->prg_rom.data[final_addr];
        }
        case 1:
        {
            uint32_t final_addr = GetPrgBankAddr(mmc3.regs[7], addr, PRG_BANK_SIZE_8KIB, cart->prg_rom.mask);
            //printf("1, Reading from addr: 0x%X\n", final_addr);
            return cart->prg_rom.data[final_addr];
        }

        case 2:
        {
            uint32_t final_addr = GetPrgBankAddr(cart->prg_rom.num_banks - 2, addr, PRG_BANK_SIZE_8KIB, cart->prg_rom.mask);
            //printf("2, Reading from addr: 0x%X\n", final_addr);
            return cart->prg_rom.data[final_addr];
        }
        case 3:
        {
            uint32_t final_addr = GetPrgBankAddr(cart->prg_rom.num_banks - 1, addr, PRG_BANK_SIZE_8KIB, cart->prg_rom.mask);
            //printf("3, Reading from addr: 0x%X\n", final_addr);
            return cart->prg_rom.data[final_addr];
        }
    }

    return 0;
}

// PRG mode 0
// CPU $6000-$7FFF: 8 KB switchable PRG RAM bank (Ignored here)
// CPU $8000-$FFFF: 32 KB switchable PRG ROM bank
static uint8_t Mmc5PrgReadMode0(Cart *cart, const uint16_t addr)
{
    const int reg_index = 0;
    //printf("Mmc5 mode 0: Reading from addr: 0x%X\n", addr);
    uint32_t final_addr = GetPrgBankAddr(mmc5.prg_bank[reg_index].raw >> 1, addr, PRG_BANK_SIZE_32KIB, cart->prg_rom.mask);
    return cart->prg_rom.data[final_addr];
}

// PRG mode 2
// CPU $6000-$7FFF: 8 KB switchable PRG RAM bank (Ignored here)
// CPU $8000-$BFFF: 16 KB switchable PRG ROM/RAM bank
// CPU $C000-$DFFF: 8 KB switchable PRG ROM/RAM bank
// CPU $E000-$FFFF: 8 KB switchable PRG ROM bank
static uint8_t Mmc5PrgReadMode2(Cart *cart, const uint16_t addr)
{
    switch ((addr >> 13) & 0x3)
    {
        case 0:
        case 1:
        {
            uint32_t final_addr = GetPrgBankAddr(mmc5.prg_bank[2].raw >> 1, addr, PRG_BANK_SIZE_16KIB, cart->prg_rom.mask);
            //printf("Mmc5 mode 2: 0, 1, Reading from addr: 0x%X\n", final_addr);
            return cart->prg_rom.data[final_addr];
        }
        case 2:
        {
            uint32_t final_addr = GetPrgBankAddr(mmc5.prg_bank[3].raw, addr, PRG_BANK_SIZE_8KIB, cart->prg_rom.mask);
            //printf("Mmc5 mode 2: 2, 3, Reading from addr: 0x%X\n", final_addr);
            return cart->prg_rom.data[final_addr];
        }
        case 3:
        {
            uint32_t final_addr = GetPrgBankAddr(mmc5.prg_bank[4].raw, addr, PRG_BANK_SIZE_8KIB, cart->prg_rom.mask);
            //printf("Mmc5 mode 2: 2, 3, Reading from addr: 0x%X\n", final_addr);
            return cart->prg_rom.data[final_addr];
        }
    }

    return 0;
}

// PRG mode 3
// CPU $6000-$7FFF: 8 KB switchable PRG RAM bank (Ignored here for now)
// CPU $8000-$9FFF: 8 KB switchable PRG ROM/RAM bank
// CPU $A000-$BFFF: 8 KB switchable PRG ROM/RAM bank
// CPU $C000-$DFFF: 8 KB switchable PRG ROM/RAM bank
// CPU $E000-$FFFF: 8 KB switchable PRG ROM bank
static uint8_t Mmc5PrgReadMode3(Cart *cart, const uint16_t addr)
{
    const int reg_index = 1 + ((addr >> 13) & 3);
    //printf("Mmc5 mode 3: Reading from addr: 0x%X\n", addr);
    //printf("Prg reg index: %d\n", reg_index);
    uint32_t final_addr = GetPrgBankAddr(mmc5.prg_bank[reg_index].raw, addr, PRG_BANK_SIZE_8KIB, cart->prg_rom.mask);
    return cart->prg_rom.data[final_addr];
}

static uint8_t Mmc5ReadPrgRom(Cart *cart, const uint16_t addr)
{
    switch (mmc5.prg_mode)
    {
        case 0:
            return Mmc5PrgReadMode0(cart, addr);
        case 2:
            return Mmc5PrgReadMode2(cart, addr);
        case 3:
            return Mmc5PrgReadMode3(cart, addr);
    }

    printf("MMC5 PRG MODE NOT IMPLEMNENTD: %d\n", mmc5.prg_mode);
    return 0;
}

static uint8_t Mmc1ReadPrgRom(Cart *cart, const uint16_t addr)
{
    // Should this be in BusRead instead?
    mmc1.consec_write = false;

    switch (mmc1.control.prg_rom_bank_mode)
    {
        case 0:
        case 1:
            return Mmc1PrgReadMode01(cart, mmc1.prg_bank.select >> 1, addr);
        case 2:
            return Mmc1PrgReadMode2(cart, mmc1.prg_bank.select, addr);
        case 3:
            return Mmc1PrgReadMode3(cart, mmc1.prg_bank.select, addr);
    }

    return 0;
}

static uint8_t UxRomReadPrgRom(Cart *cart, const uint16_t addr)
{
    // UxROM prg reads are just like mmc1's prg mode 3
    return Mmc1PrgReadMode3(cart, ux_rom.bank & 0x7, addr);
}

static uint8_t AxRomReadPrgRom(Cart *cart, const uint16_t addr)
{
    const uint32_t final_addr = GetPrgBankAddr(ax_rom.bank, addr, PRG_BANK_SIZE_32KIB, cart->prg_rom.mask);
    return cart->prg_rom.data[final_addr];
}

static uint8_t ColorDreamsReadPrgRom(Cart *cart, const uint16_t addr)
{
    const uint32_t final_addr = GetPrgBankAddr(color_dreams.prg_bank, addr, PRG_BANK_SIZE_32KIB, cart->prg_rom.mask);
    return cart->prg_rom.data[final_addr];
}

static uint8_t NinjaReadPrgRom(Cart *cart, const uint16_t addr)
{
    const uint32_t final_addr = GetPrgBankAddr(ninja.prg_bank, addr, PRG_BANK_SIZE_32KIB, cart->prg_rom.mask);
    return cart->prg_rom.data[final_addr];
}

static uint8_t BnRomReadPrgRom(Cart *cart, const uint16_t addr)
{
    const uint32_t final_addr = GetPrgBankAddr(bn_rom.bank, addr, PRG_BANK_SIZE_32KIB, cart->prg_rom.mask);
    return cart->prg_rom.data[final_addr];
}

static uint8_t NanjingReadPrgRom(Cart *cart, const uint16_t addr)
{
    const int bank = nanjing.prg_high_reg << 4 | nanjing.prg_low_reg.prg_bank_low;
    uint32_t final_addr = GetPrgBankAddr(bank , addr, PRG_BANK_SIZE_32KIB, cart->prg_rom.mask);
    return cart->prg_rom.data[final_addr];
}

static uint8_t NromReadChrRom(Cart *cart, const uint16_t addr)
{
    return cart->chr_rom.data[addr & cart->chr_rom.mask];
}

static uint8_t Mmc1ReadChrRom(Cart *cart, const uint16_t addr)
{
    uint32_t bank_size = mmc1_chr_bank_sizes[mmc1.control.chr_rom_bank_mode];

    // Select chr bank (5-bit value, max 32 banks)
    uint32_t bank = (addr < 0x1000 || !mmc1.control.chr_rom_bank_mode) ? mmc1.chr_bank0 : mmc1.chr_bank1;

    // Ignore low bit in 8 Kib mode
    bank >>= !mmc1.control.chr_rom_bank_mode;

    // If CHR is only 8 KiB, the bank number is ANDed with 1
    if (cart->chr_rom.size == 0x2000)
        bank &= 1;

    // Compute CHR-ROM address
    uint32_t final_addr = ((bank * bank_size) + (addr & (bank_size - 1)));

    return cart->chr_rom.data[final_addr & cart->chr_rom.mask];
}

static inline uint32_t GetMmc3ChrAddr(Cart *cart, const uint16_t addr)
{
    const uint32_t effective_addr = addr ^ (mmc3.bank_sel.chr_a12_invert * 0x1000);

    // Branch version
    //uint32_t final_addr = 0;
    //if (effective_addr < 0x1000)
    //{
    //    // Reg 0 or 1
    //    final_addr = ((mmc3.regs[effective_addr >> 11] * 0x800) + (effective_addr & 0x7FF));
    //}
    //else
    //{
    //    // Reg 2–5
    //    final_addr = ((mmc3.regs[(effective_addr >> 10) - 2] * 0x400) + (effective_addr & 0x3FF));
    //}

    // Branchless
    uint32_t region = effective_addr >> 12;
    uint32_t shift = 10 + (region ^ 1);
    uint32_t offset = region * 2;
    uint32_t bank_size = 1 << shift;

    uint32_t index = (effective_addr >> shift) - offset;
    uint32_t bank_base = mmc3.regs[index] << shift;
    uint32_t final_addr = bank_base | (effective_addr & (bank_size - 1));
    return final_addr & cart->chr_rom.mask;
}

static uint8_t Mmc3ReadChr(Cart *cart, const uint16_t addr)
{
    return cart->chr_rom.data[GetMmc3ChrAddr(cart, addr)];
}

static void Mmc3WriteChr(Cart *cart, const uint16_t addr, const uint8_t data)
{
    cart->chr_rom.data[GetMmc3ChrAddr(cart, addr)] = data;
}

static inline uint32_t Mmc5ChrReadMode3(Cart *cart, uint16_t addr)
{
    int reg_index = addr >> 10;

    if (mmc5.sprite_mode && mmc5.sub_mode && !mmc5.matches)
    {
        switch (reg_index)
        {
            case 0:
            case 4:
                reg_index = 8;
                break;
            case 1:
            case 5:
                reg_index = 9;
                break;
            case 2:
            case 6:
                reg_index = 10;
                break;
            case 3:
            case 7:
                reg_index = 11;
                break;
        }
    }

    return ((mmc5.chr_select[reg_index] * 0x400) + (addr & 0x3FF)) & cart->chr_rom.mask;
}

static inline int32_t GetMmc5ChrAddr(Cart *cart, const uint16_t addr)
{
    //if (mmc5.ext_ram_mode == 0x01)
    //{
    //    //return (((((mmc5.chr_high << 2) | (mmc5.ext_ram[addr & 0x3FF] & 0x1F))) + (addr & 0x1FFF))) & cart->chr_rom.mask;
    //}
    switch (mmc5.chr_mode)
    {
        case 0:
            return ((mmc5.chr_select[7] * 0x2000) + (addr & 0x1FFF)) & cart->chr_rom.mask;
        case 3:
            return Mmc5ChrReadMode3(cart, addr);
    }

    printf("Unimpl CHR mode %d\n", mmc5.chr_mode);
    return 0;
}

static uint8_t Mmc5ReadChr(Cart *cart, const uint16_t addr)
{
    return cart->chr_rom.data[GetMmc5ChrAddr(cart, addr)];
}

static void Mmc5WriteChr(Cart *cart, const uint16_t addr, const uint8_t data)
{
    cart->chr_rom.data[GetMmc5ChrAddr(cart, addr)] = data;
}

static uint8_t CnromReadChrRom(Cart *cart, const uint16_t addr)
{
    return cart->chr_rom.data[((cn_rom.chr_bank * 0x2000) + (addr & 0x1FFF)) & cart->chr_rom.mask];
}

static uint8_t ColorDreamsReadChrRom(Cart *cart, const uint16_t addr)
{
    return cart->chr_rom.data[((color_dreams.chr_bank * 0x2000) + (addr & 0x1FFF)) & cart->chr_rom.mask];
}

static uint8_t NinjaReadChrRom(Cart *cart, const uint16_t addr)
{
    const int bank = addr < 0x1000 ? ninja.chr_bank0 : ninja.chr_bank1;
    //printf("BANK: %d ADDR: 0x%X\n", bank, addr);
    return cart->chr_rom.data[((bank * 0x1000) + (addr & 0xFFF)) & cart->chr_rom.mask];
}

static uint16_t GetNanjingChrAddr(Cart *cart, uint16_t addr)
{
    uint16_t final_addr = addr;
    if (nanjing.prg_low_reg.chr_ram_auto_switch && addr < 0x1000)
    {
        final_addr = (SystemGetPpuA9() * 0x1000) + (addr & 0xFFF);
    }
    return final_addr & cart->chr_rom.mask;
}

static uint8_t NanjingReadChrRom(Cart *cart, const uint16_t addr)
{
    return cart->chr_rom.data[GetNanjingChrAddr(cart, addr)];
}

static void NanjingWriteChr(Cart *cart, const uint16_t addr, const uint8_t data)
{
    cart->chr_rom.data[GetNanjingChrAddr(cart, addr)] = data;
}

static const int mmc1_mirror_map[4] =
{
    NAMETABLE_FOUR_SCREEN,
    NAMETABLE_SINGLE_SCREEN,
    NAMETABLE_VERTICAL,
    NAMETABLE_HORIZONTAL
};

static void Mmc1RegWrite(const uint16_t addr, const uint8_t data)
{
    if ((data >> 7) & 1)
    {
        DEBUG_LOG("Mmc1 reset request from addr: 0x%04X\n", addr);

        // Mmc1 reset
        mmc1.shift.raw = 0x10;
        mmc1.shift_count = 0;
        // Set last bank at $C000 and switch 16 KB bank at $8000
        mmc1.control.prg_rom_bank_mode = 0x3;
        mmc1.consec_write = true;
        return;
    }

    if (mmc1.consec_write)
        return;

    mmc1.consec_write = true;
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
            DEBUG_LOG("Set prg rom bank index to %d\n", mmc1.prg_bank.select);
            break;
    }
    mmc1.shift.raw = 0x10;
    mmc1.shift_count = 0;
}

static void Mmc3RegWriteOdd(const uint16_t addr, const uint8_t data)
{
    switch ((addr >> 13) & 0x3)
    {
        // Bank data ($8001-$9FFF, odd)
        case 0:
        {
            uint8_t effective_data = data;
            if (mmc3.bank_sel.reg == 0x6 || mmc3.bank_sel.reg == 0x7)
            {
                effective_data &= 0x3F;
            }
            else if (mmc3.bank_sel.reg == 0x0 || mmc3.bank_sel.reg == 0x1)
            {
                effective_data >>= 1;
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
}

static void Mmc3RegWriteEven(const uint16_t addr, const uint8_t data)
{
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
            //printf("Set MMC3 nametable mirroring mode: %d\n", !mmc3.name_table_arrgmnt);
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

static void Mmc3RegWrite(const uint16_t addr, const uint8_t data)
{
    if (addr & 1)
    {
        Mmc3RegWriteOdd(addr, data);
    }
    else
    {
        Mmc3RegWriteEven(addr, data);
    }
}

static void UxRomRegWrite(const uint16_t addr, const uint8_t data)
{
    UNUSED(addr);

    ux_rom.bank = data;
    DEBUG_LOG("Set prg rom bank index to %d\n", data & 0x7);
}

static void AxRomRegWrite(const uint16_t addr, const uint8_t data)
{
    UNUSED(addr);

    ax_rom.raw = data;
    PpuSetMirroring(2, ax_rom.page);
}

static void CnRomRegWrite(const uint16_t addr, const uint8_t data)
{
    UNUSED(addr);

    cn_rom.raw = data;
}

static void ColorDreamsRegWrite(const uint16_t addr, const uint8_t data)
{
    UNUSED(addr);

    color_dreams.raw = data;
}

static void NinjaRegWrite(const uint16_t addr, const uint8_t data)
{
    switch (addr)
    {
        // PRG Bank Select ($7FFD, write);
        case 0x7FFD:
            //printf("PRG BANK addr: 0x%X data: 0x%X\n", addr, data);
            ninja.prg_bank = data;
            break;
        // CHR Bank Select 0 ($7FFE, write)
        case 0x7FFE:
            ninja.chr_bank0 = data;
            break;
        // CHR Bank Select 1 ($7FFF, write)
        case 0x7FFF:
            ninja.chr_bank1 = data;
            break;
        default:
            //printf("UNK addr: 0x%X\n", addr);
            break;
    }
}

static void BnRomRegWrite(const uint16_t addr, const uint8_t data)
{
    UNUSED(addr);
    // TODO: Bus conflict like this?
    // data &= cart->prg_rom.data[addr];
    bn_rom.bank = data;
}

static void NanjingRegWrite(const uint16_t addr, const uint8_t data)
{
    switch (addr)
    {
        // PRG Bank Low/CHR-RAM Switch ($5000, write);
        case 0x5000:
            //printf("PRG BANK LOW addr: 0x%X data: 0x%X\n", addr, data);
            nanjing.prg_low_reg.raw = data;
            break;
        // Feedback Write ($5100-$5101, write)
        case 0x5100:
            //printf("NANJING Feedback Write addr: 0x%X data: %X\n", addr, data);
            nanjing.feedback.raw = data;
            nanjing.feedback.flip_latch = 0;
            break;
        case 0x5101:
            //printf("NANJING Feedback Write addr: 0x%X data: %X\n", addr, data);
            nanjing.feedback.latch ^= data & 1;
            break;
        // PRG Bank High ($5200, write)
        case 0x5200:
            //printf("PRG BANK HIGH addr: 0x%X data: 0x%X\n", addr, data);
            nanjing.prg_high_reg = data;
            break;
        // Mode ($5300, write))
        case 0x5300:
            //printf("NANJING Mode addr: 0x%X data: 0x%X\n", addr, data);
            nanjing.mode.raw = data;
            break;
        default:
            //printf("UNK addr: 0x%X\n", addr);
            break;
    }
}

static void Mmc5RegWrite(const uint16_t addr, const uint8_t data)
{
    switch (addr)
    {
        // 8x16 mode enable ($2000 = PPUCTRL)
        case 0x2000:
            mmc5.sprite_mode = (data >> 5) & 1;
            break;
        // PPU Data Substitution Enable ($2001 = PPUMASK)
        case 0x2001:
            mmc5.sub_mode = (data >> 3) & 3;
            break;
        // PRG mode ($5100)
        case 0x5100:
            mmc5.prg_mode = data;
            //printf("Mmc5 PRG Mode addr: 0x%X data: 0x%X\n", addr, data);
            break;
        // CHR mode ($5101)
        case 0x5101:
            mmc5.chr_mode = data;
            //printf("Mmc5 CHR Mode addr: 0x%X data: 0x%X\n", addr, data);
            break;
        // PRG RAM Protect 1 ($5102)
        case 0x5102:
            //printf("Mmc5 Prg Ram Protect addr: 0x%X data: 0x%X\n", addr, data);
            break;
        // PRG RAM Protect 2 ($5103)
        case 0x5103:
            //printf("Mmc5 Prg Ram Protect2 addr: 0x%X data: 0x%X\n", addr, data);
            break;
        // Internal extended RAM mode ($5104)
        case 0x5104:
            mmc5.ext_ram_mode = data;
            if (data & 1)
            {
                PpuSetMirroring(2, 1);
            }
            //printf("MMC5 Ext-ram mode addr: 0x%X data: 0x%X\n", addr, data);
            break;
        // Nametable mapping ($5105)
        case 0x5105:
            mmc5.mirroring.raw = data;
            PpuSetNameTable(0, mmc5.mirroring.page0);
            PpuSetNameTable(1, mmc5.mirroring.page1);
            PpuSetNameTable(2, mmc5.mirroring.page2);
            PpuSetNameTable(3, mmc5.mirroring.page3);
            break;
        // Fill-mode tile ($5106)
        case 0x5106:
            //printf("MMC5 Fill mode Tile addr: 0x%X data: 0x%X\n", addr, data);
            break;
        // Fill-mode color ($5107)
        case 0x5107:
            break;
        case 0x5113:
            mmc5.prg_bank[0].raw = data;
            break;
        case 0x5114:
            mmc5.prg_bank[1].raw = data;
            break;
        case 0x5115:
            mmc5.prg_bank[2].raw = data;
            break;
        case 0x5116:
            mmc5.prg_bank[3].raw = data;
            break;
        case 0x5117:
            mmc5.prg_bank[4].raw = data;
            break;
        case 0x5120:
        case 0x5121:
        case 0x5122:
        case 0x5123:
        case 0x5124:
        case 0x5125:
        case 0x5126:
        case 0x5127:
        case 0x5128:
        case 0x5129:
        case 0x512A:
        case 0x512B:
            //printf("MMC5 Set chr bank: %d data %d\n", addr - 0x5120, data);
            mmc5.chr_select[addr & 0xF] = (mmc5.chr_high << 2) | data;
            break;
        // Upper CHR Bank bits ($5130)
        case 0x5130:
            //printf("MMC5 Set upper chr bank bits data: %X\n", data);
            mmc5.chr_high = data;
            break;
        // Vertical Split Mode ($5200)
        case 0x5200:
            //printf("MMC5 Vertical Split Mode: %X\n", data);
            break;
        // IRQ Scanline Compare Value ($5203)
        case 0x5203:
            mmc5.target_scanline = data;
            //printf("MMC5 Set target scanline: %d\n", mmc5.target_scanline);
            break;
        // Scanline IRQ Status ($5204, write)
        case 0x5204:
            mmc5.irq_enable = data >> 7;
            //printf("MMC5 Irq enable: %d\n", mmc5.irq_enable);
            break;
        case 0x5300:
            break;
        default:
            if (addr >= 0x5C00)
            {
                mmc5.ext_ram[addr & 0x3FF] = data;
            }
            //printf("MMC5 UNK addr: 0x%X\n", addr);
            break;
    }
}

static uint8_t Mmc5RegRead(const uint16_t addr)
{
    switch (addr)
    {
        case 0x5204:
        {
            Mmc5IrqStatusReg status = mmc5.irq_status;
            mmc5.irq_status.irq_pending = false;
            return status.raw;
        }
        case 0xFFFA:
        case 0xFFFB:
        {
            mmc5.irq_status.in_frame = 0;
            mmc5.prev_addr = 0;
            return 0;
        }
        default:
            if (addr >= 0x5C00)
            {
                return mmc5.ext_ram[addr & 0x3FF];
            }
            //printf("MMC5 Read UNK addr: 0x%X\n", addr);
            return 0;
    }
}

static uint8_t NanjingRegRead(const uint16_t addr)
{
    UNUSED(addr);
    return ~nanjing.feedback.raw;
}

uint8_t MapperReadPrgRom(Cart *cart, const uint16_t addr)
{
    return cart->PrgReadFn(cart, addr);
}

uint8_t MapperReadChrRom(Cart *cart, const uint16_t addr)
{
    return cart->ChrReadFn(cart, addr);
}

uint8_t MapperReadReg(Cart *cart, const uint16_t addr)
{
    return cart->RegReadFn(addr);
}

void MapperWriteChrRam(Cart *cart, const uint16_t addr, const uint8_t data)
{
    cart->ChrWriteFn(cart, addr, data);
}

void MapperWriteReg(Cart *cart, const uint16_t addr, uint8_t data)
{
    cart->RegWriteFn(addr, data);
}

void Mmc3ClockIrqCounter(Cart *cart)
{
    UNUSED(cart);

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
}

void Mmc5ClockIrqCounter(Cart *cart, const uint16_t addr)
{
    UNUSED(cart);

    if (((addr >> 12) == 2) && mmc5.prev_addr == addr)
    {
        mmc5.prev_addr = addr;
        if (++mmc5.matches != 2)
            return;
        // If the "in-frame" flag (register $5204) was clear,
        // it becomes set, and the internal 8-bit scanline counter is reset to zero;
        // but if it was already set, the scanline counter is incremented, then compared against the value written to $5203.
        // If they match, the "irq pending" flag is set. 
        if (!mmc5.irq_status.in_frame)
        {
            //printf("MMC5 In frame: %d\n", mmc5.scanline);
            mmc5.irq_status.in_frame = 1;
            mmc5.scanline = 0;
        }
        else
        {
            if (++mmc5.scanline && mmc5.scanline == mmc5.target_scanline)
            {
                mmc5.irq_status.irq_pending = 1;
            }
        }
    }
    else
    {
        mmc5.prev_addr = addr;
        mmc5.matches = 0;
    }
}

bool PollMapperIrq(void)
{
    return mmc3.irq_pending | (mmc5.irq_status.irq_pending & mmc5.irq_enable);
}

void MapperReset(Cart *cart)
{
    switch (cart->mapper_num)
    {
        case MAPPER_MMC5:
            mmc5.irq_enable = 0;
            break;
        case MAPPER_NANJING:
            nanjing.feedback.raw = 0;
            nanjing.mode.raw = 0;
            break;
        default:
            break;
    }
}

void MapperInit(Cart *cart)
{
    switch (cart->mapper_num)
    {
        case MAPPER_NROM:
            cart->PrgReadFn = NromReadPrgRom;
            cart->ChrReadFn = NromReadChrRom;
            cart->ChrWriteFn = ChrWriteGeneric;
            SystemAddMemMapRead(0x6000, 0x7FFF, MEM_SWRAM_READ);
            SystemAddMemMapWrite(0x6000, 0x7FFF, MEM_SWRAM_WRITE);
            SystemAddMemMapRead(0x8000, 0xFFFF, MEM_PRG_READ);
            break;
        case MAPPER_MMC1:
            mmc1.control.prg_rom_bank_mode = 3;
            cart->PrgReadFn = Mmc1ReadPrgRom;
            cart->ChrReadFn = Mmc1ReadChrRom;
            cart->ChrWriteFn = ChrWriteGeneric;
            cart->RegWriteFn = Mmc1RegWrite;
            SystemAddMemMapRead(0x6000, 0x7FFF, MEM_SWRAM_READ);
            SystemAddMemMapWrite(0x6000, 0x7FFF, MEM_SWRAM_WRITE);
            SystemAddMemMapRead(0x8000, 0xFFFF, MEM_PRG_READ);
            SystemAddMemMapWrite(0x8000, 0xFFFF, MEM_REG_WRITE);
            cart->prg_rom.num_banks = GetNumPrgRomBanks(cart->prg_rom.size, PRG_BANK_SIZE_16KIB);
            break;
        case MAPPER_UXROM:
            cart->PrgReadFn = UxRomReadPrgRom;
            cart->ChrReadFn = NromReadChrRom;
            cart->ChrWriteFn = ChrWriteGeneric;
            cart->RegWriteFn = UxRomRegWrite;
            SystemAddMemMapRead(0x6000, 0x7FFF, MEM_SWRAM_READ);
            SystemAddMemMapWrite(0x6000, 0x7FFF, MEM_SWRAM_WRITE);
            SystemAddMemMapRead(0x8000, 0xFFFF, MEM_PRG_READ);
            SystemAddMemMapWrite(0x8000, 0xFFFF, MEM_REG_WRITE);
            cart->prg_rom.num_banks = GetNumPrgRomBanks(cart->prg_rom.size, PRG_BANK_SIZE_16KIB);
            break;
        case MAPPER_CNROM:
            cart->PrgReadFn = NromReadPrgRom;
            cart->ChrReadFn = CnromReadChrRom;
            cart->ChrWriteFn = ChrWriteGeneric;
            cart->RegWriteFn = CnRomRegWrite;
            SystemAddMemMapRead(0x6000, 0x7FFF, MEM_SWRAM_READ);
            SystemAddMemMapWrite(0x6000, 0x7FFF, MEM_SWRAM_WRITE);
            SystemAddMemMapRead(0x8000, 0xFFFF, MEM_PRG_READ);
            SystemAddMemMapWrite(0x8000, 0xFFFF, MEM_REG_WRITE);
            break;
        case MAPPER_MMC3:
        {
            cart->PrgReadFn = Mmc3ReadPrgRom;
            cart->ChrReadFn = Mmc3ReadChr;
            cart->ChrWriteFn = Mmc3WriteChr;
            cart->RegWriteFn = Mmc3RegWrite;
            SystemAddMemMapRead(0x6000, 0x7FFF, MEM_SWRAM_READ);
            SystemAddMemMapWrite(0x6000, 0x7FFF, MEM_SWRAM_WRITE);
            SystemAddMemMapRead(0x8000, 0xFFFF, MEM_PRG_READ);
            SystemAddMemMapWrite(0x8000, 0xFFFF, MEM_REG_WRITE);
            cart->prg_rom.num_banks = GetNumPrgRomBanks(cart->prg_rom.size, PRG_BANK_SIZE_8KIB);
            break;
        }
        case MAPPER_MMC5:
            mmc5.prg_mode = 3;
            mmc5.chr_mode = 3;
            mmc5.prg_bank[4].raw = 0xFF;
            cart->PrgReadFn = Mmc5ReadPrgRom;
            cart->ChrReadFn = Mmc5ReadChr;
            cart->ChrWriteFn = Mmc5WriteChr;
            cart->RegWriteFn = Mmc5RegWrite;
            cart->RegReadFn = Mmc5RegRead;
            SystemAddMemMapWrite(0x2000, 0x2001, MEM_REG_WRITE);
            SystemAddMemMapWrite(0x5000, 0x5FFF, MEM_REG_WRITE);
            SystemAddMemMapRead(0x5000, 0x5FFF, MEM_REG_READ);
            SystemAddMemMapRead(0x6000, 0x7FFF, MEM_SWRAM_READ);
            SystemAddMemMapWrite(0x6000, 0x7FFF, MEM_SWRAM_WRITE);
            SystemAddMemMapRead(0xFFFA, 0xFFFB, MEM_REG_READ);
            SystemAddMemMapRead(0x8000, 0xFFFF, MEM_PRG_READ);
            cart->prg_rom.num_banks = GetNumPrgRomBanks(cart->prg_rom.size, PRG_BANK_SIZE_16KIB);
            break;
        case MAPPER_AXROM:
            cart->PrgReadFn = AxRomReadPrgRom;
            cart->ChrReadFn = NromReadChrRom;
            cart->ChrWriteFn = ChrWriteGeneric;
            cart->RegWriteFn = AxRomRegWrite;
            SystemAddMemMapRead(0x8000, 0xFFFF, MEM_PRG_READ);
            SystemAddMemMapWrite(0x8000, 0xFFFF, MEM_REG_WRITE);
            cart->prg_rom.num_banks = GetNumPrgRomBanks(cart->prg_rom.size, PRG_BANK_SIZE_32KIB);
            break;
        case MAPPER_COLORDREAMS:
            cart->PrgReadFn = ColorDreamsReadPrgRom;
            cart->ChrReadFn = ColorDreamsReadChrRom;
            cart->ChrWriteFn = ChrWriteGeneric;
            cart->RegWriteFn = ColorDreamsRegWrite;
            SystemAddMemMapRead(0x8000, 0xFFFF, MEM_PRG_READ);
            SystemAddMemMapWrite(0x8000, 0xFFFF, MEM_REG_WRITE);
            cart->prg_rom.num_banks = GetNumPrgRomBanks(cart->prg_rom.size, PRG_BANK_SIZE_32KIB);
            break;
        case MAPPER_BNROM_NINJA:
            cart->prg_rom.num_banks = GetNumPrgRomBanks(cart->prg_rom.size, PRG_BANK_SIZE_32KIB);
            if (cart->chr_rom.size > 0x2000)
            {
                cart->PrgReadFn = NinjaReadPrgRom;
                cart->ChrReadFn = NinjaReadChrRom;
                cart->ChrWriteFn = ChrWriteGeneric;
                cart->RegWriteFn = NinjaRegWrite;
                SystemAddMemMapRead(0x6000, 0x7FFF, MEM_SWRAM_READ);
                SystemAddMemMapWrite(0x6000, 0x7FFF, MEM_SWRAM_WRITE);
                SystemAddMemMapWrite(0x7FFD, 0x7FFF, MEM_REG_WRITE);
                SystemAddMemMapRead(0x8000, 0xFFFF, MEM_PRG_READ);
                break;
            }
            cart->PrgReadFn = BnRomReadPrgRom;
            cart->ChrReadFn = NromReadChrRom;
            cart->ChrWriteFn = ChrWriteGeneric;
            cart->RegWriteFn = BnRomRegWrite;
            SystemAddMemMapRead(0x8000, 0xFFFF, MEM_PRG_READ);
            SystemAddMemMapWrite(0x8000, 0xFFFF, MEM_REG_WRITE);
            break;
        case MAPPER_NANJING:
            cart->PrgReadFn = NanjingReadPrgRom;
            cart->ChrReadFn = NanjingReadChrRom;
            cart->ChrWriteFn = NanjingWriteChr;
            cart->RegWriteFn = NanjingRegWrite;
            cart->RegReadFn = NanjingRegRead;
            SystemAddMemMapRead(0x5000, 0x5FFF, MEM_REG_READ);
            SystemAddMemMapWrite(0x5000, 0x5FFF, MEM_REG_WRITE);
            SystemAddMemMapRead(0x6000, 0x7FFF, MEM_SWRAM_READ);
            SystemAddMemMapWrite(0x6000, 0x7FFF, MEM_SWRAM_WRITE);
            SystemAddMemMapRead(0x8000, 0xFFFF, MEM_PRG_READ);
            break;
        default:
            printf("Bad Mapper type!: %d\n", cart->mapper_num);
            break;
    }
}
