#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>

#include "mem.h"
#include "nes.h"
#include "cpu.h"
#include "ppu.h"


static uint8_t *LoadRom(const char *path, NES2_Header *hdr)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return NULL;

    //NES2_Header *nes2 = malloc(sizeof(*nes2));
    fread(hdr, 1, 16, fp);

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
    uint16_t base_addr = 0x8000;
    uint32_t rom_size = hdr->prg_rom_size_lsb * 0x4000;
    uint8_t *rom = malloc(rom_size);

    if (!hdr->trainer_area_512)
    {
        fseek(fp, 16, SEEK_SET);
        fread(rom, rom_size, 1, fp);
    }
    else
    {
        fseek(fp, 512 + 16, SEEK_SET);
        fread(rom, rom_size, 1, fp);
    }

    fclose(fp);
    return rom;
}

void MapMem(uint8_t *rom, int mapper, int prg_rom_size)
{
    switch (mapper)
    {
        case 0:
        {
            if (prg_rom_size == 1)
            {
                uint16_t base_addr = 0x8000;

                memcpy(CPUGetPtr(0x8000), &rom[0], 0x4000);
                MemAddMap(0x8000, 0x4000);
                //memcpy(CPUGetPtr(0xC000), &rom[0], 0x4000);
                MemAddMirror(0xC000, 0x4000, 0x8000, false);
                //MemAddMirror(0xFFF0, 16, 0xBFF0, false);
                /*
                if (i == 0)
                memcpy(&memory[0x8000], &rom[bank_addr - 16], 0x4000);
            else
                memcpy(&memory[0xC000], &rom[bank_addr - 16], 0x4000);
            */
            }
            break;
        }
        default:
            printf("Unknown mapper %d\n", mapper);
            break;
    }   
}

int main(int arc, char **argv)
{
    Cpu cpu_state;
    CPU_Init(&cpu_state);
    MemInit();
    PPU_Init();

    NES2_Header hdr;
    uint8_t *rom = LoadRom(argv[1], &hdr);

    MapMem(rom, hdr.mapper_number_d3d0, hdr.prg_rom_size_lsb);
    cpu_state.pc = 0x8000;

    free(rom);
    uint32_t max_ticks = 6000000;
    uint32_t ticks = max_ticks;
    while (max_ticks != 0)
    {
        CPU_Update(&cpu_state);
        PPU_Update(cpu_state.cycles);
        usleep(100000);
        ticks--;
    }

    PPU_Reset();
    CPU_Reset(&cpu_state);

    return 0;
}