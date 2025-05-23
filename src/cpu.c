#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#include "apu.h"

#include "ppu.h"
#include "joypad.h"
#include "arena.h"
#include "cart.h"
#include "mapper.h"
#include "cpu.h"
#include "bus.h"
#include "utils.h"

static uint8_t CpuRead8(const uint16_t addr)
{
    return BusRead(addr);
}

static void CpuWrite8(const uint16_t addr, const uint8_t data)
{
    BusWrite(addr, data);
}

static uint16_t CpuReadVector(uint16_t addr)
{
    return (uint16_t)CpuRead8(addr + 1) << 8 | CpuRead8(addr);
}

static inline void StackPush(Cpu *state, uint8_t data)
{
    uint8_t *ptr = BusGetPtr(STACK_START + state->sp--);
    *ptr = data;
}

// Retrieve the value on the top of the stack and then pop it
static inline uint8_t StackPull(Cpu *state)
{
    return *BusGetPtr(STACK_START + (++state->sp));
}

static bool InsidePage(uint16_t src_addr, uint16_t dst_addr)
{
    // return ((src_addr / PAGE_SIZE) == (dst_addr / PAGE_SIZE));
    // Using faster version
    return ((src_addr & 0xFF00) == (dst_addr & 0xFF00));
}

static inline bool CpuPollIRQ(Cpu *cpu)
{
    return !cpu->status.i && cpu->irq_pending;
}

static void HandleIRQ(Cpu *cpu)
{
    //cpu->irq_ready = false;
    StackPush(cpu, (cpu->pc >> 8) & 0xFF);
    StackPush(cpu, cpu->pc & 0xFF);
    // Push Processor Status (clear Break flag)
    Flags status = cpu->status;
    status.b = 0;
    status.unused = 1;
    StackPush(cpu, status.raw);
    cpu->status.i = 1;
    cpu->pc = CpuReadVector(0xFFFE);
}

// PC += 2 
static inline uint16_t GetAbsoluteAddr(Cpu *state)
{
    uint8_t addr_low = CpuRead8(++state->pc);
    uint8_t addr_high = CpuRead8(++state->pc);
    return (uint16_t)addr_high << 8 | addr_low;
}

// PC += 2 
static inline uint16_t GetAbsoluteXAddr(Cpu *state, bool add_cycle)
{
    uint16_t base_addr = GetAbsoluteAddr(state);
    uint16_t final_addr = base_addr + state->x;

    // Apply extra cycle only if required
    state->cycles += (add_cycle && !InsidePage(base_addr, final_addr));

    return final_addr;
}

static inline uint16_t GetAbsoluteYAddr(Cpu *state, bool add_cycle)
{
    uint16_t base_addr = GetAbsoluteAddr(state);
    uint16_t final_addr = base_addr + state->y;

    // Apply extra cycle only if required
    state->cycles += (add_cycle && !InsidePage(base_addr, final_addr));

    return final_addr;
}

// PC += 1
static inline uint8_t GetZPAddr(Cpu *state)
{
    return CpuRead8(++state->pc);
}

// PC += 1
static inline uint16_t GetZPIndexedAddr(Cpu *state, uint8_t reg)
{
    uint8_t zp_addr = CpuRead8(++state->pc);

    // Wrap in zero-page
    return (zp_addr + reg) & PAGE_MASK;
}

// PC += 2
static inline uint16_t GetIndirectAddr(Cpu *state)
{
    uint8_t ptr_low = CpuRead8(++state->pc);
    uint8_t ptr_high = CpuRead8(++state->pc);
    
    uint16_t ptr = (uint16_t)ptr_high << 8 | ptr_low;
    uint16_t new_pc;
    // 6502 Page Boundary Bug** (If ptr is at 0xXXFF, high byte comes from 0xXX00, not 0xXXFF+1)
    if ((ptr & 0xFF) == 0xFF)
    {
        new_pc = (uint16_t)CpuRead8(ptr) | ((uint16_t)CpuRead8(ptr & 0xFF00) << 8);
    }
    else
    {
        new_pc = (uint16_t)CpuRead8(ptr) | ((uint16_t)CpuRead8(ptr + 1) << 8);
    }

    return new_pc;
}

// PC += 1
static inline uint16_t GetPostIndexedIndirectAddr(Cpu *state, bool page_cycle)
{
    uint8_t zp_addr = GetZPAddr(state);
    uint8_t addr_low = CpuRead8(zp_addr);
    // Fetch high (with zero-page wraparound)
    uint8_t addr_high = CpuRead8((zp_addr + 1) & PAGE_MASK);

    uint16_t base_addr = (uint16_t)addr_high << 8 | addr_low;
    uint16_t final_addr = base_addr + state->y;

    // Extra cycle if in different pages
    state->cycles += (page_cycle && !InsidePage(base_addr, final_addr));

    return final_addr;
}

// PC += 1
static inline uint16_t GetPreIndexedIndirectAddr(Cpu *state, uint8_t reg)
{
    uint8_t zp_addr = GetZPAddr(state);
    // Wrap in zero-page
    uint8_t effective_ptr = (zp_addr + reg) & PAGE_MASK;
    uint8_t addr_low = CpuRead8(effective_ptr);
    // Wrap in zero-page
    uint8_t addr_high = CpuRead8((effective_ptr + 1) & PAGE_MASK);
    return (uint16_t)addr_high << 8 | addr_low;
}

static inline void CompareRegAndSetFlags(Cpu *state, uint8_t reg, uint8_t operand)
{
    uint8_t result = reg - operand;
    // Negative flag (bit 7)
    state->status.n = GET_NEG_BIT(result);
    // Zero flag (is result zero?)
    state->status.z = (result == 0) ? 1 : 0;
    // Update the Carry Flag (C)
    state->status.c = (reg >= operand) ? 1 : 0;
}

static inline void RotateOneLeft(Cpu *state, uint8_t *operand)
{
    uint8_t old_carry = state->status.c;
    // Store bit 7 in carry before rotating
    state->status.c = (*operand >> 7) & 1;
    // Shift all bits left one position and insert old carry into bit 0
    *operand = (*operand << 1) | old_carry;
    // Update status flags
    UPDATE_FLAGS_NZ(*operand);
}

static inline void RotateOneLeftFromMem(Cpu *state, const uint16_t operand_addr)
{
    uint8_t operand = CpuRead8(operand_addr);
    uint8_t old_carry = state->status.c;
    // Store bit 7 in carry before rotating
    state->status.c = (operand >> 7) & 1;
    // Shift all bits left one position and insert old carry into bit 0
    operand = (operand << 1) | old_carry;
    // Write to the bus
    CpuWrite8(operand_addr, operand);
    // Update status flags
    UPDATE_FLAGS_NZ(operand);
}

static inline void RotateOneRight(Cpu *state, uint8_t *operand)
{
    uint8_t old_carry = state->status.c;
    // Store bit 0 in carry before rotating
    state->status.c = *operand & 1;
    // Shift all bits right one position and insert old carry into bit 7
    *operand = (*operand >> 1) | (old_carry << 7);
    // Update status flags
    UPDATE_FLAGS_NZ(*operand);
}

static inline void RotateOneRightFromMem(Cpu *state, const uint16_t operand_addr)
{
    uint8_t operand = CpuRead8(operand_addr);
    uint8_t old_carry = state->status.c;
    // Store bit 0 in carry before rotating
    state->status.c = operand & 1;
    // Shift all bits right one position and insert old carry into bit 7
    operand = (operand >> 1) | (old_carry << 7);
    // Write to the bus
    CpuWrite8(operand_addr, operand);
    // Update status flags
    UPDATE_FLAGS_NZ(operand);
}

static inline void ShiftOneRight(Cpu *state, uint8_t *operand)
{
    // Store bit 0 in carry before shifting
    state->status.c = *operand & 1;
    // Shift all bits left by one position
    *operand >>= 1;
    // Update status flags
    state->status.n = 0;    // Clear N flag 
    // Zero flag (is A zero?)
    state->status.z = (*operand == 0) ? 1 : 0;
}

static inline void ShiftOneRightFromMem(Cpu *state, const uint16_t operand_addr)
{
    uint8_t operand = CpuRead8(operand_addr);
    // Store bit 0 in carry before shifting
    state->status.c = operand & 1;
    // Shift all bits left by one position
    operand >>= 1;
    // Write to the bus
    CpuWrite8(operand_addr, operand);
    // Update status flags
    state->status.n = 0;    // Clear N flag 
    // Zero flag (is operand zero?)
    state->status.z = (operand == 0) ? 1 : 0;
}

static inline void ShiftOneLeft(Cpu *state, uint8_t *operand)
{
    // Store bit 7 in carry before shifting
    state->status.c = (*operand >> 7) & 1;
    // Shift all bits left by one position
    *operand <<= 1;
    // Update status flags
    UPDATE_FLAGS_NZ(*operand);
}

static inline void ShiftOneLeftFromMem(Cpu *state, const uint16_t operand_addr)
{
    uint8_t operand = CpuRead8(operand_addr);
    // Store bit 7 in carry before shifting
    state->status.c = (operand >> 7) & 1;
    // Shift all bits left by one position
    operand <<= 1;
    // Write to the bus
    CpuWrite8(operand_addr, operand);
    // Update status flags
    UPDATE_FLAGS_NZ(operand);
}

// ADC only uses the A register (Accumulator)
static inline void AddWithCarry(Cpu *state, uint8_t operand)
{
    uint16_t sum = state->a + operand + state->status.c;

    // Set Carry Flag (C) - Set if result is > 255 (unsigned overflow)
    state->status.c = (sum > UINT8_MAX) ? 1 : 0;

    // Set Overflow Flag (V) - Detect signed overflow
    state->status.v = (~(state->a ^ operand) & (state->a ^ sum) & 0x80) ? 1 : 0;

    // Store result
    state->a = (uint8_t)sum;
    
    // Update status flags
    UPDATE_FLAGS_NZ(state->a);

    CPU_LOG("Operand %x\n", operand);
}

static inline uint8_t GetOperandFromMem(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    switch (addr_mode)
    {
        case Accumulator:
            return state->a;
        case Relative:
        case Immediate:
            return CpuRead8(++state->pc);
        case ZeroPage:
            return CpuRead8(GetZPAddr(state));
        case ZeroPageX:
            return CpuRead8(GetZPIndexedAddr(state, state->x));
        case ZeroPageY:
            return CpuRead8(GetZPIndexedAddr(state, state->y));
        case Absolute:
            return CpuRead8(GetAbsoluteAddr(state));
        case AbsoluteX:
            return CpuRead8(GetAbsoluteXAddr(state, page_cycle));
        case AbsoluteY:
            return CpuRead8(GetAbsoluteYAddr(state, page_cycle));
        case PreIndexedIndirect:
            return CpuRead8(GetPreIndexedIndirectAddr(state, state->x));
        case PostIndexedIndirect:
            return CpuRead8(GetPostIndexedIndirectAddr(state, page_cycle));
        case Implied:
        case Indirect:
            break;
    }

    return -1;
}

// Used by STA/STY/STX instrs
static inline void SetOperandToMem(Cpu *state, AddressingMode addr_mode, uint8_t operand)
{
    switch (addr_mode)
    {
        case Accumulator:
        case Relative:
        case Immediate:
            CpuWrite8(++state->pc,  operand);
            break;
        case ZeroPage:
            CpuWrite8(GetZPAddr(state), operand);
            break;
        case ZeroPageX:
            CpuWrite8(GetZPIndexedAddr(state, state->x), operand);
            break;
        case ZeroPageY:
            CpuWrite8(GetZPIndexedAddr(state, state->y), operand);
            break;
        case Absolute:
            CpuWrite8(GetAbsoluteAddr(state), operand);
            break;
        case AbsoluteX:
            CpuWrite8(GetAbsoluteAddr(state) + state->x, operand);
            break;
        case AbsoluteY:
            CpuWrite8(GetAbsoluteAddr(state) + state->y, operand);
            break;
        case PreIndexedIndirect:
            CpuWrite8(GetPreIndexedIndirectAddr(state, state->x), operand);
            break;
        case PostIndexedIndirect:
            CpuWrite8(GetPostIndexedIndirectAddr(state, false),  operand);
            break;
        case Implied:
        case Indirect:
            break;
        default:
            printf("Unknown or invalid adddress mode!: %d\n", addr_mode);
            break;
    }
}

static inline uint16_t GetOperandAddrFromMem(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    switch (addr_mode)
    {
        case Relative:
        case Immediate:
            return ++state->pc;
        case ZeroPage:
            return GetZPAddr(state);
        case ZeroPageX:
            return GetZPIndexedAddr(state, state->x);
        case ZeroPageY:
            return GetZPIndexedAddr(state, state->y);
        case Absolute:
            return GetAbsoluteAddr(state);
        case AbsoluteX:
            return GetAbsoluteXAddr(state, page_cycle);
        case AbsoluteY:
            return GetAbsoluteYAddr(state, page_cycle);
        case PreIndexedIndirect:
            return GetPreIndexedIndirectAddr(state, state->x);
        case PostIndexedIndirect:
            return GetPostIndexedIndirectAddr(state, page_cycle);
        case Implied:
        case Indirect:
        case Accumulator:
            return 0;
    }

    return 0;
}

static inline void ADC_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    AddWithCarry(state, operand);
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void AND_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);

    state->a &= operand;

    // Update status flags
    UPDATE_FLAGS_NZ(state->a);
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void ASL_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    if (addr_mode == Accumulator)
    {
        ShiftOneLeft(state, &state->a);
    }
    else
    {
        ShiftOneLeftFromMem(state, GetOperandAddrFromMem(state, addr_mode, page_cycle));
    }

    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void BCC_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++state->pc);

    if (!state->status.c)
    {
        // Extra cycle if the branch crosses a page boundary
        state->cycles += !InsidePage(state->pc, state->pc + offset);

        state->pc += offset;
        state->cycles++;
        CPU_LOG("BCC pc offset: %d\n", offset);
    }
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void BCS_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++state->pc);
    if (state->status.c)
    {
        // Extra cycle if the branch crosses a page boundary
        state->cycles += !InsidePage(state->pc, state->pc + offset);

        state->pc += offset;
        state->cycles++;
        CPU_LOG("BCS pc offset: %d\n", offset);
    }
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void BEQ_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++state->pc);
    if (state->status.z)
    {
        // Extra cycle if the branch crosses a page boundary
        state->cycles += !InsidePage(state->pc, state->pc + offset);

        state->pc += offset;
        state->cycles++;
        CPU_LOG("BEQ pc offset: %d\n", offset);
    }
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void BIT_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    state->status.n = GET_NEG_BIT(operand);
    state->status.v = GET_OVERFLOW_BIT(operand);
    state->status.z = !(state->a & operand);
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void BMI_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++state->pc);
    if (state->status.n)
    {
        // Extra cycle if the branch crosses a page boundary
        state->cycles += !InsidePage(state->pc, state->pc + offset);

        state->pc += offset;
        state->cycles++;
        CPU_LOG("BMI pc offset: %d\n", offset);
    }
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void BNE_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++state->pc);
    if (!state->status.z)
    {
        // Extra cycle if the branch crosses a page boundary
        state->cycles += !InsidePage(state->pc, state->pc + offset);

        state->pc += offset;
        state->cycles++;
        CPU_LOG("BNE pc offset: %d\n", offset);
    }
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void BPL_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++state->pc);
    if (!state->status.n)
    {
        // Extra cycle if the branch crosses a page boundary
        state->cycles += !InsidePage(state->pc, state->pc + offset);

        state->pc += offset;
        state->cycles++;
        CPU_LOG("BPL pc offset: %d\n", offset);
        //printf("BPL cross page triggered 0x%X --> 0x%X\n", state->pc, state->pc + offset);
    }
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void BRK_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    // Implied BRK	00	1	7
    state->pc += 2;
    StackPush(state, (state->pc >> 8) & 0xFF);
    StackPush(state, state->pc & 0xFF);

    // Push status status regs with the b(bit4) and bit5 flag set
    Flags status = state->status;
    status.b = 1;
    status.unused = 1;
    StackPush(state, status.raw);

    state->status.i = true;
    // Load IRQ vector ($FFFE-$FFFF) into PC
    state->pc = CpuReadVector(0xFFFE);

    CPU_LOG("Jumping to IRQ vector at 0x%X\n", state->pc);
}

static inline void BVC_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++state->pc);
    if (!state->status.v)
    {
        // Extra cycle if the branch crosses a page boundary
        state->cycles += !InsidePage(state->pc, state->pc + offset);

        state->pc += offset;
        state->cycles++;
        CPU_LOG("BVC pc offset: %d\n", offset);
    }
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void BVS_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++state->pc);
    if (state->status.v)
    {
        // Extra cycle if the branch crosses a page boundary
        state->cycles += !InsidePage(state->pc, state->pc + offset);

        state->pc += offset;
        state->cycles++;
        CPU_LOG("PC Offset %d\n", offset);
    }
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void CLC_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->status.c = 0;
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void CLD_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->status.d = 0;
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void CLI_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->irq_ready = CpuPollIRQ(state);
    state->status.i = 0;
    state->pc++;
}

static inline void CLV_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->status.v = 0;
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void CMP_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    CompareRegAndSetFlags(state, state->a, operand);
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void CPX_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    CompareRegAndSetFlags(state, state->x, operand);
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void CPY_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    CompareRegAndSetFlags(state, state->y, operand);
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void DEC_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    const uint16_t operand_addr = GetOperandAddrFromMem(state, addr_mode, page_cycle);
    const uint8_t operand = CpuRead8(operand_addr) - 1;

    CpuWrite8(operand_addr, operand);
    // Update status flags
    UPDATE_FLAGS_NZ(operand);

    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void DEX_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->x--;
    // Update status flags
    UPDATE_FLAGS_NZ(state->x);

    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void DEY_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->y--;

    // Update status flags
    UPDATE_FLAGS_NZ(state->y);
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void EOR_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    state->a ^= operand;

    // Update status flags
    UPDATE_FLAGS_NZ(state->a);
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void INC_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    if (addr_mode == Accumulator)
    {
        state->a += 1;

        UPDATE_FLAGS_NZ(state->a);
    }
    else
    {
        const uint16_t operand_addr = GetOperandAddrFromMem(state, addr_mode, page_cycle);
        const uint8_t operand = CpuRead8(operand_addr) + 1;
        CpuWrite8(operand_addr, operand);
        UPDATE_FLAGS_NZ(operand);
    }

    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void INX_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->x++;

    // Update status flags
    UPDATE_FLAGS_NZ(state->x);

    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void INY_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->y++;

    // Update status flags
    UPDATE_FLAGS_NZ(state->y);

    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void JMP_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    UNUSED(page_cycle);

    state->pc = addr_mode == Absolute ? GetAbsoluteAddr(state) : GetIndirectAddr(state);
    state->irq_ready = CpuPollIRQ(state);
}

static inline void JSR_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    uint8_t pc_low = CpuRead8(++state->pc);
    uint8_t pc_high = CpuRead8(++state->pc);

    StackPush(state, (state->pc >> 8) & 0xFF);
    StackPush(state, state->pc & 0xFF);

    uint16_t new_pc = (uint16_t)pc_high << 8 | pc_low;
    state->pc = new_pc;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void LDA_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    state->a = GetOperandFromMem(state, addr_mode, page_cycle);

    // Update status flags
    UPDATE_FLAGS_NZ(state->a);
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void LDX_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    state->x = GetOperandFromMem(state, addr_mode, page_cycle);

    // Update status flags
    UPDATE_FLAGS_NZ(state->x);
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void LDY_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    state->y = GetOperandFromMem(state, addr_mode, page_cycle);

    // Update status flags
    UPDATE_FLAGS_NZ(state->y);
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void LSR_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    if (addr_mode == Accumulator)
    {
        ShiftOneRight(state, &state->a);
    }
    else
    {
        ShiftOneRightFromMem(state, GetOperandAddrFromMem(state, addr_mode, page_cycle));
    }
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void NOP_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    UNUSED(page_cycle);

    switch (addr_mode)
    {
        case Implied:
            state->pc++;
            break;
        case Immediate:
        case ZeroPage:
        case ZeroPageX:
            state->pc += 2;
            break;
        case Absolute:
        case AbsoluteX:
            state->pc += 3;
            break;
        default:
            printf("Bad addr mode!: %d\n", addr_mode);
            break;
    }
    state->irq_ready = CpuPollIRQ(state);
}

static inline void ORA_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    state->a |= operand;

    // Update status flags
    UPDATE_FLAGS_NZ(state->a);
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void PHA_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    StackPush(state, state->a);
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void PHP_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    Flags status = state->status;
    status.b = true;
    status.unused = true;
    StackPush(state, status.raw);
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void PLA_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->a = StackPull(state);
    UPDATE_FLAGS_NZ(state->a);
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void PLP_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->irq_ready = CpuPollIRQ(state);
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

static inline void ROL_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    if (addr_mode == Accumulator)
    {
        RotateOneLeft(state, &state->a);
    }
    else
    {
        RotateOneLeftFromMem(state, GetOperandAddrFromMem(state, addr_mode, page_cycle));
    }
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static void ROR_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    if (addr_mode == Accumulator)
    {
        RotateOneRight(state, &state->a);
    }
    else
    {
        RotateOneRightFromMem(state, GetOperandAddrFromMem(state, addr_mode, page_cycle));
    }
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void RTI_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

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
    state->pc = (uint16_t)pc_high << 8 | pc_low;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void RTS_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    uint8_t pc_low = StackPull(state);
    uint8_t pc_high = StackPull(state);

    uint16_t new_pc = (uint16_t)pc_high << 8 | pc_low;

    state->pc = new_pc;
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void SBC_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    uint16_t temp = state->a - operand - (1 - state->status.c);

    // Carry is set if no borrow occurs
    state->status.c = (state->a >= operand + (1 - state->status.c)) ? 1 : 0;

    // Set Overflow Flag: Checks signed overflow
    state->status.v = ((state->a ^ operand) & (state->a ^ temp) & 0x80) ? 1 : 0;
    state->a = (uint8_t)temp;

    UPDATE_FLAGS_NZ(state->a);
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void SEC_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->status.c = 1;
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void SED_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->status.d = 1;
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void SEI_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->irq_ready = CpuPollIRQ(state);
    state->status.i = 1;
    state->pc++;
}

static inline void STA_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    UNUSED(page_cycle);

    SetOperandToMem(state, addr_mode, state->a);
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void STX_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    UNUSED(page_cycle);

    SetOperandToMem(state, addr_mode, state->x);
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static inline void STY_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    UNUSED(page_cycle);

    SetOperandToMem(state, addr_mode, state->y);
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

// Transfer Accumulator to Index X
static inline void TAX_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->x = state->a;
    // Update status flags
    UPDATE_FLAGS_NZ(state->x);

    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

// Transfer Accumulator to Index Y
static inline void TAY_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->y = state->a;
    // Update status flags
    UPDATE_FLAGS_NZ(state->y);

    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

// Transfer Stack Pointer to Index X
static inline void TSX_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->x = state->sp;
    // Update status flags
    UPDATE_FLAGS_NZ(state->x);

    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

// Transfer Index X to Accumulator
static inline void TXA_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->a = state->x;
    // Update status flags
    UPDATE_FLAGS_NZ(state->a);

    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

// Transfer Index X to Stack Register
static inline void TXS_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->sp = state->x;

    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

// Transfer Index Y to Accumulator
static inline void TYA_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->a = state->y;

    UPDATE_FLAGS_NZ(state->a);
    state->pc++;
    state->irq_ready = CpuPollIRQ(state);
}

static const OpcodeHandler opcodes[256] =
{
    [0x00] = { BRK_Instr, "BRK", 1, 7, false, Implied  },
    [0x01] = { ORA_Instr, "ORA (ind,X)", 2, 6, false, PreIndexedIndirect },
    [0x04] = { NOP_Instr, "NOP", 2, 3, false, ZeroPage },
    [0x05] = { ORA_Instr, "ORA zp", 2, 3, false, ZeroPage },
    [0x06] = { ASL_Instr, "ASL zp", 2, 5, false, ZeroPage },
    [0x08] = { PHP_Instr, "PHP", 1, 3, false, Implied },
    [0x09] = { ORA_Instr, "ORA #imm", 2, 2, false, Immediate },
    [0x0A] = { ASL_Instr, "ASL A", 1, 2, false, Accumulator },
    [0x0C] = { NOP_Instr, "NOP", 3, 4, false, Absolute },
    [0x0D] = { ORA_Instr, "ORA abs", 3, 4, false, Absolute },
    [0x0E] = { ASL_Instr, "ASL abs", 3, 6, false, Absolute },

    [0x10] = { BPL_Instr, "BPL rel", 2, 2, true, Relative },
    [0x11] = { ORA_Instr, "ORA (ind),Y", 2, 5, true, PostIndexedIndirect},
    [0x14] = { NOP_Instr, "NOP", 2, 4, false, ZeroPageX },
    [0x15] = { ORA_Instr, "ORA zp,X", 2, 4, false, ZeroPageX },
    [0x16] = { ASL_Instr, "ASL zp,X", 2, 6, false, ZeroPageX },
    [0x18] = { CLC_Instr, "CLC", 1, 2, false, Implied },
    [0x19] = { ORA_Instr, "ORA abs,Y", 3, 4, true, AbsoluteY},
    [0x1A] = { NOP_Instr, "NOP", 1, 2, false, Implied },
    [0x1C] = { NOP_Instr, "NOP", 3, 4, true, AbsoluteX },
    [0x1D] = { ORA_Instr, "ORA abs,X", 3, 4, true, AbsoluteX },
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
    [0x34] = { NOP_Instr, "NOP", 2, 4, false, ZeroPageX },
    [0x35] = { AND_Instr, "AND zp,X", 2, 4, false, ZeroPageX },
    [0x36] = { ROL_Instr, "ROL zp,X", 2, 6, false, ZeroPageX },
    [0x38] = { SEC_Instr, "SEC", 1, 2, false, Implied },
    [0x39] = { AND_Instr, "AND abs,Y", 3, 4, true, AbsoluteY },
    [0x3A] = { NOP_Instr, "NOP", 1, 2, false, Implied },
    [0x3C] = { NOP_Instr, "NOP", 3, 4, true, AbsoluteX },
    [0x3D] = { AND_Instr, "AND abs,X", 3, 4, true, AbsoluteX },
    [0x3E] = { ROL_Instr, "ROL abs,X", 3, 7, false, AbsoluteX },

    [0x40] = { RTI_Instr, "RTI", 1, 6, false, Implied },
    [0x41] = { EOR_Instr, "EOR (ind,X)", 2, 6, false, PreIndexedIndirect },
    [0x44] = { NOP_Instr, "NOP", 2, 3, false, ZeroPage },
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
    [0x54] = { NOP_Instr, "NOP", 2, 4, false, ZeroPageX },
    [0x55] = { EOR_Instr, "EOR zp,X", 2, 4, false, ZeroPageX },
    [0x56] = { LSR_Instr, "LSR zp,X", 2, 6, false, ZeroPageX},
    [0x58] = { CLI_Instr, "CLI", 1, 2, false, Implied},
    [0x59] = { EOR_Instr, "EOR abs,Y", 3, 4, true, AbsoluteY},
    [0x5A] = { NOP_Instr, "NOP", 1, 2, false, Implied },
    [0x5C] = { NOP_Instr, "NOP", 3, 4, true, AbsoluteX },
    [0x5D] = { EOR_Instr, "EOR abs,X", 3, 4, true, AbsoluteX },
    [0x5E] = { LSR_Instr, "LSR abs,X", 3, 7, false, AbsoluteX },

    [0x60] = { RTS_Instr, "RTS", 1, 6, false, Implied },
    [0x61] = { ADC_Instr, "ADC (ind,X)", 2, 6, false, PreIndexedIndirect },
    [0x64] = { NOP_Instr, "NOP", 2, 3, false, ZeroPage },
    [0x65] = { ADC_Instr, "ADC zp", 2, 3, false, ZeroPage },
    [0x66] = { ROR_Instr, "ROR zp", 2, 5, false, ZeroPage },
    [0x68] = { PLA_Instr, "PLA", 1, 4, false, Implied },
    [0x69] = { ADC_Instr, "ADC #imm", 2, 2, false, Immediate },
    [0x6A] = { ROR_Instr, "ROR A", 1, 2, false, Accumulator },
    [0x6C] = { JMP_Instr, "JMP (ind)", 3, 5, false, Indirect },
    [0x6D] = { ADC_Instr, "ADC abs", 3, 4, false, Absolute },
    [0x6E] = { ROR_Instr, "ROR abs", 3, 6, false, Absolute },

    [0x70] = { BVS_Instr, "BVS rel", 2, 2, true, Relative },
    [0x71] = { ADC_Instr, "ADC (ind),Y", 2, 5, true, PostIndexedIndirect },
    [0x74] = { NOP_Instr, "NOP", 2, 4, false, ZeroPageX },
    [0x75] = { ADC_Instr, "ADC zp,X", 2, 4, false, ZeroPageX },
    [0x76] = { ROR_Instr, "ROR zp,X", 2, 6, false, ZeroPageX },
    [0x78] = { SEI_Instr, "SEI", 1, 2, false, Implied },
    [0x79] = { ADC_Instr, "ADC abs,Y", 3, 4, true, AbsoluteY },
    [0x7A] = { NOP_Instr, "NOP", 1, 2, false, Implied },
    [0x7C] = { NOP_Instr, "NOP", 3, 4, true, AbsoluteX },
    [0x7D] = { ADC_Instr, "ADC abs,X", 3, 4, true, AbsoluteX },
    [0x7E] = { ROR_Instr, "ROR abs,X", 3, 7, false, AbsoluteX },

    [0x80] = { NOP_Instr, "NOP", 2, 2, false, Immediate },
    [0x81] = { STA_Instr, "STA (ind,X)", 2, 6, false, PreIndexedIndirect },
    [0x82] = { NOP_Instr, "NOP", 2, 2, false, Immediate },
    [0x84] = { STY_Instr, "STY zp", 2, 3, false, ZeroPage },
    [0x85] = { STA_Instr, "STA zp", 2, 3, false, ZeroPage },
    [0x86] = { STX_Instr, "STX zp", 2, 3, false, ZeroPage },
    [0x88] = { DEY_Instr, "DEY", 1, 2, false, Implied },
    [0x89] = { NOP_Instr, "NOP", 2, 2, false, Immediate },
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
    [0xC2] = { NOP_Instr, "NOP", 2, 2, false, Immediate },
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
    [0xD4] = { NOP_Instr, "NOP", 2, 4, false, ZeroPageX },
    [0xD5] = { CMP_Instr, "CMP zp,X", 2, 4, false, ZeroPageX },
    [0xD6] = { DEC_Instr, "DEC zp,X", 2, 6, false, ZeroPageX },
    [0xD8] = { CLD_Instr, "CLD", 1, 2, false, Implied },
    [0xD9] = { CMP_Instr, "CMP abs,Y", 3, 4, true, AbsoluteY },
    [0xDA] = { NOP_Instr, "NOP", 1, 2, false, Implied },
    [0xDC] = { NOP_Instr, "NOP", 3, 4, true, AbsoluteX },
    [0xDD] = { CMP_Instr, "CMP abs,X", 3, 4, true, AbsoluteX },
    [0xDE] = { DEC_Instr, "DEC abs,X", 3, 7, false, AbsoluteX },

    [0xE0] = { CPX_Instr, "CPX #imm", 2, 2, false, Immediate },
    [0xE1] = { SBC_Instr, "SBC (ind,X)", 2, 6, false, PreIndexedIndirect },
    [0xE2] = { NOP_Instr, "NOP", 2, 2, false, Immediate },
    [0xE4] = { CPX_Instr, "CPX zp", 2, 3, false, ZeroPage },
    [0xE5] = { SBC_Instr, "SBC zp", 2, 3, false, ZeroPage },
    [0xE6] = { INC_Instr, "INC zp", 2, 5, false, ZeroPage },
    [0xE8] = { INX_Instr, "INX", 1, 2, false, Implied },
    [0xE9] = { SBC_Instr, "SBC #imm", 2, 2, false, Immediate },
    [0xEA] = { NOP_Instr, "NOP", 1, 2, false, Implied },
    [0xEB] = { SBC_Instr, "SBC #imm", 2, 2, false, Immediate },
    [0xEC] = { CPX_Instr, "CPX abs", 3, 4, false, Absolute },
    [0xED] = { SBC_Instr, "SBC abs", 3, 4, false, Absolute },
    [0xEE] = { INC_Instr, "INC abs", 3, 6, false, Absolute },

    [0xF0] = { BEQ_Instr, "BEQ rel", 2, 2, true, Relative },
    [0xF1] = { SBC_Instr, "SBC (ind),Y", 2, 5, true, PostIndexedIndirect },
    [0xF4] = { NOP_Instr, "NOP", 2, 4, false, ZeroPageX },
    [0xF5] = { SBC_Instr, "SBC zp,X", 2, 4, false, ZeroPageX },
    [0xF6] = { INC_Instr, "INC zp,X", 2, 6, false, ZeroPageX },
    [0xF8] = { SED_Instr, "SED", 1, 2, false, Implied },
    [0xF9] = { SBC_Instr, "SBC abs,Y", 3, 4, true, AbsoluteY },
    [0xFA] = { NOP_Instr, "NOP", 1, 2, false, Implied },
    [0xFC] = { NOP_Instr, "NOP", 3, 4, true, AbsoluteX },
    [0xFD] = { SBC_Instr, "SBC abs,X", 3, 4, true, AbsoluteX },
    [0xFE] = { INC_Instr, "INC abs,X", 3, 7, false, AbsoluteX },
};

static void ExecuteOpcode(Cpu *state)
{
    const uint8_t opcode = CpuRead8(state->pc);
    const OpcodeHandler *handler = &opcodes[opcode];

    if (handler->InstrFn)
    {
        CPU_LOG("Executing %s (Opcode: 0x%02X) at PC: 0x%04X\n", handler->name, opcode, state->pc);
        snprintf(state->debug_msg, sizeof(state->debug_msg), "PC:%04X %s", state->pc, handler->name);

        BusUpdate(state->cycles);
        if (PPU_NmiTriggered())
        {
            state->nmi_pending = true;
        }

        // Execute instruction
        handler->InstrFn(state, handler->addr_mode, handler->page_cross_penalty);

        state->cycles += handler->cycles;

        if (state->nmi_pending)
        {
            CPU_TriggerNMI(state);
            state->nmi_pending = false;
        }
        else if (state->irq_ready)
        {
            ClearIrq();
            HandleIRQ(state);
        }
    }
    else
    {
        printf("Unhandled opcode: 0x%02X at PC: 0x%04X\n\n", opcode, state->pc);
        printf("Cycles done: %lu\n", state->cycles);
        exit(EXIT_FAILURE);
    }
}

void CPU_Init(Cpu *state)
{
    memset(state, 0, sizeof(*state));
    // Read the reset vector from 0xFFFC (little-endian)
    uint16_t reset_vector = CpuReadVector(0xFFFC); 
    
    printf("CPU Init: Loading reset vector at PC:0x%04X\n", reset_vector);

    // Set PC to the reset vector address
    state->pc = reset_vector;

    // Stack pointer is decremented by 3
    state->sp -= 3;

    state->status.i = 1;
}

void CPU_TriggerNMI(Cpu *state)
{
    // Push high first
    StackPush(state, (state->pc >> 8) & 0xFF);
    // Push low next
    StackPush(state, state->pc & 0xFF);
    // Push status with bit 5 set
    StackPush(state, state->status.raw | 0x20);

    state->pc = CpuReadVector(0xFFFA);
    state->status.i = 1;
}

void CPU_Update(Cpu *state)
{
    ExecuteOpcode(state);
}

void CPU_Reset(Cpu *state)
{
    // Read the reset vector from 0xFFFC (little-endian)
    uint16_t reset_vector = CpuReadVector(0xFFFC); 
    
    printf("CPU Reset: Loading reset vector at PC:0x%04X\n", reset_vector);

    // Set PC to the reset vector address
    state->pc = reset_vector;

    // Stack pointer is decremented by 3 (emulating an interrupt return state)
    state->sp -= 3;

    // Set interrupt disable flag (I) to prevent IRQs immediately after reset
    state->status.i = 1;

    // Reset cycles
    state->cycles = 7;
}
