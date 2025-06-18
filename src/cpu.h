#ifndef CPU_H
#define CPU_H

typedef enum
{
    Accumulator,
    Relative,
    Implied,
    Immediate,
    ZeroPage,
    ZeroPageX,
    ZeroPageY,
    Absolute,
    AbsoluteX,
    AbsoluteY,
    Indirect,
    IndirectX,
    IndirectY
} AddressingMode;

typedef union
{
    uint8_t raw;
    struct {
        // Carry Flag (Bit 0)
        uint8_t c : 1;
        // Zero Flag (Bit 1)
        uint8_t z : 1;
        // Interrupt Disable (Bit 2)
        uint8_t i : 1;
        // Decimal Mode (Bit 3)
        uint8_t d : 1;
        // Break Command (Bit 4)
        uint8_t b : 1;
        // Unused (Bit 5)
        uint8_t unused : 1;
        // Overflow Flag (Bit 6)
        uint8_t v : 1;
        // Negative Flag (Bit 7)
        uint8_t n : 1;
    };
} Flags;

typedef struct
{
    char debug_msg[128];
    uint64_t cycles;
    uint16_t pc;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t sp;
    Flags status;
    uint8_t nmi_pin;
    bool nmi_pending;
    bool irq_pending;
} Cpu;

typedef struct
{
    void (*InstrFn)(Cpu *cpu, AddressingMode addr_mode, bool page_cross_penalty);
    // Mnemonic (e.g., "AND", "ASL")
    const char *name;
    // Number of bytes the instruction takes
    uint8_t bytes;
    // Base cycle count
    uint8_t cycles;
    // Extra cycle(s) if page boundary is crossed
    bool page_cross_penalty;
    AddressingMode addr_mode;
} OpcodeHandler;

#define PAGE_MASK 0xFF
#define PAGE_SIZE 256
#define STACK_START 0x100
#define STACK_SIZE 0x100
#define STACK_END (STACK_START + STACK_SIZE)

#define CHECK_BIT(var, pos) ((var) & (1 << (pos)))
#define GET_NEG_BIT(operand) ((operand >> 7) & 1)
#define GET_OVERFLOW_BIT(operand) ((operand >> 6) & 1)
#define UPDATE_FLAGS_NZ(var) \
    cpu->status.n = GET_NEG_BIT(var); \
    cpu->status.z = !var

void CPU_Init(Cpu *cpu);
void CPU_Update(Cpu *cpu);
void CPU_Reset(Cpu *cpu);

#endif
