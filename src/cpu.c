#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#include "ppu.h"
#include "utils.h"

#include "mem.h"
#include "cpu.h"
#include "ppu.h"
#include "mapper.h"

#define SYS_RAM_SIZE 0x2000
static uint8_t memory[SYS_RAM_SIZE];
//static uint8_t *memory = NULL;

uint16_t AddressDecode(const uint16_t addr)
{
    // Extract A15, A14, and A13
    uint8_t region = (addr >> 13) & 0x7;

    switch (region)
    {
        case 0x0:  // $0000 - $1FFF
            return addr & 0x7FF;  // Internal RAM (mirrored)

        case 0x1:  // $2000 - $3FFF
            return addr & 7;  // PPU Registers (mirrored every 8)

        case 0x2:  // $4000 - $5FFF
            if (addr < 0x4018)
            {
                DEBUG_LOG("Trying to read APU/IO reg at 0x%04X\n", addr);
                break;
                //return apu_io_read(addr);  // APU & I/O
            }
            else
            {
                printf("Trying to read unknown (Mapper reg?) value at 0x%04X\n", addr);
                break;  // $4018-$5FFF might be mapper-controlled
            }


        case 0x3:  // $6000 - $7FFF
            return addr - 0x6000;  // Battery-backed SRAM (if present)

        case 0x4:  // $8000 - $9FFF
        case 0x5:  // $A000 - $BFFF
        case 0x6:  // $C000 - $DFFF
        case 0x7:  // $E000 - $FFFF
            return addr - 0x8000;
    }

    return 0;  // Default case (open bus behavior)
}

uint32_t CpuRead(const uint16_t addr, const int size)
{
    uint16_t ret = 0;
    // Extract A15, A14, and A13
    uint8_t region = (addr >> 13) & 0x7;

    switch (region)
    {
        case 0x0:  // $0000 - $1FFF
        {
            memcpy(&ret, &memory[addr & 0x7FF], size);
            return ret;
        }

        case 0x1:  // $2000 - $3FFF
        {
            if (size == 1)
            {
                ret = ReadPPURegister(addr);
            }
            //memcpy(&ret, &g_ppu_regs[addr & 7], size);
            return ret;
        }

        case 0x2:  // $4000 - $5FFF
            if (addr < 0x4018)
            {
                //printf("Trying to read APU/IO reg at 0x%04X\n", addr);
                //break;
                memcpy(&ret, &g_apu_regs[addr & 0x17], size);
                return ret;
                //return apu_io_read(addr);  // APU & I/O
            }
            else
            {
                printf("Trying to read unknown (Mapper reg?) value at 0x%04X\n", addr);
                break;
            }


        case 0x3:  // $6000 - $7FFF
        {
            memcpy(&ret, &g_sram[addr & 0x1FFF], size);
            return ret;
        }
    
        case 0x4:  // $8000 - $9FFF
        case 0x5:  // $A000 - $BFFF
        case 0x6:  // $C000 - $DFFF
        case 0x7:  // $E000 - $FFFF
        {
            //memcpy(&ret, &g_prg_rom[addr - 0xC000], size);
            memcpy(&ret, &g_prg_rom[addr % g_prg_rom_size], size);
            return ret;
            //return MapperRead(addr, size);
        }
    }

    return 0;  // open bus
}

uint8_t CpuRead8(const uint16_t addr)
{
    return (uint8_t)CpuRead(addr, 1);
}

uint16_t CpuRead16(const uint16_t addr)
{
    return (uint16_t)CpuRead(addr, 2);
}

/*
uint8_t CpuRead8(const uint16_t addr)
{
    // Extract A15, A14, and A13
    uint8_t region = (addr >> 13) & 0x7;

    switch (region)
    {
        case 0x0:  // $0000 - $1FFF
            return memory[addr & 0x7FF];  // Internal RAM (mirrored)

        case 0x1:  // $2000 - $3FFF
            return g_ppu_regs[(addr & 7) - 0x2000];  // PPU Registers (mirrored every 8)

        case 0x2:  // $4000 - $5FFF
            if (addr < 0x4018)
            {
                printf("Trying to read APU/IO reg at 0x%04X\n", addr);
                break;
                //return apu_io_read(addr);  // APU & I/O
            }
            else
            {
                printf("Trying to read unknown (Mapper reg?) value at 0x%04X\n", addr);
                break;  // $4018-$5FFF might be mapper-controlled
            }


        case 0x3:  // $6000 - $7FFF
            return g_sram[addr - 0x6000];  // Battery-backed SRAM (if present)

        case 0x4:  // $8000 - $9FFF
        case 0x5:  // $A000 - $BFFF
        case 0x6:  // $C000 - $DFFF
        case 0x7:  // $E000 - $FFFF
            return g_prg_rom[addr - 0x8000];
    }

    return 0;  // Default case (open bus behavior)
}
*/

void CpuWrite8(const uint16_t addr, const uint8_t data)
{
    // Extract A15, A14, and A13
    uint8_t region = (addr >> 13) & 0x7;

    switch (region)
    {
        case 0x0:  // $0000 - $1FFF
            // Internal RAM (mirrored)
            memory[addr & 0x7FF] = data;
            break;

        case 0x1:  // $2000 - $3FFF
            // PPU Registers (mirrored every 8)
            g_ppu_regs[(addr & 7)] = data;
            //WritePPURegister(addr, data);
            break;

        case 0x2:  // $4000 - $5FFF
            if (addr < 0x4018)
            {
                if (addr == 0x4014)
                {
                    DEBUG_LOG("Requested OAM DMA 0x%04X\n", addr);
                    OAM_Dma(data);
                    break;
                }
                DEBUG_LOG("Writing to APU/IO reg at 0x%04X\n", addr);
                g_apu_regs[addr % 0x18] = data;

                break;
                //return apu_io_read(addr);  // APU & I/O
            }
            else
            {
                printf("Trying to write unknown (Mapper reg?) value at 0x%04X\n", addr);
                break;  // $4018-$5FFF might be mapper-controlled
            }


        case 0x3:  // $6000 - $7FFF
            g_sram[addr & 0x1FFF] = data;  // Battery-backed SRAM (if present)
            break;

        case 0x4:  // $8000 - $9FFF
        case 0x5:  // $A000 - $BFFF
        case 0x6:  // $C000 - $DFFF
        case 0x7:  // $E000 - $FFFF
            printf("Trying to write to rom at addr 0x%x!\n", addr);
            // Hack
            //g_prg_rom[addr % g_prg_rom_size] = data;
            // Something like this?
            /// MapperWrite(addr, data, 1);
            break;
    }
}

uint8_t *CpuGetPtr(const uint16_t addr)
{
    // Extract A15, A14, and A13
    uint8_t region = (addr >> 13) & 0x7;

    switch (region)
    {
        case 0x0:  // $0000 - $1FFF
        {
            return &memory[addr & 0x7FF];
        }

        case 0x1:  // $2000 - $3FFF
        {
            return &g_ppu_regs[(addr & 7)];
        }

        case 0x2:  // $4000 - $5FFF
            if (addr < 0x4018)
            {
                return &g_apu_regs[addr % 0x18];
                //DEBUG_LOG("Trying to read APU/IO reg at 0x%04X\n", addr);
                //break;
                //return apu_io_read(addr);  // APU & I/O
            }
            else
            {
                printf("Trying to read unknown (Mapper reg?) value at 0x%04X\n", addr);
                break;  // $4018-$5FFF might be mapper-controlled
            }


        case 0x3:  // $6000 - $7FFF
        {
            return &g_sram[addr & 0x1FFF];
        }
    
        case 0x4:  // $8000 - $9FFF
        case 0x5:  // $A000 - $BFFF
        case 0x6:  // $C000 - $DFFF
        case 0x7:  // $E000 - $FFFF
        {
            return &g_prg_rom[addr - 0xC000];
        }
    }

    return NULL;  // Default case (open bus behavior)   
}

void StackPush(Cpu *state, uint8_t data)
{
    memory[STACK_START + state->sp--] = data;
}

// Retrieve the value on the top of the stack and then pop it
uint8_t StackPull(Cpu *state)
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
static uint16_t GetAbsoluteAddr(Cpu *state)
{
    uint8_t addr_low = CpuRead8(++state->pc);
    uint8_t addr_high = CpuRead8(++state->pc);
    uint16_t ret = (uint16_t)addr_high << 8 | addr_low;
    DEBUG_LOG("Absolute addr 0x%04X\n", ret);
    return ret;
}

// PC += 2 
static uint16_t GetAbsoluteXAddr(Cpu *state)
{
    uint16_t base_addr = CpuRead16(state->pc + 1);
    uint16_t final_addr = base_addr + state->x;

    // Extra cycle if pages are different
    if (!InsidePage(base_addr, final_addr))
        state->cycles++;

    state->pc += 2;
    return final_addr;
}

static uint16_t GetAbsoluteXAddr2(Cpu *state, bool add_cycle)
{
    uint16_t base_addr = CpuRead16(state->pc + 1);
    uint16_t final_addr = base_addr + state->x;

    // Apply extra cycle only if required
    if (add_cycle && !InsidePage(base_addr, final_addr))
        state->cycles++;

    state->pc += 2;
    return final_addr;
}

static uint16_t GetAbsoluteYAddr(Cpu *state, bool add_cycle)
{
    uint16_t base_addr = CpuRead16(state->pc + 1);
    uint16_t final_addr = base_addr + state->y;

    // Apply extra cycle only if required
    if (add_cycle && !InsidePage(base_addr, final_addr))
        state->cycles++;

    state->pc += 2;
    return final_addr;
}

// PC += 1
static uint8_t GetZPAddr(Cpu *state)
{
    return CpuRead8(++state->pc);
}

// PC += 1
static uint16_t GetZPIndexedAddr(Cpu *state, uint8_t reg)
{
    uint8_t zp_addr = CpuRead8(++state->pc);

    // Wrap in zero-page
    return (zp_addr + reg) & PAGE_MASK;
}

// PC += 2
static uint16_t GetIndirectAddr(Cpu *state)
{
    uint8_t ptr_low = CpuRead8(++state->pc);
    uint8_t ptr_high = CpuRead8(++state->pc);
    
    uint16_t ptr = (uint16_t)ptr_high << 8 | ptr_low;
    uint16_t new_pc;
    // **6502 Page Boundary Bug** (If ptr is at 0xXXFF, high byte comes from 0xXX00, not 0xXXFF+1)
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
static uint16_t GetPostIndexedIndirectAddr(Cpu *state, bool page_cycle)
{
    uint8_t zp_addr = GetZPAddr(state);
    uint8_t addr_low = CpuRead8(zp_addr);
    uint8_t addr_high = CpuRead8((zp_addr + 1) & PAGE_MASK); // Fetch high (with zero-page wraparound)

    uint16_t base_addr = (uint16_t)addr_high << 8 | addr_low;
    uint16_t final_addr = base_addr + state->y;

    // Extra cycle if in different pages
    if (page_cycle && !InsidePage(base_addr, final_addr))
        state->cycles++;

    return final_addr;
}

// PC += 1
static uint16_t GetPreIndexedIndirectAddr(Cpu *state, uint8_t reg)
{
    uint8_t zp_addr = GetZPAddr(state);
    uint8_t effective_ptr = (zp_addr + reg) & PAGE_MASK; // Wrap in zero-page
    uint8_t addr_low = CpuRead8(effective_ptr);
    uint8_t addr_high = CpuRead8((effective_ptr + 1) & PAGE_MASK); // Wrap in zero-page
    return (uint16_t)addr_high << 8 | addr_low;
}

static void CompareRegAndSetFlags(Cpu *state, uint8_t reg, uint8_t operand)
{
    uint8_t result = reg - operand;
    // Negative flag (bit 7)
    state->status.n = GET_NEG_BIT(result);
    // Zero flag (is result zero?)
    state->status.z = (result == 0) ? 1 : 0;
    // Update the Carry Flag (C)
    state->status.c = (reg >= operand) ? 1 : 0;
}

static void RotateOneLeft(Cpu *state, uint8_t *operand)
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

static void RotateOneRight(Cpu *state, uint8_t *operand)
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

static void ShiftOneRight(Cpu *state, uint8_t *operand)
{
    // Store bit 0 in carry before shifting
    state->status.c = *operand & 1;
    // Shift all bits left by one position
    *operand >>= 1;
    // Update status flags
    state->status.n = 0;    // Clear N flag 
    state->status.z = (*operand == 0) ? 1 : 0;   // Zero flag (is A zero?)
}

static void ShiftOneLeft(Cpu *state, uint8_t *operand)
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
static void AddWithCarry(Cpu *state, uint8_t operand)
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

static void BitwiseAnd(Cpu *state, uint8_t operand)
{
    state->a &= operand;

    state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
    state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
}

uint8_t GetOperandFromMem(Cpu *state, AddressingMode addr_mode, bool page_cycle)
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
            //return memory[GetAbsoluteAddr(state)];
        case AbsoluteX:
            return CpuRead8(GetAbsoluteXAddr2(state, page_cycle));
            //return memory[GetAbsoluteAddr(state) + state->x];
        case AbsoluteY:
            return CpuRead8(GetAbsoluteYAddr(state, page_cycle));
            //return memory[GetAbsoluteAddr(state) + state->y];
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
static void SetOperandToMem(Cpu *state, AddressingMode addr_mode, uint8_t operand)
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
            //memory[GetAbsoluteAddr(state)] = operand;
            CpuWrite8(GetAbsoluteAddr(state), operand);
            break;
        case AbsoluteX:
            CpuWrite8(GetAbsoluteAddr(state) + state->x, operand);
            //memory[GetAbsoluteAddr(state) + state->x] = operand;
            break;
        case AbsoluteY:
            CpuWrite8(GetAbsoluteAddr(state) + state->y, operand);
            //memory[GetAbsoluteAddr(state) + state->y] = operand;
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

uint16_t GetJMPAddr(Cpu *state, AddressingMode addr_mode)
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

uint8_t *GetOperandPtrFromMem(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    switch (addr_mode)
    {
        case Accumulator:
            return &state->a;
        case Relative:
        case Immediate:
            return CpuGetPtr(++state->pc);
        case ZeroPage:
            return CpuGetPtr(GetZPAddr(state));
        case ZeroPageX:
            return CpuGetPtr(GetZPIndexedAddr(state, state->x));
        case ZeroPageY:
            return CpuGetPtr(GetZPIndexedAddr(state, state->y));
        case Absolute:
            return CpuGetPtr(GetAbsoluteAddr(state));
        case AbsoluteX:
            return CpuGetPtr(GetAbsoluteXAddr2(state, page_cycle));
            //return &memory[GetAbsoluteAddr(state) + state->x];
        case AbsoluteY:
            return CpuGetPtr(GetAbsoluteYAddr(state, page_cycle));
            //return &memory[GetAbsoluteAddr(state) + state->y];
        case PreIndexedIndirect:
            return CpuGetPtr(GetPreIndexedIndirectAddr(state, state->x));
        case PostIndexedIndirect:
            return CpuGetPtr(GetPostIndexedIndirectAddr(state, page_cycle));
        case Implied:
        case Indirect:
            return NULL;
    
    }

    return NULL;
}

static void ADC_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    AddWithCarry(state, operand);
    state->pc++;
}

static void AND_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    BitwiseAnd(state, operand);
    state->pc++;
}

static void ASL_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t *operand = GetOperandPtrFromMem(state, addr_mode, page_cycle);
    ShiftOneLeft(state, operand);
    state->pc++;
}

static void BCC_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++state->pc);
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

static void BCS_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++state->pc);
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

static void BEQ_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++state->pc);
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

static void BIT_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    state->status.n = GET_NEG_BIT(operand);
    state->status.v = GET_OVERFLOW_BIT(operand);
    state->status.z = !(state->a & operand);
    state->pc++;
}

static void BMI_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++state->pc);
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

static void BNE_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++state->pc);
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

static void BPL_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++state->pc);
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

static void BRK_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

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
    //uint16_t ret = (uint16_t)MemRead8(0xFFFF) << 8 | MemRead8( 0xFFFE);
    uint16_t brk_non_mirror_addr = CpuRead16(0xFFFE);
    ////uint16_t ret = 0;
    uint16_t ret = (uint16_t)CpuRead8(brk_non_mirror_addr + 1) << 8 | CpuRead8(brk_non_mirror_addr);
    DEBUG_LOG("New PC at 0x%04X\n", ret);
    //state->pc = MemGetNonMirroredAddr(ret);
    state->pc = ret;

    DEBUG_LOG("Jumping to IRQ vector at 0x%X\n", state->pc);
}

static void BVC_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++state->pc);
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

static void BVS_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    int8_t offset = (int8_t)CpuRead8(++state->pc);
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

static void CLC_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->status.c = 0;
    state->pc++;
}

static void CLD_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->status.d = 0;
    state->pc++;
}

static void CLI_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->status.i = 0;
    state->pc++;
}

static void CLV_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->status.v = 0;
    state->pc++;
}

static void CMP_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    CompareRegAndSetFlags(state, state->a, operand);
    state->pc++;
}

static void CPX_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    CompareRegAndSetFlags(state, state->x, operand);
    state->pc++;
}

static void CPY_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    CompareRegAndSetFlags(state, state->y, operand);
    state->pc++;
}

static void DEC_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t *operand = GetOperandPtrFromMem(state, addr_mode, page_cycle);

    *operand -= 1;

    state->status.n = GET_NEG_BIT(*operand);  // Negative flag (bit 7)
    state->status.z = (*operand == 0) ? 1 : 0;    // Zero flag (is value zero?)

    state->pc++;
}

static void DEX_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->x--;
    state->status.n = GET_NEG_BIT(state->x);  // Negative flag (bit 7)
    state->status.z = (state->x == 0) ? 1 : 0;    // Zero flag (is X zero?)

    state->pc++;
}

static void DEY_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->y--;
    state->status.n = GET_NEG_BIT(state->y);  // Negative flag (bit 7)
    state->status.z = (state->y == 0) ? 1 : 0;    // Zero flag (is X zero?)

    state->pc++;
}

static void EOR_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    state->a ^= operand;
    state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
    state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
    state->pc++;
}

static void INC_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t *operand = GetOperandPtrFromMem(state, addr_mode, page_cycle);

    *operand += 1;

    state->status.n = GET_NEG_BIT(*operand);  // Negative flag (bit 7)
    state->status.z = (*operand == 0) ? 1 : 0;    // Zero flag (is value zero?)

    state->pc++;
}

static void INX_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->x++;
    state->status.n = GET_NEG_BIT(state->x);  // Negative flag (bit 7)
    state->status.z = (state->x == 0) ? 1 : 0;    // Zero flag (is value zero?)

    state->pc++;
}

static void INY_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->y++;
    state->status.n = GET_NEG_BIT(state->y);  // Negative flag (bit 7)
    state->status.z = (state->y == 0) ? 1 : 0;    // Zero flag (is value zero?)

    state->pc++;
}

static void JMP_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    UNUSED(page_cycle);

    uint16_t new_pc;
    if (addr_mode == Absolute)
        new_pc = GetAbsoluteAddr(state);
    else
        new_pc = GetIndirectAddr(state);

    //state->pc = MemGetNonMirroredAddr(new_pc);
    state->pc = new_pc;
}

static void JSR_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    uint8_t pc_low = CpuRead8(++state->pc);
    uint8_t pc_high = CpuRead8(++state->pc);

    StackPush(state, (state->pc >> 8) & 0xFF);
    StackPush(state, state->pc & 0xFF);

    //uint16_t new_pc = MemGetNonMirroredAddr((uint16_t)pc_high << 8 | pc_low);
    uint16_t new_pc = (uint16_t)pc_high << 8 | pc_low;
    state->pc = new_pc;
}

static void LDA_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    state->a = operand;

    // Update status flags
    state->status.n = GET_NEG_BIT(state->a);  // Negative flag (bit 7)
    state->status.z = (state->a == 0) ? 1 : 0; // Zero flag (is A zero?)
    state->pc++;
}

static void LDX_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    state->x = operand;

    // Update status flags
    state->status.n = GET_NEG_BIT(state->x);  // Negative flag (bit 7)
    state->status.z = (state->x == 0) ? 1 : 0; // Zero flag (is A zero?)
    state->pc++;
}

static void LDY_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    state->y = operand;

    // Update status flags
    UPDATE_FLAGS_NZ(state->y)
    state->status.n = GET_NEG_BIT(state->y);  // Negative flag (bit 7)
    state->status.z = (state->y == 0) ? 1 : 0; // Zero flag (is A zero?)
    state->pc++;
}

static void LSR_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t *operand = GetOperandPtrFromMem(state, addr_mode, page_cycle);
    ShiftOneRight(state, operand);
    state->pc++;
}

static void NOP_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->pc++;
}

static void ORA_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t operand = GetOperandFromMem(state, addr_mode, page_cycle);
    state->a |= operand;
    state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
    state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)
    state->pc++;
}

static void PHA_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    StackPush(state, state->a);
    state->pc++;
}

static void PHP_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    Flags status = state->status;
    status.b = true;
    status.unused = true;
    StackPush(state, status.raw);
    state->pc++;
}

static void PLA_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->a = StackPull(state);
    state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
    state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is Y zero?)
    state->pc++;
}

static void PLP_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
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

    state->pc++;
}

static void ROL_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t *operand = GetOperandPtrFromMem(state, addr_mode, page_cycle);
    RotateOneLeft(state, operand);
    state->pc++;
}

static void ROR_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    uint8_t *operand = GetOperandPtrFromMem(state, addr_mode, page_cycle);
    RotateOneRight(state, operand);
    state->pc++;
}

static void RTI_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
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

    uint16_t new_pc = (uint16_t)pc_high << 8 | pc_low;
    //state->pc = MemGetNonMirroredAddr(new_pc);
    state->pc = new_pc;
}

static void RTS_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    uint8_t pc_low = StackPull(state);
    uint8_t pc_high = StackPull(state);

    uint16_t new_pc = (uint16_t)pc_high << 8 | pc_low;
    //state->pc = MemGetNonMirroredAddr(new_pc);
    state->pc = new_pc;
    state->pc++;
}

static void SBC_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
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

static void SEC_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->status.c = 1;
    state->pc++;
}

static void SED_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->status.d = 1;
    state->pc++;
}

static void SEI_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    // Disable interrupts
    state->status.i = 1;
    state->pc++;
}

static void STA_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    UNUSED(page_cycle);

    SetOperandToMem(state, addr_mode, state->a);
    state->pc++;
}

static void STX_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    UNUSED(page_cycle);

    SetOperandToMem(state, addr_mode, state->x);
    state->pc++;
}

static void STY_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    UNUSED(page_cycle);

    SetOperandToMem(state, addr_mode, state->y);
    state->pc++;
}

// Transfer Accumulator to Index X
static void TAX_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->x = state->a;
    state->status.n = GET_NEG_BIT(state->x);  // Negative flag (bit 7)
    state->status.z = (state->x == 0) ? 1 : 0; // Zero flag (is x zero?)

    state->pc++;
}

// Transfer Accumulator to Index Y
static void TAY_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->y = state->a;
    state->status.n = GET_NEG_BIT(state->y);  // Negative flag (bit 7)
    state->status.z = (state->y == 0) ? 1 : 0; // Zero flag (is x zero?)

    state->pc++;
}

// Transfer Stack Pointer to Index X
static void TSX_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->x = state->sp;
    state->status.n = GET_NEG_BIT(state->x);  // Negative flag (bit 7)
    state->status.z = (state->x == 0) ? 1 : 0; // Zero flag (is x zero?)

    state->pc++;
}

// Transfer Stack Pointer to Accumulator
static void TSA_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->a = state->sp;
    state->status.n = GET_NEG_BIT(state->a);  // Negative flag (bit 7)
    state->status.z = (state->a == 0) ? 1 : 0; // Zero flag (is x zero?)

    state->pc++;
}

// Transfer Index X to Accumulator
static void TXA_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->a = state->x;
    state->status.n = GET_NEG_BIT(state->a);  // Negative flag (bit 7)
    state->status.z = (state->a == 0) ? 1 : 0; // Zero flag (is x zero?)

    state->pc++;
}

// Transfer Index X to Stack Register
static void TXS_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->sp = state->x;

    state->pc++;
}

// Transfer Index Y to Accumulator
static void TYA_Instr(Cpu *state, AddressingMode addr_mode, bool page_cycle)
{
    // Unused
    UNUSED(addr_mode);
    UNUSED(page_cycle);

    state->a = state->y;
    state->status.n = GET_NEG_BIT(state->a);    // Negative flag (bit 7)
    state->status.z = (state->a == 0) ? 1 : 0;  // Zero flag (is A zero?)

    state->pc++;
}

typedef struct {
    void (*InstrFn)(Cpu *state, AddressingMode addr_mode, bool page_cross_penalty);  // Function pointer to execute opcode
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

void ExecuteOpcode(Cpu *state)
{
    const uint8_t opcode = CpuRead8(state->pc);
    const OpcodeHandler *handler = &opcodes[opcode];

    if (handler->InstrFn)
    {
        DEBUG_LOG("Executing %s (Opcode: 0x%02X) at PC: 0x%04X\n", handler->name, opcode, state->pc);
        handler->InstrFn(state, handler->addr_mode, handler->page_cross_penalty);
        state->cycles += handler->cycles;  // Add base cycles
    }
    else
    {
        printf("Unhandled opcode: 0x%02X at PC: 0x%04X\n\n", opcode, state->pc);
        printf("Cycles done: %lu\n", state->cycles);
        state->pc++;
        //exit(EXIT_FAILURE);
    }
}

void CPU_Init(Cpu *state)
{
    memset(state, 0, sizeof(*state));
}

static uint16_t ReadVector(uint16_t addr)
{
    return (uint16_t)CpuRead8(addr + 1) << 8 | CpuRead8(addr);
}

void HandleIRQ(Cpu *cpu)
{
    cpu->status.i = 1;  // Disable further IRQs
    StackPush(cpu, (cpu->pc >> 8) & 0xFF);  // Push PC high byte
    StackPush(cpu, cpu->pc & 0xFF); // Push PC low byte
    StackPush(cpu, cpu->status.raw & ~0x10); // Push Processor Status (clear Break flag)
    //cpu->pc = MemRead16(0xFFFE); // Load IRQ vector

    //uint16_t irq_non_mirror_addr = MemGetNonMirroredAddr(0xFFFE);
    uint16_t ret = ReadVector(0xFFFE);
    DEBUG_LOG("New PC at 0x%04X\n", ret);
    //cpu->pc = MemGetNonMirroredAddr(ret);
    cpu->pc = ret;
}

void CPU_TriggerNMI(Cpu *state)
{
    // Push high first
    StackPush(state, (state->pc >> 8) & 0xFF);
    // Push low next
    StackPush(state, state->pc & 0xFF);
    StackPush(state, state->status.raw | 0x20); // Push status with bit 5 set


    //uint16_t nmi_non_mirror_addr = MemGetNonMirroredAddr(0xFFFA);
    //uint16_t ret = (uint16_t)MemRead8(nmi_non_mirror_addr + 1) << 8 | MemRead8(nmi_non_mirror_addr);
    //DEBUG_LOG("New PC at 0x%04X\n", ret);
    //state->pc = MemGetNonMirroredAddr(ret);
    state->pc = ReadVector(0xFFFA);
    state->status.i = 1; // Set interrupt disable flag
}

static bool irq_pending;
static bool nmi_pending;

//void CPU_Update(Cpu *state, bool irq_pending, bool nmi_pending)
void CPU_Update(Cpu *state)
{
    ExecuteOpcode(state);

    // NMI always comes first
    if (PPU_NmiTriggered())
    {
        printf("NMI triggered!\n");
        CPU_TriggerNMI(state);
    }

    // Check for interrupts AFTER executing an instruction
    if (irq_pending && !state->status.i)
    {
        HandleIRQ(state);
    }

}

void CPU_Reset(Cpu *state)
{
    // Read the reset vector from 0xFFFC (little-endian)
    uint16_t reset_vector = CpuRead8(0xFFFC) | (CpuRead8(0xFFFD) << 8);
    
    DEBUG_LOG("New PC at 0x%04X\n", reset_vector);

    // Set PC to the reset vector address
    state->pc = reset_vector;

    // Stack pointer is decremented by 3 (emulating an interrupt return state)
    state->sp -= 3;

    // Set interrupt disable flag (I) to prevent IRQs immediately after reset
    state->status.i = 1;

    // Reset cycles
    state->cycles = 7;
}
