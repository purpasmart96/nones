#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "ppu.h"
#include "utils.h"

//static uint8_t sram[0x2000];
//static MemMap mappings[10];
//static int num_mappings;
//
//static uint8_t *memory = NULL;

void CartWrite8(uint16_t addr, uint8_t data)
{

}

uint8_t CartRead(const uint16_t addr)
{
    return 0;
}

void CartWriteChunk(uint16_t addr, uint16_t size)
{
    
}