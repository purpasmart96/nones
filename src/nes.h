
#ifndef NES_H
#define NES_H

#include <stdint.h>
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

#endif
