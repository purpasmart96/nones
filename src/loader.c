#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "arena.h"
#include "loader.h"
#include "cpu.h"
#include "apu.h"
#include "ppu.h"
#include "arena.h"
#include "bus.h"


/*
int LoaderLoadCart(const char *path, Cart *cart)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return -1;

    NES2_Header hdr;
    fread(&hdr, 1, HEADER_SIZE, fp);

    // iNES / NES2 header magic
    const char magic[4] = { 0x4E, 0x45, 0x53, 0x1A };

    if (memcmp(magic, hdr.id_string, 4))
    {
        printf("Not a valid file format!\n");
        fclose(fp);
        return -1;
    }

    printf("ID String: %s\n", hdr.id_string);
    printf("PRG Rom Size in 16 KiB units: %d\n", hdr.prg_rom_size_lsb);
    printf("CHR Rom Size in 8 KiB units: %d\n", hdr.chr_rom_size_lsb);
    printf("Nametable layout: %d\n", hdr.name_table_layout);
    printf("Battery: %d\n", hdr.battery);
    printf("Trainer: %d\n", hdr.trainer_area_512);
    printf("Alt nametable layout: %d\n", hdr.alt_name_tables);
    printf("Mapper: %d\n", hdr.mapper_number_d3d0);

#ifdef DUMP_BANKS
    char filenames[16] = {'\0'};
#endif

    fseek(fp, 16, SEEK_END);
    uint32_t rom_size = ftell(fp);
    uint8_t *rom = malloc(rom_size);

    cart->prg_rom_size = hdr.prg_rom_size_lsb * 0x4000;
    cart->chr_rom_size = hdr.chr_rom_size_lsb * 0x2000;
    cart->name_table_layout = hdr.name_table_layout;
    cart->battery = hdr.battery;

    cart->prg_rom = malloc(cart->prg_rom_size);

    fseek(fp, HEADER_SIZE, SEEK_SET);
    fread(rom, rom_size, 1, fp);

    if (!hdr.trainer_area_512)
    {
        memcpy(cart->prg_rom, rom, cart->prg_rom_size);
        fread(cart->prg_rom, cart->prg_rom_size, 1, fp);

        if (cart->chr_rom_size)
        {
            cart->chr_rom = malloc(cart->chr_rom_size);
            memcpy(cart->chr_rom, &rom[cart->prg_rom_size], cart->chr_rom_size);
        }

        if (cart->battery)
        {
            cart->sram = malloc(0x2000);
        }
    }
    else
    {
        memcpy(cart->prg_rom, &rom[512], cart->prg_rom_size);
        fread(cart->prg_rom, cart->prg_rom_size, 1, fp);
        if (cart->chr_rom_size)
        {
            cart->chr_rom = malloc(cart->chr_rom_size);
            memcpy(cart->chr_rom, &rom[cart->prg_rom_size + 512], cart->chr_rom_size);
        }
    
        if (hdr.battery)
        {
            cart->sram = malloc(0x2000);
        }
    }

    fclose(fp);
    free(rom);

    return 0;
}
*/

int LoaderLoadCart(Arena *arena, Cart *cart, const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
    {
        fprintf(stderr, "Failed to open %s!\n", path);
        return -1;
    }

    NES2_Header hdr;
    fread(&hdr, 1, 16, fp);

    // iNES / NES2 header magic
    const char magic[4] = { 0x4E, 0x45, 0x53, 0x1A };

    if (memcmp(magic, hdr.id_string, 4))
    {
        fprintf(stderr, "Not a valid iNES/NES2 file format!\n");
        fclose(fp);
        return -1;
    }

    printf("Loading %s\n", path);
    printf("ID String: %s\n", hdr.id_string);
    printf("PRG Rom Size in 16 KiB units: %d\n", hdr.prg_rom_size_lsb);
    printf("CHR Rom Size in 8 KiB units: %d\n", hdr.chr_rom_size_lsb);
    printf("Nametable layout: %d\n", hdr.name_table_layout);
    printf("Battery: %d\n", hdr.battery);
    printf("Trainer: %d\n", hdr.trainer_area_512);
    printf("Alt nametable layout: %d\n", hdr.alt_name_tables);
    printf("Mapper: %d\n", hdr.mapper_number_d3d0);

#ifdef DUMP_BANKS
    char filenames[16] = {'\0'};
#endif

    fseek(fp, HEADER_SIZE, SEEK_END);
    uint32_t rom_size = ftell(fp);
    uint8_t *rom = malloc(rom_size);

    char *base_name = strrchr(path, '/');
    char *filename = base_name ? base_name : (char*)path;

    cart->name = strtok(filename, ".");

    cart->prg_rom.size = hdr.prg_rom_size_lsb * 0x4000;
    cart->chr_rom.size = hdr.chr_rom_size_lsb * 0x2000;
    cart->mirroring = hdr.name_table_layout;
    cart->battery = hdr.battery;
    cart->mapper_type = hdr.mapper_number_d3d0;

    cart->prg_rom.data = ArenaPush(arena, cart->prg_rom.size);

    const uint16_t trainer_offset = hdr.trainer_area_512 * 512;

    fseek(fp, HEADER_SIZE + trainer_offset, SEEK_SET);
    fread(rom, rom_size, 1, fp);

    memcpy(cart->prg_rom.data, rom, cart->prg_rom.size);

    if (cart->chr_rom.size)
    {
        cart->chr_rom.data = ArenaPush(arena, cart->chr_rom.size);
        memcpy(cart->chr_rom.data, &rom[cart->prg_rom.size], cart->chr_rom.size);
    }
    else
    {
        // chr rom size is 0, assume it's chr ram with a size of 8kib
        printf("Using chr ram\n");
        cart->chr_rom.data = ArenaPush(arena, CHR_RAM_SIZE);
        cart->chr_rom.size = CHR_RAM_SIZE;
        cart->chr_rom.is_ram = true;  
    }

    // Sram / Wram
    cart->ram = ArenaPush(arena, CART_RAM_SIZE);

    // Load Sram
    if (cart->battery)
    {
        char save_path[128];
        snprintf(save_path, sizeof(save_path), "%s.sav", cart->name);

        FILE *sav = fopen(save_path, "rb");
        if (sav)
        {
            fread(cart->ram, CART_RAM_SIZE, 1, sav);
            fclose(sav);
        }
    }

    fclose(fp);
    free(rom);

    return 0;
}
