
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "ppu.h"
#include "mem.h"
#include "utils.h"


typedef struct {
    uint8_t **ptr;
    uint16_t base;
    uint16_t size;
    bool ro;
    bool mirror;
} MemoryMap;
//static uint8_t *

static uint8_t *sys_ram = NULL;

static MemoryMap views[] =
{
	{ &sys_ram,  0x0000, 0x800,  false, false},
    { NULL,  0x0800, 0x800,  false, true},
    { NULL,  0x1000, 0x800,  false, true},
    { NULL,  0x1800, 0x800,  false, true},
};

static uint16_t MemRead16(uint16_t addr)
{
    // Extract A15, A14, A13
    //uint8_t region = (addr >> 13) & 0x7;
}