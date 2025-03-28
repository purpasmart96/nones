#ifndef LOADER_H
#define LOADER_H

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
} PrgRom;

typedef struct
{
    uint8_t *data;
    uint32_t size;
    bool is_ram;
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
    } mmc1b;

    struct
    {
        // Select 16 KB PRG-ROM bank (low bit ignored in 32 KB mode)
        uint8_t select : 4;
        // Bit 3 bypasses fixed bank logic in 16K mode (0: fixed bank affects A17-A14;
        // 1: fixed bank affects A16-A14 and bit 3 directly controls A17)
        uint8_t bypass_16k_logic : 1;
    } mmc1a;

} PrgBank;

typedef struct 
{
    uint16_t shift_count;
    Mmc1ShiftReg shift;
    Mmc1LoadReg load;
    Mmc1ControlReg control;
    PrgBank prg_bank;
    // Select 4 KB or 8 KB CHR bank at PPU $0000 (low bit ignored in 8 KB mode)
    uint8_t chr_bank0 : 5;
    // Select 4 KB CHR bank at PPU $1000 (ignored in 8 KB mode)
    uint8_t chr_bank1 : 5;
} Mmc1;

typedef struct
{
    uint8_t bank;
} UxRom;

typedef struct
{
    uint8_t (*ReadFn)(void *mapper, const uint16_t addr);
    void (*WriteFn)(void *mapper, const uint16_t addr, const uint8_t data);
} Mapper;

typedef struct {
    PrgRom prg_rom;
    ChrRom chr_rom;
    // WRAM or SRAM
    uint8_t *ram;
    //Mapper mapper;
    Mmc1 mmc1;
    UxRom ux_rom;
    int mapper_type;
    int mirroring;
    const char *name;
    bool battery;
} Cart;

int LoaderLoadCart(Arena *arena, Cart *cart, const char *path);

#endif
