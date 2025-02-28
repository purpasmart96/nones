#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#include "nes.h"
#include "cpu.h"
#include "ppu.h"

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
    PreIndexedIndirect,
    PostIndexedIndirect
} AddressingMode;

typedef union
{
    uint8_t raw;
    struct {
        uint8_t c : 1;      // Carry Flag (Bit 0)
        uint8_t z : 1;      // Zero Flag (Bit 1)
        uint8_t i : 1;      // Interrupt Disable (Bit 2)
        uint8_t d : 1;      // Decimal Mode (Bit 3)
        uint8_t b : 1;      // Break Command (Bit 4)
        uint8_t unused : 1; // Unused (Bit 5)
        uint8_t v : 1;      // Overflow Flag (Bit 6)
        uint8_t n : 1;      // Negative Flag (Bit 7)
    };
} Flags;

typedef struct
{
    uint64_t cycles;
    uint16_t pc;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t sp;
    Flags status;
} _6502_State;

#define PAGE_MASK 0xFF
#define PAGE_SIZE 256
// MEMORY_SIZE = PAGE_SIZE * PAGE_SIZE
#define MEMORY_SIZE 0x10000
static uint8_t memory[MEMORY_SIZE];

#define STACK_START 0x100
#define STACK_SIZE 0x100
#define STACK_END (STACK_START + STACK_SIZE)

#define CHECK_BIT(var, pos) ((var) & (1 << (pos)))
#define GET_NEG_BIT(operand) ((operand >> 7) & 1)
#define GET_OVERFLOW_BIT(operand) ((operand >> 6) & 1)

#ifndef DISABLE_DEBUG
#define DEBUG_LOG(fmt, ...) \
do { fprintf(stdout, fmt, __VA_ARGS__); } while (0)
#else
#define DEBUG_LOG(fmt, ...) ((void)0)
#endif

#define UNUSED(var) ((void)(var))

uint8_t MemRead8(uint16_t addr)
{
    return memory[addr];
}

uint16_t MemRead16(uint16_t addr)
{
    if (addr & 1)
    {
        return *(uint16_t*)&memory[addr];
    }

    uint16_t ret;
    memcpy(&ret, &memory[addr], sizeof(uint16_t));
    return ret;
}

void MemWrite8(uint16_t addr, uint8_t data)
{
    memory[addr] = data;
}

void MemWrite16(uint16_t addr, uint16_t data)
{
    memcpy(&memory[addr], &data, sizeof(uint16_t));
}

uint8_t *MemGetPtr(uint16_t addr)
{
    return &memory[addr];
}

static uint32_t MemRead32(uint16_t addr)
{
    return *(uint32_t*)&memory[addr];
}

void StackPush(_6502_State *state, uint8_t data)
{
    memory[STACK_START + state->sp--] = data;
}

// Retrieve the value on the top of the stack and then pop it
uint8_t StackPull(_6502_State *state)
{
    return memory[STACK_START + (++state->sp)];
}

static bool InsidePage(uint16_t src_addr, uint16_t dst_addr)
{
    // return ((src_addr / PAGE_SIZE) == (dst_addr / PAGE_SIZE));
    // Using faster version
    return ((src_addr & 0xFF00) == (dst_addr & 0xFF00));
}

// PC += 2 
static uint16_t GetAbsoluteAddr(_6502_State *state)
{
    uint8_t addr_low = memory[++state->pc];
    uint8_t addr_high = memory[++state->pc];
    return (uint16_t)addr_high << 8 | addr_low;
}

// PC += 2 
static uint16_t GetAbsoluteXAddr(_6502_State *state)
{
    uint16_t base_addr = MemRead16(state->pc + 1);
    uint16_t final_addr = base_addr + state->x;

    // Extra cycle if pages are different
    if (!InsidePage(base_addr, final_addr))
        state->cycles++;

    state->pc += 2;
    return final_addr;
}

static uint16_t GetAbsoluteXAddr2(_6502_State *state, bool add_cycle)
{
    uint16_t base_addr = MemRead16(state->pc + 1);
    uint16_t final_addr = base_addr + state->x;

    // Apply extra cycle only if required
    if (add_cycle && !InsidePage(base_addr, final_addr))
        state->cycles++;

    state->pc += 2;
    return final_addr;
}

static uint16_t GetAbsoluteYAddr(_6502_State *state, bool add_cycle)
{
    uint16_t base_addr = MemRead16(state->pc + 1);
    uint16_t final_addr = base_addr + state->y;

    // Apply extra cycle only if required
    if (add_cycle && !InsidePage(base_addr, final_addr))
        state->cycles++;

    state->pc += 2;
    return final_addr;
}

// PC += 1
static uint8_t GetZPAddr(_6502_State *state)
{
    return memory[++state->pc];
}

// PC += 1
static uint16_t GetZPIndexedAddr(_6502_State *state, uint8_t reg)
{
    uint8_t zp_addr = memory[++state->pc];

    // Wrap in zero-page
    return (zp_addr + reg) & PAGE_MASK;
}

// PC += 2
static uint16_t GetIndirectAddr(_6502_State *state)
{
    uint8_t ptr_low = memory[++state->pc];
    uint8_t ptr_high = memory[++state->pc];
    
    uint16_t ptr = (uint16_t)ptr_high << 8 | ptr_low;
    uint16_t new_pc;
    // **6502 Page Boundary Bug** (If ptr is at 0xXXFF, high byte comes from 0xXX00, not 0xXXFF+1)
    if ((ptr & 0xFF) == 0xFF)
    {
        new_pc = (uint16_t)memory[ptr] | ((uint16_t)memory[ptr & 0xFF00] << 8);
    }
    else
    {
        new_pc = (uint16_t)memory[ptr] | ((uint16_t)memory[ptr + 1] << 8);
    }

    return new_pc;
}

// PC += 1
static uint16_t GetPostIndexedIndirectAddr(_6502_State *state)
{
    uint8_t zp_addr = memory[++state->pc]; 
    uint8_t addr_low = memory[zp_addr];
    uint8_t addr_high = memory[(zp_addr + 1) & PAGE_MASK]; // Fetch high (with zero-page wraparound)

    return (uint16_t)addr_high << 8 | addr_low;
}

// PC += 1
static uint16_t GetPostIndexedIndirectAddr2(_6502_State *state, bool page_cycle)
{
    uint8_t zp_addr = memory[++state->pc]; 
    uint8_t addr_low = memory[zp_addr];
    uint8_t addr_high = memory[(zp_addr + 1) & PAGE_MASK]; // Fetch high (with zero-page wraparound)

    uint16_t base_addr = (uint16_t)addr_high << 8 | addr_low;
    uint16_t final_addr = base_addr + state->y;

    // Extra cycle if in different pages
    if (page_cycle && !InsidePage(base_addr, final_addr))
        state->cycles++;

    return final_addr;
}

// PC += 1
static uint16_t GetPreIndexedIndirectAddr(_6502_State *state, uint8_t reg)
{
    uint8_t zp_addr = memory[++state->pc];
    uint8_t effective_ptr = (zp_addr + reg) & PAGE_MASK; // Wrap in zero-page
    uint8_t addr_low = memory[effective_ptr];
    uint8_t addr_high = memory[(effective_ptr + 1) & PAGE_MASK]; // Wrap in zero-page
    return (uint16_t)addr_high << 8 | addr_low;
}

static void CompareRegAndSetFlags(_6502_State *state, uint8_t reg, uint8_t operand)
{
    uint8_t result = reg - operand;
    // Negative flag (bit 7)
    state->status.n = GET_NEG_BIT(result);
    // Zero flag (is result zero?)
    state->status.z = (result == 0) ? 1 : 0;
    // Update the Carry Flag (C)
    state->status.c = (reg >= operand) ? 1 : 0;
}

static void RotateOneLeft(_6502_State *state, uint8_t *operand)
{
    uint8_t old_carry = state->status.c;
    // Store bit 7 in carry before rotating
    state->status.c = (*operand >> 7) & 1;
    // Shift all bits left one position and insert old carry into bit 0
    *operand = (*operand << 1) | old_carry;
    // Update status flags
    state->status.n = GET_NEG_BIT(*operand);     // Negative flag (bit 7)
    state->status.z = (*operand == 0) ? 1 : 0;   // Zero flag (is result zero?)
}

static void RotateOneRight(_6502_State *state, uint8_t *operand)
{
    uint8_t old_carry = state->status.c;
    // Store bit 0 in carry before rotating
    state->status.c = *operand & 1;
    // Shift all bits right one position and insert old carry into bit 7
    *operand = (*operand >> 1) | (old_carry << 7);
    // Update status flags
    state->status.n = GET_NEG_BIT(*operand);     // Negative flag (bit 7)
    state->status.z = (*operand == 0) ? 1 : 0;   // Zero flag (is result zero?)
}

static void ShiftOneRight(_6502_State *state, uint8_t *operand)
{
    // Store bit 0 in carry before shifting
    state->status.c = *operand & 1;
    // Shift all bits left by one position
    *operand >>= 1;
    // Update status flags
    state->status.n = 0;    // Clear N flag 
    state->status.z = (*operand == 0) ? 1 : 0;   // Zero flag (is A zero?)
}

static void ShiftOneLeft(_6502_State *state, uint8_t *operand)
{
    // Store bit 7 in carry before shifting
    state->status.c = (*operand >> 7) & 1;
    // Shift all bits left by one position
    *operand <<= 1;
    // Update status flags
    state->status.n = GET_NEG_BIT(*operand);     // Negative flag (bit 7)
    state->status.z = (*operand == 0) ? 1 : 0;   // Zero flag (is A zero?)
}

// ADC only uses the A register (Accumulator)
static void AddWithCarry(_6502_State *state, uint8_t operand)
{
    uint16_t sum = state->a + operand + state->status.c;

    // Set Carry Flag (C) - Set if result is > 255 (unsigned overflow)
    state->status.c = (sum > UINT8_MAX) ? 1 : 0;

    // Set Overflow Flag (V) - Detect signed overflow
    state->status.v = (~(state->a ^ operand) & (state->a ^ sum) & 0x80) ? 1 : 0;

    // Store result
    state->a = (uint8_t)sum;
    
    state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
    state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
    DEBUG_LOG("Operand %x\n", operand);
}

static void BitwiseAnd(_6502_State *state, uint8_t operand)
{
    state->a &= operand;

    state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
    state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
}

static uint8_t GetGroup(uint8_t opcode)
{
    return opcode >> 6 & 3;
}

static uint8_t GetInstrInGroup(uint8_t opcode)
{
    return opcode & 7;
}

// Todo
static uint8_t GetAddressMode(uint8_t opcode)
{
    uint8_t group = GetGroup(opcode);
    uint8_t addr_mode = opcode >> 3 & 7;
    return addr_mode;
}

uint8_t GetOperandFromMem(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    switch (addr_mode)
    {
        case Accumulator:
            return state->a;
        case Relative:
        case Immediate:
            return memory[++state->pc];
        case ZeroPage:
            return memory[GetZPAddr(state)];
        case ZeroPageX:
            return memory[GetZPIndexedAddr(state, state->x)];
        case ZeroPageY:
            return memory[GetZPIndexedAddr(state, state->y)];
        case Absolute:
            return memory[GetAbsoluteAddr(state)];
        case AbsoluteX:
            return memory[GetAbsoluteXAddr2(state, page_cycle)];
            //return memory[GetAbsoluteAddr(state) + state->x];
        case AbsoluteY:
            return memory[GetAbsoluteYAddr(state, page_cycle)];
            //return memory[GetAbsoluteAddr(state) + state->y];
        case PreIndexedIndirect:
            return memory[GetPreIndexedIndirectAddr(state, state->x)];
        case PostIndexedIndirect:
            return memory[GetPostIndexedIndirectAddr2(state, page_cycle)];
        case Implied:
        case Indirect:
            break;
    }

    return -1;
}

// Used by STA/STY/STX instrs
static void SetOperandToMem(_6502_State *state, AddressingMode addr_mode, uint8_t operand)
{
    switch (addr_mode)
    {
        case Accumulator:
        case Relative:
        case Immediate:
            memory[++state->pc] = operand;
            break;
        case ZeroPage:
            memory[GetZPAddr(state)] = operand;
            break;
        case ZeroPageX:
            memory[GetZPIndexedAddr(state, state->x)] = operand;
            break;
        case ZeroPageY:
            memory[GetZPIndexedAddr(state, state->y)] = operand;
            break;
        case Absolute:
            memory[GetAbsoluteAddr(state)] = operand;
            break;
        case AbsoluteX:
            memory[GetAbsoluteAddr(state) + state->x] = operand;
            break;
        case AbsoluteY:
            memory[GetAbsoluteAddr(state) + state->y] = operand;
            break;
        case PreIndexedIndirect:
            memory[GetPreIndexedIndirectAddr(state, state->x)] = operand;
            break;
        case PostIndexedIndirect:
            memory[GetPostIndexedIndirectAddr2(state, false)] = operand;
            break;
        case Implied:
        case Indirect:
            break;
        default:
            printf("Unknown or invalid adddress mode!: %d\n", addr_mode);
            break;
    }
}

uint16_t GetJMPAddr(_6502_State *state, AddressingMode addr_mode)
{
    switch (addr_mode)
    {
        case Absolute:
            return GetAbsoluteAddr(state);
        case Indirect:
            return GetIndirectAddr(state);
        default:
            return -1;
    }

    return -1;
}

uint8_t *GetOperandPtrFromMem(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    switch (addr_mode)
    {
        case Accumulator:
            return &state->a;
        case Relative:
        case Immediate:
            return &memory[++state->pc];
        case ZeroPage:
            return &memory[GetZPAddr(state)];
        case ZeroPageX:
            return &memory[GetZPIndexedAddr(state, state->x)];
        case ZeroPageY:
            return &memory[GetZPIndexedAddr(state, state->y)];
        case Absolute:
            return &memory[GetAbsoluteAddr(state)];
        case AbsoluteX:
            return &memory[GetAbsoluteXAddr2(state, page_cycle)];
            //return &memory[GetAbsoluteAddr(state) + state->x];
        case AbsoluteY:
            return &memory[GetAbsoluteYAddr(state, page_cycle)];
            //return &memory[GetAbsoluteAddr(state) + state->y];
        case PreIndexedIndirect:
            return &memory[GetPreIndexedIndirectAddr(state, state->x)];
        case PostIndexedIndirect:
            return &memory[GetPostIndexedIndirectAddr2(state, page_cycle)];
        case Implied:
        case Indirect:
            return NULL;
    
    }

    return NULL;
}

static void ADC_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    AddWithCarry(state, operand);
    state->pc++;
}

static void AND_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    BitwiseAnd(state, operand);
    state->pc++;
}

static void ASL_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t *operand = GetOperandPtrFromMem(state, addr_mode, page_cycle);
    ShiftOneLeft(state, operand);
    state->pc++;
}

static void BCC_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    int8_t offset = (int8_t)memory[++state->pc];
    if (!state->status.c)
    {
        // Extra cycle if the branch crosses a page boundary
        if (!InsidePage(state->pc, state->pc + offset))
            state->cycles++;

        state->pc += offset;
        state->cycles++;
        DEBUG_LOG("PC Offset %d\n", offset);
    }
    state->pc++;
}

static void BCS_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    int8_t offset = (int8_t)memory[++state->pc];
    if (state->status.c)
    {
        // Extra cycle if the branch crosses a page boundary
        if (!InsidePage(state->pc, state->pc + offset))
            state->cycles++;

        state->pc += offset;
        state->cycles++;
        DEBUG_LOG("PC Offset %d\n", offset);
    }
    state->pc++;
}

static void BEQ_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    int8_t offset = (int8_t)memory[++state->pc];
    if (state->status.z)
    {
        // Extra cycle if the branch crosses a page boundary
        if (!InsidePage(state->pc, state->pc + offset))
            state->cycles++;

        state->pc += offset;
        state->cycles++;
        DEBUG_LOG("PC Offset %d\n", offset);
    }
    state->pc++;
}

static void BIT_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    state->status.n = GET_NEG_BIT(operand);
    state->status.v = GET_OVERFLOW_BIT(operand);
    state->status.z = !(state->a & operand);
    state->pc++;
}

static void BMI_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);
    (void)(page_cycle);

    int8_t offset = (int8_t)memory[++state->pc];
    if (state->status.n)
    {
        // Extra cycle if the branch crosses a page boundary
        if (!InsidePage(state->pc, state->pc + offset))
            state->cycles++;

        state->pc += offset;
        state->cycles++;
        DEBUG_LOG("PC Offset %d\n", offset);
    }
    state->pc++;
}

static void BNE_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);
    (void)(page_cycle);

    int8_t offset = (int8_t)memory[++state->pc];
    if (!state->status.z)
    {
        // Extra cycle if the branch crosses a page boundary
        if (!InsidePage(state->pc, state->pc + offset))
            state->cycles++;

        state->pc += offset;
        state->cycles++;
        DEBUG_LOG("PC Offset %d\n", offset);
    }
    state->pc++;
}

static void BPL_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);
    (void)(page_cycle);

    int8_t offset = (int8_t)memory[++state->pc];
    if (!state->status.n)
    {
        // Extra cycle if the branch crosses a page boundary
        if (!InsidePage(state->pc, state->pc + offset))
            state->cycles++;

        state->pc += offset;
        state->cycles++;
        DEBUG_LOG("PC Offset %d\n", offset);
    }
    state->pc++;
}

static void BRK_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    // Implied BRK	00	1	7
    state->pc += 2;
    StackPush(state, (state->pc >> 8) & 0xFF);
    StackPush(state, state->pc & 0xFF);

    Flags status = state->status;
    status.b = true;
    status.unused = true;
    StackPush(state, status.raw);

    state->status.i = true;
    // Load IRQ/BRK vector ($FFFE-$FFFF) into PC
    //uint8_t vector_low = memory[0xFFFE];
    //uint8_t vector_high = memory[0xFFFF];
    //state->pc = (vector_high << 8) | vector_low;
    state->pc = MemRead16(0xFFFE);

    DEBUG_LOG("Jumping to IRQ vector at 0x%X\n", state->pc);
    //state->cycles += 7;
    //skip_next_instr = true;
}

static void BVC_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    int8_t offset = (int8_t)memory[++state->pc];
    if (!state->status.v)
    {
        // Extra cycle if the branch crosses a page boundary
        if (!InsidePage(state->pc, state->pc + offset))
            state->cycles++;

        state->pc += offset;
        state->cycles++;
        DEBUG_LOG("PC Offset %d\n", offset);
    }
    state->pc++;
}

static void BVS_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    int8_t offset = (int8_t)memory[++state->pc];
    if (state->status.v)
    {
        // Extra cycle if the branch crosses a page boundary
        if (!InsidePage(state->pc, state->pc + offset))
            state->cycles++;

        state->pc += offset;
        state->cycles++;
        DEBUG_LOG("PC Offset %d\n", offset);
    }
    state->pc++;
}

static void CLC_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    state->status.c = 0;
    state->pc++;
}

static void CLD_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    state->status.d = 0;
    state->pc++;
}

static void CLI_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    state->status.i = 0;
    state->pc++;
}

static void CLV_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    state->status.v = 0;
    state->pc++;
}

static void CMP_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    CompareRegAndSetFlags(state, state->a, operand);
    state->pc++;
}

static void CPX_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    CompareRegAndSetFlags(state, state->x, operand);
    state->pc++;
}

static void CPY_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    CompareRegAndSetFlags(state, state->y, operand);
    state->pc++;
}

static void DEC_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t *operand = GetOperandPtrFromMem(state, addr_mode, page_cycle);

    *operand -= 1;

    state->status.n = GET_NEG_BIT(*operand);  // Negative flag (bit 7)
    state->status.z = (*operand == 0) ? 1 : 0;    // Zero flag (is value zero?)

    state->pc++;
}

static void DEX_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    state->x--;
    state->status.n = GET_NEG_BIT(state->x);  // Negative flag (bit 7)
    state->status.z = (state->x == 0) ? 1 : 0;    // Zero flag (is X zero?)

    state->pc++;
}

static void DEY_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    state->y--;
    state->status.n = GET_NEG_BIT(state->y);  // Negative flag (bit 7)
    state->status.z = (state->y == 0) ? 1 : 0;    // Zero flag (is X zero?)

    state->pc++;
}

static void EOR_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    state->a ^= operand;
    state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
    state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
    state->pc++;
}

static void INC_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t *operand = GetOperandPtrFromMem(state, addr_mode, page_cycle);

    *operand += 1;

    state->status.n = GET_NEG_BIT(*operand);  // Negative flag (bit 7)
    state->status.z = (*operand == 0) ? 1 : 0;    // Zero flag (is value zero?)

    state->pc++;
}

static void INX_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    state->x++;
    state->status.n = GET_NEG_BIT(state->x);  // Negative flag (bit 7)
    state->status.z = (state->x == 0) ? 1 : 0;    // Zero flag (is value zero?)

    state->pc++;
}

static void INY_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    state->y++;
    state->status.n = GET_NEG_BIT(state->y);  // Negative flag (bit 7)
    state->status.z = (state->y == 0) ? 1 : 0;    // Zero flag (is value zero?)

    state->pc++;
}

static void JMP_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    state->pc = GetJMPAddr(state, addr_mode);
}

static void JSR_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    uint8_t pc_low = memory[++state->pc];
    uint8_t pc_high = memory[++state->pc];

    StackPush(state, (state->pc >> 8) & 0xFF);
    StackPush(state, state->pc & 0xFF);

    uint16_t new_pc = (uint16_t)pc_high << 8 | pc_low;
    state->pc = new_pc;
}

static void LDA_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    state->a = operand;

    // Update status flags
    state->status.n = GET_NEG_BIT(state->a);  // Negative flag (bit 7)
    state->status.z = (state->a == 0) ? 1 : 0; // Zero flag (is A zero?)
    state->pc++;
}

static void LDX_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    state->x = operand;

    // Update status flags
    state->status.n = GET_NEG_BIT(state->x);  // Negative flag (bit 7)
    state->status.z = (state->x == 0) ? 1 : 0; // Zero flag (is A zero?)
    state->pc++;
}

static void LDY_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    state->y = operand;

    // Update status flags
    state->status.n = GET_NEG_BIT(state->y);  // Negative flag (bit 7)
    state->status.z = (state->y == 0) ? 1 : 0; // Zero flag (is A zero?)
    state->pc++;
}

static void LSR_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t *operand = GetOperandPtrFromMem(state, addr_mode, page_cycle);
    ShiftOneRight(state, operand);
    state->pc++;
}

static void NOP_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    state->pc++;
}

static void ORA_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    state->a |= operand;
    state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
    state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
    state->pc++;
}

static void PHA_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    StackPush(state, state->a);
    state->pc++;
}

static void PHP_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    Flags status = state->status;
    status.b = true;
    status.unused = true;
    StackPush(state, status.raw);
    state->pc++;
}

static void PLA_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    state->a = StackPull(state);
    state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
    state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is Y zero?)
    state->pc++;
}

static void PLP_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    uint8_t status_raw = StackPull(state);
    Flags status = {.raw = status_raw};
    // Ignore bit for break and 5th bit
    state->status.c = status.c;
    state->status.d = status.d;
    state->status.i = status.i;
    state->status.n = status.n;
    state->status.v = status.v;
    state->status.z = status.z;

    state->pc++;
}

static void ROL_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t *operand = GetOperandPtrFromMem(state, addr_mode, page_cycle);
    RotateOneLeft(state, operand);
    state->pc++;
}

static void ROR_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t *operand = GetOperandPtrFromMem(state, addr_mode, page_cycle);
    RotateOneRight(state, operand);
    state->pc++;
}

static void RTI_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    uint8_t status_raw = StackPull(state);
    Flags status = {.raw = status_raw};
    // Ignore bit for break and 5th bit
    state->status.c = status.c;
    state->status.d = status.d;
    state->status.i = status.i;
    state->status.n = status.n;
    state->status.v = status.v;
    state->status.z = status.z;

    uint8_t pc_low = StackPull(state);
    uint8_t pc_high = StackPull(state);

    uint16_t new_pc = (uint16_t)pc_high << 8 | pc_low;
    state->pc = new_pc;
}

static void RTS_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    uint8_t pc_low = StackPull(state);
    uint8_t pc_high = StackPull(state);

    uint16_t new_pc = (uint16_t)pc_high << 8 | pc_low;
    state->pc = new_pc;

    state->pc++;
}

static void SBC_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    uint16_t temp = state->a - operand - (1 - state->status.c);

    // Carry is set if no borrow occurs
    state->status.c = (state->a >= operand + (1 - state->status.c)) ? 1 : 0;

    // Set Overflow Flag: Checks signed overflow
    state->status.v = ((state->a ^ operand) & (state->a ^ temp) & 0x80) ? 1 : 0;
    state->a = (uint8_t)temp;

    state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
    state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
    state->pc++;
}

static void SEC_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    state->status.c = 1;
    state->pc++;
}

static void SED_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    state->status.d = 1;
    state->pc++;
}

static void SEI_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    state->status.i = 1;
    state->pc++;
}

static void STA_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    SetOperandToMem(state, addr_mode, state->a);
    state->pc++;
}

static void STX_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    SetOperandToMem(state, addr_mode, state->x);
    state->pc++;
}

static void STY_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    SetOperandToMem(state, addr_mode, state->y);
    state->pc++;
}

// Transfer Accumulator to Index X
static void TAX_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    state->x = state->a;
    state->status.n = GET_NEG_BIT(state->x);  // Negative flag (bit 7)
    state->status.z = (state->x == 0) ? 1 : 0; // Zero flag (is x zero?)

    state->pc++;
}

// Transfer Accumulator to Index Y
static void TAY_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    state->y = state->a;
    state->status.n = GET_NEG_BIT(state->y);  // Negative flag (bit 7)
    state->status.z = (state->y == 0) ? 1 : 0; // Zero flag (is x zero?)

    state->pc++;
}

// Transfer Stack Pointer to Index X
static void TSX_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    state->x = state->sp;
    state->status.n = GET_NEG_BIT(state->x);  // Negative flag (bit 7)
    state->status.z = (state->x == 0) ? 1 : 0; // Zero flag (is x zero?)

    state->pc++;
}

// Transfer Stack Pointer to Accumulator
static void TSA_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    state->a = state->sp;
    state->status.n = GET_NEG_BIT(state->a);  // Negative flag (bit 7)
    state->status.z = (state->a == 0) ? 1 : 0; // Zero flag (is x zero?)

    state->pc++;
}

// Transfer Index X to Accumulator
static void TXA_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    state->a = state->x;
    state->status.n = GET_NEG_BIT(state->a);  // Negative flag (bit 7)
    state->status.z = (state->a == 0) ? 1 : 0; // Zero flag (is x zero?)

    state->pc++;
}

// Transfer Index X to Stack Register
static void TXS_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    state->sp = state->x;

    state->pc++;
}

// Transfer Index Y to Accumulator
static void TYA_Instr(_6502_State *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    (void)(addr_mode);

    state->a = state->y;
    state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
    state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)

    state->pc++;
}

typedef struct {
    void (*InstrFn)(_6502_State *state, AddressingMode addr_mode, bool page_cross_penalty);  // Function pointer to execute opcode
    const char *name;                     // Mnemonic (e.g., "AND", "ASL")
    uint8_t bytes;                         // Number of bytes the instruction takes
    uint8_t cycles;                        // Base cycle count
    bool page_cross_penalty;               // Extra cycle if page boundary is crossed
    AddressingMode addr_mode;
} OpcodeHandler;

static const OpcodeHandler opcodes[256] =
{
    [0x00] = { BRK_Instr, "BRK", 1, 7, false, Implied  },
    [0x01] = { ORA_Instr, "ORA (ind,X)", 2, 6, false, PreIndexedIndirect },
    [0x05] = { ORA_Instr, "ORA zp", 2, 3, false, ZeroPage },
    [0x06] = { ASL_Instr, "ASL zp", 2, 5, false, ZeroPage },
    [0x08] = { PHP_Instr, "PHP", 1, 3, false, Implied },
    [0x09] = { ORA_Instr, "ORA #imm", 2, 2, false, Immediate },
    [0x0A] = { ASL_Instr, "ASL A", 1, 2, false, Accumulator },
    [0x0D] = { ORA_Instr, "ORA abs", 3, 4, false, Absolute },
    [0x0E] = { ASL_Instr, "ASL abs", 3, 6, false, Absolute },

    [0x10] = { BPL_Instr, "BPL rel", 2, 2, true, Relative },
    [0x11] = { ORA_Instr, "ORA (ind),Y", 2, 5, true, PostIndexedIndirect},
    [0x15] = { ORA_Instr, "ORA zp,X", 2, 4, false, ZeroPageX },
    [0x16] = { ASL_Instr, "ASL zp,X", 2, 6, false, ZeroPageX },
    [0x18] = { CLC_Instr, "CLC", 1, 2, false, Implied},
    [0x19] = { ORA_Instr, "ORA abs,Y", 3, 4, true, AbsoluteY},
    [0x1D] = { ORA_Instr, "ORA abs,X", 3, 4, true, AbsoluteX},
    [0x1E] = { ASL_Instr, "ASL abs,X", 3, 7, false, AbsoluteX },

    [0x20] = { JSR_Instr, "JSR abs", 3, 6, false, Absolute },
    [0x21] = { AND_Instr, "AND (ind,X)", 2, 6, false, PreIndexedIndirect },
    [0x24] = { BIT_Instr, "BIT zp", 2, 3, false, ZeroPage },
    [0x25] = { AND_Instr, "AND zp", 2, 3, false, ZeroPage },
    [0x26] = { ROL_Instr, "ROL zp", 2, 5, false, ZeroPage },
    [0x28] = { PLP_Instr, "PLP", 1, 4, false, Implied },
    [0x29] = { AND_Instr, "AND #imm", 2, 2, false, Immediate },
    [0x2A] = { ROL_Instr, "ROL A", 1, 2, false, Accumulator },
    [0x2C] = { BIT_Instr, "BIT abs", 3, 4, false, Absolute },
    [0x2D] = { AND_Instr, "AND abs", 3, 4, false, Absolute },
    [0x2E] = { ROL_Instr, "ROL abs", 3, 6, false, Absolute },

    [0x30] = { BMI_Instr, "BMI rel", 2, 2, true, Relative },
    [0x31] = { AND_Instr, "AND (ind),Y", 2, 5, true, PostIndexedIndirect },
    [0x35] = { AND_Instr, "AND zp,X", 2, 4, false, ZeroPageX },
    [0x36] = { ROL_Instr, "ROL zp,X", 2, 6, false, ZeroPageX },
    [0x38] = { SEC_Instr, "SEC", 1, 2, false, Implied },
    [0x39] = { AND_Instr, "AND abs,Y", 3, 4, true, AbsoluteY },
    [0x3D] = { AND_Instr, "AND abs,X", 3, 4, true, AbsoluteX},
    [0x3E] = { ROL_Instr, "ROL abs,X", 3, 7, false, AbsoluteX},

    [0x40] = { RTI_Instr, "RTI", 1, 6, false, Implied },
    [0x41] = { EOR_Instr, "EOR (ind,X)", 2, 6, false, PreIndexedIndirect },
    [0x45] = { EOR_Instr, "EOR zp", 2, 3, false, ZeroPage },
    [0x46] = { LSR_Instr, "LSR zp", 2, 5, false, ZeroPage },
    [0x48] = { PHA_Instr, "PHA", 1, 3, false, Implied },
    [0x49] = { EOR_Instr, "EOR #imm", 2, 2, false, Immediate },
    [0x4A] = { LSR_Instr, "LSR A", 1, 2, false, Accumulator },
    [0x4C] = { JMP_Instr, "JMP abs", 3, 3, false, Absolute},
    [0x4D] = { EOR_Instr, "EOR abs", 3, 4, false, Absolute},
    [0x4E] = { LSR_Instr, "LSR abs", 3, 6, false, Absolute},

    [0x50] = { BVC_Instr, "BVC rel", 2, 2, true, Relative },
    [0x51] = { EOR_Instr, "EOR (ind),Y", 2, 5, true, PostIndexedIndirect },
    [0x55] = { EOR_Instr, "EOR zp,X", 2, 4, false, ZeroPageX },
    [0x56] = { LSR_Instr, "LSR zp,X", 2, 6, false, ZeroPageX},
    [0x58] = { CLI_Instr, "CLI", 1, 2, false, Implied},
    [0x59] = { EOR_Instr, "EOR abs,Y", 3, 4, true, AbsoluteY},
    [0x5D] = { EOR_Instr, "EOR abs,X", 3, 4, true, AbsoluteX },
    [0x5E] = { LSR_Instr, "LSR abs,X", 3, 7, false, AbsoluteX },

    [0x60] = { RTS_Instr, "RTS", 1, 6, false, Implied },
    [0x61] = { ADC_Instr, "ADC (ind,X)", 2, 6, false, PreIndexedIndirect },
    [0x65] = { ADC_Instr, "ADC zp", 2, 3, false, ZeroPage },
    [0x66] = { ROR_Instr, "ROR zp", 2, 5, false, ZeroPage },
    [0x68] = { PLA_Instr, "PLA", 1, 4, false, Implied },
    [0x69] = { ADC_Instr, "ADC #imm", 2, 3, false, Immediate },
    [0x6A] = { ROR_Instr, "ROR A", 1, 2, false, Accumulator },
    [0x6C] = { JMP_Instr, "JMP (ind)", 3, 5, false, Indirect },
    [0x6D] = { ADC_Instr, "ADC abs", 3, 4, false, Absolute },
    [0x6E] = { ROR_Instr, "ROR abs", 3, 6, false, Absolute },

    [0x70] = { BVS_Instr, "BVS rel", 2, 2, true, Relative },
    [0x71] = { ADC_Instr, "ADC (ind),Y", 2, 5, true, PostIndexedIndirect },
    [0x75] = { ADC_Instr, "ADC zp,X", 2, 4, false, ZeroPageX },
    [0x76] = { ROR_Instr, "ROR zp,X", 2, 6, false, ZeroPageX },
    [0x78] = { SEI_Instr, "SEI", 1, 2, false, Implied },
    [0x79] = { ADC_Instr, "ADC abs,Y", 3, 4, true, AbsoluteY },
    [0x7D] = { ADC_Instr, "ADC abs,X", 3, 4, true, AbsoluteX },
    [0x7E] = { ROR_Instr, "ROR abs,X", 3, 7, false, AbsoluteX },

    [0x81] = { STA_Instr, "STA (ind,X)", 2, 6, false, PreIndexedIndirect },
    [0x84] = { STY_Instr, "STY zp", 2, 3, false, ZeroPage },
    [0x85] = { STA_Instr, "STA zp", 2, 3, false, ZeroPage },
    [0x86] = { STX_Instr, "STX zp", 2, 3, false, ZeroPage },
    [0x88] = { DEY_Instr, "DEY", 1, 2, false, Implied },
    [0x8A] = { TXA_Instr, "TXA", 1, 2, false, Implied },
    [0x8C] = { STY_Instr, "STY abs", 3, 4, false, Absolute },
    [0x8D] = { STA_Instr, "STA abs", 3, 4, false, Absolute },
    [0x8E] = { STX_Instr, "STX abs", 3, 4, false, Absolute },

    [0x90] = { BCC_Instr, "BCC rel", 2, 2, true, Relative },
    [0x91] = { STA_Instr, "STA (ind),Y", 2, 6, false, PostIndexedIndirect },
    [0x94] = { STY_Instr, "STY zp,X", 2, 4, false, ZeroPageX },
    [0x95] = { STA_Instr, "STA zp,X", 2, 4, false, ZeroPageX },
    [0x96] = { STX_Instr, "STX zp,Y", 2, 4, false, ZeroPageY },
    [0x98] = { TYA_Instr, "TYA", 1, 2, false, Implied },
    [0x99] = { STA_Instr, "STA abs,Y", 3, 5, false, AbsoluteY },
    [0x9A] = { TXS_Instr, "TXS", 1, 2, false, Implied },
    [0x9D] = { STA_Instr, "STA abs,X", 3, 5, false, AbsoluteX },

    [0xA0] = { LDY_Instr, "LDY #imm", 2, 2, false, Immediate },
    [0xA1] = { LDA_Instr, "LDA (ind,X)", 2, 6, false, PreIndexedIndirect },
    [0xA2] = { LDX_Instr, "LDX #imm", 2, 2, false, Immediate },
    [0xA4] = { LDY_Instr, "LDY zp", 2, 3, false, ZeroPage },
    [0xA5] = { LDA_Instr, "LDA zp", 2, 3, false, ZeroPage },
    [0xA6] = { LDX_Instr, "LDX zp", 2, 3, false, ZeroPage },
    [0xA8] = { TAY_Instr, "TAY", 1, 2, false, Implied },
    [0xA9] = { LDA_Instr, "LDA #imm", 2, 2, false, Immediate },
    [0xAA] = { TAX_Instr, "TAX", 1, 2, false, Implied },
    [0xAC] = { LDY_Instr, "LDY abs", 3, 4, false, Absolute },
    [0xAD] = { LDA_Instr, "LDA abs", 3, 4, false, Absolute },
    [0xAE] = { LDX_Instr, "LDX abs", 3, 4, false, Absolute },

    [0xB0] = { BCS_Instr, "BCS rel", 2, 2, true, Relative },
    [0xB1] = { LDA_Instr, "LDA (ind),Y", 2, 5, true, PostIndexedIndirect },
    [0xB4] = { LDY_Instr, "LDY zp,X", 2, 4, false, ZeroPageX },
    [0xB5] = { LDA_Instr, "LDA zp,X", 2, 4, false, ZeroPageX },
    [0xB6] = { LDX_Instr, "LDX zp,Y", 2, 4, false, ZeroPageY },
    [0xB8] = { CLV_Instr, "CLV", 1, 2, false, Implied },
    [0xB9] = { LDA_Instr, "LDA abs,Y", 3, 4, true, AbsoluteY },
    [0xBA] = { TSX_Instr, "TSX", 1, 2, false, Implied },
    [0xBC] = { LDY_Instr, "LDY abs,X", 3, 4, true, AbsoluteX },
    [0xBD] = { LDA_Instr, "LDA abs,X", 3, 4, true, AbsoluteX },
    [0xBE] = { LDX_Instr, "LDX abs,Y", 3, 4, true, AbsoluteY },

    [0xC0] = { CPY_Instr, "CPY #imm", 2, 2, false, Immediate },
    [0xC1] = { CMP_Instr, "CMP (ind,X)", 2, 6, false, PreIndexedIndirect },
    [0xC4] = { CPY_Instr, "CPY zp", 2, 3, false, ZeroPage },
    [0xC5] = { CMP_Instr, "CMP zp", 2, 3, false, ZeroPage },
    [0xC6] = { DEC_Instr, "DEC zp", 2, 5, false, ZeroPage },
    [0xC8] = { INY_Instr, "INY", 1, 2, false, Implied },
    [0xC9] = { CMP_Instr, "CMP #imm", 2, 2, false, Immediate },
    [0xCA] = { DEX_Instr, "DEX", 1, 2, false, Implied },
    [0xCC] = { CPY_Instr, "CPY abs", 3, 4, false, Absolute },
    [0xCD] = { CMP_Instr, "CMP abs", 3, 4, false, Absolute },
    [0xCE] = { DEC_Instr, "DEC abs", 3, 6, false, Absolute },

    [0xD0] = { BNE_Instr, "BNE rel", 2, 2, true, Relative },
    [0xD1] = { CMP_Instr, "CMP (ind),Y", 2, 5, true, PostIndexedIndirect },
    [0xD5] = { CMP_Instr, "CMP zp,X", 2, 4, false, ZeroPageX },
    [0xD6] = { DEC_Instr, "DEC zp,X", 2, 6, false, ZeroPageX },
    [0xD8] = { CLD_Instr, "CLD", 1, 2, false, Implied },
    [0xD9] = { CMP_Instr, "CMP abs,Y", 3, 4, true, AbsoluteY },
    [0xDD] = { CMP_Instr, "CMP abs,X", 3, 4, true, AbsoluteX },
    [0xDE] = { DEC_Instr, "DEC abs,X", 3, 7, false, AbsoluteX },

    [0xE0] = { CPX_Instr, "CPX #imm", 2, 2, false, Immediate },
    [0xE1] = { SBC_Instr, "SBC (ind,X)", 2, 6, false, PreIndexedIndirect },
    [0xE4] = { CPX_Instr, "CPX zp", 2, 3, false, ZeroPage },
    [0xE5] = { SBC_Instr, "SBC zp", 2, 3, false, ZeroPage },
    [0xE6] = { INC_Instr, "INC zp", 2, 5, false, ZeroPage },
    [0xE8] = { INX_Instr, "INX", 1, 2, false, Implied },
    [0xE9] = { SBC_Instr, "SBC #imm", 2, 2, false, Immediate },
    [0xEA] = { NOP_Instr, "NOP", 1, 2, false, Implied },
    [0xEC] = { CPX_Instr, "CPX abs", 3, 4, false, Absolute },
    [0xED] = { SBC_Instr, "SBC abs", 3, 4, false, Absolute },
    [0xEE] = { INC_Instr, "INC abs", 3, 6, false, Absolute },

    [0xF0] = { BEQ_Instr, "BEQ rel", 2, 2, true, Relative },
    [0xF1] = { SBC_Instr, "SBC (ind),Y", 2, 5, true, PostIndexedIndirect },
    [0xF5] = { SBC_Instr, "SBC zp,X", 2, 4, false, ZeroPageX },
    [0xF6] = { INC_Instr, "INC zp,X", 2, 6, false, ZeroPageX },
    [0xF8] = { SED_Instr, "SED", 1, 2, false, Implied },
    [0xF9] = { SBC_Instr, "SBC abs,Y", 3, 4, true, AbsoluteY },
    [0xFD] = { SBC_Instr, "SBC abs,X", 3, 4, true, AbsoluteX },
    [0xFE] = { INC_Instr, "INC abs,X", 3, 7, false, AbsoluteX },

};

void ExecuteOpcode(_6502_State *state)
{
    const uint8_t opcode = memory[state->pc];
    const OpcodeHandler *handler = &opcodes[opcode];

/*
    if (state->pc == 0x35a2)
    {
        printf("Reached core subroutine of the full binary add/subtract test\n");
        free(state);
        exit(EXIT_SUCCESS);
    }
*/
    if (handler->InstrFn)
    {
        DEBUG_LOG("Executing %s (Opcode: 0x%02X) at PC: 0x%04X\n", handler->name, opcode, state->pc);
        handler->InstrFn(state, handler->addr_mode, handler->page_cross_penalty);
        state->cycles += handler->cycles;  // Add base cycles
    }
    else
    {
        printf("Unhandled opcode: 0x%02X\n", opcode);
        printf("Cycles done: %lu\n", state->cycles);
        //state->pc++;
        free(state);
        exit(EXIT_FAILURE);
    }
}

static void ExcecuteInstr(_6502_State *state)
{
/*
    if (state->pc == (MEMORY_SIZE - 1))
    {
        printf("Reached the end of memory!\n");
        free(state);
        exit(EXIT_FAILURE);
    }
*/
    uint8_t opcode = memory[state->pc];
    uint8_t addr_mode = opcode >> 3 & 7;
    uint8_t group = GetGroup(opcode);
    uint8_t instr_group = GetInstrInGroup(opcode);

    bool skip_next_instr = false;
/*
    if (state->pc == 0x581)
    {
        printf("Broke out of loop!\n");
    }

    if (state->pc == 0x99b)
    {
        printf("Finished Sub!\n");
    }

    if (state->pc == 0x35d4)
    {
        printf("Reached prev PC!\n");
        //free(state);
        //exit(EXIT_FAILURE);
    }
*/
/*
    if (state->pc == 0x3308)
    {
        printf("Reached the start of full binary add/subtract test\n");
        //free(state);
        //exit(EXIT_FAILURE);
    }

    if (state->pc == 0x35a2)
    {
        printf("Reached core subroutine of the full binary add/subtract test\n");
    }

    if (state->pc == 0x36EC)
    {
        printf("Finished full binary add/subtract core sub\n");
    }

    if (state->pc == 0x3464)
    {
        printf("opcode testing complete\n");
        //free(state);
        //exit(EXIT_FAILURE);
    }
*/
/*
    if (state->pc == 0x35a2)
    {
        printf("Reached core subroutine of the full binary add/subtract test\n");
        free(state);
        exit(EXIT_SUCCESS);
    }
*/

    //3464 : a9f0                     lda #$f0        ;mark opcode testing complete
    if (state->pc == 0x3469)
    {
        printf("Success!\n");
        free(state);
        exit(EXIT_SUCCESS);
    }

    switch (opcode)
    {
        case 0u:
        {
            // Implied BRK	00	1	7
            DEBUG_LOG("Implied BRK opcode %x at addr 0x%x\n", opcode, state->pc);
            state->pc += 2;
            StackPush(state, (state->pc >> 8) & 0xFF);
            StackPush(state, state->pc & 0xFF);
    
            Flags status = state->status;
            status.b = true;
            status.unused = true;
            StackPush(state, status.raw);

            state->status.i = true;
            // Load IRQ/BRK vector ($FFFE-$FFFF) into PC
            uint8_t vector_low = memory[0xFFFE];
            uint8_t vector_high = memory[0xFFFF];
            state->pc = (vector_high << 8) | vector_low;
            //state->pc = MemRead16(0xFFFE);

            DEBUG_LOG("Jumping to IRQ vector at 0x%X\n", state->pc);
            state->cycles += 7;
            skip_next_instr = true;
            break;
        }

        case 0x01:
        {
            // (Indirect, X)  OR (oper, X)  01  2  6
            DEBUG_LOG("Indirect X OR opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t final_addr = GetPreIndexedIndirectAddr(state, state->x);
            state->a |= memory[final_addr];

            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 6;
            break;
        }

        case 0x05:
        {
            // OR Memory with Accumulator
            DEBUG_LOG("Zeropage OR opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];
            state->a |= memory[addr];
            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)

            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 3;
            break;
        }

        case 0x06:
        {
            // ASL - Shift Left One Bit 
            // Zeropage	ASL oper   06  2  5
            DEBUG_LOG("Zeropage ASL opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];
            uint8_t *operand = &memory[addr];
            ShiftOneLeft(state, operand);
            state->cycles += 5;
            break;
        }

        case 0x08:
        {
            // Implied PHP	08	1	3
            DEBUG_LOG("Implied PHP opcode %x at addr 0x%x\n", opcode, state->pc);
            Flags status = state->status;
            status.b = true;
            status.unused = true;
            StackPush(state, status.raw);
            state->cycles += 3;
            break;
        }

        case 0x09:
        {
            // OR Memory with Accumulator
            // immediate ORA #oper	09	2	2
            DEBUG_LOG("Immediate ORA opcode %x at addr 0x%x\n", opcode, state->pc);

            state->a |= memory[++state->pc];
            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)

            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 2;
            break;
        }

        case 0x0A:
        {
            // ASL - Shift Left One Bit 
            // Accumulator	ASL A   0A  1  2
            DEBUG_LOG("Accumulator ASL opcode %x at addr 0x%x\n", opcode, state->pc);
            ShiftOneLeft(state, &state->a);
            state->cycles += 2;
            break;
        }

        case 0x0D:
        {
            // Absolute OR oper  0D  3  4
            DEBUG_LOG("Absolute OR opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t addr = GetAbsoluteAddr(state);
            state->a |= memory[addr];

            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0x0E:
        {
            // ASL - Shift Left One Bit 
            // Absolute  ASL oper   1E  3  7
            DEBUG_LOG("Absolute ASL opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t addr = GetAbsoluteAddr(state);
            uint8_t *operand = &memory[addr];
            ShiftOneLeft(state, operand);
            state->cycles += 6;
            break;
        }

        case 0x10:
        {
            // relative BPL oper
            DEBUG_LOG("Relative BPL opcode %x at addr 0x%x\n", opcode, state->pc);
            int8_t offset = (int8_t)memory[++state->pc];
            if (!state->status.n)
            {
                // Extra cycle if the branch crosses a page boundary
                if (!InsidePage(state->pc, state->pc + offset))
                    state->cycles++;

                state->pc += offset;
                state->cycles++;
                //skip_next_instr = true;
            }
            DEBUG_LOG("PC Offset %d\n", offset);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 2;
            break;
        }

        case 0x11:
        {
            // (Indirect), Y  OR (oper), Y  11  2  5*
            DEBUG_LOG("Indirect Y OR opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetPostIndexedIndirectAddr(state);
            uint16_t final_addr = base_addr + state->y;

            // Extra cycle if in different pages
            if (!InsidePage(base_addr, final_addr))
                state->cycles++;

            state->a |= memory[final_addr];

            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 5;
            break;
        }

        case 0x15:
        {
            // Zeropage X  OR oper, X  15  2  4
            DEBUG_LOG("Zeropage X OR opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];
            uint16_t final_addr = (addr + state->x) & PAGE_MASK;
            state->a |= memory[final_addr];

            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0x16:
        {
            // ASL - Shift Left One Bit 
            // Zeropage, X	ASL oper, X   16  2  6
            DEBUG_LOG("Zeropage X ASL opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];
            uint16_t final_addr = (addr + state->x) & PAGE_MASK;

            uint8_t *operand = &memory[final_addr];
            ShiftOneLeft(state, operand);
            state->cycles += 6;
            break;
        }

        case 0x18:
        {
            // Implied CLC oper
            DEBUG_LOG("Implied CLC opcode %x at addr 0x%x\n", opcode, state->pc);
            state->status.c = 0;
            state->cycles += 2;
            break;
        }

        case 0x19:
        {
            // Absolute Y OR oper, Y  19  3  4*
            DEBUG_LOG("Absolute Y OR opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetAbsoluteAddr(state);
            uint16_t final_addr = base_addr + state->y;

            // Extra cycle if pages are different
            if (!InsidePage(base_addr, final_addr))
                state->cycles++;

            state->a |= memory[final_addr];

            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0x1D:
        {
            // Absolute X OR oper, X  1D  3  4*
            DEBUG_LOG("Absolute X OR opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetAbsoluteAddr(state);
            uint16_t final_addr = base_addr + state->x;

            // Extra cycle if pages are different
            if (!InsidePage(base_addr, final_addr))
                state->cycles++;

            state->a |= memory[final_addr];

            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0x1E:
        {
            // ASL - Shift Left One Bit 
            // Absolute, X	ASL oper, X   1E  3  7
            DEBUG_LOG("Absolute X ASL opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetAbsoluteAddr(state);
            uint16_t final_addr = base_addr + state->x;

            uint8_t *operand = &memory[final_addr];
            ShiftOneLeft(state, operand);
            state->cycles += 7;
            break;
        }

        case 0x20:
        {
            DEBUG_LOG("Absolute JSR opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t pc_low = memory[++state->pc];
            uint8_t pc_high = memory[++state->pc];

            StackPush(state, (state->pc >> 8) & 0xFF);
            StackPush(state, state->pc & 0xFF);

            uint16_t new_pc = (uint16_t)pc_high << 8 | pc_low;
            state->pc = new_pc;
            DEBUG_LOG("New PC %d\n", new_pc);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 6;
            skip_next_instr = true;
            break;
        }

        case 0x21:
        {
            // (Indirect, X)  AND (oper, X)  21  2  6
            DEBUG_LOG("Indirect X AND opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t final_addr = GetPreIndexedIndirectAddr(state, state->x);
            state->a &= memory[final_addr];

            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 6;
            break;
        }

        case 0x24:
        {
            // Zeropage	BIT oper  24  2  3
            DEBUG_LOG("Zeropage BIT opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];
            uint8_t operand = memory[addr];

            state->status.n = GET_NEG_BIT(operand);
            state->status.v = GET_OVERFLOW_BIT(operand);
            state->status.z = !(state->a & operand);

            state->cycles += 3;
            break;
        }

        case 0x25:
        {
            // Zeropage	AND oper  24  2  3
            DEBUG_LOG("Zeropage AND opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];
            state->a &= memory[addr];

            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 3;
            break;
        }

        case 0x26:
        {
            // Rotate One Bit Left 
            // Zeropage	ROL oper   26  2   5
            DEBUG_LOG("Zeropage ROL opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];
            uint8_t *operand = &memory[addr];
            RotateOneLeft(state, operand);
            state->cycles += 5;
            break;
        }

        case 0x28:
        {
            // Implied PLP 28 1 4
            DEBUG_LOG("Implied PLP opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t status_raw = StackPull(state);
            Flags status = {.raw = status_raw};
            // Ignore bit for break and 5th bit
            state->status.c = status.c;
            state->status.d = status.d;
            state->status.i = status.i;
            state->status.n = status.n;
            state->status.v = status.v;
            state->status.z = status.z;

            state->cycles += 4;
            break;
        }

        case 0x29:
        {
            // Immediate AND #oper  29  2
            DEBUG_LOG("Immediate AND opcode %x at addr 0x%x\n", opcode, state->pc);

            state->a &= memory[++state->pc];

            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 2;
            break;
        }

        case 0x2A:
        {
            // Rotate One Bit Left
            // ROL shifts all bits left one position. The Carry is shifted into bit 0 and the original bit 7 is shifted into the Carry.
            // Accumulator	ROL A   2A  1   2
            DEBUG_LOG("Accumulator ROL opcode %x at addr 0x%x\n", opcode, state->pc);

            uint8_t old_carry = state->status.c;
            // Store bit 7 in carry before rotating
            state->status.c = (state->a >> 7) & 1;

            // Shift all bits left one position and insert old carry into bit 0
            state->a = (state->a << 1) | old_carry;

            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            state->cycles += 2;
            break;
        }

        case 0x2C:
        {
            // Absolute	BIT oper  2C  3  4
            DEBUG_LOG("Absolute BIT opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t addr = GetAbsoluteAddr(state);
            uint8_t operand = memory[addr];

            state->status.n = (operand >> 7) & 1;
            state->status.v = (operand >> 6) & 1;
            //state->status.z = ((state->a & operand) == 0) ? 1 : 0;
            state->status.z = !(state->a & operand);

            state->cycles += 4;
            break;
        }

        case 0x2D:
        {
            // Absolute AND oper  2D  2  4
            DEBUG_LOG("Absolute AND opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t addr = GetAbsoluteAddr(state);
            state->a &= memory[addr];

            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0x2E:
        {
            // Rotate One Bit Left 
            // Absolute ROL oper   2E  3   6
            DEBUG_LOG("Absolute ROL opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t addr = GetAbsoluteAddr(state);
            uint8_t *operand = &memory[addr];
            RotateOneLeft(state, operand);
            state->cycles += 6;
            break;
        }

        case 0x30:
        {
            // relative	BMI oper	30	2	2**
            DEBUG_LOG("Relative BMI opcode %x at addr 0x%x\n", opcode, state->pc);
            int8_t offset = (int8_t)memory[++state->pc];
            if (state->status.n)
            {
                // Extra cycle if the branch crosses a page boundary
                if (!InsidePage(state->pc, state->pc + offset))
                    state->cycles++;

                state->pc += offset;
                state->cycles++;
                //skip_next_instr = true;
                DEBUG_LOG("PC Offset %d\n", offset);
            }
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 2;
            break;
        }

        case 0x31:
        {
            // (Indirect), Y  AND (oper), Y  31  2  5*
            DEBUG_LOG("Indirect Y AND opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetPostIndexedIndirectAddr(state);
            uint16_t final_addr = base_addr + state->y;

            // Extra cycle if in different pages
            if (!InsidePage(base_addr, final_addr))
                state->cycles++;

            state->a &= memory[final_addr];

            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 5;
            break;
        }

        case 0x35:
        {
            // Zeropage X  AND oper, X  35  2  4
            DEBUG_LOG("Zeropage X AND opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];
            uint16_t final_addr = (addr + state->x) & PAGE_MASK;
            state->a &= memory[final_addr];

            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0x36:
        {
            // Rotate One Bit Left 
            // Zeropage	X  ROL oper, X   36  2   6
            DEBUG_LOG("Zeropage X ROL opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];
            uint8_t final_addr = (addr + state->x) & PAGE_MASK;
            uint8_t *operand = &memory[final_addr];
            RotateOneLeft(state, operand);
            state->cycles += 6;
            break;
        }

        case 0x38:
        {
            // Set Carry Flag
            DEBUG_LOG("Implied SEC opcode %x at addr 0x%x\n", opcode, state->pc);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->status.c = true;
            state->cycles += 2;
            break;
        }

        case 0x39:
        {
            // Absolute Y AND oper, Y  39  3  4*
            DEBUG_LOG("Absolute Y AND opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetAbsoluteAddr(state);
            uint16_t final_addr = base_addr + state->y;

            // Extra cycle if pages are different
            if (!InsidePage(base_addr, final_addr))
                state->cycles++;

            state->a &= memory[final_addr];

            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0x3D:
        {
            // Absolute X AND oper, X  3D  3  4*
            DEBUG_LOG("Absolute X AND opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetAbsoluteAddr(state);
            uint16_t final_addr = base_addr + state->x;

            // Extra cycle if pages are different
            if (!InsidePage(base_addr, final_addr))
                state->cycles++;

            state->a &= memory[final_addr];

            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0x3E:
        {
            // Rotate One Bit Left 
            // Absolute X ROL oper, X   3E  3   7
            DEBUG_LOG("Absolute ROL opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetAbsoluteAddr(state);
            uint16_t final_addr = base_addr + state->x;
            uint8_t *operand = &memory[final_addr];
            RotateOneLeft(state, operand);
            state->cycles += 7;
            break;
        }

        case 0x40:
        {
            // Return from Interrupt
            DEBUG_LOG("Implied RTI opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t status_raw = StackPull(state);
            Flags status = {.raw = status_raw};
            // Ignore bit for break and 5th bit
            state->status.c = status.c;
            state->status.d = status.d;
            state->status.i = status.i;
            state->status.n = status.n;
            state->status.v = status.v;
            state->status.z = status.z;

            uint8_t pc_low = StackPull(state);
            uint8_t pc_high = StackPull(state);

            uint16_t new_pc = (uint16_t)pc_high << 8 | pc_low;
            state->pc = new_pc;

            state->cycles += 6;
            skip_next_instr = true;
            break;
        }

        case 0x41:
        {
            // (Indirect, X)  EOR (oper, X)  41  2  6
            DEBUG_LOG("Indirect X EOR opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t final_addr = GetPreIndexedIndirectAddr(state, state->x);
            state->a ^= memory[final_addr];

            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 6;
            break;
        }

        case 0x45:
        //case 0x52:
        {
            // Exclusive-OR Memory with Accumulator
            DEBUG_LOG("Zeropage EOR opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];
            state->a ^= memory[addr];
            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)

            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 3;
            break;
        }

        case 0x46:
        {
            // LSR - Shift One Bit Right 
            // Zeropage	LSR oper   46  2  5
            DEBUG_LOG("Zeropage LSR opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];
            uint8_t *operand = &memory[addr];
            ShiftOneRight(state, operand);
            state->cycles += 5;
            break;
        }

        case 0x48:
        {
            // Implied	PHA	48	1	3
            DEBUG_LOG("Implied PHA opcode %x at addr 0x%x\n", opcode, state->pc);
            StackPush(state, state->a);
            state->cycles += 3;
            break;
        }

        case 0x49:
        {
            // Exclusive-OR Memory with Accumulator
            DEBUG_LOG("Immediate EOR opcode %x at addr 0x%x\n", opcode, state->pc);
            state->a ^= memory[++state->pc];
            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)

            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 2;
            break;
        }

        case 0x4A:
        {
            // LSR - Shift One Bit Right 
            // Accumulator	LSR A   4A  1  2
            DEBUG_LOG("Accumulator LSR opcode %x at addr 0x%x\n", opcode, state->pc);
            ShiftOneRight(state, &state->a);
            state->cycles += 2;
            break;
        }

        case 0x4C:
        {
            DEBUG_LOG("Absolute JMP opcode %x at addr 0x%x\n", opcode, state->pc);
            state->pc = GetAbsoluteAddr(state);
            DEBUG_LOG("New PC %d\n", state->pc);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 3;
            skip_next_instr = true;
            break;
        }

        case 0x4D:
        {
            // Absolute EOR oper  4D  3  4
            DEBUG_LOG("Absolute EOR opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t addr = GetAbsoluteAddr(state);
            state->a ^= memory[addr];

            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0x4E:
        {
            // LSR - Shift One Bit Right 
            // Absolute   LSR oper   4E  3  6
            DEBUG_LOG("Absolute LSR opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t addr = GetAbsoluteAddr(state);
            uint8_t *operand = &memory[addr];
            ShiftOneRight(state, operand);
            state->cycles += 6;
            break;
        }

        case 0x50:
        {
            // relative BVC oper
            DEBUG_LOG("Relative BVC opcode %x at addr 0x%x\n", opcode, state->pc);
            int8_t offset = (int8_t)memory[++state->pc];
            if (!state->status.v)
            {
                // Extra cycle if the branch crosses a page boundary
                if (!InsidePage(state->pc, state->pc + offset))
                    state->cycles++;

                state->pc += offset;
                state->cycles++;
                //skip_next_instr = true;
                DEBUG_LOG("PC Offset %d\n", offset);
            }
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 2;
            break;
        }

        case 0x51:
        {
            // (Indirect), Y  EOR (oper), Y  51  2  5*
            DEBUG_LOG("Indirect Y EOR opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetPostIndexedIndirectAddr(state);
            uint16_t final_addr = base_addr + state->y;

            // Extra cycle if in different pages
            if (!InsidePage(base_addr, final_addr))
                state->cycles++;

            state->a ^= memory[final_addr];

            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 5;
            break;
        }

        case 0x55:
        {
            // Exclusive-OR Memory with Accumulator
            DEBUG_LOG("Zeropage X EOR opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t base_addr = memory[++state->pc];
            uint16_t final_addr = (base_addr + state->x) & PAGE_MASK;
            state->a ^= memory[final_addr];
            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)

            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 3;
            break;
        }

        case 0x56:
        {
            // LSR - Shift One Bit Right 
            // Zeropage X	LSR oper, X   56  2  6
            DEBUG_LOG("Zeropage X LSR opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];
            uint16_t final_addr = (addr + state->x) & PAGE_MASK;
            uint8_t *operand = &memory[final_addr];
            ShiftOneRight(state, operand);
            state->cycles += 6;
            break;
        }

        case 0x58:
        {
            // Clear Interrupt Disable Bit
            DEBUG_LOG("Implied CLI opcode %x at addr 0x%x\n", opcode, state->pc);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->status.i = false;
            state->cycles += 2;
            break;
        }

        case 0x59:
        {
            // Absolute Y EOR oper, Y  59  3  4*
            DEBUG_LOG("Absolute Y EOR opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetAbsoluteAddr(state);
            uint16_t final_addr = base_addr + state->y;

            // Extra cycle if pages are different
            if (!InsidePage(base_addr, final_addr))
                state->cycles++;

            state->a ^= memory[final_addr];

            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0x5D:
        {
            // Absolute X EOR oper, X  5D  3  4*
            DEBUG_LOG("Absolute X EOR opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetAbsoluteAddr(state);
            uint16_t final_addr = base_addr + state->x;

            // Extra cycle if pages are different
            if (!InsidePage(base_addr, final_addr))
                state->cycles++;

            state->a ^= memory[final_addr];

            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0x5E:
        {
            // LSR - Shift One Bit Right 
            // Absolute X	LSR oper, X   5E  3  7
            DEBUG_LOG("Absolute X LSR opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetAbsoluteAddr(state);
            uint16_t final_addr = base_addr + state->x;
            uint8_t *operand = &memory[final_addr];
            ShiftOneRight(state, operand);
            state->cycles += 7;
            break;
        }

        case 0x60:
        {
            // Return from Sub
            DEBUG_LOG("Implied RTS opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t pc_low = StackPull(state);
            uint8_t pc_high = StackPull(state);

            uint16_t new_pc = (uint16_t)pc_high << 8 | pc_low;
            state->pc = new_pc;
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 6;
            break;
        }

        case 0x61:
        {
            // (Indirect, X)  ADC (oper, X)  61  2  6
            DEBUG_LOG("Indirect X ADC opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t final_addr = GetPreIndexedIndirectAddr(state, state->x);
            uint8_t operand = memory[final_addr];
            AddWithCarry(state, operand);
            state->cycles += 6;
            break;
        }

        case 0x65:
        {
            DEBUG_LOG("Zeropage ADC opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];
            uint8_t operand = memory[addr];
            AddWithCarry(state, operand);
            state->cycles += 3;
            break;
        }

        case 0x66:
        {
            // Rotate One Bit Right 
            // Zeropage	ROR oper   66  2   5
            DEBUG_LOG("Zeropage ROR opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];
            uint8_t *operand = &memory[addr];
            RotateOneRight(state, operand);
            state->cycles += 5;
            break;
        }

        case 0x68:
        {
            // Implied PLA 68 1 4
            DEBUG_LOG("Implied PLA opcode %x at addr 0x%x\n", opcode, state->pc);
            state->a = StackPull(state);
            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is Y zero?)
            state->cycles += 4;
            break;
        }

        case 0x69:
        {
            DEBUG_LOG("Immediate ADC opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t operand = memory[++state->pc];
            AddWithCarry(state, operand);
            state->cycles += 2;
            break;
        }

        case 0x6A:
        {
            // Rotate One Bit Right
            // ROR shifts all bits right one position. The Carry is shifted into bit 7 and the original bit 0 is shifted into the Carry. 
            // Accumulator	ROR A   6A  1   2
            DEBUG_LOG("Accumulator ROR opcode %x at addr 0x%x\n", opcode, state->pc);
            RotateOneRight(state, &state->a);
            state->cycles += 2;
            break;
        }

        case 0x6C:
        {
            DEBUG_LOG("Indirect JMP opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t ptr_low = memory[++state->pc];
            uint8_t ptr_high = memory[++state->pc];

            uint16_t ptr = (uint16_t)ptr_high << 8 | ptr_low;
            uint16_t new_pc;
            // **6502 Page Boundary Bug** (If ptr is at 0xXXFF, high byte comes from 0xXX00, not 0xXXFF+1)
            if ((ptr & 0xFF) == 0xFF)
            {
                new_pc = (uint16_t)memory[ptr] | ((uint16_t)memory[ptr & 0xFF00] << 8);
            }
            else
            {
                new_pc = (uint16_t)memory[ptr] | ((uint16_t)memory[ptr + 1] << 8);
            }
            state->pc = new_pc;
            DEBUG_LOG("Ptr addr  %x\n", ptr);
            DEBUG_LOG("New PC %x\n", new_pc);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 5;
            skip_next_instr = true;
            break;
        }

        case 0x6D:
        {
            DEBUG_LOG("Absolute ADC opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t addr = GetAbsoluteAddr(state);
            uint8_t operand = memory[addr];
            AddWithCarry(state, operand);
            state->cycles += 4;
            break;
        }

        case 0x6E:
        {
            // Rotate One Bit Right 
            // Absolute	ROR oper   6E  3   6
            DEBUG_LOG("Absolute ROR opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t addr = GetAbsoluteAddr(state);
            uint8_t *operand = &memory[addr];
            RotateOneRight(state, operand);
            state->cycles += 6;
            break;
        }

        case 0x70:
        {
            // relative BVS oper
            DEBUG_LOG("Relative BVS opcode %x at addr 0x%x\n", opcode, state->pc);
            int8_t offset = (int8_t)memory[++state->pc];
            if (state->status.v)
            {
                // Extra cycle if the branch crosses a page boundary
                if (!InsidePage(state->pc, state->pc + offset))
                    state->cycles++;

                state->pc += offset;
                state->cycles++;
                //skip_next_instr = true;
                DEBUG_LOG("PC Offset %d\n", offset);
            }
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 2;
            break;
        }


        case 0x71:
        {
            // (Indirect), Y  ADC (oper), Y  71  2  5*
            DEBUG_LOG("Indirect Y ADC opcode %x at addr 0x%x\n", opcode, state->pc);

            uint16_t base_addr = GetPostIndexedIndirectAddr(state);
            uint16_t final_addr = base_addr + state->y;
            uint8_t operand = memory[final_addr];

            AddWithCarry(state, operand);
            state->cycles += 5;
            break;
        }

        case 0x75:
        {
            DEBUG_LOG("Zeropage X ADC opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t base_addr = memory[++state->pc];
            uint16_t final_addr = (base_addr + state->x) & PAGE_MASK;
            uint8_t operand = memory[final_addr];

            AddWithCarry(state, operand);
            state->cycles += 3;
            break;
        }

        case 0x76:
        {
            // Rotate One Bit Right 
            // Zeropage	X  ROR oper, X   76  2   6
            DEBUG_LOG("Zeropage X ROR opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];
            uint16_t final_addr = (addr + state->x) & PAGE_MASK;
            uint8_t *operand = &memory[final_addr];
            RotateOneRight(state, operand);
            state->cycles += 6;
            break;
        }

        case 0x78:
        {
            // Set Interrupt Disable Bit
            DEBUG_LOG("Implied SEI opcode %x at addr 0x%x\n", opcode, state->pc);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->status.i = true;
            state->cycles += 2;
            break;
        }

        case 0x79:
        {
            DEBUG_LOG("Absolute Y ADC opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetAbsoluteAddr(state);
            uint16_t final_addr = base_addr + state->y;
            uint8_t operand = memory[final_addr];
    
            // Extra cycle if pages are different
            if (!InsidePage(base_addr, final_addr))
                state->cycles++;

            AddWithCarry(state, operand);
            state->cycles += 4;
            break;
        }

        case 0x7D:
        {
            DEBUG_LOG("Absolute X ADC opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetAbsoluteAddr(state);
            uint16_t final_addr = base_addr + state->x;
            uint8_t operand = memory[final_addr];

            // Extra cycle if pages are different
            if (!InsidePage(base_addr, final_addr))
                state->cycles++;

            AddWithCarry(state, operand);
            state->cycles += 4;
            break;
        }

        case 0x7E:
        {
            // Rotate One Bit Right 
            // Absolute X	ROR oper, X   7E  3   7
            DEBUG_LOG("Absolute X ROR opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetAbsoluteAddr(state);
            uint16_t final_addr = base_addr + state->x;
            uint8_t *operand = &memory[final_addr];
            RotateOneRight(state, operand);
            state->cycles += 7;
            break;
        }

        case 0x81:
        {
            // Indirect X STA (oper, Y) 81  2  6
            DEBUG_LOG("Indirect X STA opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t final_addr = GetPreIndexedIndirectAddr(state, state->x);
        
            memory[final_addr] = state->a;

            //DEBUG_LOG("Ptr addr 0x%x\n", base_addr);
            DEBUG_LOG("Final 0x%x\n", final_addr);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 6;
            break;
        }

        case 0x84:
        {
            // Zeropage STY oper 84 2 3
            DEBUG_LOG("Zeropage STY opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];
            memory[addr] = state->y;
            DEBUG_LOG("New Addr 0x%04x\n", addr);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 3;
            break;
        }

        case 0x85:
        {
            // Zeropage STA oper 85 2 3
            DEBUG_LOG("Zeropage STA opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];
            memory[addr] = state->a;
            DEBUG_LOG("Storing A in 0x%04x\n", addr);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 3;
            break;
        }

        case 0x86:
        {
            // Zeropage STX oper 86 2 3
            DEBUG_LOG("Zeropage STX opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];
            memory[addr] = state->x;
            DEBUG_LOG("Storing X in 0x%04x\n", addr);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 3;
            break;
        }

        case 0x88:
        {
            DEBUG_LOG("Implied DEY opcode %x at addr 0x%x\n", opcode, state->pc);
            state->y--;
            state->status.n = GET_NEG_BIT(state->y);    // Negative flag (bit 7)
            state->status.z = (state->y == 0) ? 1 : 0;  // Zero flag (is Y zero?)
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 2;
            break;
        }

        case 0x8A:
        {
            // Transfer Index X to Accumulator
            DEBUG_LOG("Implied TXA opcode %x at addr 0x%x\n", opcode, state->pc);
            state->a = state->x;
            state->status.n = GET_NEG_BIT(state->a);  // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0; // Zero flag (is a zero?)
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 2;
            break;
        }

        case 0x8C:
        {
            // Absolute STY oper 8C	3 4
            DEBUG_LOG("Absolute STY opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t addr = GetAbsoluteAddr(state);

            memory[addr] = state->y;
    
            DEBUG_LOG("Storing Y in 0x%04x\n", addr);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0x8D:
        {
            // absolute STA oper	8D	3	4 
            DEBUG_LOG("Absolute STA opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t addr = GetAbsoluteAddr(state);

            memory[addr] = state->a;

            DEBUG_LOG("New Addr 0x%04x\n", addr);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0x8E:
        {
            // Absolute STX oper	8E	3	4 
            DEBUG_LOG("Absolute STX opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t addr = GetAbsoluteAddr(state);

            memory[addr] = state->x;
    
            DEBUG_LOG("New Addr 0x%04x\n", addr);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0x90:
        {
            // relative	BCC oper	90	2	2**
            DEBUG_LOG("Relative BCC opcode %x at addr 0x%x\n", opcode, state->pc);
            int8_t offset = (int8_t)memory[++state->pc];
            if (!state->status.c)
            {
                // Extra cycle if the branch crosses a page boundary
                if (!InsidePage(state->pc, state->pc + offset))
                    state->cycles++;

                state->pc += offset;
                state->cycles++;
                //skip_next_instr = true;
                DEBUG_LOG("PC Offset %d\n", offset);
            }
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 2;
            break;
        }

        case 0x91:
        {
            // Indirect Y STA (oper) Y 91  2  6
            DEBUG_LOG("Indirect Y STA opcode %x at addr 0x%x\n", opcode, state->pc);
            //uint16_t base_addr = GetIndirectAddr(state);
            uint16_t base_addr = GetPostIndexedIndirectAddr(state);
            uint16_t final_addr = base_addr + state->y;
        
            memory[final_addr] = state->a;

            DEBUG_LOG("Ptr addr 0x%x\n", base_addr);
            DEBUG_LOG("Final 0x%x\n", final_addr);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 6;
            break;
        }

        case 0x94:
        {
            // Zeropage X STY oper 94 2 4
            DEBUG_LOG("Zeropage Y STY opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];
            uint16_t final_addr = addr + state->x;

            memory[final_addr & PAGE_MASK] = state->y;

            DEBUG_LOG("Storing Y(%u) in 0x%04x\n", state->y, final_addr);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0x95:
        {
            // Zeropage X STA oper 95 2 4
            DEBUG_LOG("Zeropage X STA opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];
            uint16_t final_addr = addr + state->x;

            memory[final_addr & PAGE_MASK] = state->a;

            DEBUG_LOG("Storing A(%u) in 0x%04x\n", state->a, final_addr);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0x96:
        {
            // Zeropage Y STX oper 96 2 4
            DEBUG_LOG("Zeropage Y STX opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];
            uint16_t final_addr = addr + state->y;

            memory[final_addr & PAGE_MASK] = state->x;

            DEBUG_LOG("New Addr 0x%04x\n", final_addr);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0x98:
        {
            // Transfer Index Y to Accumulator
            DEBUG_LOG("Implied TYA opcode %x at addr 0x%x\n", opcode, state->pc);
            state->a = state->y;
            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 2;
            break;
        }

        case 0x99:
        {
            // Absolute Y STA oper 99 3 5
            DEBUG_LOG("Absolute Y STA opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetAbsoluteAddr(state);
            uint16_t final_addr = base_addr + state->y;
            memory[final_addr] = state->a;
            DEBUG_LOG("New Addr 0x%04x\n", final_addr);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 5;
            break;
        }

        case 0x9A:
        {
            //Transfer Index X to Stack Register
            DEBUG_LOG("Implied TXS opcode %x at addr 0x%x\n", opcode, state->pc);
            state->sp = state->x;
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 2;
            break;
        }

        case 0x9D:
        {
            // Absolute X STA oper 9D 3 5 
            DEBUG_LOG("Absolute X STA opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetAbsoluteAddr(state);
            uint16_t final_addr = base_addr + state->x;

            memory[final_addr] = state->a;

            DEBUG_LOG("New Addr 0x%04x\n", final_addr);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 5;
            break;
        }

        case 0xA0:
        {
            // immediate	LDY #oper	A0	2	2 
            DEBUG_LOG("Immediate LDY opcode %x at addr 0x%x\n", opcode, state->pc);
            state->y = memory[++state->pc];
            // Update status flags
            state->status.n = GET_NEG_BIT(state->y);  // Negative flag (bit 7)
            state->status.z = (state->y == 0) ? 1 : 0;    // Zero flag (is Y zero?)
            DEBUG_LOG("Operand %x\n", memory[state->pc]);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 2;
            break;
        }

        case 0xA1:
        {
            // (Indirect,X) LDA (oper, X) A1  2  6
            DEBUG_LOG("Indirect X LDA opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t addr = GetPreIndexedIndirectAddr(state, state->x);
        
            state->a = memory[addr];

            // Update status flags
            state->status.n = GET_NEG_BIT(state->a);  // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0; // Zero flag (is A zero?)

            //DEBUG_LOG("Ptr addr 0x%x\n", base_addr);
            DEBUG_LOG("Final 0x%x\n", addr);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 6;
            break;
        }

        case 0xA2:
        {
            DEBUG_LOG("Immediate LDX opcode %x at addr 0x%x\n", opcode, state->pc);
            state->x = memory[++state->pc];
            // Update status flags
            state->status.n = GET_NEG_BIT(state->x);  // Negative flag (bit 7)
            state->status.z = (state->x == 0) ? 1 : 0;    // Zero flag (is A zero?)
            DEBUG_LOG("Operand %x\n", memory[state->pc]);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 2;
            break;
        }

        case 0xA4:
        {
            // Zeropage LDY oper A4 2 3
            DEBUG_LOG("Zeropage LDY opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];

            state->y = memory[addr];

            // Update status flags
            state->status.n = GET_NEG_BIT(state->y);  // Negative flag (bit 7)
            state->status.z = (state->y == 0) ? 1 : 0;    // Zero flag (is Y zero?)
            DEBUG_LOG("New Addr 0x%04x\n", addr);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 3;
            break;
        }

        case 0xA5:
        {
            DEBUG_LOG("Zeropage LDA opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];

            state->a = memory[addr];

            // Update status flags
            state->status.n = GET_NEG_BIT(state->a);  // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;    // Zero flag (is A zero?)
            DEBUG_LOG("New Addr 0x%04x\n", addr);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 3;
            break;
        }

        case 0xA6:
        {
            // Zeropage LDX oper A6 2 3
            DEBUG_LOG("Zeropage LDX opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];

            state->x = memory[addr];

            // Update status flags
            state->status.n = GET_NEG_BIT(state->x);  // Negative flag (bit 7)
            state->status.z = (state->x == 0) ? 1 : 0;    // Zero flag (is X zero?)
            DEBUG_LOG("New Addr 0x%04x\n", addr);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 3;
            break;
        }

        case 0xA8:
        {
            // Transfer Accumulator to Index Y
            DEBUG_LOG("Implied TAY opcode %x at addr 0x%x\n", opcode, state->pc);
            state->y = state->a;
            state->status.n = GET_NEG_BIT(state->y);  // Negative flag (bit 7)
            state->status.z = (state->y == 0) ? 1 : 0; // Zero flag (is Y zero?)
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 2;
            break;
        }

        case 0xA9:
            DEBUG_LOG("Immediate LDA opcode %x at addr 0x%x\n", opcode, state->pc);
            state->a = memory[++state->pc];
            // Update status flags
            state->status.n = GET_NEG_BIT(state->a);  // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0; // Zero flag (is A zero?)
            DEBUG_LOG("Operand %x\n", memory[state->pc]);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 2;
        break;

        case 0xAA:
        {
            // Transfer Accumulator to Index X
            DEBUG_LOG("Implied TAX opcode %x at addr 0x%x\n", opcode, state->pc);
            state->x = state->a;
            state->status.n = GET_NEG_BIT(state->x);  // Negative flag (bit 7)
            state->status.z = (state->x == 0) ? 1 : 0; // Zero flag (is x zero?)
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 2;
            break;
        }

        case 0xAC:
        {
            // Absolute LDY oper AC 3 4
            DEBUG_LOG("Absolute LDY opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t addr = GetAbsoluteAddr(state);

            state->y = memory[addr];
            // Update status flags
            state->status.n = GET_NEG_BIT(state->y);  // Negative flag (bit 7)
            state->status.z = (state->y == 0) ? 1 : 0;    // Zero flag (is Y zero?)
            DEBUG_LOG("Operand %x\n", memory[state->pc]);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0xAD:
        {
            DEBUG_LOG("Absolute LDA opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t addr = GetAbsoluteAddr(state);
            state->a = memory[addr];
            // Update status flags
            state->status.n = GET_NEG_BIT(state->a);  // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;    // Zero flag (is A zero?)
            DEBUG_LOG("New Addr 0x%04x\n", addr);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0xAE:
        {
            // Absolute LDX oper AE 3 4
            DEBUG_LOG("Absolute LDX opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t addr = GetAbsoluteAddr(state);

            state->x = memory[addr];

            // Update status flags
            state->status.n = GET_NEG_BIT(state->x);  // Negative flag (bit 7)
            state->status.z = (state->x == 0) ? 1 : 0;    // Zero flag (is X zero?)
            DEBUG_LOG("New Addr 0x%04x\n", addr);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0xB0:
        {
            // relative	BCS oper	B0	2	2**
            DEBUG_LOG("Relative BCS opcode %x at addr 0x%x\n", opcode, state->pc);
            int8_t offset = (int8_t)memory[++state->pc];
            if (state->status.c)
            {
                // Extra cycle if the branch crosses a page boundary
                if (!InsidePage(state->pc, state->pc + offset))
                    state->cycles++;

                state->pc += offset;
                state->cycles++;
                //skip_next_instr = true;
            }
            DEBUG_LOG("PC Offset %d\n", offset);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 2;
            break;
        }

        case 0xB1:
        {
            // (Indirect),Y LDA (oper),Y B1  2  5*
            DEBUG_LOG("Indirect Y LDA opcode %x at addr 0x%x\n", opcode, state->pc);
            //uint16_t base_addr = GetIndirectAddr(state);
            uint16_t base_addr = GetPostIndexedIndirectAddr(state);
            uint16_t final_addr = base_addr + state->y;
        
            state->a = memory[final_addr];

            if (!InsidePage(base_addr, final_addr))
                state->cycles++;

            // Update status flags
            state->status.n = GET_NEG_BIT(state->a);  // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0; // Zero flag (is A zero?)

            DEBUG_LOG("Ptr addr 0x%x\n", base_addr);
            DEBUG_LOG("Final 0x%x\n", final_addr);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 5;
            break;
        }

        case 0xB4:
        {
            // Zeropage X LDY oper B4 2 4
            DEBUG_LOG("Zeropage X LDY opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t base_addr = memory[++state->pc];
            uint16_t final_addr = (base_addr + state->x) & PAGE_MASK; // Wrap within zero-page

            state->y = memory[final_addr];

            // Update status flags
            state->status.n = GET_NEG_BIT(state->y);  // Negative flag (bit 7)
            state->status.z = (state->y == 0) ? 1 : 0;    // Zero flag (is Y zero?)
            DEBUG_LOG("New Addr 0x%04x\n", final_addr);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0xB5:
        {
            // Zeropage,X LDA oper,X B5 2 4 
            DEBUG_LOG("Zeropage X LDA opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t base_addr = memory[++state->pc];
            uint16_t final_addr = (base_addr + state->x) & PAGE_MASK; // Wrap within zero-page

            state->a = memory[final_addr];

            // Update status flags
            state->status.n = GET_NEG_BIT(state->a);  // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;    // Zero flag (is A zero?)
            DEBUG_LOG("New Addr 0x%04x\n", final_addr);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0xB6:
        {
            // Zeropage Y LDX oper B6 2 4
            DEBUG_LOG("Zeropage Y LDX opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t base_addr = memory[++state->pc];
            uint8_t final_addr = (base_addr + state->y) & PAGE_MASK; // Wrap within zero-page

            state->x = memory[final_addr];

            // Update status flags
            state->status.n = GET_NEG_BIT(state->x);  // Negative flag (bit 7)
            state->status.z = (state->x == 0) ? 1 : 0;    // Zero flag (is X zero?)
            DEBUG_LOG("New Addr 0x%04x\n", final_addr);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0xB8:
        {
            // Clear Overflow Flag
            DEBUG_LOG("Implied CLV opcode %x at addr 0x%x\n", opcode, state->pc);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->status.v = false;
            state->cycles += 2;
            break;
        }

        case 0xB9:
        {
            DEBUG_LOG("AbsoluteY LDA opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetAbsoluteAddr(state);
            uint16_t final_addr = base_addr + state->y;

            if (!InsidePage(base_addr, final_addr))
                state->cycles++;

            state->a = memory[final_addr];
            // Update status flags
            state->status.n = GET_NEG_BIT(state->a);  // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;    // Zero flag (is A zero?)
            DEBUG_LOG("New Addr 0x%04x\n", final_addr);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0xBA:
        {
            // Transfer Stack Pointer to Index X
            DEBUG_LOG("Implied TSX opcode %x at addr 0x%x\n", opcode, state->pc);
            state->x = state->sp;
            state->status.n = GET_NEG_BIT(state->x);  // Negative flag (bit 7)
            state->status.z = (state->x == 0) ? 1 : 0; // Zero flag (is x zero?)
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 2;
            break;
        }

        case 0xBB:
        {
            DEBUG_LOG("Absolute Y LAS/LAR opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetAbsoluteAddr(state);
            uint16_t final_addr = base_addr + state->y;
            uint8_t operand = memory[final_addr] & state->sp;
            state->a = operand;
            state->x = operand;
            state->sp = operand;
            state->status.n = GET_NEG_BIT(state->a);  // Negative flag (bit 7)
            state->status.z = (operand == 0) ? 1 : 0;    // Zero flag (is value zero?)
            state->cycles += 4;
            break;
        }

        case 0xBC:
        {
            // Absolute X LDY oper, X BC 3 4* 
            DEBUG_LOG("Absolute X LDY opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetAbsoluteAddr(state);
            uint16_t final_addr = base_addr + state->x;

            if (!InsidePage(base_addr, final_addr))
                state->cycles++;

            state->y = memory[final_addr];
            // Update status flags
            state->status.n = GET_NEG_BIT(state->y);  // Negative flag (bit 7)
            state->status.z = (state->y == 0) ? 1 : 0;    // Zero flag (is Y zero?)
            DEBUG_LOG("Operand %x\n", memory[state->pc]);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0xBD:
        {
            DEBUG_LOG("AbsoluteX LDA opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetAbsoluteAddr(state);
            uint16_t final_addr = base_addr + state->x;

            if (!InsidePage(base_addr, final_addr))
                state->cycles++;

            state->a = memory[final_addr];
            // Update status flags
            state->status.n = GET_NEG_BIT(state->a);  // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;    // Zero flag (is A zero?)
            DEBUG_LOG("New Addr 0x%04x\n", final_addr);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0xBE:
        {
            // Absolute Y LDX oper BE 3 4*
            DEBUG_LOG("Absolute Y LDX opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetAbsoluteAddr(state);
            uint16_t final_addr = base_addr + state->y;

            if (!InsidePage(base_addr, final_addr))
                state->cycles++;

            state->x = memory[final_addr];

            // Update status flags
            state->status.n = GET_NEG_BIT(state->x);  // Negative flag (bit 7)
            state->status.z = (state->x == 0) ? 1 : 0;    // Zero flag (is X zero?)
            DEBUG_LOG("New Addr 0x%04x\n", final_addr);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0xC0:
        {
            // Immediate CPY #oper C0 2 2
            DEBUG_LOG("Immediate CPY opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t operand = memory[++state->pc];
            uint8_t result = state->y - operand;
            state->status.n = GET_NEG_BIT(result);  // Negative flag (bit 7)
            state->status.z = (result == 0) ? 1 : 0;    // Zero flag (is result zero?)
            // Update the Carry Flag (C)
            state->status.c = (state->y >= operand) ? 1 : 0;
            DEBUG_LOG("Operand %u\n", operand);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 2;
            break;
        }

        case 0xC1:
        {
            // (Indirect, X) CMP (oper,Y)  C1  2  6
            DEBUG_LOG("(Indirect, X) CMP opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t addr = GetPreIndexedIndirectAddr(state, state->x);

            uint8_t operand = memory[addr];
            CompareRegAndSetFlags(state, state->a, operand);

            DEBUG_LOG("Operand %x\n", operand);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 6;
            break;
        }

        case 0xC4:
        {
            // Zeropage CPY oper C4 2 3
            DEBUG_LOG("Zeropage CPY opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];
            uint8_t operand = memory[addr];
            CompareRegAndSetFlags(state, state->y, operand);

            DEBUG_LOG("Operand %u\n", operand);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 3;
            break;
        }

        case 0xC5:
        {
            // Zeropage CMP oper C5 2 3
            DEBUG_LOG("Zeropage CMP opcode %x at addr 0x%x\n", opcode, state->pc);

            uint8_t addr = memory[++state->pc];
            uint8_t operand = memory[addr];

            CompareRegAndSetFlags(state, state->a, operand);

            DEBUG_LOG("Operand %u\n", operand);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 3;
            break;
        }

        case 0xC6:
        {
            // Zeropage	DEC oper   E6   2  5  
            DEBUG_LOG("Zeropage DEC opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];
            uint8_t *operand = &memory[addr];

            *operand -= 1;

            state->status.n = GET_NEG_BIT(*operand);  // Negative flag (bit 7)
            state->status.z = (*operand == 0) ? 1 : 0;    // Zero flag (is value zero?)

            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 5;
            break;
        }

        case 0xC8:
        {
            DEBUG_LOG("Implied INY opcode %x at addr 0x%x\n", opcode, state->pc);
            state->y++;
            state->status.n = GET_NEG_BIT(state->y);  // Negative flag (bit 7)
            state->status.z = (state->y == 0) ? 1 : 0;    // Zero flag (is Y zero?)
            state->cycles += 2;
            break;
        }

        case 0xC9:
        {
            // Immediate CMP #oper C9  2  2
            DEBUG_LOG("Immediate CMP opcode %x at addr 0x%x\n", opcode, state->pc);

            uint8_t operand = memory[++state->pc];
            CompareRegAndSetFlags(state, state->a, operand);

            DEBUG_LOG("Operand %u\n", operand);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 2;
            break;
        }

        case 0xCA:
        {
            DEBUG_LOG("Implied DEX opcode %x at addr 0x%x\n", opcode, state->pc);
            state->x--;
            state->status.n = GET_NEG_BIT(state->x);  // Negative flag (bit 7)
            state->status.z = (state->x == 0) ? 1 : 0;    // Zero flag (is X zero?)
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 2;
            break;
        }

        case 0xCC:
        {
            // Absolute	CPY oper CC 3 4  
            DEBUG_LOG("Absolute CPY opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t addr = GetAbsoluteAddr(state);

            uint8_t operand = memory[addr];
            CompareRegAndSetFlags(state, state->y, operand);

            DEBUG_LOG("Operand %u\n", operand);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0xCD:
        {
            // Absolute	CMP oper CD 3 4  
            DEBUG_LOG("Absolute CMP opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t addr = GetAbsoluteAddr(state);
            uint8_t operand = memory[addr];
            CompareRegAndSetFlags(state, state->a, operand);

            DEBUG_LOG("Operand %x\n", operand);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0xCE:
        {
            // Absolute   DEC oper   CE   3  6
            DEBUG_LOG("Absolute DEC opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t addr = GetAbsoluteAddr(state);
            uint8_t *operand = &memory[addr];

            *operand -= 1;

            state->status.n = GET_NEG_BIT(*operand);  // Negative flag (bit 7)
            state->status.z = (*operand == 0) ? 1 : 0;    // Zero flag (is value zero?)

            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 6;
            break;
        }

        case 0xD0:
        {
            // relative BNE oper
            DEBUG_LOG("Relative BNE opcode %x at addr 0x%x\n", opcode, state->pc);
            int8_t offset = (int8_t)memory[++state->pc];
            if (!state->status.z)
            {
                // Extra cycle if the branch crosses a page boundary
                if (!InsidePage(state->pc, state->pc + offset))
                    state->cycles++;

                state->pc += offset;
                state->cycles++;
                //skip_next_instr = true;
                DEBUG_LOG("PC Offset %d\n", offset);
            }
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 2;
            break;
        }

        case 0xD1:
        {
            // (Indirect),Y	CMP (oper),Y  D1  2  5*
            DEBUG_LOG("Indirect Y CMP opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetPostIndexedIndirectAddr(state);

            uint16_t final_addr = base_addr + state->y;

            if (!InsidePage(base_addr, final_addr))
                state->cycles++;

            uint8_t operand = memory[final_addr];
            CompareRegAndSetFlags(state, state->a, operand);

            DEBUG_LOG("Operand %x\n", operand);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 5;
            break;
        }

        case 0xD6:
        {
            // Zeropage X	DEC oper, X   D6   2  6
            DEBUG_LOG("Zeropage X DEC opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t base_addr = memory[++state->pc];
            uint16_t final_addr = (base_addr + state->x) & PAGE_MASK;
            uint8_t *operand = &memory[final_addr];

            *operand -= 1;

            state->status.n = GET_NEG_BIT(*operand);  // Negative flag (bit 7)
            state->status.z = (*operand == 0) ? 1 : 0;    // Zero flag (is value zero?)

            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 6;
            break;
        }

        case 0xD8:
        {
            // Clear Decimal Mode
            DEBUG_LOG("Implied CLD opcode %x at addr 0x%x\n", opcode, state->pc);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->status.d = 0;
            state->cycles += 2;
            break;
        }

        case 0xD5:
        {
            // Zeropage X CMP oper D5 2 4  
            DEBUG_LOG("Zeropage X CMP opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];
            uint16_t final_addr = addr + state->x;

            // Wrap within zero-page
            uint8_t operand = memory[final_addr & PAGE_MASK];
            CompareRegAndSetFlags(state, state->a, operand);

            DEBUG_LOG("Operand %x\n", operand);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0xD9:
        {
            // Absolute Y CMP oper D9 3 4*  
            DEBUG_LOG("Absolute Y CMP opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetAbsoluteAddr(state);
            uint16_t final_addr = base_addr + state->y;

            if (!InsidePage(base_addr, final_addr))
                state->cycles++;

            uint8_t operand = memory[final_addr];
            CompareRegAndSetFlags(state, state->a, operand);

            DEBUG_LOG("Operand %x\n", operand);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0xDD:
        {
            // Absolute X CMP oper DD 3 4*  
            DEBUG_LOG("Absolute X CMP opcode %x at addr 0x%x\n", opcode, state->pc);

            uint16_t base_addr = GetAbsoluteAddr(state);
            uint16_t final_addr = base_addr + state->x;

            if (!InsidePage(base_addr, final_addr))
                state->cycles++;

            uint8_t operand = memory[final_addr];

            CompareRegAndSetFlags(state, state->a, operand);

            DEBUG_LOG("Operand %x\n", operand);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0xDE:
        {
            // Absolute X   DEC oper   DE   3  7
            DEBUG_LOG("Absolute X DEC opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetAbsoluteAddr(state);
            uint16_t final_addr = base_addr + state->x;

            uint8_t *operand = &memory[final_addr];

            *operand -= 1;

            state->status.n = GET_NEG_BIT(*operand);  // Negative flag (bit 7)
            state->status.z = (*operand == 0) ? 1 : 0;    // Zero flag (is value zero?)

            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 7;
            break;
        }

        case 0xE0:
        {
            // Immediate   CPX #oper  C0  2  2
            DEBUG_LOG("Immediate CPX opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t operand = memory[++state->pc];

            CompareRegAndSetFlags(state, state->x, operand);

            DEBUG_LOG("Operand %x\n", operand);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 2;
            break;
        }

        case 0xE1:
        {
            // (Indirect, X)  SBC (oper, X)  E1  2  6
            DEBUG_LOG("Indirect X SBC opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t final_addr = GetPreIndexedIndirectAddr(state, state->x);
            uint8_t operand = memory[final_addr];
            uint16_t temp = state->a - operand - (1 - state->status.c);

            // Carry is set if no borrow occurs
            state->status.c = (state->a >= operand + (1 - state->status.c)) ? 1 : 0;
        
            // Set Overflow Flag: Checks signed overflow
            state->status.v = ((state->a ^ operand) & (state->a ^ temp) & 0x80) ? 1 : 0;
            state->a = (uint8_t)temp;

            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Operand %x\n", operand);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 6;
            break;
        }

        case 0xE4:
        {
            // Zeropage	CPX oper E4 2 3  
            DEBUG_LOG("Zeropage CPX opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];
            uint8_t operand = memory[addr];

            CompareRegAndSetFlags(state, state->x, operand);

            DEBUG_LOG("Operand %u\n", operand);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 3;
            break;
        }

        case 0xE5:
        {
            DEBUG_LOG("Zeropage SBC opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];
            uint8_t operand = memory[addr];
            uint16_t temp = state->a - operand - (1 - state->status.c);

            // Carry is set if no borrow occurs
            state->status.c = (state->a >= operand + (1 - state->status.c)) ? 1 : 0;
        
            // Set Overflow Flag: Checks signed overflow
            state->status.v = ((state->a ^ operand) & (state->a ^ temp) & 0x80) ? 1 : 0;
            state->a = (uint8_t)temp;

            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Operand %x\n", operand);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 3;
            break;
        }

        case 0xE6:
        {
            // Zeropage	INC oper   E6   2  5  
            DEBUG_LOG("Zeropage INC opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t addr = memory[++state->pc];
            uint8_t *operand = &memory[addr];

            *operand += 1;

            state->status.n = GET_NEG_BIT(*operand);  // Negative flag (bit 7)
            state->status.z = (*operand == 0) ? 1 : 0;    // Zero flag (is value zero?)

            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 5;
            break;
        }

        case 0xE8:
        {
            DEBUG_LOG("Implied INX opcode %x at addr 0x%x\n", opcode, state->pc);
            state->x++;
            state->status.n = GET_NEG_BIT(state->x);  // Negative flag (bit 7)
            state->status.z = (state->x == 0) ? 1 : 0;    // Zero flag (is X zero?)
            state->cycles += 2;
            break;
        }

        case 0xE9:
        {
            DEBUG_LOG("Immediate SBC opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t operand = memory[++state->pc];
            uint16_t temp = state->a - operand - (1 - state->status.c);

            // Carry is set if no borrow occurs
            state->status.c = (state->a >= operand + (1 - state->status.c)) ? 1 : 0;
        
            // Set Overflow Flag: Checks signed overflow
            state->status.v = ((state->a ^ operand) & (state->a ^ temp) & 0x80) ? 1 : 0;
            state->a = (uint8_t)temp;

            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Operand %x\n", operand);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 2;
            break;
        }

        case 0XEA:
        {
            DEBUG_LOG("Implied NOP opcode %x at addr 0x%x\n", opcode, state->pc);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 2;
            break;
        }

        case 0xEC:
        {
            // Absolute	CPX oper EC 3 4
            DEBUG_LOG("Absolute CPX opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t addr = GetAbsoluteAddr(state);
            uint8_t operand = memory[addr];
            uint8_t result = state->x - operand;

            state->status.n = GET_NEG_BIT(result);  // Negative flag (bit 7)
            state->status.z = (result == 0) ? 1 : 0;    // Zero flag (is result zero?)
            // Update the Carry Flag (C)
            state->status.c = (state->x >= operand) ? 1 : 0;

            DEBUG_LOG("Operand %u\n", operand);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0xED:
        {
            DEBUG_LOG("Absolute SBC opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t addr = GetAbsoluteAddr(state);
            uint8_t operand = memory[addr];
            uint16_t temp = state->a - operand - (1 - state->status.c);

            // Carry is set if no borrow occurs
            state->status.c = (state->a >= operand + (1 - state->status.c)) ? 1 : 0;
        
            // Set Overflow Flag: Checks signed overflow
            state->status.v = ((state->a ^ operand) & (state->a ^ temp) & 0x80) ? 1 : 0;
            state->a = (uint8_t)temp;

            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Operand %x\n", operand);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0xEE:
        {
            // Absolute	INC oper   EE   3  6  
            DEBUG_LOG("Absolute INC opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t addr = GetAbsoluteAddr(state);
            uint8_t *operand = &memory[addr];

            *operand += 1;

            state->status.n = GET_NEG_BIT(*operand);  // Negative flag (bit 7)
            state->status.z = (*operand == 0) ? 1 : 0;    // Zero flag (is value zero?)

            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 6;
            break;
        }

        case 0xF0:
        {
            // relative BEQ oper
            DEBUG_LOG("Relative BEQ opcode %x at addr 0x%x\n", opcode, state->pc);
            int8_t offset = (int8_t)memory[++state->pc];
            if (state->status.z)
            {
                // Extra cycle if the branch crosses a page boundary
                if (!InsidePage(state->pc, state->pc + offset))
                    state->cycles++;

                state->pc += offset;
                state->cycles++;
                //skip_next_instr = true;
            }
            DEBUG_LOG("PC Offset %d\n", offset);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 2;
            break;
        }

        case 0xF1:
        {
            // (Indirect), Y  SBC (oper), Y  F1  2  5*
            DEBUG_LOG("Indirect Y SBC opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetPostIndexedIndirectAddr(state);
            uint16_t final_addr = base_addr + state->y;
            uint8_t operand = memory[final_addr];
            uint16_t temp = state->a - operand - (1 - state->status.c);

            // Extra cycle if pages are different
            if (!InsidePage(base_addr, final_addr))
                state->cycles++;

            // Carry is set if no borrow occurs
            state->status.c = (state->a >= operand + (1 - state->status.c)) ? 1 : 0;
        
            // Set Overflow Flag: Checks signed overflow
            state->status.v = ((state->a ^ operand) & (state->a ^ temp) & 0x80) ? 1 : 0;

            state->a = (uint8_t)temp;

            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Operand %x\n", operand);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 5;
            break;
        }

        case 0xF5:
        {
            DEBUG_LOG("Zeropage X SBC opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t base_addr = memory[++state->pc];
            uint16_t final_addr = (base_addr + state->x) & PAGE_MASK;
            uint8_t operand = memory[final_addr];
            uint16_t temp = state->a - operand - (1 - state->status.c);

            // Carry is set if no borrow occurs
            state->status.c = (state->a >= operand + (1 - state->status.c)) ? 1 : 0;
        
            // Set Overflow Flag: Checks signed overflow
            state->status.v = ((state->a ^ operand) & (state->a ^ temp) & 0x80) ? 1 : 0;
            state->a = (uint8_t)temp;

            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Operand %x\n", operand);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0xF6:
        {
            // Zeropage X	INC oper, X   F6   2  6
            DEBUG_LOG("Zeropage INC opcode %x at addr 0x%x\n", opcode, state->pc);
            uint8_t base_addr = memory[++state->pc];
            uint16_t final_addr = (base_addr + state->x) & PAGE_MASK;
            uint8_t *operand = &memory[final_addr];

            *operand += 1;

            state->status.n = GET_NEG_BIT(*operand);  // Negative flag (bit 7)
            state->status.z = (*operand == 0) ? 1 : 0;    // Zero flag (is value zero?)

            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 6;
            break;
        }

        case 0xF8:
        {
            // Set Decimal Flag
            DEBUG_LOG("Implied SED opcode %x at addr 0x%x\n", opcode, state->pc);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->status.d = true;
            state->cycles += 2;
            break;
        }

        case 0xF9:
        {
            DEBUG_LOG("Absolute Y SBC opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetAbsoluteAddr(state);
            uint16_t final_addr = base_addr + state->y;
            uint8_t operand = memory[final_addr];
            uint16_t temp = state->a - operand - (1 - state->status.c);

            if (!InsidePage(base_addr, final_addr))
                state->cycles++;

            // Carry is set if no borrow occurs
            state->status.c = (state->a >= operand + (1 - state->status.c)) ? 1 : 0;
        
            // Set Overflow Flag: Checks signed overflow
            state->status.v = ((state->a ^ operand) & (state->a ^ temp) & 0x80) ? 1 : 0;
            state->a = (uint8_t)temp;

            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Operand %x\n", operand);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0xFD:
        {
            DEBUG_LOG("Absolute X SBC opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetAbsoluteAddr(state);
            uint16_t final_addr = base_addr + state->x;
            uint8_t operand = memory[final_addr];
            uint16_t temp = state->a - operand - (1 - state->status.c);

            if (!InsidePage(base_addr, final_addr))
                state->cycles++;

            // Carry is set if no borrow occurs
            state->status.c = (state->a >= operand + (1 - state->status.c)) ? 1 : 0;
        
            // Set Overflow Flag: Checks signed overflow
            state->status.v = ((state->a ^ operand) & (state->a ^ temp) & 0x80) ? 1 : 0;
            state->a = (uint8_t)temp;
    
            state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
            state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
            DEBUG_LOG("Operand %x\n", operand);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 4;
            break;
        }

        case 0xFE:
        {
            // Absolute X   INC oper   FE   3  7
            DEBUG_LOG("Absolute INC opcode %x at addr 0x%x\n", opcode, state->pc);
            uint16_t base_addr = GetAbsoluteAddr(state);
            uint16_t final_addr = base_addr + state->x;

            uint8_t *operand = &memory[final_addr];

            *operand += 1;

            state->status.n = GET_NEG_BIT(*operand);  // Negative flag (bit 7)
            state->status.z = (*operand == 0) ? 1 : 0;    // Zero flag (is value zero?)

            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            state->cycles += 7;
            break;
        }

        default:
            printf("\nUnimplemented opcode %x at addr 0x%x\n", opcode, state->pc);
            DEBUG_LOG("Addr Mode %x, Group %x, InstrGroup %x\n", addr_mode, group, instr_group);
            free(state);
            exit(EXIT_FAILURE);
            break;
    }

    if (!skip_next_instr)
        state->pc++;
}

void load_binary(const char *filename, uint16_t start_address)
{
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Failed to open file");
        return;
    }

    // Skip the header
    fseek(file, 64, SEEK_SET);

    // Calculate the size of the program data
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file) - 64;
    fseek(file, 64, SEEK_SET);

    // Check if the program data fits in memory
    if (file_size > (MEMORY_SIZE - start_address))
    {
        fprintf(stderr, "Program data is too large to fit in memory\n");
        fclose(file);
        return;
    }

    // Read the program data into memory
    fread(&memory[start_address], 1, file_size, file);
    fclose(file);

    DEBUG_LOG("Loaded program data (%ld bytes) into memory at 0x%04X\n", file_size, start_address);
}

void load_bin_file(const char *filename, uint16_t load_address)
{
    FILE *file = fopen(filename, "rb");  // Open in binary mode
    if (!file) {
        perror("Error opening file");
        return;
    }

    // Read binary file into memory starting at load_address
    size_t bytes_read = fread(&memory[load_address], 1, MEMORY_SIZE - load_address, file);
    // Check if the program data fits in memory
    if (bytes_read > (MEMORY_SIZE - load_address))
    {
        fprintf(stderr, "Program data is too large to fit in memory\n");
        fclose(file);
        return;
    }

    fclose(file);
    printf("Loaded %zu bytes into memory at 0x%04X\n", bytes_read, load_address);
}

void StartCPU(_6502_State *state)
{
    state = calloc(1, sizeof(*state));
    state->sp = 0xFF;
}

void ResetCPU(_6502_State *state)
{
    // Reset the 6502 (set PC to the reset vector)
    uint16_t reset_vector = memory[0xFFFC] | (memory[0xFFFD] << 8);
    state->pc = reset_vector;
    printf("Reset vector: 0x%04X\n", reset_vector);
}

#define TARGET_FREQ 1789773.0  // Target frequency in Hz
#define TARGET_TIME_PER_INSTR (1.0 / TARGET_FREQ)  // Time per instruction in seconds

struct timespec timespec_diff(struct timespec start, struct timespec end) {
    struct timespec temp;
    if ((end.tv_nsec - start.tv_nsec) < 0) {
        temp.tv_sec = end.tv_sec - start.tv_sec - 1;
        temp.tv_nsec = 1000000000 + end.tv_nsec - start.tv_nsec;
    } else {
        temp.tv_sec = end.tv_sec - start.tv_sec;
        temp.tv_nsec = end.tv_nsec - start.tv_nsec;
    }
    return temp;
}

double timespec_to_sec(struct timespec ts) {
    return ts.tv_sec + (ts.tv_nsec / 1e9);
}

uint16_t GetResetVectorFromMem()
{
    return  (uint16_t)memory[0xFFFC] | (memory[0xFFFD] << 8);
}

uint16_t GetResetVector(uint8_t *rom, size_t prg_size)
{
    // Locate the last 16K PRG-ROM bank
    size_t last_bank_offset = 16 + (prg_size - 0x4000); // Skip iNES header (16 bytes)
    
    // Read reset vector at 0xFFFC-0xFFFD within the last bank
    uint16_t reset_vector = rom[last_bank_offset + 0x3FFC] | (rom[last_bank_offset + 0x3FFD] << 8);
    
    return reset_vector;
}

int main(int argc, char **argv)
{
    _6502_State *cpu_state = malloc(sizeof(*cpu_state));
    StartCPU(cpu_state);
    PPU_Init();

    if (argc > 1)
    {
        // Load nes2 rom
        FILE *fp = fopen(argv[1], "rb");
        if (!fp)
        {
            free(cpu_state);
            return 1;
        }
        NES2_Header *nes2 = malloc(sizeof(*nes2));
        fread(nes2, 1, 16, fp);
        bool ines_format = false;
        bool nes2_format = false;
        if (nes2->id_string[0] =='N' && nes2->id_string[1] =='E' && nes2->id_string[2] =='S' && nes2->id_string[3]==0x1A)
            ines_format=true;
        
        if (ines_format == true && nes2->nes2_id == 0x08)
            nes2_format=true;

        uint32_t rom_size = 0;
        if (nes2_format)
        {
            printf("ID String: %s\n", nes2->id_string);
            printf("NES 2.0 ID: %d\n", nes2->nes2_id);
            printf("Nametable layout: %d\n", nes2->name_table_layout);
            printf("Console type: %d\n", nes2->console_type);
            printf("CPU/PPU timing mode: %d\n", nes2->timing_mode);
            printf("PRG Rom LSB: %d\n", nes2->prg_rom_size_lsb);
            printf("PRG Rom MSB: %d\n", nes2->prg_rom_size_msb);
            printf("CHR Rom LSB: %d\n", nes2->chr_rom_size_lsb);
        }
        else if (ines_format)
        {
            printf("ID String: %s\n", nes2->id_string);
            printf("PRG Rom Size in 16 KiB units: %d\n", nes2->prg_rom_size_lsb);
            printf("CHR Rom Size in 8 KiB units: %d\n", nes2->chr_rom_size_lsb);
            printf("Nametable layout: %d\n", nes2->name_table_layout);
            printf("Battery: %d\n", nes2->battery);
            printf("Trainer: %d\n", nes2->trainer_area_512);
            printf("Alt nametable layout: %d\n", nes2->alt_name_tables);
            printf("Mapper: %d\n", nes2->mapper_number_d3d0);
            rom_size = nes2->prg_rom_size_lsb * 0x4000;
            if (rom_size)
            {
                uint8_t rom[rom_size];
                if (!nes2->trainer_area_512)
                    fseek(fp, 16, SEEK_SET);
                else
                    fseek(fp, 512 + 16, SEEK_SET);

                // Read all banks
                char filenames[16] = {'\0'};
                uint16_t base_addr = 0x8000;
                for (int i = 0; i < nes2->prg_rom_size_lsb; i++)
                {
                    sprintf(filenames, "bank%d.bin", i);
                    FILE *bank = fopen(filenames, "wb");
                    uint16_t bank_addr = 16 + (i * 0x4000);
                    //if (i != 7)
                    //{
                        fseek(fp, bank_addr, SEEK_SET);
                        fread(&rom[bank_addr - 16], 1, 0x4000, fp);
                        fwrite(&rom[bank_addr - 16], 1, 0x4000, bank);
                        if (i == 0)
                            memcpy(&memory[0x8000], &rom[bank_addr - 16], 0x4000);
                        else
                            memcpy(&memory[0xC000], &rom[bank_addr - 16], 0x4000);
                        //if (i == 7)
                        //{
                        //    memcpy(&memory[0xC000], &rom[bank_addr - 16], 0x4000);
                        //}
                        //else
                        //{
                        //    memcpy(&memory[base_addr += 0x4000], &rom[bank_addr - 16], 0x4000);
                        //}
                        fclose(bank);
                }
                cpu_state->pc = 0x8000; //GetResetVectorFromMem(); //GetResetVector(rom, rom_size);
            }
        }

        fclose(fp);
        //free(cpu_state);
        //free(nes2);
        //return 0;
    }
    else
    {
        load_bin_file("6502_functional_test.bin", 0);
        cpu_state->pc = 0x400;
    }

    //FILE *fp = fopen("6502_functional_test.bin", "r");
    //fread(memory + 0x800, 0x200, 1, fp);
    //load_binary("6502_functional_test.bin", 0x00);

    uint32_t max_ticks = 200000000;
    struct timespec start_time, instr_start, instr_end, elapsed;
    
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    while (max_ticks > 0)
    {
        //ExcecuteInstr(cpu_state);
        ExecuteOpcode(cpu_state);
        max_ticks--;
    /*
        // Measure execution time
        clock_gettime(CLOCK_MONOTONIC, &instr_end);
        elapsed = timespec_diff(instr_start, instr_end);
        double time_spent = timespec_to_sec(elapsed);

        // If executed too fast, sleep for the remaining time
        double sleep_time = TARGET_TIME_PER_INSTR - time_spent;
        if (sleep_time > 0) {
            struct timespec sleep_ts;
            sleep_ts.tv_sec = (time_t)sleep_time;
            sleep_ts.tv_nsec = (sleep_time - sleep_ts.tv_sec) * 1e9;
            nanosleep(&sleep_ts, NULL);  // Sleep for remaining time
        }
    */
    }

    printf("Finished with PC at %d (0x%04x)\n", cpu_state->pc, cpu_state->pc);
    printf("Did %lu cycles in %d ticks\n", cpu_state->cycles, 200000000);
    free(cpu_state);
    return 0;
}