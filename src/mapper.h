#ifndef MAPPER_H
#define MAPPER_H

uint8_t MapperReadPrgRom(Cart *cart, const uint16_t addr);
uint8_t MapperReadChrRom(Cart *cart, const uint16_t addr);
void MapperWrite(Cart *cart, const uint16_t addr, uint8_t data);

void Mmc3ClockIrqCounter(Cart *cart);
bool MapperIrqTriggered(void);
void Mmc3ClockIrqCounterHack(Cart *cart);
void MapperInit(Cart *cart);

extern Mmc1 mmc1;
extern Mmc3 mmc3;
extern UxRom ux_rom;

#endif
