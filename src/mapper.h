#ifndef MAPPER_H
#define MAPPER_H

//typedef struct
//{
//    uint8_t (*ReadFn)(void *mapper, const uint16_t addr);
//    void (*WriteFn)(void *mapper, const uint16_t addr, uint8_t data);
//} Mapper;

uint8_t MapperReadPrgRom(Cart *cart, const uint16_t addr);
uint8_t MapperReadChrRom(Cart *cart, const uint16_t addr);
void MapperWrite(Cart *cart, const uint16_t addr, uint8_t data);

#endif
