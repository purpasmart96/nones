#ifndef MAPPER_H
#define MAPPER_H

typedef enum
{
    PRG_BANK_SIZE_8KIB = 0x2000,
    PRG_BANK_SIZE_16KIB = 0x4000,
    PRG_BANK_SIZE_32KIB = 0x8000,
} PrgBankSize;

uint8_t MapperReadPrgRom(Cart *cart, const uint16_t addr);
uint8_t MapperReadChrRom(Cart *cart, const uint16_t addr);
void MapperWrite(Cart *cart, const uint16_t addr, uint8_t data);

void Mmc3ClockIrqCounter(Cart *cart);
bool PollMapperIrq(void);
void MapperInit(Cart *cart);

extern Mmc1 mmc1;
extern Mmc3 mmc3;
extern UxRom ux_rom;
extern AxRom ax_rom;
extern CnRom cn_rom;
extern ColorDreams color_dreams;

#endif
