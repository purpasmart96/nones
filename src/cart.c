#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "arena.h"
#include "cart.h"
#include "mapper.h"

static void CartLoadSram(Cart *cart)
{
    char save_path[256];
    snprintf(save_path, sizeof(save_path), "%s.sav", cart->name);
    FILE *sav = fopen(save_path, "rb");
    if (sav)
    {
        fread(cart->ram, CART_RAM_SIZE, 1, sav);
        fclose(sav);
    }
}

int CartLoad(Arena *arena, Cart *cart, const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
    {
        fprintf(stderr, "Failed to open %s!\n", path);
        return -1;
    }

    NES2_Header hdr;
    fread(&hdr, 1, HEADER_SIZE, fp);

    // iNES / NES2 header magic
    const char magic[4] = { 0x4E, 0x45, 0x53, 0x1A };

    if (memcmp(magic, hdr.id_string, 4))
    {
        fprintf(stderr, "Not a valid iNES/NES2 file format!\n");
        fclose(fp);
        return -1;
    }

    int mapper_number = hdr.mapper_number_d7d4 << 4 | hdr.mapper_number_d3d0;

    printf("Loading %s\n", path);

    if (hdr.nes2_id == 0x2)
    {
        printf("NES 2.0 header detected\n");
        mapper_number = hdr.mapper_number_d11d8 << 8 | mapper_number;
    }

    printf("ID String: %s\n", hdr.id_string);
    printf("PRG Rom Size in 16 KiB units: %d\n", hdr.prg_rom_size_lsb);
    printf("CHR Rom Size in 8 KiB units: %d\n", hdr.chr_rom_size_lsb);
    printf("Nametable layout: %d\n", hdr.name_table_layout);
    printf("Battery: %d\n", hdr.battery);
    printf("Trainer: %d\n", hdr.trainer_area_512);
    printf("Alt nametable layout: %d\n", hdr.alt_name_tables);
    printf("Mapper: %d\n", mapper_number);
    printf("Sub mapper: %d\n", hdr.submapper_number);

    switch (mapper_number)
    {
        case MAPPER_NROM:
        case MAPPER_MMC1:
        case MAPPER_UXROM:
        case MAPPER_CNROM:
        case MAPPER_MMC3:
        case MAPPER_AXROM:
        case MAPPER_COLORDREAMS:
        case MAPPER_BNROM_NINJA:
            break;
        default:
            printf("Mapper %d is not supported yet!\n", mapper_number);
            return -1;
    }

    fseek(fp, HEADER_SIZE, SEEK_END);
    uint32_t rom_size = ftell(fp);
    uint8_t *rom = malloc(rom_size);

    char *base_name = strrchr(path, '/');
    char *filename = base_name ? base_name : (char*)path;

    cart->name = strtok(filename, ".");

    cart->prg_rom.size = hdr.prg_rom_size_lsb * 0x4000;
    cart->prg_rom.mask = cart->prg_rom.size - 1;
    cart->chr_rom.size = hdr.chr_rom_size_lsb * 0x2000;
    cart->mirroring = hdr.name_table_layout;
    cart->battery = hdr.battery;
    cart->mapper_num = mapper_number;

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
        // Chr rom size is 0, assume it's chr ram with a size of 8kib
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
        CartLoadSram(cart);
    }

    fclose(fp);
    free(rom);

    MapperInit(cart);
    return 0;
}

void CartSaveSram(Cart *cart)
{
    if (cart->battery)
    {
        char save_path[128];
        snprintf(save_path, sizeof(save_path), "%s.sav", cart->name);

        FILE *sav = fopen(save_path, "wb");
        if (sav != NULL)
        {
            fwrite(cart->ram, CART_RAM_SIZE, 1, sav);
            fclose(sav);
        }
    }
}
