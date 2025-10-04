#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#include "cpu.h"
#include "apu.h"
#include "ppu.h"
#include "joypad.h"
#include "arena.h"
#include "cart.h"
#include "mapper.h"
#include "system.h"
#include "utils.h"


static uint8_t CpuRead8(const uint16_t addr)
{
    return SystemRead(addr);
}

static void CpuWrite8(const uint16_t addr, const uint8_t data)
{
    SystemWrite(addr, data);
}

static uint16_t CpuReadVector(uint16_t addr)
{
    return (uint16_t)CpuRead8(addr + 1) << 8 | CpuRead8(addr);
}

static inline void StackPush(Cpu *cpu, uint8_t data)
{
    CpuWrite8(STACK_START + cpu->sp--, data);
}

// Retrieve the value on the top of the stack and then pop it
static inline uint8_t StackPull(Cpu *cpu)
{
    return CpuRead8(STACK_START + (++cpu->sp));
}

static bool PageCross(uint16_t src_addr, uint16_t dst_addr)
{
    return ((src_addr & 0xFF00) != (dst_addr & 0xFF00));
}

static inline void CpuPollIRQ(Cpu *cpu)
{
    cpu->irq_pending = !cpu->status.i && SystemPollAllIrqs();
}

static void CpuIrqHandler(Cpu *cpu)
{
    // Dummy read of next instruction
    CpuRead8(cpu->pc);
    // Another dummy read
    CpuRead8(cpu->pc);
    //printf("IRQ at PC: 0x%X\n", cpu->pc);
    StackPush(cpu, (cpu->pc >> 8) & 0xFF);
    StackPush(cpu, cpu->pc & 0xFF);

    //const bool nmi_hijack = cpu->nmi_pending;

    // Push Processor Status (clear Break flag)
    Flags status = cpu->status;
    status.b = 0;
    status.unused = 1;
    StackPush(cpu, status.raw);
    cpu->status.i = 1;
    if (!cpu->nmi_pending)
    {
        // Load IRQ vector ($FFFE-$FFFF) into PC
        cpu->pc = CpuReadVector(IRQ_VECTOR);
        cpu->irq_pending = false;
        CPU_LOG("Jumping to IRQ vector at 0x%X\n", cpu->pc);
    }
    else
    {
        // NMI vector hijacking
        cpu->pc = CpuReadVector(NMI_VECTOR);
        cpu->nmi_pending = false;
        CPU_LOG("Jumping to NMI vector at 0x%X from hijacked IRQ\n", cpu->pc);
    }
}

static void CpuNmiHandler(Cpu *cpu)
{
    // Dummy read of next instruction byte
    CpuRead8(cpu->pc);
    CpuRead8(cpu->pc);
    //printf("Nmi at PC: 0x%X\n", cpu->pc);
    // Push high first
    StackPush(cpu, (cpu->pc >> 8) & 0xFF);
    // Push low next
    StackPush(cpu, cpu->pc & 0xFF);
    // Push status with bit 5 set
    StackPush(cpu, cpu->status.raw | 0x20);

    //uint16_t prev_pc = cpu->pc;
    cpu->pc = CpuReadVector(NMI_VECTOR);
    cpu->status.i = 1;
    cpu->nmi_pending = 0;
    //printf("NMI Jumped from: 0x%X --> 0x%X\n", prev_pc, cpu->pc);
}

static void CpuHandleInterrupts(Cpu *cpu)
{
    if (cpu->nmi_pending)
    {
        CpuNmiHandler(cpu);
    }
    else if (cpu->irq_pending)
    {
        CpuIrqHandler(cpu);
    }
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
    uint8_t addr_low = CpuRead8(++cpu->pc);
    uint8_t addr_high = CpuRead8(++cpu->pc);
    uint16_t addr_low_final = addr_low + cpu->x;
    bool page_cross = addr_low_final > 255;
    uint16_t final_addr = (uint16_t)addr_high << 8 | (uint8_t)(addr_low_final);

    if (dummy_read & (page_cross || !add_cycle))
    {
        //printf("Dummy Read at 0x%X\n", final_addr);
        CpuRead8(final_addr);
    }

    final_addr += page_cross * PAGE_SIZE;

    return final_addr;
}

static inline uint16_t GetAbsoluteYAddr(Cpu *cpu, bool add_cycle, bool dummy_read)
{
    uint8_t addr_low = CpuRead8(++cpu->pc);
    uint8_t addr_high = CpuRead8(++cpu->pc);
    uint16_t addr_low_final = addr_low + cpu->y;
    bool page_cross = addr_low_final > 255;
    uint16_t final_addr = (uint16_t)addr_high << 8 | (uint8_t)(addr_low_final);

    if (dummy_read & (page_cross || !add_cycle))
        CpuRead8(final_addr);

    final_addr += page_cross * PAGE_SIZE;

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
    CpuRead8(zp_addr);

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

    uint16_t addr_low_final = addr_low + cpu->y;
    bool page_cross = addr_low_final > 255;
    uint16_t final_addr = (uint16_t)addr_high << 8 | (uint8_t)(addr_low_final);

    if (dummy_read & (page_cross || !page_cycle))
        CpuRead8(final_addr);

    final_addr += page_cross * PAGE_SIZE;
    return final_addr;
}

// PC += 1
static inline uint16_t GetIndirectXAddr(Cpu *cpu, uint8_t reg)
{
    uint8_t zp_addr = GetZPAddr(cpu);
    CpuRead8(zp_addr);
    // Wrap in zero-page
    uint8_t effective_ptr = (zp_addr + reg) & PAGE_MASK;
    uint8_t addr_low = CpuRead8(effective_ptr);
    // Wrap in zero-page
    uint8_t addr_high = CpuRead8((effective_ptr + 1) & PAGE_MASK);
    return (uint16_t)addr_high << 8 | addr_low;
}

static inline void CompareRegAndSetFlags(Cpu *cpu, uint8_t reg, uint8_t operand)
{
    //printf("CMP 0x%X\n", operand);
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
    // Dummy write
    CpuWrite8(operand_addr, operand);
    uint8_t old_carry = cpu->status.c;
    // Store bit 7 in carry before rotating
    cpu->status.c = (operand >> 7) & 1;
    // Shift all bits left one position and insert old carry into bit 0
    operand = (operand << 1) | old_carry;
    // IRQ polling before last cycle
    CpuPollIRQ(cpu);
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
    // Dummy write
    CpuWrite8(operand_addr, operand);
    uint8_t old_carry = cpu->status.c;
    // Store bit 0 in carry before rotating
    cpu->status.c = operand & 1;
    // Shift all bits right one position and insert old carry into bit 7
    operand = (operand >> 1) | (old_carry << 7);
    // IRQ polling before last cycle
    CpuPollIRQ(cpu);
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
    // Dummy write
    CpuWrite8(operand_addr, operand);
    // Store bit 0 in carry before shifting
    cpu->status.c = operand & 1;
    // Shift all bits left by one position
    operand >>= 1;
    // IRQ polling before last cycle
    CpuPollIRQ(cpu);
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

static inline uint8_t ShiftOneLeftFromMem(Cpu *cpu, const uint16_t operand_addr)
{
    uint8_t operand = CpuRead8(operand_addr);
    // Dummy write
    CpuWrite8(operand_addr, operand);
    // Store bit 7 in carry before shifting
    cpu->status.c = (operand >> 7) & 1;
    // Shift all bits left by one position
    operand <<= 1;
    // IRQ polling before last cycle
    CpuPollIRQ(cpu);
    // Write to the bus
    CpuWrite8(operand_addr, operand);
    // Update status flags
    UPDATE_FLAGS_NZ(operand);
    return operand;
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

static inline uint16_t GetOperandAddrFromMem(Cpu *cpu, AddressingMode addr_mode, bool page_cycle, bool dummy_read)
{
    switch (addr_mode)
    {
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
            printf("At PC: 0x%X Unknown or invalid adddress mode!: %d\n", cpu->pc, addr_mode);
            printf("%s\n", cpu->debug_msg);
            exit(1);
            break;
    }

    return 0;
}

static inline void ADC_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    const uint16_t operand_addr = GetOperandAddrFromMem(cpu, addr_mode, page_cycle, true);
    CpuPollIRQ(cpu);
    AddWithCarry(cpu, CpuRead8(operand_addr));
    ++cpu->pc;
    CpuHandleInterrupts(cpu);
}

static inline void AND_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    const uint16_t operand_addr = GetOperandAddrFromMem(cpu, addr_mode, page_cycle, true);
    CpuPollIRQ(cpu);
    cpu->a &= CpuRead8(operand_addr);

    // Update status flags
    UPDATE_FLAGS_NZ(cpu->a);
    ++cpu->pc;
    CpuHandleInterrupts(cpu);
}

static inline void ASL_A_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    CpuPollIRQ(cpu);
#ifndef DISABLE_DUMMY_READ_WRITES
    // Dummy read of next instruction byte
    CpuRead8(++cpu->pc);
#else
    ++cpu->pc;
#endif
    ShiftOneLeft(cpu, &cpu->a);
    CpuHandleInterrupts(cpu);
}

static inline void ASL_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    ShiftOneLeftFromMem(cpu, GetOperandAddrFromMem(cpu, addr_mode, page_cycle, true));
    ++cpu->pc;
    CpuHandleInterrupts(cpu);
}

static inline void SLO_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    cpu->a |= ShiftOneLeftFromMem(cpu, GetOperandAddrFromMem(cpu, addr_mode, page_cycle, true));
    // Update status flags
    UPDATE_FLAGS_NZ(cpu->a);
    ++cpu->pc;
    CpuHandleInterrupts(cpu);
}

static inline void BCC_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++cpu->pc);
    ++cpu->pc;
    CpuPollIRQ(cpu);
    if (!cpu->status.c)
    {
        uint16_t final_addr = cpu->pc + offset;
        // Extra cycle if the branch crosses a page boundary
        bool page_cross = PageCross(cpu->pc, final_addr);
        // Opcode of next instruction
        CpuRead8(cpu->pc);
        if (page_cross)
        {
            CpuPollIRQ(cpu);
            CpuRead8(final_addr - PAGE_SIZE);
        }
        cpu->pc = final_addr;
        CPU_LOG("BCC pc offset: %d\n", offset);
    }
    CpuHandleInterrupts(cpu);
}

static inline void BCS_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++cpu->pc);
    ++cpu->pc;
    CpuPollIRQ(cpu);
    if (cpu->status.c)
    {
        uint16_t final_addr = cpu->pc + offset;
        // Extra cycle if the branch crosses a page boundary
        bool page_cross = PageCross(cpu->pc, final_addr);
        // Opcode of next instruction
        CpuRead8(cpu->pc);
        if (page_cross)
        {
            CpuPollIRQ(cpu);
            CpuRead8(final_addr - PAGE_SIZE);
        }
        cpu->pc = final_addr;
        CPU_LOG("BCS pc offset: %d\n", offset);
    }
    CpuHandleInterrupts(cpu);
}

static inline void BEQ_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++cpu->pc);
    ++cpu->pc;
    CpuPollIRQ(cpu);
    if (cpu->status.z)
    {
        uint16_t final_addr = cpu->pc + offset;
        // Extra cycle if the branch crosses a page boundary
        bool page_cross = PageCross(cpu->pc, final_addr);
        // Opcode of next instruction
        CpuRead8(cpu->pc);
        if (page_cross)
        {
            CpuPollIRQ(cpu);
            CpuRead8(final_addr - PAGE_SIZE);
        }
        cpu->pc = final_addr;
        CPU_LOG("BEQ pc offset: %d\n", offset);
    }
    CpuHandleInterrupts(cpu);
}

static inline void BIT_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    const uint16_t operand_addr = GetOperandAddrFromMem(cpu, addr_mode, page_cycle, true);
    CpuPollIRQ(cpu);
    uint8_t operand = CpuRead8(operand_addr);
    cpu->status.n = GET_NEG_BIT(operand);
    cpu->status.v = GET_OVERFLOW_BIT(operand);
    cpu->status.z = !(cpu->a & operand);
    ++cpu->pc;
    CpuHandleInterrupts(cpu);
}

static inline void BMI_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++cpu->pc);
    ++cpu->pc;
    CpuPollIRQ(cpu);
    if (cpu->status.n)
    {
        uint16_t final_addr = cpu->pc + offset;
        // Extra cycle if the branch crosses a page boundary
        bool page_cross = PageCross(cpu->pc, final_addr);
        // Opcode of next instruction
        CpuRead8(cpu->pc);
        if (page_cross)
        {
            CpuPollIRQ(cpu);
            CpuRead8(final_addr - PAGE_SIZE);
        }
        cpu->pc = final_addr;
        CPU_LOG("BMI pc offset: %d\n", offset);
    }
    CpuHandleInterrupts(cpu);
}

static inline void BNE_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++cpu->pc);
    ++cpu->pc;
    CpuPollIRQ(cpu);
    if (!cpu->status.z)
    {
        uint16_t final_addr = cpu->pc + offset;
        // Extra cycle if the branch crosses a page boundary
        bool page_cross = PageCross(cpu->pc, final_addr);
        // Opcode of next instruction
        CpuRead8(cpu->pc);
        if (page_cross)
        {
            CpuPollIRQ(cpu);
            CpuRead8(final_addr - PAGE_SIZE);
        }
        cpu->pc = final_addr;
        CPU_LOG("BNE pc offset: %d\n", offset);
    }
    CpuHandleInterrupts(cpu);
}

static inline void BPL_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++cpu->pc);
    ++cpu->pc;
    CpuPollIRQ(cpu);
    if (!cpu->status.n)
    {
        uint16_t final_addr = cpu->pc + offset;
        // Extra cycle if the branch crosses a page boundary
        bool page_cross = PageCross(cpu->pc, final_addr);
        // Opcode of next instruction
        CpuRead8(cpu->pc);
        if (page_cross)
        {
            CpuPollIRQ(cpu);
            CpuRead8(final_addr - PAGE_SIZE);
        }
        cpu->pc = final_addr;
        CPU_LOG("BPL pc offset: %d\n", offset);
        //printf("BPL cross page triggered 0x%X --> 0x%X\n", cpu->pc, cpu->pc + offset);
    }
    CpuHandleInterrupts(cpu);
}

static inline void BRK_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    // Dummy read of next instruction byte
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
        cpu->nmi_pending = false;
        CPU_LOG("Jumping to NMI vector at 0x%X from hijacked BRK\n", cpu->pc);
    }
}

static inline void BVC_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++cpu->pc);
    ++cpu->pc;
    CpuPollIRQ(cpu);
    if (!cpu->status.v)
    {
        uint16_t final_addr = cpu->pc + offset;
        // Extra cycle if the branch crosses a page boundary
        bool page_cross = PageCross(cpu->pc, final_addr);
        // Opcode of next instruction
        CpuRead8(cpu->pc);
        if (page_cross)
        {
            CpuPollIRQ(cpu);
            CpuRead8(final_addr - PAGE_SIZE);
        }
        cpu->pc = final_addr;
        CPU_LOG("BVC pc offset: %d\n", offset);
    }
    CpuHandleInterrupts(cpu);
}

static inline void BVS_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++cpu->pc);
    ++cpu->pc;
    CpuPollIRQ(cpu);
    if (cpu->status.v)
    {
        uint16_t final_addr = cpu->pc + offset;
        // Extra cycle if the branch crosses a page boundary
        bool page_cross = PageCross(cpu->pc, final_addr);
        // Opcode of next instruction
        CpuRead8(cpu->pc);
        if (page_cross)
        {
            CpuPollIRQ(cpu);
            CpuRead8(final_addr - PAGE_SIZE);
        }
        cpu->pc = final_addr;
        CPU_LOG("PC Offset %d\n", offset);
    }
    CpuHandleInterrupts(cpu);
}

static inline void CLC_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    CpuPollIRQ(cpu);
    // Dummy read of next instruction byte
    CpuRead8(++cpu->pc);

    cpu->status.c = 0;
    CpuHandleInterrupts(cpu);
}

static inline void CLD_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    CpuPollIRQ(cpu);
    // Dummy read of next instruction byte
    CpuRead8(++cpu->pc);

    cpu->status.d = 0;
    CpuHandleInterrupts(cpu);
}

static inline void CLI_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    CpuPollIRQ(cpu);
    // Dummy read of next instruction byte
    CpuRead8(++cpu->pc);

    cpu->status.i = 0;
    CpuHandleInterrupts(cpu);
}

static inline void CLV_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    CpuPollIRQ(cpu);
    // Dummy read of next instruction byte
    CpuRead8(++cpu->pc);

    cpu->status.v = 0;
    CpuHandleInterrupts(cpu);
}

static inline void CMP_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    const uint16_t operand_addr = GetOperandAddrFromMem(cpu, addr_mode, page_cycle, true);
    CpuPollIRQ(cpu);
    CompareRegAndSetFlags(cpu, cpu->a, CpuRead8(operand_addr));
    ++cpu->pc;
    CpuHandleInterrupts(cpu);
}

static inline void CPX_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    const uint16_t operand_addr = GetOperandAddrFromMem(cpu, addr_mode, page_cycle, false);
    CpuPollIRQ(cpu);
    CompareRegAndSetFlags(cpu, cpu->x, CpuRead8(operand_addr));
    ++cpu->pc;
    CpuHandleInterrupts(cpu);
}

static inline void CPY_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    const uint16_t operand_addr = GetOperandAddrFromMem(cpu, addr_mode, page_cycle, false);
    CpuPollIRQ(cpu);
    CompareRegAndSetFlags(cpu, cpu->y, CpuRead8(operand_addr));
    ++cpu->pc;
    CpuHandleInterrupts(cpu);
}

static inline void DEC_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    const uint16_t operand_addr = GetOperandAddrFromMem(cpu, addr_mode, page_cycle, true);
    uint8_t operand = CpuRead8(operand_addr);

    // Dummy write
    CpuWrite8(operand_addr, operand);

    CpuPollIRQ(cpu);
    CpuWrite8(operand_addr, --operand);
    // Update status flags
    UPDATE_FLAGS_NZ(operand);

    ++cpu->pc;
    CpuHandleInterrupts(cpu);
}

static inline void DEX_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    CpuPollIRQ(cpu);
    // Dummy read of next instruction byte
    CpuRead8(++cpu->pc);

    --cpu->x;
    // Update status flags
    UPDATE_FLAGS_NZ(cpu->x);

    CpuHandleInterrupts(cpu);
}

static inline void SBX_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    CpuPollIRQ(cpu);

    // Always immediate addr mode
    uint8_t operand = CpuRead8(++cpu->pc);
    cpu->x = (cpu->a & cpu->x) - operand;

    // Negative flag (bit 7)
    cpu->status.n = GET_NEG_BIT(cpu->x);
    // Zero flag (is result zero?)
    cpu->status.z = !cpu->x;
    // Update the Carry Flag (C)
    cpu->status.c = (cpu->a >= cpu->x);

    ++cpu->pc;

    CpuHandleInterrupts(cpu);
}

static inline void DEY_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    CpuPollIRQ(cpu);
    // Dummy read of next instruction byte
    CpuRead8(++cpu->pc);

    --cpu->y;
    // Update status flags
    UPDATE_FLAGS_NZ(cpu->y);

    CpuHandleInterrupts(cpu);
}

static inline void EOR_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    const uint16_t operand_addr = GetOperandAddrFromMem(cpu, addr_mode, page_cycle, true);
    CpuPollIRQ(cpu);

    cpu->a ^= CpuRead8(operand_addr);

    // Update status flags
    UPDATE_FLAGS_NZ(cpu->a);
    ++cpu->pc;
    CpuHandleInterrupts(cpu);
}

static inline void INC_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    const uint16_t operand_addr = GetOperandAddrFromMem(cpu, addr_mode, page_cycle, true);
    uint8_t operand = CpuRead8(operand_addr);

    // Dummy write
    CpuWrite8(operand_addr, operand);

    CpuPollIRQ(cpu);
    CpuWrite8(operand_addr, ++operand);
    UPDATE_FLAGS_NZ(operand);

    ++cpu->pc;
    CpuHandleInterrupts(cpu);
}

static inline void INX_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    CpuPollIRQ(cpu);
    // Dummy read of next instruction byte
    CpuRead8(++cpu->pc);

    ++cpu->x;
    // Update status flags
    UPDATE_FLAGS_NZ(cpu->x);

    CpuHandleInterrupts(cpu);
}

static inline void INY_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    CpuPollIRQ(cpu);
    // Dummy read of next instruction byte
    CpuRead8(++cpu->pc);

    ++cpu->y;
    // Update status flags
    UPDATE_FLAGS_NZ(cpu->y);

    CpuHandleInterrupts(cpu);
}

static inline void JMP_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    UNUSED(page_cycle);

    cpu->pc = addr_mode == Absolute ? GetAbsoluteAddr(cpu) : GetIndirectAddr(cpu);
    CpuPollIRQ(cpu);
    CpuHandleInterrupts(cpu);
}

static inline void JSR_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    uint8_t pc_low = CpuRead8(++cpu->pc);
    ++cpu->pc;
    // Dummy read from the stack
    CpuRead8(STACK_START + cpu->sp);

    StackPush(cpu, (cpu->pc >> 8) & 0xFF);
    StackPush(cpu, cpu->pc & 0xFF);

    CpuPollIRQ(cpu);
    uint8_t pc_high = CpuRead8(cpu->pc);
    cpu->pc = (uint16_t)pc_high << 8 | pc_low;
    CpuHandleInterrupts(cpu);
}

static inline void LDA_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    const uint16_t operand_addr = GetOperandAddrFromMem(cpu, addr_mode, page_cycle, true);
    CpuPollIRQ(cpu);

    cpu->a = CpuRead8(operand_addr);

    // Update status flags
    UPDATE_FLAGS_NZ(cpu->a);
    ++cpu->pc;

    CpuHandleInterrupts(cpu);
}

static inline void LDX_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    const uint16_t operand_addr = GetOperandAddrFromMem(cpu, addr_mode, page_cycle, true);
    CpuPollIRQ(cpu);

    cpu->x = CpuRead8(operand_addr);

    // Update status flags
    UPDATE_FLAGS_NZ(cpu->x);
    ++cpu->pc;
    CpuHandleInterrupts(cpu);
}

static inline void LDY_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    const uint16_t operand_addr = GetOperandAddrFromMem(cpu, addr_mode, page_cycle, true);
    CpuPollIRQ(cpu);

    cpu->y = CpuRead8(operand_addr);

    // Update status flags
    UPDATE_FLAGS_NZ(cpu->y);
    ++cpu->pc;
    CpuHandleInterrupts(cpu);
}

static inline void LSR_A_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    CpuPollIRQ(cpu);
    // Dummy read of next instruction byte
    CpuRead8(++cpu->pc);

    ShiftOneRight(cpu, &cpu->a);
    CpuHandleInterrupts(cpu);
}

static inline void LSR_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    ShiftOneRightFromMem(cpu, GetOperandAddrFromMem(cpu, addr_mode, page_cycle, true));
    ++cpu->pc;
    CpuPollIRQ(cpu);
    CpuHandleInterrupts(cpu);
}

static inline void NOP_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    switch (addr_mode)
    {
        case Implied:
            // Dummy read of next instruction byte
            CpuRead8(++cpu->pc);
            break;
        case Immediate:
            CpuRead8(++cpu->pc);
            ++cpu->pc;
            break;
        case ZeroPage:
            CpuRead8(GetZPAddr(cpu));
            ++cpu->pc;
            break;
        case ZeroPageX:
            CpuRead8(GetZPIndexedAddr(cpu, cpu->x));
            ++cpu->pc;
            break;
        case Absolute:
            CpuRead8(GetAbsoluteAddr(cpu));
            ++cpu->pc;
            break;
        case AbsoluteX:
            CpuRead8(GetAbsoluteXAddr(cpu, page_cycle, true));
            ++cpu->pc;
            break;
        default:
            printf("NOP has bad addr mode!: %d\n", addr_mode);
            exit(EXIT_FAILURE);
            break;
    }
    CpuPollIRQ(cpu);
    CpuHandleInterrupts(cpu);
}

static inline void ORA_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    const uint16_t operand_addr = GetOperandAddrFromMem(cpu, addr_mode, page_cycle, true);
    CpuPollIRQ(cpu);
    uint8_t operand = CpuRead8(operand_addr);
    cpu->a |= operand;

    // Update status flags
    UPDATE_FLAGS_NZ(cpu->a);
    ++cpu->pc;
    CpuHandleInterrupts(cpu);
}

static inline void PHA_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    // Dummy read of next instruction byte
    CpuRead8(++cpu->pc);

    CpuPollIRQ(cpu);
    // Push accumulator reg to stack
    StackPush(cpu, cpu->a);
    CpuHandleInterrupts(cpu);
}

static inline void PHP_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    // Dummy read of next instruction byte
    CpuRead8(++cpu->pc);

    Flags status = cpu->status;
    status.b = true;
    status.unused = true;
    CpuPollIRQ(cpu);
    StackPush(cpu, status.raw);
    CpuHandleInterrupts(cpu);
}

static inline void PLA_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    // Dummy read of next instruction byte
    CpuRead8(++cpu->pc);
    // Read for incrementing the SP
    CpuRead8(STACK_START + cpu->sp);

    CpuPollIRQ(cpu);
    cpu->a = StackPull(cpu);
    UPDATE_FLAGS_NZ(cpu->a);
    CpuHandleInterrupts(cpu);
}

static inline void PLP_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    // Dummy read of next instruction byte
    CpuRead8(++cpu->pc);
    // Read for incrementing the SP
    CpuRead8(STACK_START + cpu->sp);

    CpuPollIRQ(cpu);
    uint8_t status_raw = StackPull(cpu);
    Flags status = {.raw = status_raw};

    // Ignore bit for break and 5th bit
    cpu->status.c = status.c;
    cpu->status.d = status.d;
    cpu->status.i = status.i;
    cpu->status.n = status.n;
    cpu->status.v = status.v;
    cpu->status.z = status.z;
    CpuHandleInterrupts(cpu);
}

static inline void ROL_A_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    CpuPollIRQ(cpu);
    // Dummy read of next instruction byte
    CpuRead8(++cpu->pc);

    RotateOneLeft(cpu, &cpu->a);
    CpuHandleInterrupts(cpu);
}

static inline void ROL_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    RotateOneLeftFromMem(cpu, GetOperandAddrFromMem(cpu, addr_mode, page_cycle, true));
    ++cpu->pc;
    CpuHandleInterrupts(cpu);
}

static inline void ROR_A_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    CpuPollIRQ(cpu);
    // Dummy read of next instruction byte
    CpuRead8(++cpu->pc);

    RotateOneRight(cpu, &cpu->a);
    CpuHandleInterrupts(cpu);
}

static inline void ROR_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    RotateOneRightFromMem(cpu, GetOperandAddrFromMem(cpu, addr_mode, page_cycle, true));
    ++cpu->pc;
    CpuHandleInterrupts(cpu);
}

static inline void RTI_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    // Dummy read of next instruction byte
    CpuRead8(++cpu->pc);
    // Read for incrementing the SP
    CpuRead8(STACK_START + cpu->sp);

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
    CpuPollIRQ(cpu);
    uint8_t pc_high = StackPull(cpu);
    cpu->pc = (uint16_t)pc_high << 8 | pc_low;
    CpuHandleInterrupts(cpu);
}

static inline void RTS_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    // Dummy read of next instruction byte
    CpuRead8(++cpu->pc);
    // Read for incrementing the SP
    CpuRead8(STACK_START + cpu->sp);

    uint8_t pc_low = StackPull(cpu);
    uint8_t pc_high = StackPull(cpu);

    CpuPollIRQ(cpu);
    cpu->pc = ((uint16_t)pc_high << 8 | pc_low);
    CpuRead8(cpu->pc++);
    CpuHandleInterrupts(cpu);
}

static inline void SBC_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    const uint16_t operand_addr = GetOperandAddrFromMem(cpu, addr_mode, page_cycle, true);
    CpuPollIRQ(cpu);
    uint8_t operand = CpuRead8(operand_addr);
    // Invert operand since we are reusing ADC logic for SBC
    AddWithCarry(cpu, ~operand);

    ++cpu->pc;
    CpuPollIRQ(cpu);
    CpuHandleInterrupts(cpu);
}

static inline void SEC_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    CpuPollIRQ(cpu);
    // Dummy read of next instruction byte
    CpuRead8(++cpu->pc);

    cpu->status.c = 1;
    CpuHandleInterrupts(cpu);
}

static inline void SED_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    CpuPollIRQ(cpu);
    // Dummy read of next instruction byte
    CpuRead8(++cpu->pc);

    cpu->status.d = 1;
    CpuHandleInterrupts(cpu);
}

static inline void SEI_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    CpuPollIRQ(cpu);
    // Dummy read of next instruction byte
    CpuRead8(++cpu->pc);

    cpu->status.i = 1;
    CpuHandleInterrupts(cpu);
}

static inline void STA_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    const uint16_t operand_addr = GetOperandAddrFromMem(cpu, addr_mode, page_cycle, true);

    CpuPollIRQ(cpu);
    CpuWrite8(operand_addr, cpu->a);
    ++cpu->pc;
    CpuHandleInterrupts(cpu);
}

static inline void STX_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    const uint16_t operand_addr = GetOperandAddrFromMem(cpu, addr_mode, page_cycle, false);

    CpuPollIRQ(cpu);
    CpuWrite8(operand_addr, cpu->x);
    ++cpu->pc;
    CpuHandleInterrupts(cpu);
}

static inline void STY_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    const uint16_t operand_addr = GetOperandAddrFromMem(cpu, addr_mode, page_cycle, false);

    CpuPollIRQ(cpu);
    CpuWrite8(operand_addr, cpu->y);
    ++cpu->pc;
    CpuHandleInterrupts(cpu);
}

// Transfer Accumulator to Index X
static inline void TAX_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    CpuPollIRQ(cpu);
    // Dummy read of next instruction byte
    CpuRead8(++cpu->pc);

    cpu->x = cpu->a;
    // Update status flags
    UPDATE_FLAGS_NZ(cpu->x);

    CpuHandleInterrupts(cpu);
}

// Transfer Accumulator to Index Y
static inline void TAY_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    CpuPollIRQ(cpu);
    // Dummy read of next instruction byte
    CpuRead8(++cpu->pc);

    cpu->y = cpu->a;
    // Update status flags
    UPDATE_FLAGS_NZ(cpu->y);

    CpuHandleInterrupts(cpu);
}

// Transfer Stack Pointer to Index X
static inline void TSX_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    CpuPollIRQ(cpu);
    // Dummy read of next instruction byte
    CpuRead8(++cpu->pc);

    cpu->x = cpu->sp;
    // Update status flags
    UPDATE_FLAGS_NZ(cpu->x);

    CpuHandleInterrupts(cpu);
}

// Transfer Index X to Accumulator
static inline void TXA_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    CpuPollIRQ(cpu);
    // Dummy read of next instruction byte
    CpuRead8(++cpu->pc);

    cpu->a = cpu->x;
    // Update status flags
    UPDATE_FLAGS_NZ(cpu->a);

    CpuHandleInterrupts(cpu);
}

// Transfer Index X to Stack Register
static inline void TXS_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    CpuPollIRQ(cpu);
    // Dummy read of next instruction byte
    CpuRead8(++cpu->pc);

    cpu->sp = cpu->x;

    CpuHandleInterrupts(cpu);
}

// Transfer Index Y to Accumulator
static inline void TYA_Instr(Cpu *cpu, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    CpuPollIRQ(cpu);
    // Dummy read of next instruction byte
    CpuRead8(++cpu->pc);

    cpu->a = cpu->y;

    UPDATE_FLAGS_NZ(cpu->a);
    CpuHandleInterrupts(cpu);
}

static const OpcodeHandler opcodes[256] =
{
    [0x00] = { BRK_Instr,   "BRK",         1, false, Implied     },
    [0x01] = { ORA_Instr,   "ORA (ind,X)", 2, false, IndirectX   },
    [0x03] = { SLO_Instr,   "SLO (ind,X)", 2, false, IndirectX   },
    [0x04] = { NOP_Instr,   "NOP",         2, false, ZeroPage    },
    [0x05] = { ORA_Instr,   "ORA zp",      2, false, ZeroPage    },
    [0x06] = { ASL_Instr,   "ASL zp",      2, false, ZeroPage    },
    [0x07] = { SLO_Instr,   "SLO zp",      2, false, ZeroPage    },
    [0x08] = { PHP_Instr,   "PHP",         1, false, Implied     },
    [0x09] = { ORA_Instr,   "ORA #imm",    2, false, Immediate   },
    [0x0A] = { ASL_A_Instr, "ASL A",       1, false, Accumulator },
    [0x0C] = { NOP_Instr,   "NOP",         3, false, Absolute    },
    [0x0D] = { ORA_Instr,   "ORA abs",     3, false, Absolute    },
    [0x0E] = { ASL_Instr,   "ASL abs",     3, false, Absolute    },
    [0x0F] = { SLO_Instr,   "SLO abs",     3, false, Absolute    },

    [0x10] = { BPL_Instr,   "BPL rel",     2, true,  Relative    },
    [0x11] = { ORA_Instr,   "ORA (ind),Y", 2, true,  IndirectY   },
    [0x13] = { SLO_Instr,   "SLO (ind),Y", 2, false, IndirectY   },
    [0x14] = { NOP_Instr,   "NOP zp,X",    2, false, ZeroPageX   },
    [0x15] = { ORA_Instr,   "ORA zp,X",    2, false, ZeroPageX   },
    [0x16] = { ASL_Instr,   "ASL zp,X",    2, false, ZeroPageX   },
    [0x17] = { SLO_Instr,   "SLO zp,X",    2, false, ZeroPageX   },
    [0x18] = { CLC_Instr,   "CLC",         1, false, Implied     },
    [0x19] = { ORA_Instr,   "ORA abs,Y",   3, true,  AbsoluteY   },
    [0x1A] = { NOP_Instr,   "NOP",         1, false, Implied     },
    [0x1B] = { SLO_Instr,   "SLO abs,Y",   3, false, AbsoluteY   },
    [0x1C] = { NOP_Instr,   "NOP abs,X",   3, true,  AbsoluteX   },
    [0x1D] = { ORA_Instr,   "ORA abs,X",   3, true,  AbsoluteX   },
    [0x1E] = { ASL_Instr,   "ASL abs,X",   3, false, AbsoluteX   },
    [0x1F] = { SLO_Instr,   "SLO abs,X",   3, false, AbsoluteX   },

    [0x20] = { JSR_Instr,   "JSR abs",     3, false, Absolute    },
    [0x21] = { AND_Instr,   "AND (ind,X)", 2, false, IndirectX   },
    [0x24] = { BIT_Instr,   "BIT zp",      2, false, ZeroPage    },
    [0x25] = { AND_Instr,   "AND zp",      2, false, ZeroPage    },
    [0x26] = { ROL_Instr,   "ROL zp",      2, false, ZeroPage    },
    [0x28] = { PLP_Instr,   "PLP",         1, false, Implied     },
    [0x29] = { AND_Instr,   "AND #imm",    2, false, Immediate   },
    [0x2A] = { ROL_A_Instr, "ROL A",       1, false, Accumulator },
    [0x2C] = { BIT_Instr,   "BIT abs",     3, false, Absolute    },
    [0x2D] = { AND_Instr,   "AND abs",     3, false, Absolute    },
    [0x2E] = { ROL_Instr,   "ROL abs",     3, false, Absolute    },

    [0x30] = { BMI_Instr, "BMI rel", 2, true, Relative },
    [0x31] = { AND_Instr, "AND (ind),Y", 2, true, IndirectY },
    [0x34] = { NOP_Instr, "NOP", 2, false, ZeroPageX },
    [0x35] = { AND_Instr, "AND zp,X", 2, false, ZeroPageX },
    [0x36] = { ROL_Instr, "ROL zp,X", 2, false, ZeroPageX },
    [0x38] = { SEC_Instr, "SEC", 1, false, Implied },
    [0x39] = { AND_Instr, "AND abs,Y", 3, true, AbsoluteY },
    [0x3A] = { NOP_Instr, "NOP", 1, false, Implied },
    [0x3C] = { NOP_Instr, "NOP", 3, true, AbsoluteX },
    [0x3D] = { AND_Instr, "AND abs,X", 3, true, AbsoluteX },
    [0x3E] = { ROL_Instr, "ROL abs,X", 3, false, AbsoluteX },

    [0x40] = { RTI_Instr, "RTI", 1, false, Implied },
    [0x41] = { EOR_Instr, "EOR (ind,X)", 2, false, IndirectX },
    [0x44] = { NOP_Instr, "NOP", 2, false, ZeroPage },
    [0x45] = { EOR_Instr, "EOR zp", 2, false, ZeroPage },
    [0x46] = { LSR_Instr, "LSR zp", 2, false, ZeroPage },
    [0x48] = { PHA_Instr, "PHA", 1, false, Implied },
    [0x49] = { EOR_Instr, "EOR #imm", 2, false, Immediate },
    [0x4A] = { LSR_A_Instr, "LSR A", 1, false, Accumulator },
    [0x4C] = { JMP_Instr, "JMP abs", 3, false, Absolute},
    [0x4D] = { EOR_Instr, "EOR abs", 3, false, Absolute},
    [0x4E] = { LSR_Instr, "LSR abs", 3, false, Absolute},

    [0x50] = { BVC_Instr, "BVC rel", 2, true, Relative },
    [0x51] = { EOR_Instr, "EOR (ind),Y", 2, true, IndirectY },
    [0x54] = { NOP_Instr, "NOP", 2, false, ZeroPageX },
    [0x55] = { EOR_Instr, "EOR zp,X", 2, false, ZeroPageX },
    [0x56] = { LSR_Instr, "LSR zp,X", 2, false, ZeroPageX},
    [0x58] = { CLI_Instr, "CLI", 1, false, Implied},
    [0x59] = { EOR_Instr, "EOR abs,Y", 3, true, AbsoluteY},
    [0x5A] = { NOP_Instr, "NOP", 1, false, Implied },
    [0x5C] = { NOP_Instr, "NOP", 3, true, AbsoluteX },
    [0x5D] = { EOR_Instr, "EOR abs,X", 3, true, AbsoluteX },
    [0x5E] = { LSR_Instr, "LSR abs,X", 3, false, AbsoluteX },

    [0x60] = { RTS_Instr, "RTS", 1, false, Implied },
    [0x61] = { ADC_Instr, "ADC (ind,X)", 2, false, IndirectX },
    [0x64] = { NOP_Instr, "NOP", 2, false, ZeroPage },
    [0x65] = { ADC_Instr, "ADC zp", 2, false, ZeroPage },
    [0x66] = { ROR_Instr, "ROR zp", 2, false, ZeroPage },
    [0x68] = { PLA_Instr, "PLA", 1, false, Implied },
    [0x69] = { ADC_Instr, "ADC #imm", 2, false, Immediate },
    [0x6A] = { ROR_A_Instr, "ROR A", 1, false, Accumulator },
    [0x6C] = { JMP_Instr, "JMP (ind)", 3, false, Indirect },
    [0x6D] = { ADC_Instr, "ADC abs", 3, false, Absolute },
    [0x6E] = { ROR_Instr, "ROR abs", 3, false, Absolute },

    [0x70] = { BVS_Instr, "BVS rel", 2, true, Relative },
    [0x71] = { ADC_Instr, "ADC (ind),Y", 2, true, IndirectY },
    [0x74] = { NOP_Instr, "NOP", 2, false, ZeroPageX },
    [0x75] = { ADC_Instr, "ADC zp,X", 2, false, ZeroPageX },
    [0x76] = { ROR_Instr, "ROR zp,X", 2, false, ZeroPageX },
    [0x78] = { SEI_Instr, "SEI", 1, false, Implied },
    [0x79] = { ADC_Instr, "ADC abs,Y", 3, true, AbsoluteY },
    [0x7A] = { NOP_Instr, "NOP", 1, false, Implied },
    [0x7C] = { NOP_Instr, "NOP", 3, true, AbsoluteX },
    [0x7D] = { ADC_Instr, "ADC abs,X", 3, true, AbsoluteX },
    [0x7E] = { ROR_Instr, "ROR abs,X", 3, false, AbsoluteX },

    [0x80] = { NOP_Instr, "NOP", 2, false, Immediate },
    [0x81] = { STA_Instr, "STA (ind,X)", 2, false, IndirectX },
    [0x82] = { NOP_Instr, "NOP", 2, false, Immediate },
    [0x84] = { STY_Instr, "STY zp", 2, false, ZeroPage },
    [0x85] = { STA_Instr, "STA zp", 2, false, ZeroPage },
    [0x86] = { STX_Instr, "STX zp", 2, false, ZeroPage },
    [0x88] = { DEY_Instr, "DEY", 1, false, Implied },
    [0x89] = { NOP_Instr, "NOP", 2, false, Immediate },
    [0x8A] = { TXA_Instr, "TXA", 1, false, Implied },
    [0x8C] = { STY_Instr, "STY abs", 3, false, Absolute },
    [0x8D] = { STA_Instr, "STA abs", 3, false, Absolute },
    [0x8E] = { STX_Instr, "STX abs", 3, false, Absolute },

    [0x90] = { BCC_Instr, "BCC rel", 2, true, Relative },
    [0x91] = { STA_Instr, "STA (ind),Y", 2, false, IndirectY },
    [0x94] = { STY_Instr, "STY zp,X", 2, false, ZeroPageX },
    [0x95] = { STA_Instr, "STA zp,X", 2, false, ZeroPageX },
    [0x96] = { STX_Instr, "STX zp,Y", 2, false, ZeroPageY },
    [0x98] = { TYA_Instr, "TYA", 1, false, Implied },
    [0x99] = { STA_Instr, "STA abs,Y", 3, false, AbsoluteY },
    [0x9A] = { TXS_Instr, "TXS", 1, false, Implied },
    [0x9D] = { STA_Instr, "STA abs,X", 3, false, AbsoluteX },

    [0xA0] = { LDY_Instr, "LDY #imm", 2, false, Immediate },
    [0xA1] = { LDA_Instr, "LDA (ind,X)", 2, false, IndirectX },
    [0xA2] = { LDX_Instr, "LDX #imm", 2, false, Immediate },
    [0xA4] = { LDY_Instr, "LDY zp", 2, false, ZeroPage },
    [0xA5] = { LDA_Instr, "LDA zp", 2, false, ZeroPage },
    [0xA6] = { LDX_Instr, "LDX zp", 2, false, ZeroPage },
    [0xA8] = { TAY_Instr, "TAY", 1, false, Implied },
    [0xA9] = { LDA_Instr, "LDA #imm", 2, false, Immediate },
    [0xAA] = { TAX_Instr, "TAX", 1, false, Implied },
    [0xAC] = { LDY_Instr, "LDY abs", 3, false, Absolute },
    [0xAD] = { LDA_Instr, "LDA abs", 3, false, Absolute },
    [0xAE] = { LDX_Instr, "LDX abs", 3, false, Absolute },

    [0xB0] = { BCS_Instr, "BCS rel", 2,true, Relative },
    [0xB1] = { LDA_Instr, "LDA (ind),Y", 2, true, IndirectY },
    [0xB4] = { LDY_Instr, "LDY zp,X", 2, false, ZeroPageX },
    [0xB5] = { LDA_Instr, "LDA zp,X", 2, false, ZeroPageX },
    [0xB6] = { LDX_Instr, "LDX zp,Y", 2, false, ZeroPageY },
    [0xB8] = { CLV_Instr, "CLV", 1, false, Implied },
    [0xB9] = { LDA_Instr, "LDA abs,Y", 3, true, AbsoluteY },
    [0xBA] = { TSX_Instr, "TSX", 1, false, Implied },
    [0xBC] = { LDY_Instr, "LDY abs,X", 3, true, AbsoluteX },
    [0xBD] = { LDA_Instr, "LDA abs,X", 3, true, AbsoluteX },
    [0xBE] = { LDX_Instr, "LDX abs,Y", 3, true, AbsoluteY },

    [0xC0] = { CPY_Instr, "CPY #imm", 2, false, Immediate },
    [0xC1] = { CMP_Instr, "CMP (ind,X)", 2, false, IndirectX },
    [0xC2] = { NOP_Instr, "NOP", 2, false, Immediate },
    [0xC4] = { CPY_Instr, "CPY zp", 2, false, ZeroPage },
    [0xC5] = { CMP_Instr, "CMP zp", 2, false, ZeroPage },
    [0xC6] = { DEC_Instr, "DEC zp", 2, false, ZeroPage },
    [0xC8] = { INY_Instr, "INY", 1, false, Implied },
    [0xC9] = { CMP_Instr, "CMP #imm", 2, false, Immediate },
    [0xCA] = { DEX_Instr, "DEX", 1, false, Implied },
    [0xCB] = { SBX_Instr, "SBX", 2, false, Immediate },
    [0xCC] = { CPY_Instr, "CPY abs", 3, false, Absolute },
    [0xCD] = { CMP_Instr, "CMP abs", 3, false, Absolute },
    [0xCE] = { DEC_Instr, "DEC abs", 3, false, Absolute },

    [0xD0] = { BNE_Instr, "BNE rel", 2, true, Relative },
    [0xD1] = { CMP_Instr, "CMP (ind),Y", 2, true, IndirectY },
    [0xD4] = { NOP_Instr, "NOP", 2, false, ZeroPageX },
    [0xD5] = { CMP_Instr, "CMP zp,X", 2, false, ZeroPageX },
    [0xD6] = { DEC_Instr, "DEC zp,X", 2, false, ZeroPageX },
    [0xD8] = { CLD_Instr, "CLD", 1, false, Implied },
    [0xD9] = { CMP_Instr, "CMP abs,Y", 3, true, AbsoluteY },
    [0xDA] = { NOP_Instr, "NOP", 1, false, Implied },
    [0xDC] = { NOP_Instr, "NOP", 3, true, AbsoluteX },
    [0xDD] = { CMP_Instr, "CMP abs,X", 3, true, AbsoluteX },
    [0xDE] = { DEC_Instr, "DEC abs,X", 3, false, AbsoluteX },

    [0xE0] = { CPX_Instr, "CPX #imm", 2, false, Immediate },
    [0xE1] = { SBC_Instr, "SBC (ind,X)", 2, false, IndirectX },
    [0xE2] = { NOP_Instr, "NOP #imm", 2, false, Immediate },
    [0xE4] = { CPX_Instr, "CPX zp", 2, false, ZeroPage },
    [0xE5] = { SBC_Instr, "SBC zp", 2, false, ZeroPage },
    [0xE6] = { INC_Instr, "INC zp", 2, false, ZeroPage },
    [0xE8] = { INX_Instr, "INX", 1, false, Implied },
    [0xE9] = { SBC_Instr, "SBC #imm", 2, false, Immediate },
    [0xEA] = { NOP_Instr, "NOP", 1, false, Implied },
    [0xEB] = { SBC_Instr, "SBC #imm", 2, false, Immediate },
    [0xEC] = { CPX_Instr, "CPX abs", 3, false, Absolute },
    [0xED] = { SBC_Instr, "SBC abs", 3, false, Absolute },
    [0xEE] = { INC_Instr, "INC abs", 3, false, Absolute },

    [0xF0] = { BEQ_Instr, "BEQ rel", 2, true, Relative },
    [0xF1] = { SBC_Instr, "SBC (ind),Y", 2, true, IndirectY },
    [0xF4] = { NOP_Instr, "NOP zp,X", 2, false, ZeroPageX },
    [0xF5] = { SBC_Instr, "SBC zp,X", 2, false, ZeroPageX },
    [0xF6] = { INC_Instr, "INC zp,X", 2, false, ZeroPageX },
    [0xF8] = { SED_Instr, "SED", 1, false, Implied },
    [0xF9] = { SBC_Instr, "SBC abs,Y", 3, true, AbsoluteY },
    [0xFA] = { NOP_Instr, "NOP", 1, false, Implied },
    [0xFC] = { NOP_Instr, "NOP abs,X", 3, true, AbsoluteX },
    [0xFD] = { SBC_Instr, "SBC abs,X", 3, true, AbsoluteX },
    [0xFE] = { INC_Instr, "INC abs,X", 3, false, AbsoluteX },
};

static void ExecuteOpcode(Cpu *cpu, bool debug_info)
{
    const uint8_t opcode = CpuRead8(cpu->pc);
    const OpcodeHandler *handler = &opcodes[opcode];

    if (handler->InstrFn)
    {
        CPU_LOG("Executing %s (Opcode: 0x%02X) at PC: 0x%04X\n", handler->name, opcode, cpu->pc);
        if (debug_info)
            snprintf(cpu->debug_msg, sizeof(cpu->debug_msg), "PC:%04X %s", cpu->pc, handler->name);

        // Execute instruction
        handler->InstrFn(cpu, handler->addr_mode, handler->page_cross_penalty);
    }
    else
    {
        printf("\nUnhandled opcode: 0x%02X at PC: 0x%04X\n", opcode, cpu->pc);
        printf("A: 0x%X\nX: 0x%X\nY: 0x%X\nSP: 0x%X\nSR: 0x%X\n", cpu->a, cpu->x, cpu->y, cpu->sp, cpu->status.raw);
        printf("Cycles done: %lu\n", cpu->cycles);
        exit(EXIT_FAILURE);
    }
}

void CPU_Init(Cpu *cpu)
{
    memset(cpu, 0, sizeof(*cpu));
    CPU_Reset(cpu);
}

void CPU_Update(Cpu *cpu, bool debug_info)
{
    ExecuteOpcode(cpu, debug_info);
}

void CPU_Reset(Cpu *cpu)
{
    cpu->cycles = 0;
    cpu->pc = 0xFF;
    // Dummy read
    CpuRead8(cpu->pc);
    // Another dummy read
    CpuRead8(cpu->pc);
    // Stack pointer is decremented by 3 via 3 fake pushes
    CpuRead8(STACK_START + cpu->sp--);
    CpuRead8(STACK_START + cpu->sp--);
    CpuRead8(STACK_START + cpu->sp--);
    // Set interrupt disable flag (I) to prevent IRQs immediately after reset
    cpu->status.i = 1;
    // Read the reset vector from 0xFFFC (little-endian)
    uint16_t reset_vector = CpuReadVector(RESET_VECTOR); 
    
    printf("CPU Reset: Loading reset vector PC: 0x%04X\n", reset_vector);

    // Set PC to the reset vector address
    cpu->pc = reset_vector;
}
