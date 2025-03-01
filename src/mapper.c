#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "nes.h"
#include "ppu.h"

static uint8_t sram[0x2000];

typedef enum {
    NROM_INVALID,
    NROM_128,
    NROM_256
} NromType;


void MapperInit(void)
{

}

/*
#define DUMP_BANKS
static int LoadRom(const char *path, uint8_t *rom)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return -1;

    NES2_Header *nes2 = malloc(sizeof(*nes2));
    fread(nes2, 1, 16, fp);

#ifdef DUMP_BANKS
    char filenames[16] = {'\0'};
#endif
    uint16_t base_addr = 0x8000;
    uint32_t rom_size = nes2->prg_rom_size_lsb * 0x4000;
    for (int i = 0; i < nes2->prg_rom_size_lsb; i++)
    {
#ifdef DUMP_BANKS
        sprintf(filenames, "bank%d.bin", i);
        FILE *bank = fopen(filenames, "wb");
#endif
        uint16_t bank_addr = 16 + (i * 0x4000);
        //if (i != 7)
        //{
            fseek(fp, bank_addr, SEEK_SET);
            fread(&rom[bank_addr - 16], 1, 0x4000, fp);
#ifdef DUMP_BANKS
            fwrite(&rom[bank_addr - 16], 1, 0x4000, bank);
#endif
            if (i != nes2->prg_rom_size_lsb)
            {
                memcpy(CPUGetPtr(0x8000), &rom[bank_addr - 16], 0x4000);
                memcpy(CPUGetPtr(0xC000), &rom[bank_addr - 16], 0x4000);
            }
            //else
            //    memcpy(CPUGetPtr(0xC000), &rom[bank_addr - 16], 0x4000);
            //if (i == 7)
            //{
            //    memcpy(&memory[0xC000], &rom[bank_addr - 16], 0x4000);
            //}
            //else
            //{
            //    memcpy(&memory[base_addr += 0x4000], &rom[bank_addr - 16], 0x4000);
            //}
#ifdef DUMP_BANKS
            fclose(bank);
#endif
    }
    return 0;
}
*/

void MapperMapMem(uint8_t *rom, int mapper, int prg_rom_size)
{
    switch (mapper) {
        case 0:
        {
            if (prg_rom_size == NROM_128)
            {
                uint16_t base_addr = 0x8000;

                memcpy(CPUGetPtr(0x8000), &rom[0], 0x4000);
                memcpy(CPUGetPtr(0xC000), &rom[0], 0x4000);
                /*
                if (i == 0)
                memcpy(&memory[0x8000], &rom[bank_addr - 16], 0x4000);
            else
                memcpy(&memory[0xC000], &rom[bank_addr - 16], 0x4000);
            */
            }
            break;
        }
    }   
}
