#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#define VBLANK_ADDR 0x0040
#define LCD_STAT_ADDR 0x0048
#define TIMER_ADDR 0x0050
#define SERIAL_ADDR 0x0058
#define JOYPAD_ADDR 0x0060

__uint8_t *read_file(const char *filename, __uint8_t *buffer)
{
    FILE *file = fopen(filename, "rb");
    if (file == NULL)
    {
        perror("Failed to open file");
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long filesize = ftell(file);
    rewind(file);

    size_t bytesRead = fread(buffer, 1, filesize, file);
    if (bytesRead != filesize)
    {
        perror("Failed to read the entire file");
        free(buffer);
        fclose(file);
        return NULL;
    }

    fclose(file);

    return buffer;
}

typedef struct
{
    __uint8_t A;
    __uint8_t F;
    __uint8_t B;
    __uint8_t C;
    __uint8_t D;
    __uint8_t E;
    __uint8_t H;
    __uint8_t L;
} Registers;

typedef struct
{
    __uint16_t div_counter;
    __uint16_t tima_cycles;
    __uint8_t current_t_cycles;
    __uint8_t halted;
    __uint16_t PC;
    __uint16_t SP;
    Registers registers;
    __uint8_t Z;   // Zero Flag
    __uint8_t N;   // Subtract Flag
    __uint8_t H;   // Half Carry Flag
    __uint8_t C;   // Carry Flag
    __uint8_t IME; // IME flag
    bool ime_delay;
} CPU;

typedef struct
{
    __uint8_t memory[0xFFFF];
} Memory;

__uint8_t get_opcode(__uint8_t *buffer, CPU *cpu)
{
    return buffer[cpu->PC];
}

__uint8_t get_F(CPU *cpu)
{
    return (cpu->Z << 7) | (cpu->N << 6) | (cpu->H << 5) | (cpu->C << 4);
}

void print_cpu(CPU *cpu, Memory *memory, FILE *file)
{
    fprintf(file, "A:%02X F:%02X B:%02X C:%02X D:%02X E:%02X H:%02X L:%02X SP:%04X PC:%04X PCMEM:%02X,%02X,%02X,%02X\n",
            cpu->registers.A, get_F(cpu), cpu->registers.B, cpu->registers.C, cpu->registers.D, cpu->registers.E, cpu->registers.H,
            cpu->registers.L, cpu->SP, cpu->PC, memory->memory[cpu->PC], memory->memory[cpu->PC + 1], memory->memory[cpu->PC + 2], memory->memory[cpu->PC + 3]);
}

__int16_t sign_extend(__uint8_t value)
{
    return (__int16_t)((__int8_t)value);
}

int check_underflow_sub(__uint8_t a, __uint8_t b)
{
    return (a < b);
}

void store_HL(CPU *cpu, __uint16_t val)
{
    cpu->registers.H = (val & 0xFF00) >> 8;
    cpu->registers.L = (val & 0x00FF);
}

void store_DE(CPU *cpu, __uint16_t val)
{
    cpu->registers.D = (val & 0xFF00) >> 8;
    cpu->registers.E = (val & 0x00FF);
}

void store_BC(CPU *cpu, __uint16_t val)
{
    cpu->registers.B = (val & 0xFF00) >> 8;
    cpu->registers.C = (val & 0x00FF);
}

void store_SP(CPU *cpu, Memory *memory)
{
    memory->memory[--cpu->SP] = (cpu->PC & 0xFF00) >> 8;
    memory->memory[--cpu->SP] = cpu->PC & 0xFF;
}

void store_PC(CPU *cpu, Memory *memory)
{
    memory->memory[--cpu->SP] = (cpu->PC & 0xFF00) >> 8;
    memory->memory[--cpu->SP] = cpu->PC & 0xFF;
}

__int16_t get_HL(CPU *cpu)
{
    __uint16_t H = cpu->registers.H;
    __uint8_t L = cpu->registers.L;

    return (H << 8) | L;
}

__int16_t get_BC(CPU *cpu)
{
    __uint16_t B = cpu->registers.B;
    __uint8_t C = cpu->registers.C;

    return (B << 8) | C;
}

__int16_t get_DE(CPU *cpu)
{
    __uint16_t D = cpu->registers.D;
    __uint8_t E = cpu->registers.E;

    return (D << 8) | E;
}

__int16_t get_SP(CPU *cpu, Memory *memory)
{
    __uint8_t v1 = memory->memory[cpu->SP++];
    __uint16_t v2 = memory->memory[cpu->SP++];

    return (v2 << 8) | v1;
}

__int16_t get_a16(CPU *cpu, Memory *memory)
{
    __uint8_t v1 = memory->memory[cpu->PC++];
    __uint8_t v2 = memory->memory[cpu->PC++];

    return (v2 << 8) | v1;
}

void PUSH_PC(CPU *cpu, Memory *memory)
{
    memory->memory[--cpu->SP] = (cpu->PC & 0xFF00) >> 8;
    memory->memory[--cpu->SP] = cpu->PC & 0xFF;
}

void PUSH_DE(CPU *cpu, Memory *memory)
{
    memory->memory[--cpu->SP] = cpu->registers.D;
    memory->memory[--cpu->SP] = cpu->registers.E;
    cpu->current_t_cycles += 16;
}

void PUSH_BC(CPU *cpu, Memory *memory)
{
    memory->memory[--cpu->SP] = cpu->registers.B;
    memory->memory[--cpu->SP] = cpu->registers.C;
    cpu->current_t_cycles += 16;
}

void PUSH_AF(CPU *cpu, Memory *memory)
{
    memory->memory[--cpu->SP] = cpu->registers.A;
    memory->memory[--cpu->SP] = get_F(cpu);
    cpu->current_t_cycles += 16;
}

void PUSH_HL(CPU *cpu, Memory *memory)
{
    memory->memory[--cpu->SP] = cpu->registers.H;
    memory->memory[--cpu->SP] = cpu->registers.L;
    cpu->current_t_cycles += 16;
}

void RR_r8(CPU *cpu, __uint8_t *r8)
{
    __uint8_t val = *r8;
    __uint8_t C = val & 1u;
    val >>= 1;
    val |= (cpu->C << 7);

    *r8 = val;
    cpu->Z = val == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = C;
    cpu->current_t_cycles += 8;
}

void RRA(CPU *cpu, __uint8_t *r8)
{
    __uint8_t val = *r8;
    __uint8_t C = val & 1u;
    val >>= 1;
    val |= (cpu->C << 7);

    *r8 = val;
    cpu->Z = 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = C;
    cpu->current_t_cycles += 4;
}

void DEC_r8(CPU *cpu, __uint8_t *r8)
{
    __uint8_t val = *r8;
    cpu->H = (val & 0xF) == 0;
    val -= 1;
    cpu->Z = val == 0;
    cpu->N = 1;
    *r8 = val;
    cpu->current_t_cycles += 4;
}

void DEC_SP(CPU *cpu)
{
    cpu->SP--;
}

void INC_SP(CPU *cpu)
{
    cpu->SP++;
    cpu->current_t_cycles += 8;
}

void INC_BC(CPU *cpu, Memory *memory)
{
    __uint16_t BC = get_BC(cpu);
    BC++;
    store_BC(cpu, BC);
    cpu->registers.B = (BC & 0xFF00) >> 8;
    cpu->registers.C = (BC & 0x00FF);
    cpu->current_t_cycles += 8;
}

void INC_DE(CPU *cpu)
{
    __uint16_t DE = get_DE(cpu);
    DE++;
    store_DE(cpu, DE);
    cpu->current_t_cycles += 8;
}

void INC_r8(CPU *cpu, __uint8_t *r8)
{
    __uint8_t val = *r8;
    cpu->H = (val & 0x0F) + 1 > 0x0F;
    *r8 = ++val;
    cpu->Z = val == 0;
    cpu->N = 0;
    cpu->current_t_cycles += 4;
}

void SUB_A_n8(CPU *cpu, Memory *memory)
{
    __uint8_t n8 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    cpu->H = (cpu->registers.A & 0x0F) < (n8 & 0x0F);
    cpu->C = cpu->registers.A < n8;
    cpu->registers.A -= n8;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 1;
    cpu->current_t_cycles += 8;
}

void INC_HL(CPU *cpu, Memory *memory)
{
    __uint16_t HL = get_HL(cpu);
    HL++;
    store_HL(cpu, HL);
    cpu->current_t_cycles += 8;
}

void LD_HLD_A(CPU *cpu, Memory *memory)
{
    __uint16_t HL = get_HL(cpu);
    memory->memory[HL] = cpu->registers.A;
    HL--;
    store_HL(cpu, HL);
    cpu->current_t_cycles += 8;
}

void LD_HLI_A(CPU *cpu, Memory *memory)
{
    __uint16_t HL = get_HL(cpu);
    memory->memory[HL] = cpu->registers.A;
    HL++;
    store_HL(cpu, HL);
    cpu->current_t_cycles += 8;
}

void LD_A_HLI(CPU *cpu, Memory *memory)
{
    __uint16_t HL = get_HL(cpu);
    cpu->registers.A = memory->memory[HL];
    HL++;
    store_HL(cpu, HL);
    cpu->current_t_cycles += 8;
}

void DEC_HL_a16(CPU *cpu, Memory *memory)
{
    __uint16_t HL = get_HL(cpu);
    __uint8_t val = memory->memory[HL];
    cpu->H = (val & 0xF) == 0;
    val -= 1;
    cpu->Z = val == 0;
    cpu->N = 1;
    memory->memory[HL] = val;
    cpu->current_t_cycles += 12;
}

void DEC_HL_r16(CPU *cpu, Memory *memory)
{
    __uint16_t HL = get_HL(cpu);
    HL--;
    cpu->registers.H = (HL & 0xFF00) >> 8;
    cpu->registers.L = (HL & 0x00FF);
    cpu->current_t_cycles += 8;
}

void DEC_BC(CPU *cpu)
{
    __uint16_t BC = get_BC(cpu);
    BC--;
    cpu->registers.B = (BC & 0xFF00) >> 8;
    cpu->registers.C = (BC & 0x00FF);
    cpu->current_t_cycles += 8;
}

void DEC_DE_r16(CPU *cpu)
{
    __uint16_t DE = get_DE(cpu);
    DE--;
    store_DE(cpu, DE);
    cpu->current_t_cycles += 8;
}

void DAA(CPU *cpu)
{
    __uint8_t adj = 0;
    if (cpu->N)
    {
        if (cpu->H)
            adj += 0x6;
        if (cpu->C)
            adj += 0x60;
        cpu->registers.A -= adj;
    }
    else
    {
        if (cpu->H || ((cpu->registers.A & 0xF) > 0x9))
            adj += 0x6;
        if (cpu->C || (cpu->registers.A > 0x99))
        {
            adj += 0x60;
            cpu->C = 1;
        }
        cpu->registers.A += adj;
    }

    cpu->Z = cpu->registers.A == 0;
    cpu->H = 0;
    cpu->current_t_cycles += 4;
}

void HALT(CPU *cpu)
{
    cpu->halted = 1;
}

void EI(CPU *cpu)
{
    cpu->ime_delay = 1;
    cpu->current_t_cycles += 4;
}

void DI(CPU *cpu)
{
    cpu->IME = 0;
    cpu->ime_delay = 0;
    cpu->current_t_cycles += 4;
}

void NOP(CPU *cpu)
{
    cpu->current_t_cycles += 4;
}

void LD_r8_HL(CPU *cpu, Memory *memory, __uint8_t *r8)
{
    __uint16_t HL = get_HL(cpu);
    *r8 = memory->memory[HL];
    cpu->current_t_cycles += 8;
}

void LD_SP_HL(CPU *cpu)
{
    __uint16_t HL = get_HL(cpu);
    cpu->SP = HL;
    cpu->current_t_cycles += 8;
}

void LD_SP_n16(CPU *cpu, Memory *memory)
{
    __uint16_t a16 = get_a16(cpu, memory);
    cpu->SP = a16;
    cpu->current_t_cycles += 12;
}

void LD_a16_SP(CPU *cpu, Memory *memory)
{
    __uint8_t a1 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    __uint16_t a2 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    __uint16_t a16 = a1 | (a2 << 8);
    memory->memory[a16] = cpu->SP & 0x00FF;
    memory->memory[a16 + 1] = (cpu->SP & 0xFF00) >> 8;
    cpu->current_t_cycles += 20;
}

void LD_r8_n8(CPU *cpu, Memory *memory, __uint8_t *r8)
{
    __uint8_t n8 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    *r8 = n8;
    cpu->current_t_cycles += 8;
}

void LD_r8_r8(CPU *cpu, __uint8_t *r_dest, __uint8_t val)
{
    *r_dest = val;
    cpu->current_t_cycles += 4;
}

void LD_A_a16(CPU *cpu, Memory *memory)
{
    __uint16_t a16 = get_a16(cpu, memory);
    cpu->registers.A = memory->memory[a16];
    cpu->current_t_cycles += 16;
}

void LD_a16_A(CPU *cpu, Memory *memory)
{
    __uint16_t a16 = get_a16(cpu, memory);
    memory->memory[a16] = cpu->registers.A;
    cpu->current_t_cycles += 16;
}

void LD_a8_A(CPU *cpu, Memory *memory)
{
    __uint8_t a8 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    memory->memory[0xFF00 + a8] = cpu->registers.A;
    cpu->current_t_cycles += 12;
}

void LD_A_a8(CPU *cpu, Memory *memory)
{
    __uint8_t a8 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    cpu->registers.A = memory->memory[0xFF00 + a8];
    cpu->current_t_cycles += 12;
}

void LD_A_C(CPU *cpu, Memory *memory)
{
    __uint8_t value = memory->memory[0xFF00 + cpu->registers.C];
    cpu->registers.A = value;
    cpu->current_t_cycles += 8;
}

void LD_C_A(CPU *cpu, Memory *memory)
{
    memory->memory[0xFF00 + cpu->registers.C] = cpu->registers.A;
    cpu->current_t_cycles += 8;
}

void LD_BC_n16(CPU *cpu, Memory *memory)
{
    __uint8_t v1 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    __uint8_t v2 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    cpu->registers.C = v1;
    cpu->registers.B = v2;
    cpu->current_t_cycles += 12;
}

void LD_DE_n16(CPU *cpu, Memory *memory)
{
    __uint8_t v1 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    __uint8_t v2 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    cpu->registers.E = v1;
    cpu->registers.D = v2;
    cpu->current_t_cycles += 12;
}

void LD_HL_n16(CPU *cpu, Memory *memory)
{
    __uint8_t v1 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    __uint8_t v2 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    cpu->registers.L = v1;
    cpu->registers.H = v2;
    cpu->current_t_cycles += 12;
}

void LD_DE_A(CPU *cpu, Memory *memory)
{
    __uint16_t DE = get_DE(cpu);
    memory->memory[DE] = cpu->registers.A;
    cpu->current_t_cycles += 8;
}

void LD_A_DE(CPU *cpu, Memory *memory)
{
    __uint16_t DE = get_DE(cpu);
    cpu->registers.A = memory->memory[DE];
    cpu->current_t_cycles += 8;
}

void XOR_A_r8(CPU *cpu, __uint8_t val)
{
    cpu->registers.A ^= val;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = 0;
    cpu->current_t_cycles += 4;
}

void XOR_A_n8(CPU *cpu, Memory *memory)
{
    __uint8_t n8 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    cpu->registers.A ^= n8;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = 0;
    cpu->current_t_cycles += 8;
}

void XOR_A_HL(CPU *cpu, Memory *memory)
{
    __uint16_t HL = get_HL(cpu);
    cpu->registers.A ^= memory->memory[HL];
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = 0;
    cpu->current_t_cycles += 8;
}

void OR_A(CPU *cpu, __uint8_t val)
{
    cpu->registers.A |= val;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = 0;
    cpu->current_t_cycles += 4;
}

void OR_A_n8(CPU *cpu, Memory *memory)
{
    __uint8_t n8 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    cpu->registers.A |= n8;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = 0;
    cpu->current_t_cycles += 8;
}

void AND_A_n8(CPU *cpu, Memory *memory)
{
    __uint8_t n8 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    cpu->registers.A &= n8;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 0;
    cpu->H = 1;
    cpu->C = 0;
    cpu->current_t_cycles += 8;
}

void AND_A_r8(CPU *cpu, __uint8_t val)
{
    cpu->registers.A &= val;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 0;
    cpu->H = 1;
    cpu->C = 0;
    cpu->current_t_cycles += 4;
}

void OR_A_HL(CPU *cpu, Memory *memory)
{
    __uint16_t HL = get_HL(cpu);
    OR_A(cpu, memory->memory[HL]);
    cpu->current_t_cycles += 4;
}

void LD_HL_r8(CPU *cpu, Memory *memory, __uint8_t r8)
{
    __uint16_t HL = get_HL(cpu);
    memory->memory[HL] = r8;
    cpu->current_t_cycles += 8;
}

void LD_HL_n8(CPU *cpu, Memory *memory)
{
    __uint8_t n8 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    __uint16_t HL = get_HL(cpu);
    memory->memory[HL] = n8;
    cpu->current_t_cycles += 12;
}

void POP_DE(CPU *cpu, Memory *memory)
{
    cpu->registers.E = memory->memory[cpu->SP++];
    cpu->registers.D = memory->memory[cpu->SP++];
    cpu->current_t_cycles += 12;
}

void POP_BC(CPU *cpu, Memory *memory)
{
    cpu->registers.C = memory->memory[cpu->SP++];
    cpu->registers.B = memory->memory[cpu->SP++];
    cpu->current_t_cycles += 12;
}

void POP_AF(CPU *cpu, Memory *memory)
{
    cpu->registers.F = memory->memory[cpu->SP++];
    cpu->registers.A = memory->memory[cpu->SP++];
    cpu->Z = (cpu->registers.F & (1u << 7)) ? 1 : 0;
    cpu->N = (cpu->registers.F & (1u << 6)) ? 1 : 0;
    cpu->H = (cpu->registers.F & (1u << 5)) ? 1 : 0;
    cpu->C = (cpu->registers.F & (1u << 4)) ? 1 : 0;
    cpu->current_t_cycles += 12;
}

void POP_HL(CPU *cpu, Memory *memory)
{
    cpu->registers.L = memory->memory[cpu->SP++];
    cpu->registers.H = memory->memory[cpu->SP++];
    cpu->current_t_cycles += 12;
}

void ADC_A_r8(CPU *cpu, __uint8_t n8)
{
    __uint16_t result = cpu->registers.A + n8 + cpu->C;
    __uint8_t half_result = (cpu->registers.A & 0x0F) + (n8 & 0x0F) + cpu->C;
    cpu->H = half_result > 0xF;
    cpu->C = result > 0xFF;
    cpu->registers.A = result & 0xFF;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 0;
    cpu->current_t_cycles += 4;
}

void ADC_A_n8(CPU *cpu, Memory *memory)
{
    __uint8_t n8 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    ADC_A_r8(cpu, n8);
    cpu->current_t_cycles += 4;
}

void SBC_A_r8(CPU *cpu, __uint8_t n8)
{
    __uint16_t sub_val = n8 + cpu->C;
    __uint8_t sub_low = (n8 & 0x0F) + cpu->C;
    cpu->H = ((cpu->registers.A & 0x0F) < sub_low);
    cpu->C = cpu->registers.A < sub_val;
    cpu->registers.A -= sub_val;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 1;
    cpu->current_t_cycles += 4;
}

void SBC_A_n8(CPU *cpu, Memory *memory)
{
    __uint8_t n8 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    SBC_A_r8(cpu, n8);
    cpu->current_t_cycles += 4;
}

void ADD_HL_r16(CPU *cpu, __uint16_t r16)
{
    __uint16_t HL = get_HL(cpu);
    __uint16_t result = HL + r16;
    cpu->H = (HL & 0xFFF) + (r16 & 0xFFF) > 0xFFF;
    cpu->C = result < HL;
    HL = result;
    cpu->N = 0;
    store_HL(cpu, HL);
    cpu->current_t_cycles += 8;
}

void ADD_SP_s8(CPU *cpu, Memory *memory)
{
    __uint8_t n8 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    __uint16_t s8 = sign_extend(n8);
    __uint16_t result = cpu->SP + s8;
    cpu->H = (cpu->SP & 0xF) + (s8 & 0xF) > 0xF;
    cpu->C = (cpu->SP & 0xFF) + (s8 & 0xFF) > 0xFF;
    cpu->Z = 0;
    cpu->N = 0;
    cpu->SP = result;
    cpu->current_t_cycles += 16;
}

void ADD_A_n8(CPU *cpu, Memory *memory)
{
    __uint8_t d8 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    cpu->H = (cpu->registers.A & 0xF) + (d8 & 0xF) > 0xF;
    cpu->C = __builtin_add_overflow(cpu->registers.A, d8, &(cpu->C));
    cpu->registers.A += d8;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 0;
    cpu->current_t_cycles += 8;
}

void LD_HL_SP_s8(CPU *cpu, Memory *memory)
{
    __uint16_t HL = get_HL(cpu);
    __uint8_t n8 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    __uint16_t s8 = sign_extend(n8);
    __uint16_t result = cpu->SP + s8;
    cpu->H = (cpu->SP & 0xF) + (s8 & 0xF) > 0xF;
    cpu->C = (cpu->SP & 0xFF) + (s8 & 0xFF) > 0xFF;
    cpu->Z = 0;
    cpu->N = 0;
    store_HL(cpu, result);
    cpu->current_t_cycles += 12;
}

void CP_A_n8(CPU *cpu, Memory *memory)
{
    __uint8_t n8 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    __uint8_t result = cpu->registers.A - n8;
    cpu->Z = result == 0;
    cpu->N = 1;
    cpu->H = (cpu->registers.A & 0xF) < (n8 & 0xF);
    cpu->C = cpu->registers.A < n8;
    cpu->current_t_cycles += 8;
}

void CP_A_r8(CPU *cpu, __uint8_t r8)
{
    __uint8_t result = cpu->registers.A - r8;
    cpu->Z = result == 0;
    cpu->N = 1;
    cpu->H = (cpu->registers.A & 0xF) < (r8 & 0xF);
    cpu->C = cpu->registers.A < r8;
    cpu->current_t_cycles += 4;
}

void JP_CC_n16(CPU *cpu, Memory *memory, __uint8_t cc)
{
    __uint16_t a16 = get_a16(cpu, memory);
    if (!cc)
    {
        cpu->current_t_cycles += 12;
        return;
    }
    cpu->PC = a16;
    cpu->current_t_cycles += 16;
}

void JP_n16(CPU *cpu, Memory *memory)
{
    __uint16_t a16 = get_a16(cpu, memory);
    cpu->PC = a16;
    cpu->current_t_cycles += 16;
}

void JP_HL(CPU *cpu)
{
    cpu->PC = get_HL(cpu);
    cpu->current_t_cycles += 4;
}

void JR_CC_n16(CPU *cpu, Memory *memory, __uint8_t cc)
{
    __uint8_t e8 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    if (!cc)
    {
        cpu->current_t_cycles += 8;
        return;
    }
    cpu->PC += sign_extend(e8);
    cpu->current_t_cycles += 12;
}

void JR_n16(CPU *cpu, Memory *memory)
{
    __uint8_t n8 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    cpu->PC += sign_extend(n8);
    cpu->current_t_cycles += 12;
}

void CALL_CC_n16(CPU *cpu, Memory *memory, __uint8_t cc)
{
    __uint16_t a16 = get_a16(cpu, memory);
    if (!cc)
    {
        cpu->current_t_cycles += 12;
        return;
    }
    store_SP(cpu, memory);
    cpu->PC = a16;
    cpu->current_t_cycles += 24;
}

void CALL_n16(CPU *cpu, Memory *memory)
{
    __uint16_t a16 = get_a16(cpu, memory);
    store_PC(cpu, memory);
    cpu->PC = a16;
    cpu->current_t_cycles += 24;
}

void RST_vec(CPU *cpu, Memory *memory, __uint8_t address)
{
    store_PC(cpu, memory);
    cpu->PC = address;
    cpu->current_t_cycles += 16;
}

void RET_CC(CPU *cpu, Memory *memory, __uint8_t cc)
{
    if (!cc)
    {
        cpu->current_t_cycles += 8;
        return;
    }

    cpu->PC = get_SP(cpu, memory);
    cpu->current_t_cycles + -20;
}

void RET(CPU *cpu, Memory *memory)
{
    __uint8_t v1 = memory->memory[cpu->SP++];
    __uint8_t v2 = memory->memory[cpu->SP++];
    __uint16_t a16 = v1 | (v2 << 8);
    cpu->PC = a16;
    cpu->current_t_cycles += 16;
}

void RETI(CPU *cpu, Memory *memory)
{
    __uint16_t a16 = get_SP(cpu, memory);
    cpu->PC = a16;
    cpu->IME = 1;
    cpu->current_t_cycles += 16;
}

void exec_CB(CPU *cpu, Memory *memory)
{
    cpu->current_t_cycles += 4;
    __uint8_t opcode = get_opcode(memory->memory, cpu);
    cpu->PC++;
    switch (opcode)
    {
    case 0x38:
        __uint8_t C = (cpu->registers.B & 1u);
        cpu->registers.B >>= 1;
        cpu->Z = cpu->registers.B == 0;
        cpu->N = 0;
        cpu->H = 0;
        cpu->C = C;
        cpu->current_t_cycles += 8;
        break;
    case 0x19:
        RR_r8(cpu, &(cpu->registers.C));
        break;
    case 0x1A:
        RR_r8(cpu, &(cpu->registers.D));
        break;
    case 0x1B:
        RR_r8(cpu, &(cpu->registers.E));
        break;

    default:
        printf("invalid CB opcode: %02x\n", opcode);
        printf("PC: %02x\n", cpu->PC);
        exit(1);
        break;
    }
}

void update_IME(CPU *cpu, __uint8_t opcode)
{
    if (opcode != 0xFB && cpu->ime_delay)
    {
        cpu->IME = 1;
        cpu->ime_delay = 0;
    }
}

int handle_interrupts(CPU *cpu, Memory *memory, FILE *file)
{
    if (!cpu->IME || !(memory->memory[0xff0f] & memory->memory[0xffff]))
        return 0;
    cpu->IME = 0;
    print_cpu(cpu, memory, file);
    __uint8_t flags = memory->memory[0xff0f];
    PUSH_PC(cpu, memory);

    if (flags & (1u))
    {
        printf("vblank handle\n");
        memory->memory[0xFF0F] &= ~(1u);
        cpu->PC = VBLANK_ADDR;
        return 1;
    }
    if (flags & (1u << 1))
    {
        printf("lcd handle\n");
        memory->memory[0xFF0F] &= ~(1u << 1);
        cpu->PC = LCD_STAT_ADDR;
        return 1;
    }
    if (flags & (1u << 2))
    {
        printf("timer handle\n");
        memory->memory[0xFF0F] &= ~(1u << 2);
        cpu->PC = TIMER_ADDR;
        return 1;
    }
    if (flags & (1u << 3))
    {
        printf("serial handle\n");
        memory->memory[0xFF0F] &= ~(1u << 3);
        cpu->PC = SERIAL_ADDR;
        return 1;
    }
    if (flags & (1u << 4))
    {
        printf("joypad handle\n");
        memory->memory[0xFF0F] &= ~(1u << 4);
        cpu->PC = JOYPAD_ADDR;
        return 1;
    }

    return 0;
}

void update_timer(CPU *cpu, Memory *memory)
{
    cpu->div_counter += cpu->current_t_cycles;
    memory->memory[0xFF04] = (cpu->div_counter >> 8);

    if (memory->memory[0xFF07] & 0x04)
    {
        __uint16_t freq = (memory->memory[0xFF07] & 0x03) == 0 ? 1024 : (memory->memory[0xFF07] & 0x03) == 1 ? 16
                                                                    : (memory->memory[0xFF07] & 0x03) == 2   ? 64
                                                                                                             : 256;
        cpu->tima_cycles += cpu->current_t_cycles;
        if (cpu->tima_cycles >= freq)
        {
            memory->memory[0xFF05]++;
            cpu->tima_cycles -= freq;
            if (memory->memory[0xFF05] == 0)
            {
                memory->memory[0xFF05] = memory->memory[0xFF06];
                memory->memory[0xFF0F] |= 0x04;
            }
        }
    }
}

int main()
{
    FILE *file = fopen("output.txt", "w");

    if (file == NULL)
    {
        perror("Error opening file");
        return 1;
    }

    const char *filename = "./gb-test-roms-master/cpu_instrs/individual/08-misc instrs.gb";
    Memory memory = {0};
    __uint8_t *buffer = read_file(filename, memory.memory);
    CPU cpu = {0};
    cpu.registers.A = 0x01;
    cpu.registers.F = 0xB0;
    cpu.registers.B = 0x00;
    cpu.registers.C = 0x13;
    cpu.registers.D = 0x00;
    cpu.registers.E = 0xD8;
    cpu.registers.H = 0x01;
    cpu.registers.L = 0x4D;
    cpu.SP = 0xFFFE;
    cpu.PC = 0x100;
    memory.memory[0xFF44] = 0x90;
    cpu.Z = 1;
    cpu.N = 0;
    cpu.H = 1;
    cpu.C = 1;

    print_cpu(&cpu, &memory, file);
    int cnt = 0;
    while (cnt < 1800000)
    {
        cnt++;

        if (cpu.halted)
        {
            __uint8_t t_cycles = 4;
            cpu.current_t_cycles += t_cycles;
            update_timer(&cpu, &memory);

            if (cpu.IME && (memory.memory[0xFF0F] & memory.memory[0xFFFF]))
            {
                cpu.halted = 0;
                int handled = handle_interrupts(&cpu, &memory, file);
            }
            else if (!cpu.IME && memory.memory[0xFF0F])
            {
                cpu.halted = 0;
            }
            continue;
        }

        __uint8_t opcode = get_opcode(buffer, &cpu);

        cpu.PC++;
        switch (opcode)
        {
        case 0x00: // NOP
            NOP(&cpu);
            break;
        case 0xF0: // LDH A, [a8]
            LD_A_a8(&cpu, &memory);
            break;
        case 0xF3: // DI
            DI(&cpu);
            break;
        case 0x44: // LD B, H
            LD_r8_r8(&cpu, &cpu.registers.B, cpu.registers.H);
            break;
        case 0xFE: // CP A, n8
            CP_A_n8(&cpu, &memory);
            break;
        case 0x38: // JR C, e8
            JR_CC_n16(&cpu, &memory, cpu.C == 1);
            break;
        case 0xC3: // JP a16
            JP_n16(&cpu, &memory);
            break;
        case 0xAF: // XOR A, A
            XOR_A_r8(&cpu, cpu.registers.A);
            break;
        case 0xE0: // LDH [a8], A
            LD_a8_A(&cpu, &memory);
            break;
        case 0x20: // JR NZ, e8
            JR_CC_n16(&cpu, &memory, cpu.Z == 0);
            break;
        case 0x21: // LD HL, n16
            LD_HL_n16(&cpu, &memory);
            break;
        case 0x11: // LD DE, n16
            LD_DE_n16(&cpu, &memory);
            break;
        case 0x01: // LD BC, n16
            LD_BC_n16(&cpu, &memory);
            break;
        case 0x1A: // LD A, [DE]
            LD_A_DE(&cpu, &memory);
            break;
        case 0x22: // LD [HL+], A
            LD_HLI_A(&cpu, &memory);
            break;
        case 0x13: // INC DE
            INC_DE(&cpu);
            break;
        case 0x0B: // DEC BC
            DEC_BC(&cpu);
            break;
        case 0x78:
            LD_r8_r8(&cpu, &cpu.registers.A, cpu.registers.B);
            break;
        case 0xB1: // OR A, C
            OR_A(&cpu, cpu.registers.C);
            break;
        case 0xA7: // AND A, A
            AND_A_r8(&cpu, cpu.registers.A);
            break;
        case 0x3E: // LD A, n8
            LD_r8_n8(&cpu, &memory, &cpu.registers.A);
            break;
        case 0x47:
            LD_r8_r8(&cpu, &cpu.registers.B, cpu.registers.A);
            break;
        case 0x0E: // LD C, n8
            LD_r8_n8(&cpu, &memory, &cpu.registers.C);
            break;
        case 0x2A: // LD A, [HL+]
            LD_A_HLI(&cpu, &memory);
            break;
        case 0x12: // LD [DE], A
            LD_DE_A(&cpu, &memory);
            break;
        case 0x1C: // INC E
            INC_r8(&cpu, &cpu.registers.E);
            break;
        case 0x14: // INC D
            INC_r8(&cpu, &cpu.registers.D);
            break;
        case 0x0D: // DEC C
            DEC_r8(&cpu, &cpu.registers.C);
            break;
        case 0x31: // LD SP, n16
            LD_SP_n16(&cpu, &memory);
            break;
        case 0xEA: // LD [a16], A
            LD_a16_A(&cpu, &memory);
            break;
        case 0xCD: // CALL a16
            CALL_n16(&cpu, &memory);
            break;
        case 0x7D: // LD A, L
            LD_r8_r8(&cpu, &cpu.registers.A, cpu.registers.L);
            break;
        case 0x7C: // LD A, H
            LD_r8_r8(&cpu, &cpu.registers.A, cpu.registers.H);
            break;
        case 0xC9: // RET
            RET(&cpu, &memory);
            break;
        case 0xE5: // PUSH HL
            PUSH_HL(&cpu, &memory);
            break;
        case 0xE1: // POP HL
            POP_HL(&cpu, &memory);
            break;
        case 0xF5: // PUSH AF
            PUSH_AF(&cpu, &memory);
            break;
        case 0x23: // INC HL
            INC_HL(&cpu, &memory);
            break;
        case 0xF1: // POP AF
            POP_AF(&cpu, &memory);
            break;
        case 0x18: // JR e8
            JR_n16(&cpu, &memory);
            break;
        case 0xC5: // PUSH BC
            PUSH_BC(&cpu, &memory);
            break;
        case 0x03: // INC BC
            INC_BC(&cpu, &memory);
            break;
        case 0x28: // JR Z, e8
            JR_CC_n16(&cpu, &memory, cpu.Z == 1);
            break;
        case 0xC1: // POP BC
            POP_BC(&cpu, &memory);
            break;
        case 0xFA: // LD A, [a16]
            LD_A_a16(&cpu, &memory);
            break;
        case 0xE6: // AND A, n8
            AND_A_n8(&cpu, &memory);
            break;
        case 0xC4: // CALL NZ, a16
            CALL_CC_n16(&cpu, &memory, cpu.Z == 0);
            break;
        case 0x06: // LD B, n8
            LD_r8_n8(&cpu, &memory, &cpu.registers.B);
            break;
        case 0x77: // LD [HL], A
            LD_HL_r8(&cpu, &memory, cpu.registers.A);
            break;
        case 0x2C: // INC L
            INC_r8(&cpu, &cpu.registers.L);
            break;
        case 0x24: // INC H
            INC_r8(&cpu, &cpu.registers.H);
            break;
        case 0x05: // DEC B
            DEC_r8(&cpu, &cpu.registers.B);
            break;
        case 0xA9: // XOR A, C
            XOR_A_r8(&cpu, cpu.registers.C);
            break;
        case 0xC6: // ADD A, n8
            ADD_A_n8(&cpu, &memory);
            break;
        case 0x32: // LD [HL-], A
            LD_HLD_A(&cpu, &memory);
            break;
        case 0xD6: // SUB A, n8
            SUB_A_n8(&cpu, &memory);
            break;
        case 0xB7: // OR A, A
            OR_A(&cpu, cpu.registers.A);
            break;
        case 0xD5: // PUSH DE
            PUSH_DE(&cpu, &memory);
            break;
        case 0x46: // LD B, [HL]
            LD_r8_HL(&cpu, &memory, &cpu.registers.B);
            break;
        case 0x2D: // DEC L
            DEC_r8(&cpu, &cpu.registers.L);
            break;
        case 0x4E: // LD C, [HL]
            LD_r8_HL(&cpu, &memory, &cpu.registers.C);
            break;
        case 0x56: // LD D, [HL]
            LD_r8_HL(&cpu, &memory, &cpu.registers.D);
            break;
        case 0xAE: // XOR A, [HL]
            XOR_A_HL(&cpu, &memory);
            break;
        case 0x26: // LD H, n8
            LD_r8_n8(&cpu, &memory, &cpu.registers.H);
            break;
        case 0xCB: // PREFIX
            exec_CB(&cpu, &memory);
            break;
        case 0x1F: // RRA
            RRA(&cpu, &cpu.registers.A);
            break;
        case 0x30: // JR NC, e8
            JR_CC_n16(&cpu, &memory, cpu.C == 0);
            break;
        case 0x5F: // LD E, A
            LD_r8_r8(&cpu, &cpu.registers.E, cpu.registers.A);
            break;
        case 0xEE: // XOR A, n8
            XOR_A_n8(&cpu, &memory);
            break;
        case 0x79: // LD A, C
            LD_r8_r8(&cpu, &cpu.registers.A, cpu.registers.C);
            break;
        case 0x4F: // LD C, A
            LD_r8_r8(&cpu, &cpu.registers.C, cpu.registers.A);
            break;
        case 0x7A: // LD A, D
            LD_r8_r8(&cpu, &cpu.registers.A, cpu.registers.D);
            break;
        case 0x57: // LD D, A
            LD_r8_r8(&cpu, &cpu.registers.D, cpu.registers.A);
            break;
        case 0x7B: // LD A, E
            LD_r8_r8(&cpu, &cpu.registers.A, cpu.registers.E);
            break;
        case 0x25: // DEC H
            DEC_r8(&cpu, &(cpu.registers.H));
            break;
        case 0x72: // LD [HL], D
            LD_HL_r8(&cpu, &memory, cpu.registers.D);
            break;
        case 0x71: // LD [HL], C
            LD_HL_r8(&cpu, &memory, cpu.registers.C);
            break;
        case 0x70: // LD [HL], B
            LD_HL_r8(&cpu, &memory, cpu.registers.B);
            break;
        case 0xD1: // POP DE
            POP_DE(&cpu, &memory);
            break;
        case 0xCE: // ADC A, n8
            ADC_A_n8(&cpu, &memory);
            break;
        case 0xD0: // RET NC
            RET_CC(&cpu, &memory, cpu.C == 0);
            break;
        case 0xC8: // RET Z
            RET_CC(&cpu, &memory, cpu.Z == 1);
            break;
        case 0x3D: // DEC A
            DEC_r8(&cpu, &(cpu.registers.A));
            break;
        case 0xB6: // OR A, [HL]
            OR_A_HL(&cpu, &memory);
            break;
        case 0x35: // DEC [HL]
            DEC_HL_a16(&cpu, &memory);
            break;
        case 0x6E: // LD L, [HL]
            LD_r8_HL(&cpu, &memory, &cpu.registers.L);
            break;
        case 0x6F: // LD L, A
            LD_r8_r8(&cpu, &cpu.registers.L, cpu.registers.A);
            break;
        case 0x29: // ADD HL, HL
            ADD_HL_r16(&cpu, get_HL(&cpu));
            break;
        case 0x1D: // DEC E
            DEC_r8(&cpu, &cpu.registers.E);
            break;
        case 0xE9: // JP HL
            JP_HL(&cpu);
            break;
        case 0x2E: // LD L, n8
            LD_r8_n8(&cpu, &memory, &cpu.registers.L);
            break;
        case 0x5D: // LD E, L
            LD_r8_r8(&cpu, &cpu.registers.E, cpu.registers.L);
            break;
        case 0x1B: // DEC DE
            DEC_DE_r16(&cpu);
            break;
        case 0x73: // LD [HL], E
            LD_HL_r8(&cpu, &memory, cpu.registers.E);
            break;
        case 0x5E: // LD E, [HL]
            LD_r8_HL(&cpu, &memory, &cpu.registers.E);
            break;
        case 0x08: // LD [a16], SP
            LD_a16_SP(&cpu, &memory);
            break;
        case 0x66: // LD H, [HL]
            LD_r8_HL(&cpu, &memory, &cpu.registers.H);
            break;
        case 0xF9: // LD SP, HL
            LD_SP_HL(&cpu);
            break;
        case 0x62: // LD H, D
            LD_r8_r8(&cpu, &cpu.registers.H, cpu.registers.D);
            break;
        case 0x6B: // LD L, E
            LD_r8_r8(&cpu, &cpu.registers.L, cpu.registers.E);
            break;
        case 0x33: // INC SP
            INC_SP(&cpu);
            break;
        case 0xAD: // XOR A, L
            XOR_A_r8(&cpu, cpu.registers.L);
            break;
        case 0x7E: // LD A, [HL]
            LD_r8_HL(&cpu, &memory, &cpu.registers.A);
            break;
        case 0x67: // LD H, A
            LD_r8_r8(&cpu, &cpu.registers.H, cpu.registers.A);
            break;
        case 0xB0: // OR A, B
            OR_A(&cpu, cpu.registers.B);
            break;
        case 0x3B: // DEC SP
            DEC_SP(&cpu);
            break;
        case 0x39: // ADD HL, SP
            ADD_HL_r16(&cpu, cpu.SP);
            break;
        case 0xE8: // ADD SP, e8
            ADD_SP_s8(&cpu, &memory);
            break;
        case 0xF8: // LD HL, SP + e8
            LD_HL_SP_s8(&cpu, &memory);
            break;
        case 0x3C: // INC A
            INC_r8(&cpu, &cpu.registers.A);
            break;
        case 0xC2: // JP NZ, a16
            JP_CC_n16(&cpu, &memory, cpu.Z == 0);
            break;
        case 0xBB: // CP A, E
            CP_A_r8(&cpu, cpu.registers.E);
            break;
        case 0x04: // INC B
            INC_r8(&cpu, &cpu.registers.B);
            break;
        case 0x0C: // INC C
            INC_r8(&cpu, &cpu.registers.C);
            break;
        case 0x27: // DAA
            DAA(&cpu);
            break;
        case 0xBA: // CP A, D
            CP_A_r8(&cpu, cpu.registers.D);
            break;
        case 0xB9: // CP A, C
            CP_A_r8(&cpu, cpu.registers.C);
            break;
        case 0xB8: // CP A, B
            CP_A_r8(&cpu, cpu.registers.B);
            break;
        case 0xFB: // EI
            EI(&cpu);
            break;
        case 0xCA: // JP Z, a16
            JP_CC_n16(&cpu, &memory, cpu.Z == 1);
            break;
        case 0x76: // HALT
            HALT(&cpu);
            break;
        case 0xD8: // RET C
            RET_CC(&cpu, &memory, cpu.C == 1);
            break;
        case 0x36: // LD [HL], n8
            LD_HL_n8(&cpu, &memory);
            break;
        case 0x16: // LD D, n8
            LD_r8_n8(&cpu, &memory, &cpu.registers.D);
            break;
        case 0x1E: // LD E, n8
            LD_r8_n8(&cpu, &memory, &cpu.registers.E);
            break;
        case 0xF6: // OR A, n8
            OR_A_n8(&cpu, &memory);
            break;
        case 0xDE: // SBC A, n8
            SBC_A_n8(&cpu, &memory);
            break;
        case 0x2B: // DEC HL
            DEC_HL_r16(&cpu, &memory);
            break;
        case 0x09: // ADD HL, BC
            ADD_HL_r16(&cpu, get_BC(&cpu));
            break;
        case 0x19: // ADD HL, DE
            ADD_HL_r16(&cpu, get_DE(&cpu));
            break;
        case 0x40: // LD B, B
            LD_r8_r8(&cpu, &cpu.registers.B, cpu.registers.B);
            break;
        case 0x41: // LD B, C
            LD_r8_r8(&cpu, &cpu.registers.B, cpu.registers.C);
            break;
        case 0x42: // LD B, D
            LD_r8_r8(&cpu, &cpu.registers.B, cpu.registers.D);
            break;
        case 0x43: // LD B, E
            LD_r8_r8(&cpu, &cpu.registers.B, cpu.registers.E);
            break;
        case 0x45: // LD B, L
            LD_r8_r8(&cpu, &cpu.registers.B, cpu.registers.L);
            break;
        case 0x48: // LD C, B
            LD_r8_r8(&cpu, &cpu.registers.C, cpu.registers.B);
            break;
        case 0x49: // LD C, C
            LD_r8_r8(&cpu, &cpu.registers.C, cpu.registers.C);
            break;
        case 0x4A: // LD C, D
            LD_r8_r8(&cpu, &cpu.registers.C, cpu.registers.D);
            break;
        case 0x4B: // LD C, E
            LD_r8_r8(&cpu, &cpu.registers.C, cpu.registers.E);
            break;
        case 0x4C: // LD C, H
            LD_r8_r8(&cpu, &cpu.registers.C, cpu.registers.H);
            break;
        case 0x4D: // LD C, L
            LD_r8_r8(&cpu, &cpu.registers.C, cpu.registers.L);
            break;
        case 0x50: // LD D, B
            LD_r8_r8(&cpu, &cpu.registers.D, cpu.registers.B);
            break;
        case 0x51: // LD D, C
            LD_r8_r8(&cpu, &cpu.registers.D, cpu.registers.C);
            break;
        case 0x52: // LD D, D
            LD_r8_r8(&cpu, &cpu.registers.D, cpu.registers.D);
            break;
        case 0x53: // LD D, E
            LD_r8_r8(&cpu, &cpu.registers.D, cpu.registers.E);
            break;
        case 0x54: // LD D, H
            LD_r8_r8(&cpu, &cpu.registers.D, cpu.registers.H);
            break;
        case 0x55: // LD D, L
            LD_r8_r8(&cpu, &cpu.registers.D, cpu.registers.L);
            break;
        case 0x58: // LD E, B
            LD_r8_r8(&cpu, &cpu.registers.E, cpu.registers.B);
            break;
        case 0x59: // LD E, C
            LD_r8_r8(&cpu, &cpu.registers.E, cpu.registers.C);
            break;
        case 0x5A: // LD E, D
            LD_r8_r8(&cpu, &cpu.registers.E, cpu.registers.D);
            break;
        case 0x5B: // LD E, E
            LD_r8_r8(&cpu, &cpu.registers.E, cpu.registers.E);
            break;
        case 0x5C: // LD E, H
            LD_r8_r8(&cpu, &cpu.registers.E, cpu.registers.H);
            break;
        case 0x60: // LD H, B
            LD_r8_r8(&cpu, &cpu.registers.H, cpu.registers.B);
            break;
        case 0x61: // LD H, C
            LD_r8_r8(&cpu, &cpu.registers.H, cpu.registers.C);
            break;
        case 0x63: // LD H, E
            LD_r8_r8(&cpu, &cpu.registers.H, cpu.registers.E);
            break;
        case 0x64: // LD H, H
            LD_r8_r8(&cpu, &cpu.registers.H, cpu.registers.H);
            break;
        case 0x65: // LD H, L
            LD_r8_r8(&cpu, &cpu.registers.H, cpu.registers.L);
            break;
        case 0x68: // LD L, B
            LD_r8_r8(&cpu, &cpu.registers.L, cpu.registers.B);
            break;
        case 0x69: // LD L, C
            LD_r8_r8(&cpu, &cpu.registers.L, cpu.registers.C);
            break;
        case 0x6A: // LD L, D
            LD_r8_r8(&cpu, &cpu.registers.L, cpu.registers.D);
            break;
        case 0x6C: // LD L, H
            LD_r8_r8(&cpu, &cpu.registers.L, cpu.registers.H);
            break;
        case 0x6D: // LD L, L
            LD_r8_r8(&cpu, &cpu.registers.L, cpu.registers.L);
            break;
        case 0x74: // LD [HL], H
            LD_HL_r8(&cpu, &memory, cpu.registers.H);
            break;
        case 0x75: // LD [HL], L
            LD_HL_r8(&cpu, &memory, cpu.registers.L);
            break;
        case 0x7F: // LD [HL], L
            LD_r8_r8(&cpu, &cpu.registers.A, cpu.registers.A);
            break;
        case 0xD2: // JP NC, a16
            JP_CC_n16(&cpu, &memory, cpu.C == 0);
            break;
        case 0xDA: // JP C, a16
            JP_CC_n16(&cpu, &memory, cpu.C == 1);
            break;
        case 0xCC: // CALL Z, a16
            CALL_CC_n16(&cpu, &memory, cpu.Z == 1);
            break;
        case 0xD4: // CALL NC, a16
            CALL_CC_n16(&cpu, &memory, cpu.C == 0);
            break;
        case 0xDC: // CALL C, a16
            CALL_CC_n16(&cpu, &memory, cpu.C == 1);
            break;
        case 0xC0: // RET NZ
            RET_CC(&cpu, &memory, cpu.Z == 0);
            break;
        case 0xD9: // RETI
            RETI(&cpu, &memory);
            break;
        case 0xC7: // RST $00
            RST_vec(&cpu, &memory, 0x0);
            break;
        case 0xCF: // RST $08
            RST_vec(&cpu, &memory, 0x08);
            break;
        case 0xD7: // RST $10
            RST_vec(&cpu, &memory, 0x10);
            break;
        case 0xDF: // RST $18
            RST_vec(&cpu, &memory, 0x18);
            break;
        case 0xE7: // RST $20
            RST_vec(&cpu, &memory, 0x20);
            break;
        case 0xEF: // RST $28
            RST_vec(&cpu, &memory, 0x28);
            break;
        case 0xF7: // RST $30
            RST_vec(&cpu, &memory, 0x30);
            break;
        case 0xFF: // RST $38
            RST_vec(&cpu, &memory, 0x38);
            break;
        case 0xF2: // LDH A, [C]
            LD_A_C(&cpu, &memory);
            break;
        case 0xE2: // LDH [C], A
            LD_C_A(&cpu, &memory);
            break;
        default:
            printf("invalid opcode: %02x\n", opcode);
            printf("PC: %02x\n", cpu.PC);
            exit(1);
            break;
        }
        update_timer(&cpu, &memory);

        update_IME(&cpu, opcode);
        int handled = handle_interrupts(&cpu, &memory, file);
        if (!handled)
            print_cpu(&cpu, &memory, file);
    }

    free(buffer);
    fclose(file);

    return 0;
}