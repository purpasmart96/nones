#ifndef CART_H
#define CART_H

#define HEADER_SIZE 16

typedef struct
{
    char id_string[4];
    uint8_t prg_rom_size_lsb;
    uint8_t chr_rom_size_lsb;
    uint8_t name_table_arrangement : 1;
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

typedef struct Cart {
    PrgRom prg_rom;
    ChrRom chr_rom;
    // WRAM or SRAM
    uint8_t *ram;
    int mapper_num;
    int arrangement;
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
