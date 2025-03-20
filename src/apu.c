#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "apu.h"
#include "cpu.h"
#include "ppu.h"
#include "loader.h"
#include "arena.h"
#include "bus.h"

uint8_t g_apu_regs[APU_IO_REGS_SIZE] = { 0 };

static void GenerateTriangleWave()
{
    //y = abs((x++ % 6) - 3);
}


void APU_Init(void)
{

}

void APU_Update(uint64_t cycles)
{

}

void APU_Reset(void)
{

}
