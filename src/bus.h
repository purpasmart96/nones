#ifndef BUS_H
#define BUS_H

typedef struct
{
    Cpu *cpu;
    void *apu;
    Ppu *ppu;

    Cart *cart;
    //uint8_t *sram;
    uint8_t *sys_ram;
} Bus;

typedef enum
{
    CPU_BUS,
    PPU_BUS
} BusType;


#endif
