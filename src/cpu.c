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

//#define DISABLE_DUMMY_READ_WRITES

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

static inline void StackPush(Cpu *cpu, uint8_t data)
{
    uint8_t *ptr = BusGetPtr(STACK_START + cpu->sp--);
    *ptr = data;
}

// Retrieve the value on the top of the stack and then pop it
static inline uint8_t StackPull(Cpu *cpu)
{
    return *BusGetPtr(STACK_START + (++cpu->sp));
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
    StackPush(cpu, (cpu->pc >> 8) & 0xFF);
    StackPush(cpu, cpu->pc & 0xFF);
    // Push Processor Status (clear Break flag)
    Flags status = cpu->status;
    status.b = 0;
    status.unused = 1;
    StackPush(cpu, status.raw);
    cpu->status.i = 1;
    cpu->pc = CpuReadVector(0xFFFE);
    // NMI and IRQ have a 7 cycle cost
    cpu->cycles += 7;
}

// PC += 2 
static inline uint16_t GetAbsoluteAddr(Cpu *cpu)
{
    uint8_t addr_low = CpuRead8(++cpu->pc);
    uint8_t addr_high = CpuRead8(++cpu->pc);
    return (uint16_t)addr_high << 8 | addr_low;
}

// PC += 2 
static inline uint16_t GetAbsoluteXAddr(Cpu *cpu, bool add_cycle, bool dummy_read)
{
    uint16_t base_addr = GetAbsoluteAddr(cpu);
    uint16_t final_addr = base_addr + cpu->x;

    bool page_cross = !InsidePage(base_addr, final_addr);
    // Apply extra cycle only if required
    cpu->cycles += (page_cross & add_cycle);
#ifndef DISABLE_DUMMY_READ_WRITES
    if (page_cross & dummy_read)
        CpuRead8(final_addr - PAGE_SIZE);
#endif
    return final_addr;
}

static inline uint16_t GetAbsoluteYAddr(Cpu *cpu, bool add_cycle, bool dummy_read)
{
    uint16_t base_addr = GetAbsoluteAddr(cpu);
    uint16_t final_addr = base_addr + cpu->y;

    bool page_cross = !InsidePage(base_addr, final_addr);

    // Apply extra cycle only if required
    cpu->cycles += (page_cross & add_cycle);
#ifndef DISABLE_DUMMY_READ_WRITES
    if (page_cross & dummy_read)
        CpuRead8(final_addr - PAGE_SIZE);
#endif
    return final_addr;
}

// PC += 1
static inline uint8_t GetZPAddr(Cpu *cpu)
{
    return CpuRead8(++cpu->pc);
}

// PC += 1
static inline uint16_t GetZPIndexedAddr(Cpu *cpu, uint8_t reg)
{
    uint8_t zp_addr = CpuRead8(++cpu->pc);

    return (zp_addr + reg) & PAGE_MASK;
}

// PC += 2
static inline uint16_t GetIndirectAddr(Cpu *cpu)
{
    uint8_t ptr_low = CpuRead8(++cpu->pc);
    uint8_t ptr_high = CpuRead8(++cpu->pc);
    
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
static inline uint16_t GetIndirectYAddr(Cpu *cpu, bool page_cycle, bool dummy_read)
{
    uint8_t zp_addr = GetZPAddr(cpu);
    uint8_t addr_low = CpuRead8(zp_addr);
    // Fetch high (with zero-page wraparound)
    uint8_t addr_high = CpuRead8((zp_addr + 1) & PAGE_MASK);

    uint16_t base_addr = (uint16_t)addr_high << 8 | addr_low;
    uint16_t final_addr = base_addr + cpu->y;

    bool page_cross = !InsidePage(base_addr, final_addr);

    // Apply extra cycle only if required
    cpu->cycles += (page_cross & page_cycle);
#ifndef DISABLE_DUMMY_READ_WRITES
    if (page_cross & dummy_read)
        CpuRead8(final_addr - PAGE_SIZE);
#endif
    return final_addr;
}

// PC += 1
static inline uint16_t GetIndirectXAddr(Cpu *cpu, uint8_t reg)
{
    uint8_t zp_addr = GetZPAddr(cpu);
    // Wrap in zero-page
    uint8_t effective_ptr = (zp_addr + reg) & PAGE_MASK;
    uint8_t addr_low = CpuRead8(effective_ptr);
    // Wrap in zero-page
    uint8_t addr_high = CpuRead8((effective_ptr + 1) & PAGE_MASK);
    return (uint16_t)addr_high << 8 | addr_low;
}

static inline void CompareRegAndSetFlags(Cpu *cpu, uint8_t reg, uint8_t operand)
{
    uint8_t result = reg - operand;
    // Negative flag (bit 7)
    cpu->status.n = GET_NEG_BIT(result);
    // Zero flag (is result zero?)
    cpu->status.z = !result;
    // Update the Carry Flag (C)
    cpu->status.c = (reg >= operand);
}

static inline void RotateOneLeft(Cpu *cpu, uint8_t *operand)
{
    uint8_t old_carry = cpu->status.c;
    // Store bit 7 in carry before rotating
    cpu->status.c = (*operand >> 7) & 1;
    // Shift all bits left one position and insert old carry into bit 0
    *operand = (*operand << 1) | old_carry;
    // Update status flags
    UPDATE_FLAGS_NZ(*operand);
}

static inline void RotateOneLeftFromMem(Cpu *cpu, const uint16_t operand_addr)
{
    uint8_t operand = CpuRead8(operand_addr);
#ifndef DISABLE_DUMMY_READ_WRITES
    // Dummy write
    CpuWrite8(operand_addr, operand);
#endif
    uint8_t old_carry = cpu->status.c;
    // Store bit 7 in carry before rotating
    cpu->status.c = (operand >> 7) & 1;
    // Shift all bits left one position and insert old carry into bit 0
    operand = (operand << 1) | old_carry;
    // Write to the bus
    CpuWrite8(operand_addr, operand);
    // Update status flags
    UPDATE_FLAGS_NZ(operand);
}

static inline void RotateOneRight(Cpu *cpu, uint8_t *operand)
{
    uint8_t old_carry = cpu->status.c;
    // Store bit 0 in carry before rotating
    cpu->status.c = *operand & 1;
    // Shift all bits right one position and insert old carry into bit 7
    *operand = (*operand >> 1) | (old_carry << 7);
    // Update status flags
    UPDATE_FLAGS_NZ(*operand);
}

static inline void RotateOneRightFromMem(Cpu *cpu, const uint16_t operand_addr)
{
    uint8_t operand = CpuRead8(operand_addr);
#ifndef DISABLE_DUMMY_READ_WRITES
    // Dummy write
    CpuWrite8(operand_addr, operand);
#endif
    uint8_t old_carry = cpu->status.c;
    // Store bit 0 in carry before rotating
    cpu->status.c = operand & 1;
    // Shift all bits right one position and insert old carry into bit 7
    operand = (operand >> 1) | (old_carry << 7);
    // Write to the bus
    CpuWrite8(operand_addr, operand);
    // Update status flags
    UPDATE_FLAGS_NZ(operand);
}

static inline void ShiftOneRight(Cpu *cpu, uint8_t *operand)
{
    // Store bit 0 in carry before shifting
    cpu->status.c = *operand & 1;
    // Shift all bits left by one position
    *operand >>= 1;
    // Clear N flag 
    cpu->status.n = 0;
    // Zero flag (is operand zero?)
    cpu->status.z = !(*operand);
}

static inline void ShiftOneRightFromMem(Cpu *cpu, const uint16_t operand_addr)
{
    uint8_t operand = CpuRead8(operand_addr);
#ifndef DISABLE_DUMMY_READ_WRITES
    // Dummy write
    CpuWrite8(operand_addr, operand);
#endif
    // Store bit 0 in carry before shifting
    cpu->status.c = operand & 1;
    // Shift all bits left by one position
    operand >>= 1;
    // Write to the bus
    CpuWrite8(operand_addr, operand);
    // Clear N flag 
    cpu->status.n = 0;
    // Zero flag (is operand zero?)
    cpu->status.z = !operand;
}

static inline void ShiftOneLeft(Cpu *cpu, uint8_t *operand)
{
    // Store bit 7 in carry before shifting
    cpu->status.c = (*operand >> 7) & 1;
    // Shift all bits left by one position
    *operand <<= 1;
    // Update status flags
    UPDATE_FLAGS_NZ(*operand);
}

static inline void ShiftOneLeftFromMem(Cpu *cpu, const uint16_t operand_addr)
{
    uint8_t operand = CpuRead8(operand_addr);
#ifndef DISABLE_DUMMY_READ_WRITES
    // Dummy write
    CpuWrite8(operand_addr, operand);
#endif
    // Store bit 7 in carry before shifting
    cpu->status.c = (operand >> 7) & 1;
    // Shift all bits left by one position
    operand <<= 1;
    // Write to the bus
    CpuWrite8(operand_addr, operand);
    // Update status flags
    UPDATE_FLAGS_NZ(operand);
}

// ADC/SBC only uses the A register (Accumulator)
static inline void AddWithCarry(Cpu *cpu, uint8_t operand)
{
    uint16_t sum = cpu->a + operand + cpu->status.c;

    // Set Carry Flag (C) - Set if result is > 255 (unsigned overflow)
    cpu->status.c = (sum > UINT8_MAX);

    // Set Overflow Flag (V) - Detect signed overflow
    cpu->status.v = ((sum ^ cpu->a) & (sum ^ operand) & 0x80) > 0;

    // Store result
    cpu->a = (uint8_t)sum;
    
    // Update status flags
    UPDATE_FLAGS_NZ(cpu->a);

    CPU_LOG("ADC/SBC Operand: %x\n", operand);
}

static inline uint8_t GetOperandFromMem(Cpu *cpu, AddressingMode addr_mode, bool page_cycle, bool dummy_read)
{
    switch (addr_mode)
    {
        case Accumulator:
            return cpu->a;
        case Relative:
        case Immediate:
            return CpuRead8(++cpu->pc);
        case ZeroPage:
            return CpuRead8(GetZPAddr(cpu));
        case ZeroPageX:
            return CpuRead8(GetZPIndexedAddr(cpu, cpu->x));
        case ZeroPageY:
            return CpuRead8(GetZPIndexedAddr(cpu, cpu->y));
        case Absolute:
            return CpuRead8(GetAbsoluteAddr(cpu));
        case AbsoluteX:
            return CpuRead8(GetAbsoluteXAddr(cpu, page_cycle, dummy_read));
        case AbsoluteY:
            return CpuRead8(GetAbsoluteYAddr(cpu, page_cycle, dummy_read));
        case IndirectX:
            return CpuRead8(GetIndirectXAddr(cpu, cpu->x));
        case IndirectY:
            return CpuRead8(GetIndirectYAddr(cpu, page_cycle, dummy_read));
        case Implied:
        case Indirect:
            break;
    }

    return -1;
}

// Used by STA/STY/STX instrs
static inline void SetOperandToMem(Cpu *cpu, AddressingMode addr_mode, uint8_t operand, bool dummy_read)
{
    switch (addr_mode)
    {
        case Accumulator:
        case Relative:
        case Immediate:
            CpuWrite8(++cpu->pc,  operand);
            break;
        case ZeroPage:
            CpuWrite8(GetZPAddr(cpu), operand);
            break;
        case ZeroPageX:
            CpuWrite8(GetZPIndexedAddr(cpu, cpu->x), operand);
            break;
        case ZeroPageY:
            CpuWrite8(GetZPIndexedAddr(cpu, cpu->y), operand);
            break;
        case Absolute:
            CpuWrite8(GetAbsoluteAddr(cpu), operand);
            break;
        case AbsoluteX:
            CpuWrite8(GetAbsoluteXAddr(cpu, false, dummy_read), operand);
            break;
        case AbsoluteY:
            CpuWrite8(GetAbsoluteYAddr(cpu, false, dummy_read), operand);
            break;
        case IndirectX:
            CpuWrite8(GetIndirectXAddr(cpu, cpu->x), operand);
            break;
        case IndirectY:
            CpuWrite8(GetIndirectYAddr(cpu, false, dummy_read),  operand);
            break;
        case Implied:
        case Indirect:
            break;
        default:
            printf("Unknown or invalid adddress mode!: %d\n", addr_mode);
            break;
    }
}

static inline uint16_t GetOperandAddrFromMem(Cpu *cpu, AddressingMode addr_mode, bool page_cycle, bool dummy_read)
{
    switch (addr_mode)
    {
        case Relative:
        case Immediate:
            return ++cpu->pc;
        case ZeroPage:
            return GetZPAddr(cpu);
        case ZeroPageX:
            return GetZPIndexedAddr(cpu, cpu->x);
        case ZeroPageY:
            return GetZPIndexedAddr(cpu, cpu->y);
        case Absolute:
            return GetAbsoluteAddr(cpu);
        case AbsoluteX:
            return GetAbsoluteXAddr(cpu, page_cycle, dummy_read);
        case AbsoluteY:
            return GetAbsoluteYAddr(cpu, page_cycle, dummy_read);
        case IndirectX:
            return GetIndirectXAddr(cpu, cpu->x);
        case IndirectY:
            return GetIndirectYAddr(cpu, page_cycle, dummy_read);
        default:
            return 0;
    }
}

static inline void ADC_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(cpu, addr_mode, page_cycle, true);
    AddWithCarry(cpu, operand);
    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void AND_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(cpu, addr_mode, page_cycle, true);

    cpu->a &= operand;

    // Update status flags
    UPDATE_FLAGS_NZ(cpu->a);
    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void ASL_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    if (addr_mode == Accumulator)
    {
        ShiftOneLeft(cpu, &cpu->a);
    }
    else
    {
        ShiftOneLeftFromMem(cpu, GetOperandAddrFromMem(cpu, addr_mode, page_cycle, true));
    }

    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void BCC_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++cpu->pc);
    cpu->pc++;
    if (!cpu->status.c)
    {
        // Extra cycle if the branch crosses a page boundary
        cpu->cycles += !InsidePage(cpu->pc, cpu->pc + offset);

        cpu->pc += offset;
        cpu->cycles++;
        CPU_LOG("BCC pc offset: %d\n", offset);
    }
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void BCS_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++cpu->pc);
    cpu->pc++;
    if (cpu->status.c)
    {
        // Extra cycle if the branch crosses a page boundary
        cpu->cycles += !InsidePage(cpu->pc, cpu->pc + offset);

        cpu->pc += offset;
        cpu->cycles++;
        CPU_LOG("BCS pc offset: %d\n", offset);
    }
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void BEQ_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++cpu->pc);
    cpu->pc++;
    if (cpu->status.z)
    {
        // Extra cycle if the branch crosses a page boundary
        cpu->cycles += !InsidePage(cpu->pc, cpu->pc + offset);

        cpu->pc += offset;
        cpu->cycles++;
        CPU_LOG("BEQ pc offset: %d\n", offset);
    }
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void BIT_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(cpu, addr_mode, page_cycle, true);
    cpu->status.n = GET_NEG_BIT(operand);
    cpu->status.v = GET_OVERFLOW_BIT(operand);
    cpu->status.z = !(cpu->a & operand);
    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void BMI_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++cpu->pc);
    cpu->pc++;
    if (cpu->status.n)
    {
        // Extra cycle if the branch crosses a page boundary
        cpu->cycles += !InsidePage(cpu->pc, cpu->pc + offset);

        cpu->pc += offset;
        cpu->cycles++;
        CPU_LOG("BMI pc offset: %d\n", offset);
    }
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void BNE_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++cpu->pc);
    cpu->pc++;
    if (!cpu->status.z)
    {
        // Extra cycle if the branch crosses a page boundary
        cpu->cycles += !InsidePage(cpu->pc, cpu->pc + offset);

        cpu->pc += offset;
        cpu->cycles++;
        CPU_LOG("BNE pc offset: %d\n", offset);
    }
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void BPL_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++cpu->pc);
    cpu->pc++;
    if (!cpu->status.n)
    {
        // Extra cycle if the branch crosses a page boundary
        cpu->cycles += !InsidePage(cpu->pc, cpu->pc + offset);

        cpu->pc += offset;
        cpu->cycles++;
        CPU_LOG("BPL pc offset: %d\n", offset);
        //printf("BPL cross page triggered 0x%X --> 0x%X\n", cpu->pc, cpu->pc + offset);
    }
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void BRK_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    // Implied BRK	00	1	7
    // Dummy read
    CpuRead8(++cpu->pc);
    ++cpu->pc;
    // Push PC += 2
    StackPush(cpu, (cpu->pc >> 8) & 0xFF);
    StackPush(cpu, cpu->pc & 0xFF);

    // Push status status regs with the b(bit4) and bit5 flag set
    Flags status = cpu->status;
    status.b = 1;
    status.unused = 1;
    StackPush(cpu, status.raw);

    cpu->status.i = 1;
    if (!cpu->nmi_pending)
    {
        // Load IRQ vector ($FFFE-$FFFF) into PC
        cpu->pc = CpuReadVector(0xFFFE);
        CPU_LOG("Jumping to IRQ vector at 0x%X\n", cpu->pc);
    }
    else
    {
        // NMI vector hijacking
        cpu->pc = CpuReadVector(0xFFFA);
        CPU_LOG("Jumping to NMI vector at 0x%X from hijacked BRK\n", cpu->pc);
    }

}

static inline void BVC_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++cpu->pc);
    cpu->pc++;
    if (!cpu->status.v)
    {
        // Extra cycle if the branch crosses a page boundary
        cpu->cycles += !InsidePage(cpu->pc, cpu->pc + offset);

        cpu->pc += offset;
        cpu->cycles++;
        CPU_LOG("BVC pc offset: %d\n", offset);
    }
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void BVS_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++cpu->pc);
    cpu->pc++;
    if (cpu->status.v)
    {
        // Extra cycle if the branch crosses a page boundary
        cpu->cycles += !InsidePage(cpu->pc, cpu->pc + offset);

        cpu->pc += offset;
        cpu->cycles++;
        CPU_LOG("PC Offset %d\n", offset);
    }
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void CLC_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    cpu->status.c = 0;
    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void CLD_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    cpu->status.d = 0;
    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void CLI_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    cpu->irq_ready = CpuPollIRQ(cpu);
    cpu->status.i = 0;
    cpu->pc++;
}

static inline void CLV_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    cpu->status.v = 0;
    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void CMP_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(cpu, addr_mode, page_cycle, true);
    CompareRegAndSetFlags(cpu, cpu->a, operand);
    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void CPX_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(cpu, addr_mode, page_cycle, false);
    CompareRegAndSetFlags(cpu, cpu->x, operand);
    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void CPY_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(cpu, addr_mode, page_cycle, false);
    CompareRegAndSetFlags(cpu, cpu->y, operand);
    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void DEC_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    const uint16_t operand_addr = GetOperandAddrFromMem(cpu, addr_mode, page_cycle, true);
    uint8_t operand = CpuRead8(operand_addr);

#ifndef DISABLE_DUMMY_READ_WRITES
    // Dummy write
    CpuWrite8(operand_addr, operand);
#endif

    CpuWrite8(operand_addr, --operand);
    // Update status flags
    UPDATE_FLAGS_NZ(operand);

    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void DEX_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    cpu->x--;
    // Update status flags
    UPDATE_FLAGS_NZ(cpu->x);

    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void DEY_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    cpu->y--;

    // Update status flags
    UPDATE_FLAGS_NZ(cpu->y);
    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void EOR_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(cpu, addr_mode, page_cycle, true);
    cpu->a ^= operand;

    // Update status flags
    UPDATE_FLAGS_NZ(cpu->a);
    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void INC_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    const uint16_t operand_addr = GetOperandAddrFromMem(cpu, addr_mode, page_cycle, true);
    uint8_t operand = CpuRead8(operand_addr);

#ifndef DISABLE_DUMMY_READ_WRITES
    // Dummy write
    CpuWrite8(operand_addr, operand);
#endif

    CpuWrite8(operand_addr, ++operand);
    UPDATE_FLAGS_NZ(operand);

    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void INX_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    cpu->x++;

    // Update status flags
    UPDATE_FLAGS_NZ(cpu->x);

    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void INY_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    cpu->y++;

    // Update status flags
    UPDATE_FLAGS_NZ(cpu->y);

    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void JMP_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    UNUSED(page_cycle);

    cpu->pc = addr_mode == Absolute ? GetAbsoluteAddr(cpu) : GetIndirectAddr(cpu);
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void JSR_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    uint8_t pc_low = CpuRead8(++cpu->pc);
    uint8_t pc_high = CpuRead8(++cpu->pc);

    StackPush(cpu, (cpu->pc >> 8) & 0xFF);
    StackPush(cpu, cpu->pc & 0xFF);

    uint16_t new_pc = (uint16_t)pc_high << 8 | pc_low;
    cpu->pc = new_pc;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void LDA_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    cpu->a = GetOperandFromMem(cpu, addr_mode, page_cycle, true);

    // Update status flags
    UPDATE_FLAGS_NZ(cpu->a);
    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void LDX_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    cpu->x = GetOperandFromMem(cpu, addr_mode, page_cycle, true);

    // Update status flags
    UPDATE_FLAGS_NZ(cpu->x);
    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void LDY_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    cpu->y = GetOperandFromMem(cpu, addr_mode, page_cycle, true);

    // Update status flags
    UPDATE_FLAGS_NZ(cpu->y);
    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void LSR_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    if (addr_mode == Accumulator)
    {
        ShiftOneRight(cpu, &cpu->a);
        //CpuRead8(cpu->pc + 1);
    }
    else
    {
        ShiftOneRightFromMem(cpu, GetOperandAddrFromMem(cpu, addr_mode, page_cycle, true));
    }
    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void NOP_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    UNUSED(page_cycle);

    //CpuRead8(cpu->pc + 1);
    switch (addr_mode)
    {
        case Implied:
            cpu->pc++;
            break;
        case Immediate:
        case ZeroPage:
        case ZeroPageX:
            cpu->pc += 2;
            break;
        case Absolute:
        case AbsoluteX:
            cpu->pc += 3;
            break;
        default:
            printf("Bad addr mode!: %d\n", addr_mode);
            break;
    }
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void ORA_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(cpu, addr_mode, page_cycle, true);
    cpu->a |= operand;

    // Update status flags
    UPDATE_FLAGS_NZ(cpu->a);
    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void PHA_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    StackPush(cpu, cpu->a);
    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
    CpuRead8(cpu->pc);
}

static inline void PHP_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    Flags status = cpu->status;
    status.b = true;
    status.unused = true;
    StackPush(cpu, status.raw);
    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
    CpuRead8(cpu->pc);
}

static inline void PLA_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    cpu->a = StackPull(cpu);
    UPDATE_FLAGS_NZ(cpu->a);
    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
    CpuRead8(cpu->pc);
}

static inline void PLP_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    cpu->irq_ready = CpuPollIRQ(cpu);
    uint8_t status_raw = StackPull(cpu);
    Flags status = {.raw = status_raw};

    // Ignore bit for break and 5th bit
    cpu->status.c = status.c;
    cpu->status.d = status.d;
    cpu->status.i = status.i;
    cpu->status.n = status.n;
    cpu->status.v = status.v;
    cpu->status.z = status.z;

    cpu->pc++;
}

static inline void ROL_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    if (addr_mode == Accumulator)
    {
        RotateOneLeft(cpu, &cpu->a);
    }
    else
    {
        RotateOneLeftFromMem(cpu, GetOperandAddrFromMem(cpu, addr_mode, page_cycle, true));
    }
    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static void ROR_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    if (addr_mode == Accumulator)
    {
        RotateOneRight(cpu, &cpu->a);
    }
    else
    {
        RotateOneRightFromMem(cpu, GetOperandAddrFromMem(cpu, addr_mode, page_cycle, true));
    }
    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void RTI_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    uint8_t status_raw = StackPull(cpu);
    Flags status = {.raw = status_raw};

    // Ignore bit for break and 5th bit
    cpu->status.c = status.c;
    cpu->status.d = status.d;
    cpu->status.i = status.i;
    cpu->status.n = status.n;
    cpu->status.v = status.v;
    cpu->status.z = status.z;

    uint8_t pc_low = StackPull(cpu);
    uint8_t pc_high = StackPull(cpu);
    cpu->pc = (uint16_t)pc_high << 8 | pc_low;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void RTS_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    uint8_t pc_low = StackPull(cpu);
    uint8_t pc_high = StackPull(cpu);

    uint16_t new_pc = (uint16_t)pc_high << 8 | pc_low;

    cpu->pc = new_pc;
    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void SBC_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(cpu, addr_mode, page_cycle, true);

    // Invert operand since were reusing ADC logic for SBC
    AddWithCarry(cpu, ~operand);

    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void SEC_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    cpu->status.c = 1;
    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void SED_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    cpu->status.d = 1;
    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void SEI_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    cpu->irq_ready = CpuPollIRQ(cpu);
    cpu->status.i = 1;
    cpu->pc++;
}

static inline void STA_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    UNUSED(page_cycle);

    bool dummy_read = true; // (addr_mode == AbsoluteY || addr_mode == AbsoluteX || addr_mode == IndirectY);
    SetOperandToMem(cpu, addr_mode, cpu->a, dummy_read);

    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void STX_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    UNUSED(page_cycle);

    SetOperandToMem(cpu, addr_mode, cpu->x, false);
    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static inline void STY_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    UNUSED(page_cycle);

    SetOperandToMem(cpu, addr_mode, cpu->y, false);
    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

// Transfer Accumulator to Index X
static inline void TAX_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    cpu->x = cpu->a;
    // Update status flags
    UPDATE_FLAGS_NZ(cpu->x);

    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

// Transfer Accumulator to Index Y
static inline void TAY_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    cpu->y = cpu->a;
    // Update status flags
    UPDATE_FLAGS_NZ(cpu->y);

    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

// Transfer Stack Pointer to Index X
static inline void TSX_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    cpu->x = cpu->sp;
    // Update status flags
    UPDATE_FLAGS_NZ(cpu->x);

    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

// Transfer Index X to Accumulator
static inline void TXA_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    cpu->a = cpu->x;
    // Update status flags
    UPDATE_FLAGS_NZ(cpu->a);

    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

// Transfer Index X to Stack Register
static inline void TXS_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    cpu->sp = cpu->x;

    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

// Transfer Index Y to Accumulator
static inline void TYA_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    cpu->a = cpu->y;

    UPDATE_FLAGS_NZ(cpu->a);
    cpu->pc++;
    cpu->irq_ready = CpuPollIRQ(cpu);
}

static const OpcodeHandler opcodes[256] =
{
    [0x00] = { BRK_Instr, "BRK", 1, 7, false, Implied  },
    [0x01] = { ORA_Instr, "ORA (ind,X)", 2, 6, false, IndirectX },
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
    [0x11] = { ORA_Instr, "ORA (ind),Y", 2, 5, true, IndirectY},
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
    [0x21] = { AND_Instr, "AND (ind,X)", 2, 6, false, IndirectX },
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
    [0x31] = { AND_Instr, "AND (ind),Y", 2, 5, true, IndirectY },
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
    [0x41] = { EOR_Instr, "EOR (ind,X)", 2, 6, false, IndirectX },
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
    [0x51] = { EOR_Instr, "EOR (ind),Y", 2, 5, true, IndirectY },
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
    [0x61] = { ADC_Instr, "ADC (ind,X)", 2, 6, false, IndirectX },
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
    [0x71] = { ADC_Instr, "ADC (ind),Y", 2, 5, true, IndirectY },
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
    [0x81] = { STA_Instr, "STA (ind,X)", 2, 6, false, IndirectX },
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
    [0x91] = { STA_Instr, "STA (ind),Y", 2, 6, false, IndirectY },
    [0x94] = { STY_Instr, "STY zp,X", 2, 4, false, ZeroPageX },
    [0x95] = { STA_Instr, "STA zp,X", 2, 4, false, ZeroPageX },
    [0x96] = { STX_Instr, "STX zp,Y", 2, 4, false, ZeroPageY },
    [0x98] = { TYA_Instr, "TYA", 1, 2, false, Implied },
    [0x99] = { STA_Instr, "STA abs,Y", 3, 5, false, AbsoluteY },
    [0x9A] = { TXS_Instr, "TXS", 1, 2, false, Implied },
    [0x9D] = { STA_Instr, "STA abs,X", 3, 5, false, AbsoluteX },

    [0xA0] = { LDY_Instr, "LDY #imm", 2, 2, false, Immediate },
    [0xA1] = { LDA_Instr, "LDA (ind,X)", 2, 6, false, IndirectX },
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
    [0xB1] = { LDA_Instr, "LDA (ind),Y", 2, 5, true, IndirectY },
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
    [0xC1] = { CMP_Instr, "CMP (ind,X)", 2, 6, false, IndirectX },
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
    [0xD1] = { CMP_Instr, "CMP (ind),Y", 2, 5, true, IndirectY },
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
    [0xE1] = { SBC_Instr, "SBC (ind,X)", 2, 6, false, IndirectX },
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
    [0xF1] = { SBC_Instr, "SBC (ind),Y", 2, 5, true, IndirectY },
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

static void ExecuteOpcode(Cpu *cpu)
{
    const uint8_t opcode = CpuRead8(cpu->pc);
    const OpcodeHandler *handler = &opcodes[opcode];

    if (handler->InstrFn)
    {
        CPU_LOG("Executing %s (Opcode: 0x%02X) at PC: 0x%04X\n", handler->name, opcode, cpu->pc);
        snprintf(cpu->debug_msg, sizeof(cpu->debug_msg), "PC:%04X %s", cpu->pc, handler->name);

        BusUpdate(cpu->cycles);

        if (PPU_NmiTriggered())
        {
            cpu->nmi_pending = true;
        }

        // Execute instruction
        handler->InstrFn(cpu, handler->addr_mode, handler->page_cross_penalty);

        cpu->cycles += handler->cycles;

        if (cpu->nmi_pending)
        {
            CPU_TriggerNMI(cpu);
            cpu->nmi_pending = false;
        }
        else if (cpu->irq_ready)
        {
            ClearIrq();
            HandleIRQ(cpu);
        }
    }
    else
    {
        printf("Unhandled opcode: 0x%02X at PC: 0x%04X\n\n", opcode, cpu->pc);
        printf("Cycles done: %lu\n", cpu->cycles);
        exit(EXIT_FAILURE);
    }
}

void CPU_Init(Cpu *cpu)
{
    memset(cpu, 0, sizeof(*cpu));
    // Read the reset vector from 0xFFFC (little-endian)
    uint16_t reset_vector = CpuReadVector(0xFFFC); 
    
    printf("CPU Init: Loading reset vector PC:0x%04X\n", reset_vector);

    // Set PC to the reset vector address
    cpu->pc = reset_vector;

    // Stack pointer is decremented by 3
    cpu->sp -= 3;

    cpu->status.i = 1;
}

void CPU_TriggerNMI(Cpu *cpu)
{
    // Push high first
    StackPush(cpu, (cpu->pc >> 8) & 0xFF);
    // Push low next
    StackPush(cpu, cpu->pc & 0xFF);
    // Push status with bit 5 set
    StackPush(cpu, cpu->status.raw | 0x20);

    cpu->pc = CpuReadVector(0xFFFA);
    cpu->status.i = 1;
    // NMI and IRQ have a 7 cycle cost
    cpu->cycles += 7;
}

void CPU_Update(Cpu *cpu)
{
    ExecuteOpcode(cpu);
}

void CPU_Reset(Cpu *cpu)
{
    // Read the reset vector from 0xFFFC (little-endian)
    uint16_t reset_vector = CpuReadVector(0xFFFC); 
    
    printf("CPU Reset: Loading reset vector PC:0x%04X\n", reset_vector);

    // Set PC to the reset vector address
    cpu->pc = reset_vector;

    // Stack pointer is decremented by 3 (emulating an interrupt return state)
    cpu->sp -= 3;

    // Set interrupt disable flag (I) to prevent IRQs immediately after reset
    cpu->status.i = 1;

    // Reset cycles
    cpu->cycles = 7;
}
