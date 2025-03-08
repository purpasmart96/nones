#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "loader.h"
#include "mem.h"

uint8_t *g_prg_rom = NULL;
uint32_t g_prg_rom_size = 0;

uint8_t *g_chr_rom = NULL;
uint32_t g_chr_rom_size = 0;
uint8_t *g_sram = NULL;

int LoaderLoadRom(const char *path, NES2_Header *hdr)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return -1;

    fread(hdr, 1, 16, fp);

    // iNES / NES2 header magic
    const char magic[4] = { 0x4E, 0x45, 0x53, 0x1A };

    if (memcmp(magic, hdr->id_string, 4))
    {
        printf("Not a valid iNES/NES2 file format!\n");
        fclose(fp);
        return -1;
    }

    printf("ID String: %s\n", hdr->id_string);
    printf("PRG Rom Size in 16 KiB units: %d\n", hdr->prg_rom_size_lsb);
    printf("CHR Rom Size in 8 KiB units: %d\n", hdr->chr_rom_size_lsb);
    printf("Nametable layout: %d\n", hdr->name_table_layout);
    printf("Battery: %d\n", hdr->battery);
    printf("Trainer: %d\n", hdr->trainer_area_512);
    printf("Alt nametable layout: %d\n", hdr->alt_name_tables);
    printf("Mapper: %d\n", hdr->mapper_number_d3d0);

#ifdef DUMP_BANKS
    char filenames[16] = {'\0'};
#endif

    fseek(fp, HEADER_SIZE, SEEK_END);
    uint32_t rom_size = ftell(fp);
    uint8_t *rom = malloc(rom_size);

    uint32_t prg_rom_size = hdr->prg_rom_size_lsb * 0x4000;
    uint32_t chr_rom_size = hdr->chr_rom_size_lsb * 0x2000;

    g_prg_rom_size = prg_rom_size;
    g_chr_rom_size = chr_rom_size;

    g_prg_rom = malloc(prg_rom_size);

    fseek(fp, HEADER_SIZE, SEEK_SET);
    fread(rom, rom_size, 1, fp);

    if (!hdr->trainer_area_512)
    {
        memcpy(g_prg_rom, rom, prg_rom_size);
        fread(g_prg_rom, prg_rom_size, 1, fp);

        if (chr_rom_size)
        {
            g_chr_rom = malloc(chr_rom_size);
            memcpy(g_chr_rom, &rom[prg_rom_size], chr_rom_size);
        }

        if (hdr->battery)
        {
            g_sram = malloc(0x2000);
        }
    }
    else
    {
        memcpy(g_prg_rom, &rom[512], prg_rom_size);
        fread(g_prg_rom, prg_rom_size, 1, fp);
        if (chr_rom_size)
        {
            g_chr_rom = malloc(chr_rom_size);
            memcpy(g_chr_rom, &rom[prg_rom_size + 512], chr_rom_size);
        }
    
        if (hdr->battery)
        {
            g_sram = malloc(0x2000);
        }
    }

    fclose(fp);
    free(rom);

    return 0;
}

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

void LoaderFreeCart(Cart *cart)
{
    free(cart->prg_rom);

    if (cart->chr_rom_size)
        free(cart->chr_rom);
    if (cart->battery)
        free(cart->sram);
}