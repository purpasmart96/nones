#ifndef CART_H
#define CART_H

#define HEADER_SIZE 16

typedef struct
{
    char id_string[4];
    uint8_t prg_rom_size_lsb;
    uint8_t chr_rom_size_lsb;
    uint8_t name_table_layout : 1;
    uint8_t battery : 1;
    uint8_t trainer_area_512 : 1;
    uint8_t alt_name_tables : 1;
    uint8_t mapper_number_d3d0 : 4;
    uint8_t console_type : 2;
    uint8_t nes2_id : 2;
    uint8_t mapper_number_d7d4 : 4;
    uint8_t mapper_number_d11d8 : 4;
    uint8_t submapper_number : 4;
    uint8_t prg_rom_size_msb : 4;
    uint8_t chr_rom_size_msb : 4;
    uint8_t prg_ram_shift_count : 4;
    uint8_t prg_nvram_eeprom_shift_count : 4;
    uint8_t chr_ram_shift_count : 4;
    uint8_t chr_nvram_shift_count : 4;
    uint8_t timing_mode : 2;
    uint8_t padding0 : 6;
    //uint8_t byte13;
    uint8_t vs_ppu_type : 4;
    uint8_t vs_hardware_type : 4;
    uint8_t misc_roms_present : 2;
    uint8_t padding1 : 6;
    uint8_t default_expansion_dev : 6;
    uint8_t padding2 : 2;
} NES2_Header;

typedef struct
{
    uint8_t *data;
    uint32_t size;
    uint32_t mask;
    // Number of banks depending on the bank size;
    int num_banks;
} PrgRom;

typedef struct
{
    uint8_t *data;
    uint32_t size;
    uint32_t mask;
    bool ram;
} ChrRom;

typedef union
{
    uint8_t raw : 5;
    struct
    {
        uint8_t bit0 : 1;
        uint8_t bit1 : 1;
        uint8_t bit2 : 1;
        uint8_t bit3 : 1;
        uint8_t bit4 : 1;
    };
} Mmc1ShiftReg;

typedef union
{
    uint8_t raw;
    struct
    {
        uint8_t data : 1;
        uint8_t : 6;
        uint8_t reset : 1;
    };
} Mmc1LoadReg;

typedef union
{
    uint8_t raw : 5;
    struct
    {
        // Nametable arrangement:
        // 0: one-screen, lower bank;
        // 1: one-screen, upper bank;
        // 2: horizontal arrangement ("vertical mirroring", PPU A10);
        // 3: vertical arrangement ("horizontal mirroring", PPU A11)
        uint8_t name_table_setup : 2;
        // PRG-ROM bank mode:
        // 0, 1: switch 32 KB at $8000, ignoring low bit of bank number;
        // 2: fix first bank at $8000 and switch 16 KB bank at $C000;
        // 3: fix last bank at $C000 and switch 16 KB bank at $8000)
        uint8_t prg_rom_bank_mode : 2;
        // CHR-ROM bank mode:
        // 0: switch 8 KB at a time;
        // 1: switch two separate 4 KB banks
        uint8_t chr_rom_bank_mode : 1;
    };

} Mmc1ControlReg;

typedef union
{
    uint8_t raw : 5;
    struct
    {
        // Select 16 KB PRG-ROM bank (low bit ignored in 32 KB mode)
        uint8_t select : 4;
        // PRG-RAM chip enable (0: enabled; 1: disabled; ignored on MMC1A)
        uint8_t ram_enable : 1;
    };

} Mmc1PrgBankReg;

typedef struct 
{
    uint16_t shift_count;
    Mmc1ShiftReg shift;
    Mmc1LoadReg load;
    Mmc1ControlReg control;
    Mmc1PrgBankReg prg_bank;
    bool consec_write;
    // Select 4 KB or 8 KB CHR bank at PPU $0000 (low bit ignored in 8 KB mode)
    uint8_t chr_bank0 : 5;
    // Select 4 KB CHR bank at PPU $1000 (ignored in 8 KB mode)
    uint8_t chr_bank1 : 5;
} Mmc1;

typedef union
{
    uint8_t raw;
    struct
    {
        // Specify which bank register to update on next write to Bank Data register:
        // 000: R0: Select 2 KB CHR bank at PPU $0000-$07FF (or $1000-$17FF);
        // 001: R1: Select 2 KB CHR bank at PPU $0800-$0FFF (or $1800-$1FFF);
        // 010: R2: Select 1 KB CHR bank at PPU $1000-$13FF (or $0000-$03FF);
        // 011: R3: Select 1 KB CHR bank at PPU $1400-$17FF (or $0400-$07FF);
        // 100: R4: Select 1 KB CHR bank at PPU $1800-$1BFF (or $0800-$0BFF);
        // 101: R5: Select 1 KB CHR bank at PPU $1C00-$1FFF (or $0C00-$0FFF);
        // 110: R6: Select 8 KB PRG ROM bank at $8000-$9FFF (or $C000-$DFFF);
        // 111: R7: Select 8 KB PRG ROM bank at $A000-$BFFF
        uint8_t reg : 3;
        uint8_t : 3;
        // PRG ROM bank mode;
        // 0: $8000-$9FFF swappable, $C000-$DFFF fixed to second-last bank;
        // 1: $C000-$DFFF swappable, $8000-$9FFF fixed to second-last bank;
        uint8_t prg_rom_bank_mode : 1;
        // CHR A12 inversion;
        // 0: two 2 KB banks at $0000-$0FFF, four 1 KB banks at $1000-$1FFF;
        // 1: two 2 KB banks at $1000-$1FFF, four 1 KB banks at $0000-$0FFF;
        uint8_t chr_a12_invert : 1;
    };

} Mmc3BankReg;

typedef union
{
    uint8_t raw;
    struct
    {
        uint8_t : 4;
        uint8_t : 2;
        uint8_t write_protect : 1;
        uint8_t prg_ram_enable : 1;
    };

} Mmc3PrgRamReg;

typedef struct 
{
    uint8_t regs[8];
    Mmc3BankReg bank_sel;
    // Nametable arrangement is the swapped form of mirroring
    uint8_t name_table_arrgmnt;
    Mmc3PrgRamReg prg_ram_protect;
    uint8_t irq_counter;
    uint8_t irq_latch;
    uint8_t irq_reload;
    bool irq_enable;
    bool irq_pending;
} Mmc3;

typedef union
{
    uint8_t raw;
    struct
    {
        uint8_t enable: 2;
    };

} Mmc5PrgRamReg;

typedef union
{
    uint8_t raw;
    struct
    {
        // 7  bit  0
        // ---- ----
        // DDCC BBAA
        // |||| ||||
        // |||| ||++- Select nametable at PPU $2000-$23FF
        uint8_t page0 : 2;
        // |||| ++--- Select nametable at PPU $2400-$27FF
        uint8_t page1 : 2;
        // ||++------ Select nametable at PPU $2800-$2BFF
        uint8_t page2 : 2;
        // ++-------- Select nametable at PPU $2C00-$2FFF
        uint8_t page3 : 2;
    };

} Mmc5NtReg;

typedef union
{
    uint8_t raw;
    struct
    {
        // 7  bit  0
        // ---- ----
        // RAAA AaAA
        // |||| ||||
        // |||| |||+- PRG ROM/RAM A13
        uint8_t a13 : 1;
        // |||| ||+-- PRG ROM/RAM A14
        uint8_t a14 : 1;
        // |||| |+--- PRG ROM/RAM A15, also selecting between PRG RAM /CE 0 and 1
        uint8_t a15 : 1;
        // |||| +---- PRG ROM/RAM A16
        uint8_t a16 : 1;
        // |||+------ PRG ROM A17
        uint8_t a17 : 1;
        // ||+------- PRG ROM A18
        uint8_t a18 : 1;
        // |+-------- PRG ROM A19
        uint8_t a19 : 1;
        // +--------- RAM/ROM toggle (0: RAM; 1: ROM) (registers $5114-$5116 only)
        uint8_t rom : 1;
    };

} Mmc5PrgBankReg;

typedef union
{
    uint8_t raw;
    struct
    {
        uint8_t : 6;
        uint8_t in_frame : 1;
        uint8_t irq_pending : 1;
    };

} Mmc5IrqStatusReg;

typedef struct
{
    uint8_t ext_ram[0x400];
    uint8_t chr_select[12];
    Mmc5PrgBankReg prg_bank[5];
    Mmc5PrgRamReg prg_ram[3];
    uint16_t prev_addr;
    Mmc5NtReg mirroring;
    Mmc5IrqStatusReg irq_status;
    uint8_t target_scanline;
    uint8_t scanline;
    uint8_t matches;
    bool ppu_reading;
    uint8_t ext_ram_mode : 2;
    // 1,2,3: Substitutions enabled; 0: substitutions disabled
    uint8_t sub_mode : 2;
    // PRG bank mode:
    // 0 - One 32KB bank;
    // 1 - Two 16KB bankScanline IRQ Status;
    // 2 - One 16KB bank ($8000-$BFFF) and two 8KB banks ($C000-$DFFF and $E000-$FFFF);
    // 3 - Four 8KB banks;
    uint8_t prg_mode : 2;
    uint8_t chr_mode : 2;
    uint8_t chr_high : 2;
    uint8_t sprite_mode : 1;
    uint8_t irq_enable : 1; 
} Mmc5;

typedef struct
{
    uint8_t bank;
} UxRom;

typedef union
{
    uint8_t raw;
    struct
    {
        // CHR A14..A13 (8 KiB bank)
        uint8_t chr_bank : 2;
        uint8_t : 2;
        uint8_t diode2 : 1;
        uint8_t diode1 : 1;
    };

} CnRom;

typedef union
{
    uint8_t raw;
    struct
    {
        // Select 32 KB PRG ROM bank for CPU $8000-$FFFF
        uint8_t bank : 3;
        uint8_t : 1;
        // Select 1 KB VRAM page for all 4 nametables
        uint8_t page : 1;
    };

} AxRom;

typedef union
{
    uint8_t raw;
    struct
    {
        // Select 32 KB PRG ROM bank for CPU $8000-$FFFF
        uint8_t prg_bank : 2;
        // Used for lockout defeat
        uint8_t lockout : 2;
        // Select 8 KB CHR ROM bank for PPU $0000-$1FFF
        uint8_t chr_bank : 4;
    };

} ColorDreams;

typedef struct
{
    // Using a full byte to represent the prg bank rather than two bits
    uint8_t prg_bank;
    // CHR A15..A12 (4 KiB bank) at PPU $0000
    uint8_t chr_bank0 : 4;
    // CHR A15..A12 (4 KiB bank) at PPU $1000
    uint8_t chr_bank1 : 4;
} Nina;

typedef union
{
    uint8_t raw;
    struct
    {
        uint8_t prg_bank_low : 4;
        uint8_t : 3;
        uint8_t chr_ram_auto_switch : 1;
    };

} NanjingPrgLowReg;

typedef union
{
    uint8_t raw;
    struct
    {
        uint8_t swap_d0d1 : 1;
        uint8_t : 1;
        uint8_t prg_15_16: 1;
    };

} NanjingModeReg;

typedef union
{
    uint8_t raw;
    struct
    {
        uint8_t flip_latch : 1;
        uint8_t pad : 1;
        uint8_t latch: 1;
    };

} NanjingFeedbackReg;

typedef struct
{
    NanjingPrgLowReg prg_low_reg;
    uint8_t prg_high_reg : 2;
    NanjingFeedbackReg feedback;
    NanjingModeReg mode;
} Nanjing;

typedef struct
{
    // Using a full byte to represent the prg bank rather than two bits
    uint8_t bank;
} BnRom;

typedef struct
{
    uint8_t outer_bank : 2;
    uint8_t mirroring : 1;
    uint8_t inner_bank : 4;
} Camerica;

typedef struct Cart {
    PrgRom prg_rom;
    ChrRom chr_rom;
    // WRAM or SRAM
    uint8_t *ram;
    int mapper_num;
    int mirroring;
    const char *name;
    bool battery;
    uint8_t (*PrgReadFn)(struct Cart *cart, const uint16_t addr);
    uint8_t (*ChrReadFn)(struct Cart *cart, const uint16_t addr);
    void (*ChrWriteFn)(struct Cart *cart, const uint16_t addr, const uint8_t data);
    void (*RegWriteFn)(const uint16_t addr, const uint8_t data);
    uint8_t (*RegReadFn)(const uint16_t addr);
} Cart;

#define CART_RAM_SIZE 0x2000
#define CHR_RAM_SIZE 0x2000

int CartLoad(Arena *arena, Cart *cart, const char *path);
void CartSaveSram(Cart *cart);

#endif
