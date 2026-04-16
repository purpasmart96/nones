#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "arena.h"
#include "cart.h"
#include "ppu.h"
#include "apu.h"
#include "mapper.h"
#include "utils.h"

static void CartLoadSram(Cart *cart)
{
    if (!cart->battery)
        return;

    char save_path[256];
    snprintf(save_path, sizeof(save_path), "%s.sav", cart->name);
    FILE *sav = fopen(save_path, "rb");
    if (sav)
    {
        fread(cart->prg_ram.data, CART_RAM_SIZE, 1, sav);
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
    printf("Nametable arrangement: %d\n", hdr.name_table_arrangement);
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
        case MAPPER_MMC5:
        case MAPPER_AXROM:
        case MAPPER_MMC2:
        case MAPPER_COLORDREAMS:
        case MAPPER_BNROM_NINA:
        case MAPPER_CAMERICA:
        case MAPPER_NANJING:
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
    cart->arrangement = hdr.name_table_arrangement;
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
        // If Chr rom size is 0, and chr ram shift count is 0. Assume it's Chr ram with a size of 8 Kib
        cart->chr_rom.size = hdr.chr_ram_shift_count ? 64 << hdr.chr_ram_shift_count : CHR_RAM_SIZE;
        printf("CHR Ram Size: %d KiB\n", cart->chr_rom.size >> 10);
        cart->chr_rom.data = ArenaPush(arena, cart->chr_rom.size);
        cart->chr_rom.ram = true;
    }

    cart->chr_rom.mask = cart->chr_rom.size - 1;

    const uint32_t sram_size = hdr.prg_ram_shift_count ? 64 << hdr.prg_ram_shift_count : 0;
    const uint32_t nvram_size = hdr.prg_nvram_shift_count ? 64 << hdr.prg_nvram_shift_count : 0;

    // We need to maintain backwards compatibility for iNES;
    // So always allocate PRG RAM with a size of at least 8 Kib
    cart->prg_ram.size = MAX(sram_size + nvram_size, CART_RAM_SIZE);

    if (cart->battery && mapper_number == 5 && hdr.nes2_id != 2)
    {
        // For MMC5 iNES games, there is not much we can do.
        // Since we have know way of knowing how big PRG RAM actually is.
        // So I'm going to set the ram size to one that causes the least amount of issues (16 Kib).
        cart->prg_ram.size = 0x4000;
    }

    if (cart->battery || hdr.prg_ram_shift_count || hdr.prg_nvram_shift_count)
        printf("Prg Ram Size: %d KiB\n", cart->prg_ram.size >> 10);

    cart->prg_ram.data = ArenaPush(arena, cart->prg_ram.size);
    cart->prg_ram.mask = cart->prg_ram.size - 1;

    // Load Sram
    CartLoadSram(cart);

    fclose(fp);
    free(rom);

    MapperInit(cart);
    return 0;
}

uint8_t CartReadPrgRam(Cart *cart, const uint32_t addr)
{
    return cart->prg_ram.data[addr & cart->prg_ram.mask];
}

void CartWritePrgRam(Cart *cart, const uint32_t addr, const uint8_t data)
{
    cart->prg_ram.data[addr & cart->prg_ram.mask] = data;
}

uint8_t CartReadPrgRom(Cart *cart, const uint32_t addr)
{
    return cart->prg_rom.data[addr & cart->prg_rom.mask];
}

uint8_t CartReadChr(Cart *cart, const uint32_t addr)
{
    return cart->chr_rom.data[addr & cart->chr_rom.mask];
}

void CartWriteChr(Cart *cart, const uint32_t addr, const uint8_t data)
{
    cart->chr_rom.data[addr & cart->chr_rom.mask] = data;
}

void CartSaveSram(Cart *cart)
{
    if (!cart->battery)
        return;

    char save_path[256];
    snprintf(save_path, sizeof(save_path), "%s.sav", cart->name);

    FILE *sav = fopen(save_path, "wb");
    if (sav != NULL)
    {
        fwrite(cart->prg_ram.data, CART_RAM_SIZE, 1, sav);
        fclose(sav);
    }
}
