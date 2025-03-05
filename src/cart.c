#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "nes.h"
#include "ppu.h"
#include "mem.h"
#include "utils.h"

static uint8_t sram[0x2000];
static MemMap mappings[10];
static int num_mappings;

static uint8_t *memory = NULL;

void CartWrite8(uint16_t addr, uint8_t data)
{

}

void CartWrite81(uint16_t addr, uint8_t data)
{

}

void CartWritechunk(uint16_t addr, uint16_t size)
{
    
}