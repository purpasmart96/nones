#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "mem.h"
#include "ppu.h"

typedef enum {
    NROM_INVALID,
    NROM_128,
    NROM_256
} NromType;



static int mapper_type;
//static uint32_t prg_rom_size;

uint8_t NromRead8(uint16_t addr)
{
    //return g_prg_rom[addr %  g_prg_rom_size];
}

void NromWrite(uint16_t addr, uint8_t data)
{

}


void Mmc1Read(uint16_t addr, uint8_t data)
{

}

uint32_t MapperRead(const uint16_t addr, const int size)
{
    uint32_t ret;
    //return NromRead8(addr);
    memcpy(&ret, &g_prg_rom[addr % g_prg_rom_size], size);
    return ret;
}

void MapperWrite(uint16_t addr, uint8_t data)
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

                //memcpy(CPUGetPtr(0x8000), &rom[0], 0x4000);
                //memcpy(CPUGetPtr(0xC000), &rom[0], 0x4000);
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

