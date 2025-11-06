#ifndef MAPPER_H
#define MAPPER_H

typedef enum
{
    PRG_BANK_SIZE_8KIB = 0x2000,
    PRG_BANK_SIZE_16KIB = 0x4000,
    PRG_BANK_SIZE_32KIB = 0x8000,
} PrgBankSize;

typedef enum
{
    MAPPER_NROM,
    MAPPER_MMC1,
    MAPPER_UXROM,
    MAPPER_CNROM,
    MAPPER_MMC3,
    MAPPER_AXROM = 7,
    MAPPER_COLORDREAMS = 11,
    MAPPER_BNROM_NINJA = 34,
    MAPPER_NANJING = 163
} MapperType;

typedef enum
{
    MEM_PERM_READ,
    MEM_PERM_WRITE,
    MEM_PERM_READ_WRITE,
} MemPermissions;

typedef enum
{
    MEM_PRG_READ,
    MEM_REG_READ,
    MEM_SWRAM_READ, 
    MEM_REG_WRITE,
    MEM_SWRAM_WRITE
} MemOperation;

typedef struct
{
    uint32_t start_addr;
    uint32_t end_addr;
    MemPermissions perms;
    MemOperation op;
} MemMap;


uint8_t MapperReadPrgRom(Cart *cart, const uint16_t addr);
uint8_t MapperReadChrRom(Cart *cart, const uint16_t addr);
uint8_t MapperReadReg(Cart *cart, const uint16_t addr);
void MapperWriteChrRam(Cart *cart, const uint16_t addr, const uint8_t data);
void MapperWriteReg(Cart *cart, const uint16_t addr, uint8_t data);

void Mmc3ClockIrqCounter(Cart *cart);
bool PollMapperIrq(void);
void MapperReset(Cart *cart);
void MapperInit(Cart *cart);

extern Mmc1 mmc1;
extern Mmc3 mmc3;
extern UxRom ux_rom;
extern AxRom ax_rom;
extern CnRom cn_rom;
extern ColorDreams color_dreams;
extern Ninja ninja;
extern BnRom bn_rom;
extern Nanjing nanjing;

#endif
