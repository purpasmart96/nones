#ifndef CPU_H
#define CPU_H

uint8_t MemRead8(uint16_t addr);
uint16_t MemRead16(uint16_t addr);

void MemWrite8(uint16_t addr, uint8_t data);
void MemWrite16(uint16_t addr, uint16_t data);
uint8_t *MemGetPtr(uint16_t addr);

#endif
