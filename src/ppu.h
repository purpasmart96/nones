#ifndef PPU_H
#define PPU_H

void PPU_Init(void);
void PPU_Update(uint64_t cycles);
void PPU_Reset(void);
void PPU_Write8(uint16_t addr, uint8_t data);

bool PPU_NmiTriggered(void);

#endif
