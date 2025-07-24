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
ColorDreams color_dreams;

static const uint16_t mmc1_chr_bank_sizes[2] = 
{
    0x2000, 0x1000
};

static uint8_t NromReadPrgRom(Cart *cart, uint16_t addr)
{
    return cart->prg_rom.data[addr & cart->prg_rom.mask];
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
        default:
            printf("Mmc1 prg bank mode: %d not implemented yet!\n", mmc1.control.prg_rom_bank_mode);
            break;
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

static uint8_t NromReadChrRom(Cart *cart, const uint16_t addr)
{
    return cart->chr_rom.data[addr & (cart->chr_rom.size - 1)];
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
    return cart->chr_rom.data[(cn_rom.chr_bank * 0x2000) + (addr & 0x1FFF)];
}

static uint8_t ColorDreamsReadChrRom(Cart *cart, const uint16_t addr)
{
    return cart->chr_rom.data[(color_dreams.chr_bank * 0x2000) + (addr & 0x1FFF)];
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
            DEBUG_LOG("Set prg rom bank index to %d\n", mmc1.prg_bank.mmc1a.select);
            //DEBUG_LOG("Bypass 16k logic? %d\n", mmc1->prg_bank.mmc1a.bypass_16k_logic);
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

uint8_t MapperReadPrgRom(Cart *cart, const uint16_t addr)
{
    return cart->PrgReadFn(cart, addr);
}

uint8_t MapperReadChrRom(Cart *cart, const uint16_t addr)
{
    return cart->ChrReadFn(cart, addr);
}

void MapperWrite(Cart *cart, const uint16_t addr, uint8_t data)
{
    if (cart->mapper_num != MAPPER_NROM)
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

bool PollMapperIrq(void)
{
    return mmc3.irq_pending;
}

void MapperInit(Cart *cart)
{
    switch (cart->mapper_num)
    {
        case MAPPER_NROM:
            cart->PrgReadFn = NromReadPrgRom;
            cart->ChrReadFn = NromReadChrRom;
            break;
        case MAPPER_MMC1:
            mmc1.control.prg_rom_bank_mode = 3;
            cart->PrgReadFn = Mmc1ReadPrgRom;
            cart->ChrReadFn = Mmc1ReadChrRom;
            cart->RegWriteFn = Mmc1RegWrite;
            cart->prg_rom.num_banks = GetNumPrgRomBanks(cart->prg_rom.size, PRG_BANK_SIZE_16KIB);
            break;
        case MAPPER_UXROM:
            cart->PrgReadFn = UxRomReadPrgRom;
            cart->ChrReadFn = NromReadChrRom;
            cart->RegWriteFn = UxRomRegWrite;
            cart->prg_rom.num_banks = GetNumPrgRomBanks(cart->prg_rom.size, PRG_BANK_SIZE_16KIB);
            break;
        case MAPPER_CNROM:
            cart->PrgReadFn = NromReadPrgRom;
            cart->ChrReadFn = CnromReadChrRom;
            cart->RegWriteFn = CnRomRegWrite;
            break;
        case MAPPER_MMC3:
            cart->PrgReadFn = Mmc3ReadPrgRom;
            cart->ChrReadFn = Mmc3ReadChrRom;
            cart->RegWriteFn = Mmc3RegWrite;
            cart->prg_rom.num_banks = GetNumPrgRomBanks(cart->prg_rom.size, PRG_BANK_SIZE_8KIB);
            break;
        case MAPPER_AXROM:
            cart->PrgReadFn = AxRomReadPrgRom;
            cart->ChrReadFn = NromReadChrRom;
            cart->RegWriteFn = AxRomRegWrite;
            cart->prg_rom.num_banks = GetNumPrgRomBanks(cart->prg_rom.size, PRG_BANK_SIZE_32KIB);
            break;
        case MAPPER_COLORDREAMS:
            cart->PrgReadFn = ColorDreamsReadPrgRom;
            cart->ChrReadFn = ColorDreamsReadChrRom;
            cart->RegWriteFn = ColorDreamsRegWrite;
            cart->prg_rom.num_banks = GetNumPrgRomBanks(cart->prg_rom.size, PRG_BANK_SIZE_32KIB);
            break;
        default:
            printf("Bad Mapper type!: %d\n", cart->mapper_num);
            break;
    }
}
