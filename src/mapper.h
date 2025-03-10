#ifndef MAPPER_H
#define MAPPER_H

uint8_t MapperRead(Cart *cart, const uint16_t addr);
void MapperWrite(Cart *cart, const uint16_t addr, uint8_t data);

#endif
