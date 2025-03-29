#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
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
    __uint32_t cycles;
    __uint8_t *vram;
    __uint8_t frame[144 * 160];
} PPU;

typedef struct
{
    __uint16_t div_cycles;
    __uint16_t tima_cycles;
    // __uint8_t current_t_cycles;
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
    PPU ppu;
    __uint8_t memory[0xFFFF];
} CPU;

void update_timer(CPU *cpu, __uint8_t t_cycles);

__uint8_t read_opcode(CPU *cpu)
{
    return cpu->memory[cpu->PC++];
}

void write_memory(CPU *cpu, uint16_t address, uint8_t value)
{
    if (address == 0xFF04)
    {
        cpu->div_cycles = 0;
        cpu->memory[0xFF04] = 0;
    }
    else
    {
        cpu->memory[address] = value;
    }
    if (address == 0xFF02 && value == 0x81)
    {
        printf("%c", cpu->memory[0xFF01]);
        cpu->memory[0xFF02] = 0;
    }
}

__uint8_t get_F(CPU *cpu)
{
    return (cpu->Z << 7) | (cpu->N << 6) | (cpu->H << 5) | (cpu->C << 4);
}

void print_cpu(CPU *cpu, FILE *file)
{
    fprintf(file, "A:%02X F:%02X B:%02X C:%02X D:%02X E:%02X H:%02X L:%02X SP:%04X PC:%04X PCMEM:%02X,%02X,%02X,%02X\n",
            cpu->registers.A, get_F(cpu), cpu->registers.B, cpu->registers.C, cpu->registers.D, cpu->registers.E, cpu->registers.H,
            cpu->registers.L, cpu->SP, cpu->PC, cpu->memory[cpu->PC], cpu->memory[cpu->PC + 1], cpu->memory[cpu->PC + 2], cpu->memory[cpu->PC + 3]);
}

__int16_t sign_extend(__uint8_t value)
{
    return (__int16_t)((__int8_t)value);
}

__uint8_t check_underflow_sub(__uint8_t a, __uint8_t b)
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

__int16_t get_SP(CPU *cpu)
{
    __uint8_t v1 = cpu->memory[cpu->SP++];
    update_timer(cpu, 4);
    __uint16_t v2 = cpu->memory[cpu->SP++];
    update_timer(cpu, 4);

    return (v2 << 8) | v1;
}

__int16_t get_a16(CPU *cpu)
{
    __uint8_t v1 = cpu->memory[cpu->PC++];
    update_timer(cpu, 4);
    __uint8_t v2 = cpu->memory[cpu->PC++];
    update_timer(cpu, 4);

    return (v2 << 8) | v1;
}

void PUSH_PC(CPU *cpu)
{
    write_memory(cpu, --cpu->SP, (cpu->PC & 0xFF00) >> 8);
    update_timer(cpu, 4);
    write_memory(cpu, --cpu->SP, cpu->PC & 0xFF);
    update_timer(cpu, 4);
}

__uint8_t PUSH_DE(CPU *cpu)
{
    write_memory(cpu, --cpu->SP, cpu->registers.D);
    update_timer(cpu, 4);
    write_memory(cpu, --cpu->SP, cpu->registers.E);
    update_timer(cpu, 8);
    return 16;
}

__uint8_t PUSH_BC(CPU *cpu)
{
    write_memory(cpu, --cpu->SP, cpu->registers.B);
    update_timer(cpu, 4);
    write_memory(cpu, --cpu->SP, cpu->registers.C);
    update_timer(cpu, 8);
    return 16;
}

__uint8_t PUSH_AF(CPU *cpu)
{
    write_memory(cpu, --cpu->SP, cpu->registers.A);
    update_timer(cpu, 4);
    write_memory(cpu, --cpu->SP, get_F(cpu));
    update_timer(cpu, 8);
    return 16;
}

__uint8_t PUSH_HL(CPU *cpu)
{
    write_memory(cpu, --cpu->SP, cpu->registers.H);
    update_timer(cpu, 4);
    write_memory(cpu, --cpu->SP, cpu->registers.L);
    update_timer(cpu, 8);
    return 16;
}

__uint8_t RR_r8(CPU *cpu, __uint8_t *r8)
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
    return 8;
}

__uint8_t RR_HL(CPU *cpu)
{
    __uint16_t HL = get_HL(cpu);
    __uint8_t value = cpu->memory[HL];
    update_timer(cpu, 4);
    __uint8_t C = value & 1u;

    value >>= 1;
    value |= (cpu->C << 7);
    write_memory(cpu, HL, value);
    update_timer(cpu, 4);
    cpu->Z = value == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = C;
    return 16;
}

__uint8_t SWAP_r8(CPU *cpu, __uint8_t *r8)
{
    __uint8_t value = *r8;
    __uint8_t upper_bits = value & 0xF0;
    __uint8_t lower_bits = value & 0x0F;

    *r8 = (upper_bits >> 4) | (lower_bits << 4);
    cpu->Z = *r8 == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = 0;
    return 8;
}

__uint8_t SWAP_HL(CPU *cpu)
{
    __uint16_t HL = get_HL(cpu);
    __uint8_t value = cpu->memory[HL];
    update_timer(cpu, 4);
    __uint8_t upper_bits = value & 0xF0;
    __uint8_t lower_bits = value & 0x0F;

    write_memory(cpu, HL, (upper_bits >> 4) | (lower_bits << 4));
    update_timer(cpu, 4);
    cpu->Z = cpu->memory[HL] == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = 0;
    return 16;
}

__uint8_t SRA_r8(CPU *cpu, __uint8_t *r8)
{
    __uint8_t val = *r8;
    __uint8_t C = val & 1u;
    __uint8_t sign = val & (1u << 7);

    val >>= 1;
    val |= sign;
    *r8 = val;
    cpu->Z = val == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = C;
    return 8;
}

__uint8_t SRA_HL(CPU *cpu)
{
    __uint16_t HL = get_HL(cpu);
    __uint8_t value = cpu->memory[HL];
    update_timer(cpu, 4);
    __uint8_t C = value & 1u;
    __uint8_t sign = value & (1u << 7);

    value >>= 1;
    value |= sign;
    write_memory(cpu, HL, value);
    update_timer(cpu, 4);
    cpu->Z = value == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = C;
    return 16;
}

__uint8_t SRL_r8(CPU *cpu, __uint8_t *r8)
{
    __uint8_t val = *r8;
    __uint8_t C = val & 1u;

    val >>= 1;
    *r8 = val;
    cpu->Z = val == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = C;
    return 8;
}

__uint8_t SRL_HL(CPU *cpu)
{
    __uint16_t HL = get_HL(cpu);
    __uint8_t value = cpu->memory[HL];
    update_timer(cpu, 4);
    __uint8_t C = value & 1u;

    value >>= 1;
    write_memory(cpu, HL, value);
    update_timer(cpu, 4);
    cpu->Z = value == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = C;
    return 16;
}

__uint8_t BIT_u3_r8(CPU *cpu, __uint8_t u3, __uint8_t *r8)
{
    __uint8_t mask = 1 << u3;

    cpu->Z = (*r8 & mask) ? 0 : 1;
    cpu->N = 0;
    cpu->H = 1;
    return 8;
}

__uint8_t BIT_u3_HL(CPU *cpu, __uint8_t u3)
{
    __uint16_t HL = get_HL(cpu);
    __uint8_t value = cpu->memory[HL];
    update_timer(cpu, 4);
    __uint8_t mask = 1 << u3;

    cpu->Z = (value & mask) ? 0 : 1;
    cpu->N = 0;
    cpu->H = 1;
    return 12;
}

__uint8_t RES_u3_r8(CPU *cpu, __uint8_t u3, __uint8_t *r8)
{
    __uint8_t mask = ~(1 << u3);

    *r8 &= mask;
    return 8;
}

__uint8_t RES_u3_HL(CPU *cpu, __uint8_t u3)
{
    __uint16_t HL = get_HL(cpu);
    __uint8_t value = cpu->memory[HL];
    update_timer(cpu, 4);
    __uint8_t mask = ~(1 << u3);

    value &= mask;
    write_memory(cpu, HL, value);
    update_timer(cpu, 4);
    return 16;
}

__uint8_t SET_u3_r8(CPU *cpu, __uint8_t u3, __uint8_t *r8)
{
    __uint8_t mask = 1 << u3;

    *r8 |= mask;
    return 8;
}

__uint8_t SET_u3_HL(CPU *cpu, __uint8_t u3)
{
    __uint16_t HL = get_HL(cpu);
    __uint8_t value = cpu->memory[HL] | (1 << u3);
    update_timer(cpu, 4);

    write_memory(cpu, HL, value);
    update_timer(cpu, 4);
    return 16;
}

__uint8_t RLC_r8(CPU *cpu, __uint8_t *r8)
{
    __uint8_t value = *r8;
    __uint8_t C = (value & (1u << 7)) ? 1 : 0;

    value <<= 1;
    value |= C;
    *r8 = value;
    cpu->Z = value == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = C;
    return 8;
}

__uint8_t RLC_HL_r8(CPU *cpu)
{
    __uint16_t HL = get_HL(cpu);
    __uint8_t value = cpu->memory[HL];
    update_timer(cpu, 4);
    __uint8_t C = (value & (1u << 7)) ? 1 : 0;

    value <<= 1;
    value |= C;
    write_memory(cpu, HL, value);
    update_timer(cpu, 4);
    cpu->Z = value == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = C;
    return 16;
}

__uint8_t RL_r8(CPU *cpu, __uint8_t *r8)
{
    __uint8_t value = *r8;
    __uint8_t C = (value & (1u << 7)) ? 1 : 0;

    value <<= 1;
    value |= cpu->C;
    *r8 = value;
    cpu->Z = value == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = C;
    return 8;
}

__uint8_t RL_HL(CPU *cpu)
{
    __uint16_t HL = get_HL(cpu);
    __uint8_t value = cpu->memory[HL];
    update_timer(cpu, 4);
    __uint8_t C = (value & (1u << 7)) ? 1 : 0;

    value <<= 1;
    value |= cpu->C;
    write_memory(cpu, HL, value);
    update_timer(cpu, 4);
    cpu->Z = value == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = C;
    return 16;
}

__uint8_t SLA_r8(CPU *cpu, __uint8_t *r8)
{
    __uint8_t value = *r8;
    __uint8_t C = (value & (1u << 7)) ? 1 : 0;

    value <<= 1;
    *r8 = value;
    cpu->Z = value == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = C;
    return 8;
}

__uint8_t SLA_HL(CPU *cpu)
{
    __uint16_t HL = get_HL(cpu);
    __uint8_t value = cpu->memory[HL];
    update_timer(cpu, 4);
    __uint8_t C = (value & (1u << 7)) ? 1 : 0;

    value <<= 1;
    write_memory(cpu, HL, value);
    update_timer(cpu, 4);
    cpu->Z = value == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = C;
    return 16;
}

__uint8_t RRC_r8(CPU *cpu, __uint8_t *r8)
{
    __uint8_t value = *r8;
    __uint8_t C = value & 1u;

    value >>= 1;
    value |= (C << 7);
    *r8 = value;
    cpu->Z = value == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = C;
    return 8;
}

__uint8_t RRC_HL(CPU *cpu)
{
    __uint16_t HL = get_HL(cpu);
    __uint8_t value = cpu->memory[HL];
    update_timer(cpu, 4);
    __uint8_t C = value & 1u;

    value >>= 1;
    value |= (C << 7);
    write_memory(cpu, HL, value);
    update_timer(cpu, 4);
    cpu->Z = value == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = C;
    return 16;
}

__uint8_t RRA(CPU *cpu, __uint8_t *r8)
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
    return 4;
}

__uint8_t RLCA(CPU *cpu)
{
    __uint8_t C = (cpu->registers.A & (1u << 7)) ? 1 : 0;

    cpu->registers.A <<= 1;
    cpu->registers.A |= C;
    cpu->Z = 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = C;
    return 4;
}

__uint8_t RLA(CPU *cpu)
{
    __uint8_t C = (cpu->registers.A & (1u << 7)) ? 1 : 0;

    cpu->registers.A <<= 1;
    cpu->registers.A |= cpu->C;
    cpu->Z = 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = C;
    return 4;
}

__uint8_t RRCA(CPU *cpu)
{
    __uint8_t C = cpu->registers.A & 1u;

    cpu->registers.A >>= 1;
    cpu->registers.A |= (C << 7);
    cpu->Z = 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = C;
    return 4;
}

__uint8_t DEC_r8(CPU *cpu, __uint8_t *r8)
{
    __uint8_t val = *r8;

    cpu->H = (val & 0xF) == 0;
    val -= 1;
    cpu->Z = val == 0;
    cpu->N = 1;
    *r8 = val;
    return 4;
}

__uint8_t DEC_SP(CPU *cpu)
{
    cpu->SP--;
    update_timer(cpu, 4);
    return 8;
}

__uint8_t INC_SP(CPU *cpu)
{
    cpu->SP++;
    update_timer(cpu, 4);
    return 8;
}

__uint8_t INC_BC(CPU *cpu)
{
    __uint16_t BC = get_BC(cpu);

    BC++;
    store_BC(cpu, BC);
    update_timer(cpu, 4);
    return 8;
}

__uint8_t INC_DE(CPU *cpu)
{
    __uint16_t DE = get_DE(cpu);

    DE++;
    store_DE(cpu, DE);
    update_timer(cpu, 4);
    return 8;
}

__uint8_t INC_r8(CPU *cpu, __uint8_t *r8)
{
    __uint8_t val = *r8;

    cpu->H = (val & 0x0F) + 1 > 0x0F;
    *r8 = ++val;
    cpu->Z = val == 0;
    cpu->N = 0;
    return 4;
}

__uint8_t INC_aHL(CPU *cpu)
{
    __uint16_t HL = get_HL(cpu);
    __uint8_t value = cpu->memory[HL];
    update_timer(cpu, 4);
    cpu->H = (value & 0x0F) + 1 > 0x0F;
    value += 1;
    write_memory(cpu, HL, value);
    update_timer(cpu, 4);
    cpu->Z = value == 0;
    cpu->N = 0;
    return 12;
}

__uint8_t SUB_A_n8(CPU *cpu)
{
    __uint8_t n8 = read_opcode(cpu);
    update_timer(cpu, 4);
    cpu->H = (cpu->registers.A & 0x0F) < (n8 & 0x0F);
    cpu->C = cpu->registers.A < n8;
    cpu->registers.A -= n8;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 1;
    return 8;
}

__uint8_t SUB_A_HL(CPU *cpu)
{
    __uint16_t HL = get_HL(cpu);
    __uint8_t value = cpu->memory[HL];
    update_timer(cpu, 4);

    cpu->H = (cpu->registers.A & 0x0F) < (value & 0x0F);
    cpu->C = cpu->registers.A < value;
    cpu->registers.A -= value;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 1;
    return 8;
}

__uint8_t SUB_A_r8(CPU *cpu, __uint8_t value)
{
    cpu->H = (cpu->registers.A & 0x0F) < (value & 0x0F);
    cpu->C = cpu->registers.A < value;
    cpu->registers.A -= value;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 1;
    return 4;
}

__uint8_t INC_HL(CPU *cpu)
{
    __uint16_t HL = get_HL(cpu);

    HL++;
    store_HL(cpu, HL);
    update_timer(cpu, 4);
    return 8;
}

__uint8_t LD_HLD_A(CPU *cpu)
{
    __uint16_t HL = get_HL(cpu);

    write_memory(cpu, HL, cpu->registers.A);
    update_timer(cpu, 4);
    HL--;
    store_HL(cpu, HL);
    return 8;
}

__uint8_t LD_HLI_A(CPU *cpu)
{
    __uint16_t HL = get_HL(cpu);

    write_memory(cpu, HL, cpu->registers.A);
    update_timer(cpu, 4);
    HL++;
    store_HL(cpu, HL);
    return 8;
}

__uint8_t LD_A_r16(CPU *cpu, __uint16_t address)
{
    __uint8_t value = cpu->memory[address];
    update_timer(cpu, 4);
    cpu->registers.A = value;
    return 8;
}

__uint8_t LD_r16_A(CPU *cpu, __uint16_t address)
{
    write_memory(cpu, address, cpu->registers.A);
    update_timer(cpu, 4);
    return 8;
}

__uint8_t LD_A_HLI(CPU *cpu)
{
    __uint16_t HL = get_HL(cpu);
    __uint8_t value = cpu->memory[HL];
    update_timer(cpu, 4);
    cpu->registers.A = value;
    HL++;
    store_HL(cpu, HL);
    return 8;
}

__uint8_t LD_A_HLD(CPU *cpu)
{
    __uint16_t HL = get_HL(cpu);
    __uint8_t value = cpu->memory[HL];
    update_timer(cpu, 4);
    cpu->registers.A = value;
    HL--;
    store_HL(cpu, HL);
    return 8;
}

__uint8_t DEC_HL_a16(CPU *cpu)
{
    __uint16_t HL = get_HL(cpu);
    __uint8_t value = cpu->memory[HL];
    update_timer(cpu, 4);

    cpu->H = (value & 0xF) == 0;
    value -= 1;
    cpu->Z = value == 0;
    cpu->N = 1;
    write_memory(cpu, HL, value);
    update_timer(cpu, 4);
    return 12;
}

__uint8_t DEC_HL_r16(CPU *cpu)
{
    __uint16_t HL = get_HL(cpu);

    HL--;
    store_HL(cpu, HL);
    update_timer(cpu, 4);
    return 8;
}

__uint8_t DEC_BC(CPU *cpu)
{
    __uint16_t BC = get_BC(cpu);

    BC--;
    store_BC(cpu, BC);
    update_timer(cpu, 4);
    return 8;
}

__uint8_t DEC_DE_r16(CPU *cpu)
{
    __uint16_t DE = get_DE(cpu);

    DE--;
    store_DE(cpu, DE);
    update_timer(cpu, 4);

    return 8;
}

__uint8_t DAA(CPU *cpu)
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
    return 4;
}

__uint8_t HALT(CPU *cpu)
{
    cpu->halted = 1;
}

__uint8_t EI(CPU *cpu)
{
    cpu->ime_delay = 1;
    return 4;
}

__uint8_t DI(CPU *cpu)
{
    cpu->IME = 0;
    cpu->ime_delay = 0;
    return 4;
}

__uint8_t NOP(CPU *cpu)
{
    return 4;
}

__uint8_t LD_r8_HL(CPU *cpu, __uint8_t *r8)
{
    __uint16_t HL = get_HL(cpu);
    __uint8_t value = cpu->memory[HL];
    update_timer(cpu, 4);
    *r8 = value;
    return 8;
}

__uint8_t LD_SP_HL(CPU *cpu)
{
    __uint16_t HL = get_HL(cpu);
    cpu->SP = HL;
    update_timer(cpu, 4);

    return 8;
}

__uint8_t LD_SP_n16(CPU *cpu)
{
    __uint16_t a16 = get_a16(cpu);
    cpu->SP = a16;

    return 12;
}

__uint8_t LD_a16_SP(CPU *cpu)
{
    __uint8_t a1 = read_opcode(cpu);
    update_timer(cpu, 4);
    __uint16_t a2 = read_opcode(cpu);
    update_timer(cpu, 4);
    __uint16_t a16 = a1 | (a2 << 8);

    write_memory(cpu, a16, cpu->SP & 0x00FF);
    update_timer(cpu, 4);
    write_memory(cpu, a16 + 1, (cpu->SP & 0xFF00) >> 8);
    update_timer(cpu, 4);
    return 20;
}

__uint8_t LD_r8_n8(CPU *cpu, __uint8_t *r8)
{
    __uint8_t n8 = read_opcode(cpu);
    update_timer(cpu, 4);

    *r8 = n8;
    return 8;
}

__uint8_t LD_r8_r8(CPU *cpu, __uint8_t *r_dest, __uint8_t val)
{
    *r_dest = val;
    return 4;
}

__uint8_t LD_A_a16(CPU *cpu)
{
    __uint16_t a16 = get_a16(cpu);
    __uint8_t value = cpu->memory[a16];
    update_timer(cpu, 4);
    cpu->registers.A = value;
    return 16;
}

__uint8_t LD_a16_A(CPU *cpu)
{
    __uint16_t a16 = get_a16(cpu);
    write_memory(cpu, a16, cpu->registers.A);
    update_timer(cpu, 4);

    return 16;
}

__uint8_t LD_a8_A(CPU *cpu)
{
    __uint8_t a8 = read_opcode(cpu);
    update_timer(cpu, 4);
    write_memory(cpu, 0xFF00 + a8, cpu->registers.A);
    update_timer(cpu, 4);

    return 12;
}

__uint8_t LD_A_a8(CPU *cpu)
{
    __uint8_t a8 = read_opcode(cpu);
    update_timer(cpu, 4);
    cpu->registers.A = cpu->memory[0xFF00 + a8];
    update_timer(cpu, 4);
    return 12;
}

__uint8_t LD_A_C(CPU *cpu)
{
    __uint8_t value = cpu->memory[0xFF00 + cpu->registers.C];
    update_timer(cpu, 4);
    cpu->registers.A = value;
    return 8;
}

__uint8_t LD_C_A(CPU *cpu)
{
    write_memory(cpu, 0xFF00 + cpu->registers.C, cpu->registers.A);
    update_timer(cpu, 4);

    return 8;
}

__uint8_t LD_BC_n16(CPU *cpu)
{
    __uint8_t v1 = read_opcode(cpu);
    update_timer(cpu, 4);
    __uint8_t v2 = read_opcode(cpu);
    update_timer(cpu, 4);

    cpu->registers.C = v1;
    cpu->registers.B = v2;
    return 12;
}

__uint8_t LD_DE_n16(CPU *cpu)
{
    __uint8_t v1 = read_opcode(cpu);
    update_timer(cpu, 4);
    __uint8_t v2 = read_opcode(cpu);
    update_timer(cpu, 4);

    cpu->registers.E = v1;
    cpu->registers.D = v2;
    return 12;
}

__uint8_t LD_HL_n16(CPU *cpu)
{
    __uint8_t v1 = read_opcode(cpu);
    update_timer(cpu, 4);
    __uint8_t v2 = read_opcode(cpu);
    update_timer(cpu, 4);

    cpu->registers.L = v1;
    cpu->registers.H = v2;
    return 12;
}

__uint8_t LD_DE_A(CPU *cpu)
{
    __uint16_t DE = get_DE(cpu);

    write_memory(cpu, DE, cpu->registers.A);
    update_timer(cpu, 4);

    return 8;
}

__uint8_t LD_A_DE(CPU *cpu)
{
    __uint16_t DE = get_DE(cpu);
    __uint8_t value = cpu->memory[DE];
    update_timer(cpu, 4);
    cpu->registers.A = value;
    return 8;
}

__uint8_t XOR_A_r8(CPU *cpu, __uint8_t val)
{
    cpu->registers.A ^= val;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = 0;
    return 4;
}

__uint8_t XOR_A_n8(CPU *cpu)
{
    __uint8_t n8 = read_opcode(cpu);
    update_timer(cpu, 4);

    cpu->registers.A ^= n8;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = 0;
    return 8;
}

__uint8_t XOR_A_HL(CPU *cpu)
{
    __uint16_t HL = get_HL(cpu);
    __uint8_t value = cpu->memory[HL];
    update_timer(cpu, 4);
    cpu->registers.A ^= value;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = 0;
    return 8;
}

__uint8_t OR_A_r8(CPU *cpu, __uint8_t val)
{
    cpu->registers.A |= val;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = 0;
    return 4;
}

__uint8_t OR_A_n8(CPU *cpu)
{
    __uint8_t n8 = read_opcode(cpu);
    update_timer(cpu, 4);

    cpu->registers.A |= n8;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = 0;
    return 8;
}

__uint8_t AND_A_n8(CPU *cpu)
{
    __uint8_t n8 = read_opcode(cpu);
    update_timer(cpu, 4);

    cpu->registers.A &= n8;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 0;
    cpu->H = 1;
    cpu->C = 0;
    return 8;
}

__uint8_t AND_A_HL(CPU *cpu)
{
    __uint16_t HL = get_HL(cpu);
    __uint8_t value = cpu->memory[HL];
    update_timer(cpu, 4);

    cpu->registers.A &= value;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 0;
    cpu->H = 1;
    cpu->C = 0;
    return 8;
}

__uint8_t AND_A_r8(CPU *cpu, __uint8_t val)
{
    cpu->registers.A &= val;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 0;
    cpu->H = 1;
    cpu->C = 0;
    return 4;
}

__uint8_t OR_A_HL(CPU *cpu)
{
    __uint16_t HL = get_HL(cpu);
    __uint8_t value = cpu->memory[HL];
    update_timer(cpu, 4);
    OR_A_r8(cpu, value);
    return 8;
}

__uint8_t LD_HL_r8(CPU *cpu, __uint8_t r8)
{
    __uint16_t HL = get_HL(cpu);

    write_memory(cpu, HL, r8);
    update_timer(cpu, 4);
    return 8;
}

__uint8_t LD_HL_n8(CPU *cpu)
{
    __uint8_t n8 = read_opcode(cpu);
    update_timer(cpu, 4);
    __uint16_t HL = get_HL(cpu);

    write_memory(cpu, HL, n8);
    update_timer(cpu, 4);
    return 12;
}

__uint8_t POP_DE(CPU *cpu)
{
    cpu->registers.E = cpu->memory[cpu->SP++];
    update_timer(cpu, 4);
    cpu->registers.D = cpu->memory[cpu->SP++];
    update_timer(cpu, 4);
    return 12;
}

__uint8_t POP_BC(CPU *cpu)
{
    cpu->registers.C = cpu->memory[cpu->SP++];
    update_timer(cpu, 4);
    cpu->registers.B = cpu->memory[cpu->SP++];
    update_timer(cpu, 4);
    return 12;
}

__uint8_t POP_AF(CPU *cpu)
{
    cpu->registers.F = cpu->memory[cpu->SP++];
    update_timer(cpu, 4);
    cpu->registers.A = cpu->memory[cpu->SP++];
    update_timer(cpu, 4);
    cpu->Z = (cpu->registers.F & (1u << 7)) ? 1 : 0;
    cpu->N = (cpu->registers.F & (1u << 6)) ? 1 : 0;
    cpu->H = (cpu->registers.F & (1u << 5)) ? 1 : 0;
    cpu->C = (cpu->registers.F & (1u << 4)) ? 1 : 0;
    return 12;
}

__uint8_t POP_HL(CPU *cpu)
{
    cpu->registers.L = cpu->memory[cpu->SP++];
    update_timer(cpu, 4);
    cpu->registers.H = cpu->memory[cpu->SP++];
    update_timer(cpu, 4);
    return 12;
}

__uint8_t ADC_A_r8(CPU *cpu, __uint8_t n8)
{
    __uint16_t result = cpu->registers.A + n8 + cpu->C;
    __uint8_t half_result = (cpu->registers.A & 0x0F) + (n8 & 0x0F) + cpu->C;

    cpu->H = half_result > 0xF;
    cpu->C = result > 0xFF;
    cpu->registers.A = result & 0xFF;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 0;
    return 4;
}

__uint8_t ADC_A_n8(CPU *cpu)
{
    __uint8_t n8 = read_opcode(cpu);
    update_timer(cpu, 4);
    ADC_A_r8(cpu, n8);
    return 8;
}

__uint8_t ADC_A_HL(CPU *cpu)
{
    __uint16_t HL = get_HL(cpu);
    __uint8_t value = cpu->memory[HL];
    update_timer(cpu, 4);

    ADC_A_r8(cpu, value);
    return 8;
}

__uint8_t SBC_A_r8(CPU *cpu, __uint8_t n8)
{
    __uint16_t sub_val = n8 + cpu->C;
    __uint8_t sub_low = (n8 & 0x0F) + cpu->C;

    cpu->H = ((cpu->registers.A & 0x0F) < sub_low);
    cpu->C = cpu->registers.A < sub_val;
    cpu->registers.A -= sub_val;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 1;
    return 4;
}

__uint8_t SBC_A_n8(CPU *cpu)
{
    __uint8_t n8 = read_opcode(cpu);
    update_timer(cpu, 4);

    SBC_A_r8(cpu, n8);
    return 8;
}

__uint8_t SBC_A_HL(CPU *cpu)
{
    __uint16_t HL = get_HL(cpu);
    __uint8_t value = cpu->memory[HL];
    update_timer(cpu, 4);

    SBC_A_r8(cpu, value);
    return 8;
}

__uint8_t ADD_HL_r16(CPU *cpu, __uint16_t r16)
{
    __uint16_t HL = get_HL(cpu);
    __uint16_t result = HL + r16;

    cpu->H = (HL & 0xFFF) + (r16 & 0xFFF) > 0xFFF;
    update_timer(cpu, 4);
    cpu->C = result < HL;
    HL = result;
    cpu->N = 0;
    store_HL(cpu, HL);
    return 8;
}

__uint8_t ADD_SP_s8(CPU *cpu)
{
    __uint8_t n8 = read_opcode(cpu);
    update_timer(cpu, 4);
    __uint16_t s8 = sign_extend(n8);
    __uint16_t result = cpu->SP + s8;

    cpu->H = (cpu->SP & 0xF) + (s8 & 0xF) > 0xF;
    cpu->C = (cpu->SP & 0xFF) + (s8 & 0xFF) > 0xFF;
    cpu->Z = 0;
    cpu->N = 0;
    cpu->SP = result;
    update_timer(cpu, 8);
    return 16;
}

__uint8_t ADD_A_n8(CPU *cpu)
{
    __uint8_t d8 = read_opcode(cpu);
    update_timer(cpu, 4);

    cpu->H = (cpu->registers.A & 0xF) + (d8 & 0xF) > 0xF;
    cpu->C = __builtin_add_overflow(cpu->registers.A, d8, &(cpu->C));
    cpu->registers.A += d8;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 0;
    return 8;
}

__uint8_t ADD_A_HL(CPU *cpu)
{
    __uint16_t HL = get_HL(cpu);
    __uint8_t value = cpu->memory[HL];
    update_timer(cpu, 4);

    cpu->H = (cpu->registers.A & 0xF) + (value & 0xF) > 0xF;
    cpu->C = __builtin_add_overflow(cpu->registers.A, value, &(cpu->C));
    cpu->registers.A += value;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 0;
    return 8;
}

__uint8_t ADD_A_r8(CPU *cpu, __uint8_t value)
{
    cpu->H = (cpu->registers.A & 0xF) + (value & 0xF) > 0xF;
    cpu->C = __builtin_add_overflow(cpu->registers.A, value, &(cpu->C));
    cpu->registers.A += value;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 0;
    return 4;
}

__uint8_t LD_HL_SP_s8(CPU *cpu)
{
    __uint16_t HL = get_HL(cpu);
    __uint8_t n8 = read_opcode(cpu);
    update_timer(cpu, 4);
    __uint16_t s8 = sign_extend(n8);
    __uint16_t result = cpu->SP + s8;

    cpu->H = (cpu->SP & 0xF) + (s8 & 0xF) > 0xF;
    cpu->C = (cpu->SP & 0xFF) + (s8 & 0xFF) > 0xFF;
    cpu->Z = 0;
    cpu->N = 0;
    store_HL(cpu, result);
    update_timer(cpu, 4);
    return 12;
}

__uint8_t CPL(CPU *cpu)
{
    cpu->registers.A = ~cpu->registers.A;
    cpu->N = 1;
    cpu->H = 1;
    return 4;
}

__uint8_t SCF(CPU *cpu)
{
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = 1;
    return 4;
}

__uint8_t CCF(CPU *cpu)
{
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = !cpu->C;
    return 4;
}

__uint8_t CP_A_n8(CPU *cpu)
{
    __uint8_t n8 = read_opcode(cpu);
    update_timer(cpu, 4);
    __uint8_t result = cpu->registers.A - n8;

    cpu->Z = result == 0;
    cpu->N = 1;
    cpu->H = (cpu->registers.A & 0xF) < (n8 & 0xF);
    cpu->C = cpu->registers.A < n8;
    return 8;
}

__uint8_t CP_A_HL(CPU *cpu)
{
    __uint16_t HL = get_HL(cpu);
    __uint8_t value = cpu->memory[HL];
    update_timer(cpu, 4);
    __uint8_t result = cpu->registers.A - value;

    cpu->Z = result == 0;
    cpu->N = 1;
    cpu->H = (cpu->registers.A & 0xF) < (value & 0xF);
    cpu->C = cpu->registers.A < value;
    return 8;
}

__uint8_t CP_A_r8(CPU *cpu, __uint8_t r8)
{
    __uint8_t result = cpu->registers.A - r8;

    cpu->Z = result == 0;
    cpu->N = 1;
    cpu->H = (cpu->registers.A & 0xF) < (r8 & 0xF);
    cpu->C = cpu->registers.A < r8;
    return 4;
}

__uint8_t JP_CC_n16(CPU *cpu, __uint8_t cc)
{
    __uint16_t a16 = get_a16(cpu);

    if (!cc)
    {
        return 12;
    }
    cpu->PC = a16;
    update_timer(cpu, 4);
    return 16;
}

__uint8_t JP_n16(CPU *cpu)
{
    __uint16_t a16 = get_a16(cpu);

    cpu->PC = a16;
    update_timer(cpu, 4);
    return 16;
}

__uint8_t JP_HL(CPU *cpu)
{
    cpu->PC = get_HL(cpu);
    return 4;
}

__uint8_t JR_CC_n16(CPU *cpu, __uint8_t cc)
{
    __uint8_t e8 = read_opcode(cpu);
    update_timer(cpu, 4);

    if (!cc)
    {
        return 8;
    }
    cpu->PC += sign_extend(e8);
    update_timer(cpu, 4);
    return 12;
}

__uint8_t JR_n16(CPU *cpu)
{
    __uint8_t n8 = read_opcode(cpu);
    update_timer(cpu, 4);
    cpu->PC += sign_extend(n8);
    update_timer(cpu, 4);
    return 12;
}

__uint8_t CALL_CC_n16(CPU *cpu, __uint8_t cc)
{
    __uint16_t a16 = get_a16(cpu);

    if (!cc)
    {
        return 12;
    }
    PUSH_PC(cpu);
    cpu->PC = a16;
    update_timer(cpu, 4);
    return 24;
}

__uint8_t CALL_n16(CPU *cpu)
{
    __uint16_t a16 = get_a16(cpu);

    PUSH_PC(cpu);
    cpu->PC = a16;
    update_timer(cpu, 4);
    return 24;
}

__uint8_t RST_vec(CPU *cpu, __uint8_t address)
{
    PUSH_PC(cpu);
    cpu->PC = address;
    update_timer(cpu, 4);
    return 16;
}

__uint8_t RET_CC(CPU *cpu, __uint8_t cc)
{
    if (!cc)
    {
        update_timer(cpu, 4);
        return 8;
    }

    cpu->PC = get_SP(cpu);
    update_timer(cpu, 8);
    return 20;
}

__uint8_t RET(CPU *cpu)
{
    __uint8_t v1 = cpu->memory[cpu->SP++];
    update_timer(cpu, 4);
    __uint8_t v2 = cpu->memory[cpu->SP++];
    update_timer(cpu, 4);
    __uint16_t a16 = v1 | (v2 << 8);

    cpu->PC = a16;
    update_timer(cpu, 4);
    return 16;
}

__uint8_t RETI(CPU *cpu)
{
    __uint16_t a16 = get_SP(cpu);
    cpu->PC = a16;
    update_timer(cpu, 4);
    cpu->IME = 1;
    return 16;
}

SDL_Window *SDL_Window_init()
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return NULL;
    }

    SDL_Window *window = SDL_CreateWindow("GB-emu",
                                          SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED,
                                          WINDOW_WIDTH,
                                          WINDOW_HEIGHT,
                                          SDL_WINDOW_SHOWN);
    if (window == NULL)
    {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        return NULL;
    }

    return window;
}

SDL_Renderer *SDL_Renderer_init(SDL_Window *window)
{
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == NULL)
    {
        printf("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        return NULL;
    }

    return renderer;
}

__uint8_t display_frame(SDL_Window *window, SDL_Renderer *renderer, __uint8_t *frame)
{
    __uint8_t cell_width = WINDOW_WIDTH / 160;
    __uint8_t cell_height = WINDOW_HEIGHT / 144;

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    for (__uint8_t y = 0; y < 144; y++)
    {
        for (__uint8_t x = 0; x < 160; x++)
        {
            switch (frame[y * 160 + x])
            {
            case 0: // White
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                break;
            case 1: // Grey
                SDL_SetRenderDrawColor(renderer, 166, 166, 166, 255);
                break;
            case 2: // Dark grey
                SDL_SetRenderDrawColor(renderer, 77, 77, 77, 255);
                break;
            case 3: // Black
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                break;
            }

            SDL_Rect cell = {
                x * cell_width,
                y * cell_height,
                cell_width,
                cell_height};
            SDL_RenderFillRect(renderer, &cell);
        }
    }

    SDL_RenderPresent(renderer);
}

__uint8_t render_scanline(CPU *cpu, __uint8_t ly)
{
    __uint8_t lcdc = cpu->memory[0xFF40];
    __uint8_t scy = cpu->memory[0xFF42]; // SCY
    __uint8_t scx = cpu->memory[0xFF43]; // SCX
    __uint8_t bgp = cpu->memory[0xFF47];

    __uint16_t tilemap = (lcdc & 0x08) ? 0x9C00 : 0x9800;
    __uint16_t tiledata = (lcdc & 0x10) ? 0x8000 : 0x8800;

    __uint8_t y = (ly + scy) % 256;
    __uint8_t tile_row = y / 8;
    __uint8_t pixel_row = y % 8;

    for (__uint8_t x = 0; x < 160; x++)
    {

        __uint8_t x_pos = (x + scx) % 256;
        __uint8_t tile_col = x_pos / 8;

        __uint16_t tile_addr = tilemap + (tile_row * 32) + tile_col;
        __uint8_t tile_num = cpu->ppu.vram[tile_addr - 0x8000];

        __uint16_t tile_offset = (tiledata == 0x8800 && tile_num < 128) ? tile_num + 256 : tile_num;

        __uint16_t data_addr = tiledata + (tile_offset * 16) + (pixel_row * 2);
        __uint8_t low = cpu->ppu.vram[data_addr - 0x8000];
        __uint8_t high = cpu->ppu.vram[data_addr + 1 - 0x8000];

        __uint8_t bit = 7 - (x_pos % 8);
        __uint8_t color = ((high >> bit) & 1) << 1 | ((low >> bit) & 1);

        __uint8_t shade = (bgp >> (color * 2)) & 0x03;
        cpu->ppu.frame[ly * 160 + x] = shade;
    }
}

void update_ppu(CPU *cpu, __uint8_t t_cycles, SDL_Window *window, SDL_Renderer *renderer)
{
    __uint32_t prev_cycles = cpu->ppu.cycles;
    cpu->ppu.cycles += t_cycles;

    __uint32_t prev_line_cycles = prev_cycles % 456;
    __uint32_t new_line_cycles = cpu->ppu.cycles % 456;

    if (prev_line_cycles > new_line_cycles || cpu->ppu.cycles >= 70244)
    {
        __uint8_t *ly = &cpu->memory[0xFF44];
        *ly = (cpu->ppu.cycles / 456) % 154;

        if (!(cpu->memory[0xFF40] & 0x80))
        {
            *ly = 0;
            return;
        }

        if (*ly < 144)
        {
            render_scanline(cpu, *ly);
        }
        else if (*ly == 144)
        {
            write_memory(cpu, 0xFF0F, cpu->memory[0xFF0F] | 0x01); // Set VBlank IF bit
        }

        // Reset frame at 70224 T-cycles
        if (cpu->ppu.cycles >= 70224)
        {
            cpu->ppu.cycles -= 70224;
            display_frame(window, renderer, cpu->ppu.frame);
        }
    }
}

__uint8_t exec_CB(CPU *cpu)
{
    __uint8_t opcode = read_opcode(cpu);
    update_timer(cpu, 4);
    __uint8_t t_cycles = 0;
    switch (opcode)
    {
    case 0x00: // RLC B
        t_cycles = RLC_r8(cpu, &cpu->registers.B);
        break;
    case 0x01: // RLC C
        t_cycles = RLC_r8(cpu, &cpu->registers.C);
        break;
    case 0x02: // RLC D
        t_cycles = RLC_r8(cpu, &cpu->registers.D);
        break;
    case 0x03: // RLC E
        t_cycles = RLC_r8(cpu, &cpu->registers.E);
        break;
    case 0x04: // RLC H
        t_cycles = RLC_r8(cpu, &cpu->registers.H);
        break;
    case 0x05: // RLC L
        t_cycles = RLC_r8(cpu, &cpu->registers.L);
        break;
    case 0x06: // RLC [HL]
        t_cycles = RLC_HL_r8(cpu);
        break;
    case 0x07: // RLC A
        t_cycles = RLC_r8(cpu, &cpu->registers.A);
        break;
    case 0x08: // RRC B
        t_cycles = RRC_r8(cpu, &cpu->registers.B);
        break;
    case 0x09: // RRC C
        t_cycles = RRC_r8(cpu, &cpu->registers.C);
        break;
    case 0x0A: // RRC D
        t_cycles = RRC_r8(cpu, &cpu->registers.D);
        break;
    case 0x0B: // RRC E
        t_cycles = RRC_r8(cpu, &cpu->registers.E);
        break;
    case 0x0C: // RRC H
        t_cycles = RRC_r8(cpu, &cpu->registers.H);
        break;
    case 0x0D: // RRC L
        t_cycles = RRC_r8(cpu, &cpu->registers.L);
        break;
    case 0x0E: // RRC [HL]
        t_cycles = RRC_HL(cpu);
        break;
    case 0x0F: // RRC A
        t_cycles = RRC_r8(cpu, &cpu->registers.A);
        break;
    case 0x10: // RL B
        t_cycles = RL_r8(cpu, &cpu->registers.B);
        break;
    case 0x11: // RL C
        t_cycles = RL_r8(cpu, &cpu->registers.C);
        break;
    case 0x12: // RL D
        t_cycles = RL_r8(cpu, &cpu->registers.D);
        break;
    case 0x13: // RL E
        t_cycles = RL_r8(cpu, &cpu->registers.E);
        break;
    case 0x14: // RL H
        t_cycles = RL_r8(cpu, &cpu->registers.H);
        break;
    case 0x15: // RL L
        t_cycles = RL_r8(cpu, &cpu->registers.L);
        break;
    case 0x16: // RL [HL]
        t_cycles = RL_HL(cpu);
        break;
    case 0x17: // RL A
        t_cycles = RL_r8(cpu, &cpu->registers.A);
        break;
    case 0x18: // RL B
        t_cycles = RR_r8(cpu, &cpu->registers.B);
        break;
    case 0x19: // RR C
        t_cycles = RR_r8(cpu, &cpu->registers.C);
        break;
    case 0x1A: // RR D
        t_cycles = RR_r8(cpu, &cpu->registers.D);
        break;
    case 0x1B: // RR E
        t_cycles = RR_r8(cpu, &cpu->registers.E);
        break;
    case 0x1C: // RL H
        t_cycles = RR_r8(cpu, &cpu->registers.H);
        break;
    case 0x1D: // RL L
        t_cycles = RR_r8(cpu, &cpu->registers.L);
        break;
    case 0x1E: // RR [HL]
        t_cycles = RR_HL(cpu);
        break;
    case 0x1F: // RL A
        t_cycles = RR_r8(cpu, &cpu->registers.A);
        break;
    case 0x20: // SLA B
        t_cycles = SLA_r8(cpu, &cpu->registers.B);
        break;
    case 0x21: // SLA C
        t_cycles = SLA_r8(cpu, &cpu->registers.C);
        break;
    case 0x22: // SLA D
        t_cycles = SLA_r8(cpu, &cpu->registers.D);
        break;
    case 0x23: // SLA E
        t_cycles = SLA_r8(cpu, &cpu->registers.E);
        break;
    case 0x24: // SLA H
        t_cycles = SLA_r8(cpu, &cpu->registers.H);
        break;
    case 0x25: // SLA L
        t_cycles = SLA_r8(cpu, &cpu->registers.L);
        break;
    case 0x26: // SLA [HL]
        t_cycles = SLA_HL(cpu);
        break;
    case 0x27: // SLA A
        t_cycles = SLA_r8(cpu, &cpu->registers.A);
        break;
    case 0x28: // SRA B
        t_cycles = SRA_r8(cpu, &cpu->registers.B);
        break;
    case 0x29: // SRA C
        t_cycles = SRA_r8(cpu, &cpu->registers.C);
        break;
    case 0x2A: // SRA D
        t_cycles = SRA_r8(cpu, &cpu->registers.D);
        break;
    case 0x2B: // SRA E
        t_cycles = SRA_r8(cpu, &cpu->registers.E);
        break;
    case 0x2C: // SRA H
        t_cycles = SRA_r8(cpu, &cpu->registers.H);
        break;
    case 0x2D: // SRA L
        t_cycles = SRA_r8(cpu, &cpu->registers.L);
        break;
    case 0x2E: // SRA [HL]
        t_cycles = SRA_HL(cpu);
        break;
    case 0x2F: // SRA F
        t_cycles = SRA_r8(cpu, &cpu->registers.A);
        break;
    case 0x30: // SWAP B
        t_cycles = SWAP_r8(cpu, &cpu->registers.B);
        break;
    case 0x31: // SWAP C
        t_cycles = SWAP_r8(cpu, &cpu->registers.C);
        break;
    case 0x32: // SWAP D
        t_cycles = SWAP_r8(cpu, &cpu->registers.D);
        break;
    case 0x33: // SWAP E
        t_cycles = SWAP_r8(cpu, &cpu->registers.E);
        break;
    case 0x34: // SWAP H
        t_cycles = SWAP_r8(cpu, &cpu->registers.H);
        break;
    case 0x35: // SWAP L
        t_cycles = SWAP_r8(cpu, &cpu->registers.L);
        break;
    case 0x36: // SWAP [HL]
        t_cycles = SWAP_HL(cpu);
        break;
    case 0x37: // SWAP A
        t_cycles = SWAP_r8(cpu, &cpu->registers.A);
        break;
    case 0x38: // SRL B
        t_cycles = SRL_r8(cpu, &cpu->registers.B);
        break;
    case 0x39: // SRL C
        t_cycles = SRL_r8(cpu, &cpu->registers.C);
        break;
    case 0x3A: // SRL D
        t_cycles = SRL_r8(cpu, &cpu->registers.D);
        break;
    case 0x3B: // SRL E
        t_cycles = SRL_r8(cpu, &cpu->registers.E);
        break;
    case 0x3C: // SRL H
        t_cycles = SRL_r8(cpu, &cpu->registers.H);
        break;
    case 0x3D: // SRL L
        t_cycles = SRL_r8(cpu, &cpu->registers.L);
        break;
    case 0x3E: // SRL [HL]
        t_cycles = SRL_HL(cpu);
        break;
    case 0x3F: // SRL A
        t_cycles = SRL_r8(cpu, &cpu->registers.A);
        break;
    case 0x40: // BIT 0, B
        t_cycles = BIT_u3_r8(cpu, 0, &cpu->registers.B);
        break;
    case 0x41: // BIT 0, C
        t_cycles = BIT_u3_r8(cpu, 0, &cpu->registers.C);
        break;
    case 0x42: // BIT 0, D
        t_cycles = BIT_u3_r8(cpu, 0, &cpu->registers.D);
        break;
    case 0x43: // BIT 0, E
        t_cycles = BIT_u3_r8(cpu, 0, &cpu->registers.E);
        break;
    case 0x44: // BIT 0, H
        t_cycles = BIT_u3_r8(cpu, 0, &cpu->registers.H);
        break;
    case 0x45: // BIT 0, L
        t_cycles = BIT_u3_r8(cpu, 0, &cpu->registers.L);
        break;
    case 0x46: // BIT 0, [HL]
        t_cycles = BIT_u3_HL(cpu, 0);
        break;
    case 0x47: // BIT 0, A
        t_cycles = BIT_u3_r8(cpu, 0, &cpu->registers.A);
        break;
    case 0x48: // BIT 1, B
        t_cycles = BIT_u3_r8(cpu, 1, &cpu->registers.B);
        break;
    case 0x49: // BIT 1, C
        t_cycles = BIT_u3_r8(cpu, 1, &cpu->registers.C);
        break;
    case 0x4A: // BIT 1, D
        t_cycles = BIT_u3_r8(cpu, 1, &cpu->registers.D);
        break;
    case 0x4B: // BIT 1, E
        t_cycles = BIT_u3_r8(cpu, 1, &cpu->registers.E);
        break;
    case 0x4C: // BIT 1, H
        t_cycles = BIT_u3_r8(cpu, 1, &cpu->registers.H);
        break;
    case 0x4D: // BIT 1, L
        t_cycles = BIT_u3_r8(cpu, 1, &cpu->registers.L);
        break;
    case 0x4E: // BIT 1, [HL]
        t_cycles = BIT_u3_HL(cpu, 1);
        break;
    case 0x4F: // BIT 1, A
        t_cycles = BIT_u3_r8(cpu, 1, &cpu->registers.A);
        break;
    case 0x50: // BIT 2, B
        t_cycles = BIT_u3_r8(cpu, 2, &cpu->registers.B);
        break;
    case 0x51: // BIT 2, C
        t_cycles = BIT_u3_r8(cpu, 2, &cpu->registers.C);
        break;
    case 0x52: // BIT 2, D
        t_cycles = BIT_u3_r8(cpu, 2, &cpu->registers.D);
        break;
    case 0x53: // BIT 2, E
        t_cycles = BIT_u3_r8(cpu, 2, &cpu->registers.E);
        break;
    case 0x54: // BIT 2, H
        t_cycles = BIT_u3_r8(cpu, 2, &cpu->registers.H);
        break;
    case 0x55: // BIT 2, L
        t_cycles = BIT_u3_r8(cpu, 2, &cpu->registers.L);
        break;
    case 0x56: // BIT 2, [HL]
        t_cycles = BIT_u3_HL(cpu, 2);
        break;
    case 0x57: // BIT 2, A
        t_cycles = BIT_u3_r8(cpu, 2, &cpu->registers.A);
        break;
    case 0x58: // BIT 3, B
        t_cycles = BIT_u3_r8(cpu, 3, &cpu->registers.B);
        break;
    case 0x59: // BIT 3, C
        t_cycles = BIT_u3_r8(cpu, 3, &cpu->registers.C);
        break;
    case 0x5A: // BIT 3, D
        t_cycles = BIT_u3_r8(cpu, 3, &cpu->registers.D);
        break;
    case 0x5B: // BIT 3, E
        t_cycles = BIT_u3_r8(cpu, 3, &cpu->registers.E);
        break;
    case 0x5C: // BIT 3, H
        t_cycles = BIT_u3_r8(cpu, 3, &cpu->registers.H);
        break;
    case 0x5D: // BIT 3, L
        t_cycles = BIT_u3_r8(cpu, 3, &cpu->registers.L);
        break;
    case 0x5E: // BIT 3, [HL]
        t_cycles = BIT_u3_HL(cpu, 3);
        break;
    case 0x5F: // BIT 3, A
        t_cycles = BIT_u3_r8(cpu, 3, &cpu->registers.A);
        break;
    case 0x60: // BIT 4, B
        t_cycles = BIT_u3_r8(cpu, 4, &cpu->registers.B);
        break;
    case 0x61: // BIT 4, C
        t_cycles = BIT_u3_r8(cpu, 4, &cpu->registers.C);
        break;
    case 0x62: // BIT 4, D
        t_cycles = BIT_u3_r8(cpu, 4, &cpu->registers.D);
        break;
    case 0x63: // BIT 4, E
        t_cycles = BIT_u3_r8(cpu, 4, &cpu->registers.E);
        break;
    case 0x64: // BIT 4, H
        t_cycles = BIT_u3_r8(cpu, 4, &cpu->registers.H);
        break;
    case 0x65: // BIT 4, L
        t_cycles = BIT_u3_r8(cpu, 4, &cpu->registers.L);
        break;
    case 0x66: // BIT 4, [HL]
        t_cycles = BIT_u3_HL(cpu, 4);
        break;
    case 0x67: // BIT 4, A
        t_cycles = BIT_u3_r8(cpu, 4, &cpu->registers.A);
        break;
    case 0x68: // BIT 5, B
        t_cycles = BIT_u3_r8(cpu, 5, &cpu->registers.B);
        break;
    case 0x69: // BIT 5, C
        t_cycles = BIT_u3_r8(cpu, 5, &cpu->registers.C);
        break;
    case 0x6A: // BIT 5, D
        t_cycles = BIT_u3_r8(cpu, 5, &cpu->registers.D);
        break;
    case 0x6B: // BIT 5, E
        t_cycles = BIT_u3_r8(cpu, 5, &cpu->registers.E);
        break;
    case 0x6C: // BIT 5, H
        t_cycles = BIT_u3_r8(cpu, 5, &cpu->registers.H);
        break;
    case 0x6D: // BIT 5, L
        t_cycles = BIT_u3_r8(cpu, 5, &cpu->registers.L);
        break;
    case 0x6E: // BIT 5, [HL]
        t_cycles = BIT_u3_HL(cpu, 5);
        break;
    case 0x6F: // BIT 5, A
        t_cycles = BIT_u3_r8(cpu, 5, &cpu->registers.A);
        break;
    case 0x70: // BIT 6, B
        t_cycles = BIT_u3_r8(cpu, 6, &cpu->registers.B);
        break;
    case 0x71: // BIT 6, C
        t_cycles = BIT_u3_r8(cpu, 6, &cpu->registers.C);
        break;
    case 0x72: // BIT 6, D
        t_cycles = BIT_u3_r8(cpu, 6, &cpu->registers.D);
        break;
    case 0x73: // BIT 6, E
        t_cycles = BIT_u3_r8(cpu, 6, &cpu->registers.E);
        break;
    case 0x74: // BIT 6, H
        t_cycles = BIT_u3_r8(cpu, 6, &cpu->registers.H);
        break;
    case 0x75: // BIT 6, L
        t_cycles = BIT_u3_r8(cpu, 6, &cpu->registers.L);
        break;
    case 0x76: // BIT 6, [HL]
        t_cycles = BIT_u3_HL(cpu, 6);
        break;
    case 0x77: // BIT 6, A
        t_cycles = BIT_u3_r8(cpu, 6, &cpu->registers.A);
        break;
    case 0x78: // BIT 7, B
        t_cycles = BIT_u3_r8(cpu, 7, &cpu->registers.B);
        break;
    case 0x79: // BIT 7, C
        t_cycles = BIT_u3_r8(cpu, 7, &cpu->registers.C);
        break;
    case 0x7A: // BIT 7, D
        t_cycles = BIT_u3_r8(cpu, 7, &cpu->registers.D);
        break;
    case 0x7B: // BIT 7, E
        t_cycles = BIT_u3_r8(cpu, 7, &cpu->registers.E);
        break;
    case 0x7C: // BIT 7, H
        t_cycles = BIT_u3_r8(cpu, 7, &cpu->registers.H);
        break;
    case 0x7D: // BIT 7, L
        t_cycles = BIT_u3_r8(cpu, 7, &cpu->registers.L);
        break;
    case 0x7E: // BIT 7, [HL]
        t_cycles = BIT_u3_HL(cpu, 7);
        break;
    case 0x7F: // BIT 7, A
        t_cycles = BIT_u3_r8(cpu, 7, &cpu->registers.A);
        break;
    case 0x80: // RES 0, B
        t_cycles = RES_u3_r8(cpu, 0, &cpu->registers.B);
        break;
    case 0x81: // RES 0, C
        t_cycles = RES_u3_r8(cpu, 0, &cpu->registers.C);
        break;
    case 0x82: // RES 0, D
        t_cycles = RES_u3_r8(cpu, 0, &cpu->registers.D);
        break;
    case 0x83: // RES 0, E
        t_cycles = RES_u3_r8(cpu, 0, &cpu->registers.E);
        break;
    case 0x84: // RES 0, H
        t_cycles = RES_u3_r8(cpu, 0, &cpu->registers.H);
        break;
    case 0x85: // RES 0, L
        t_cycles = RES_u3_r8(cpu, 0, &cpu->registers.L);
        break;
    case 0x86: // RES 0, [HL]
        t_cycles = RES_u3_HL(cpu, 0);
        break;
    case 0x87: // RES 0, A
        t_cycles = RES_u3_r8(cpu, 0, &cpu->registers.A);
        break;
    case 0x88: // RES 1, B
        t_cycles = RES_u3_r8(cpu, 1, &cpu->registers.B);
        break;
    case 0x89: // RES 1, C
        t_cycles = RES_u3_r8(cpu, 1, &cpu->registers.C);
        break;
    case 0x8A: // RES 1, D
        t_cycles = RES_u3_r8(cpu, 1, &cpu->registers.D);
        break;
    case 0x8B: // RES 1, E
        t_cycles = RES_u3_r8(cpu, 1, &cpu->registers.E);
        break;
    case 0x8C: // RES 1, H
        t_cycles = RES_u3_r8(cpu, 1, &cpu->registers.H);
        break;
    case 0x8D: // RES 1, L
        t_cycles = RES_u3_r8(cpu, 1, &cpu->registers.L);
        break;
    case 0x8E: // RES 1, [HL]
        t_cycles = RES_u3_HL(cpu, 1);
        break;
    case 0x8F: // RES 1, A
        t_cycles = RES_u3_r8(cpu, 1, &cpu->registers.A);
        break;
    case 0x90: // RES 2, B
        t_cycles = RES_u3_r8(cpu, 2, &cpu->registers.B);
        break;
    case 0x91: // RES 2, C
        t_cycles = RES_u3_r8(cpu, 2, &cpu->registers.C);
        break;
    case 0x92: // RES 2, D
        t_cycles = RES_u3_r8(cpu, 2, &cpu->registers.D);
        break;
    case 0x93: // RES 2, E
        t_cycles = RES_u3_r8(cpu, 2, &cpu->registers.E);
        break;
    case 0x94: // RES 2, H
        t_cycles = RES_u3_r8(cpu, 2, &cpu->registers.H);
        break;
    case 0x95: // RES 2, L
        t_cycles = RES_u3_r8(cpu, 2, &cpu->registers.L);
        break;
    case 0x96: // RES 2, [HL]
        t_cycles = RES_u3_HL(cpu, 2);
        break;
    case 0x97: // RES 2, A
        t_cycles = RES_u3_r8(cpu, 2, &cpu->registers.A);
        break;
    case 0x98: // RES 3, B
        t_cycles = RES_u3_r8(cpu, 3, &cpu->registers.B);
        break;
    case 0x99: // RES 3, C
        t_cycles = RES_u3_r8(cpu, 3, &cpu->registers.C);
        break;
    case 0x9A: // RES 3, D
        t_cycles = RES_u3_r8(cpu, 3, &cpu->registers.D);
        break;
    case 0x9B: // RES 3, E
        t_cycles = RES_u3_r8(cpu, 3, &cpu->registers.E);
        break;
    case 0x9C: // RES 3, H
        t_cycles = RES_u3_r8(cpu, 3, &cpu->registers.H);
        break;
    case 0x9D: // RES 3, L
        t_cycles = RES_u3_r8(cpu, 3, &cpu->registers.L);
        break;
    case 0x9E: // RES 3, [HL]
        t_cycles = RES_u3_HL(cpu, 3);
        break;
    case 0x9F: // RES 3, A
        t_cycles = RES_u3_r8(cpu, 3, &cpu->registers.A);
        break;
    case 0xA0: // RES 4, B
        t_cycles = RES_u3_r8(cpu, 4, &cpu->registers.B);
        break;
    case 0xA1: // RES 4, C
        t_cycles = RES_u3_r8(cpu, 4, &cpu->registers.C);
        break;
    case 0xA2: // RES 4, D
        t_cycles = RES_u3_r8(cpu, 4, &cpu->registers.D);
        break;
    case 0xA3: // RES 4, E
        t_cycles = RES_u3_r8(cpu, 4, &cpu->registers.E);
        break;
    case 0xA4: // RES 4, H
        t_cycles = RES_u3_r8(cpu, 4, &cpu->registers.H);
        break;
    case 0xA5: // RES 4, L
        t_cycles = RES_u3_r8(cpu, 4, &cpu->registers.L);
        break;
    case 0xA6: // RES 4, [HL]
        t_cycles = RES_u3_HL(cpu, 4);
        break;
    case 0xA7: // RES 4, A
        t_cycles = RES_u3_r8(cpu, 4, &cpu->registers.A);
        break;
    case 0xA8: // RES 5, B
        t_cycles = RES_u3_r8(cpu, 5, &cpu->registers.B);
        break;
    case 0xA9: // RES 5, C
        t_cycles = RES_u3_r8(cpu, 5, &cpu->registers.C);
        break;
    case 0xAA: // RES 5, D
        t_cycles = RES_u3_r8(cpu, 5, &cpu->registers.D);
        break;
    case 0xAB: // RES 5, E
        t_cycles = RES_u3_r8(cpu, 5, &cpu->registers.E);
        break;
    case 0xAC: // RES 5, H
        t_cycles = RES_u3_r8(cpu, 5, &cpu->registers.H);
        break;
    case 0xAD: // RES 5, L
        t_cycles = RES_u3_r8(cpu, 5, &cpu->registers.L);
        break;
    case 0xAE: // RES 5, [HL]
        t_cycles = RES_u3_HL(cpu, 5);
        break;
    case 0xAF: // RES 5, A
        t_cycles = RES_u3_r8(cpu, 5, &cpu->registers.A);
        break;
    case 0xB0: // RES 6, B
        t_cycles = RES_u3_r8(cpu, 6, &cpu->registers.B);
        break;
    case 0xB1: // RES 6, C
        t_cycles = RES_u3_r8(cpu, 6, &cpu->registers.C);
        break;
    case 0xB2: // RES 6, D
        t_cycles = RES_u3_r8(cpu, 6, &cpu->registers.D);
        break;
    case 0xB3: // RES 6, E
        t_cycles = RES_u3_r8(cpu, 6, &cpu->registers.E);
        break;
    case 0xB4: // RES 6, H
        t_cycles = RES_u3_r8(cpu, 6, &cpu->registers.H);
        break;
    case 0xB5: // RES 6, L
        t_cycles = RES_u3_r8(cpu, 6, &cpu->registers.L);
        break;
    case 0xB6: // RES 6, [HL]
        t_cycles = RES_u3_HL(cpu, 6);
        break;
    case 0xB7: // RES 6, A
        t_cycles = RES_u3_r8(cpu, 6, &cpu->registers.A);
        break;
    case 0xB8: // RES 7, B
        t_cycles = RES_u3_r8(cpu, 7, &cpu->registers.B);
        break;
    case 0xB9: // RES 7, C
        t_cycles = RES_u3_r8(cpu, 7, &cpu->registers.C);
        break;
    case 0xBA: // RES 7, D
        t_cycles = RES_u3_r8(cpu, 7, &cpu->registers.D);
        break;
    case 0xBB: // RES 7, E
        t_cycles = RES_u3_r8(cpu, 7, &cpu->registers.E);
        break;
    case 0xBC: // RES 7, H
        t_cycles = RES_u3_r8(cpu, 7, &cpu->registers.H);
        break;
    case 0xBD: // RES 7, L
        t_cycles = RES_u3_r8(cpu, 7, &cpu->registers.L);
        break;
    case 0xBE: // RES 7, [HL]
        t_cycles = RES_u3_HL(cpu, 7);
        break;
    case 0xBF: // RES 7, A
        t_cycles = RES_u3_r8(cpu, 7, &cpu->registers.A);
        break;
    case 0xC0: // SET 0, B
        t_cycles = SET_u3_r8(cpu, 0, &cpu->registers.B);
        break;
    case 0xC1: // SET 0, C
        t_cycles = SET_u3_r8(cpu, 0, &cpu->registers.C);
        break;
    case 0xC2: // SET 0, D
        t_cycles = SET_u3_r8(cpu, 0, &cpu->registers.D);
        break;
    case 0xC3: // SET 0, E
        t_cycles = SET_u3_r8(cpu, 0, &cpu->registers.E);
        break;
    case 0xC4: // SET 0, H
        t_cycles = SET_u3_r8(cpu, 0, &cpu->registers.H);
        break;
    case 0xC5: // SET 0, L
        t_cycles = SET_u3_r8(cpu, 0, &cpu->registers.L);
        break;
    case 0xC6: // SET 0, [HL]
        t_cycles = SET_u3_HL(cpu, 0);
        break;
    case 0xC7: // SET 0, A
        t_cycles = SET_u3_r8(cpu, 0, &cpu->registers.A);
        break;
    case 0xC8: // SET 1, B
        t_cycles = SET_u3_r8(cpu, 1, &cpu->registers.B);
        break;
    case 0xC9: // SET 1, C
        t_cycles = SET_u3_r8(cpu, 1, &cpu->registers.C);
        break;
    case 0xCA: // SET 1, D
        t_cycles = SET_u3_r8(cpu, 1, &cpu->registers.D);
        break;
    case 0xCB: // SET 1, E
        t_cycles = SET_u3_r8(cpu, 1, &cpu->registers.E);
        break;
    case 0xCC: // SET 1, H
        t_cycles = SET_u3_r8(cpu, 1, &cpu->registers.H);
        break;
    case 0xCD: // SET 1, L
        t_cycles = SET_u3_r8(cpu, 1, &cpu->registers.L);
        break;
    case 0xCE: // SET 1, [HL]
        t_cycles = SET_u3_HL(cpu, 1);
        break;
    case 0xCF: // SET 1, A
        t_cycles = SET_u3_r8(cpu, 1, &cpu->registers.A);
        break;
    case 0xD0: // SET 2, B
        t_cycles = SET_u3_r8(cpu, 2, &cpu->registers.B);
        break;
    case 0xD1: // SET 2, C
        t_cycles = SET_u3_r8(cpu, 2, &cpu->registers.C);
        break;
    case 0xD2: // SET 2, D
        t_cycles = SET_u3_r8(cpu, 2, &cpu->registers.D);
        break;
    case 0xD3: // SET 2, E
        t_cycles = SET_u3_r8(cpu, 2, &cpu->registers.E);
        break;
    case 0xD4: // SET 2, H
        t_cycles = SET_u3_r8(cpu, 2, &cpu->registers.H);
        break;
    case 0xD5: // SET 2, L
        t_cycles = SET_u3_r8(cpu, 2, &cpu->registers.L);
        break;
    case 0xD6: // SET 2, [HL]
        t_cycles = SET_u3_HL(cpu, 2);
        break;
    case 0xD7: // SET 2, A
        t_cycles = SET_u3_r8(cpu, 2, &cpu->registers.A);
        break;
    case 0xD8: // SET 3, B
        t_cycles = SET_u3_r8(cpu, 3, &cpu->registers.B);
        break;
    case 0xD9: // SET 3, C
        t_cycles = SET_u3_r8(cpu, 3, &cpu->registers.C);
        break;
    case 0xDA: // SET 3, D
        t_cycles = SET_u3_r8(cpu, 3, &cpu->registers.D);
        break;
    case 0xDB: // SET 3, E
        t_cycles = SET_u3_r8(cpu, 3, &cpu->registers.E);
        break;
    case 0xDC: // SET 3, H
        t_cycles = SET_u3_r8(cpu, 3, &cpu->registers.H);
        break;
    case 0xDD: // SET 3, L
        t_cycles = SET_u3_r8(cpu, 3, &cpu->registers.L);
        break;
    case 0xDE: // SET 3, [HL]
        t_cycles = SET_u3_HL(cpu, 3);
        break;
    case 0xDF: // SET 3, A
        t_cycles = SET_u3_r8(cpu, 3, &cpu->registers.A);
        break;
    case 0xE0: // SET 4, B
        t_cycles = SET_u3_r8(cpu, 4, &cpu->registers.B);
        break;
    case 0xE1: // SET 4, C
        t_cycles = SET_u3_r8(cpu, 4, &cpu->registers.C);
        break;
    case 0xE2: // SET 4, D
        t_cycles = SET_u3_r8(cpu, 4, &cpu->registers.D);
        break;
    case 0xE3: // SET 4, E
        t_cycles = SET_u3_r8(cpu, 4, &cpu->registers.E);
        break;
    case 0xE4: // SET 4, H
        t_cycles = SET_u3_r8(cpu, 4, &cpu->registers.H);
        break;
    case 0xE5: // SET 4, L
        t_cycles = SET_u3_r8(cpu, 4, &cpu->registers.L);
        break;
    case 0xE6: // SET 4, [HL]
        t_cycles = SET_u3_HL(cpu, 4);
        break;
    case 0xE7: // SET 4, A
        t_cycles = SET_u3_r8(cpu, 4, &cpu->registers.A);
        break;
    case 0xE8: // SET 5, B
        t_cycles = SET_u3_r8(cpu, 5, &cpu->registers.B);
        break;
    case 0xE9: // SET 5, C
        t_cycles = SET_u3_r8(cpu, 5, &cpu->registers.C);
        break;
    case 0xEA: // SET 5, D
        t_cycles = SET_u3_r8(cpu, 5, &cpu->registers.D);
        break;
    case 0xEB: // SET 5, E
        t_cycles = SET_u3_r8(cpu, 5, &cpu->registers.E);
        break;
    case 0xEC: // SET 5, H
        t_cycles = SET_u3_r8(cpu, 5, &cpu->registers.H);
        break;
    case 0xED: // SET 5, L
        t_cycles = SET_u3_r8(cpu, 5, &cpu->registers.L);
        break;
    case 0xEE: // SET 5, [HL]
        t_cycles = SET_u3_HL(cpu, 5);
        break;
    case 0xEF: // SET 5, A
        t_cycles = SET_u3_r8(cpu, 5, &cpu->registers.A);
        break;
    case 0xF0: // SET 6, B
        t_cycles = SET_u3_r8(cpu, 6, &cpu->registers.B);
        break;
    case 0xF1: // SET 6, C
        t_cycles = SET_u3_r8(cpu, 6, &cpu->registers.C);
        break;
    case 0xF2: // SET 6, D
        t_cycles = SET_u3_r8(cpu, 6, &cpu->registers.D);
        break;
    case 0xF3: // SET 6, E
        t_cycles = SET_u3_r8(cpu, 6, &cpu->registers.E);
        break;
    case 0xF4: // SET 6, H
        t_cycles = SET_u3_r8(cpu, 6, &cpu->registers.H);
        break;
    case 0xF5: // SET 6, L
        t_cycles = SET_u3_r8(cpu, 6, &cpu->registers.L);
        break;
    case 0xF6: // SET 6, [HL]
        t_cycles = SET_u3_HL(cpu, 6);
        break;
    case 0xF7: // SET 6, A
        t_cycles = SET_u3_r8(cpu, 6, &cpu->registers.A);
        break;
    case 0xF8: // SET 7, B
        t_cycles = SET_u3_r8(cpu, 7, &cpu->registers.B);
        break;
    case 0xF9: // SET 7, C
        t_cycles = SET_u3_r8(cpu, 7, &cpu->registers.C);
        break;
    case 0xFA: // SET 7, D
        t_cycles = SET_u3_r8(cpu, 7, &cpu->registers.D);
        break;
    case 0xFB: // SET 7, E
        t_cycles = SET_u3_r8(cpu, 7, &cpu->registers.E);
        break;
    case 0xFC: // SET 7, H
        t_cycles = SET_u3_r8(cpu, 7, &cpu->registers.H);
        break;
    case 0xFD: // SET 7, L
        t_cycles = SET_u3_r8(cpu, 7, &cpu->registers.L);
        break;
    case 0xFE: // SET 7, [HL]
        t_cycles = SET_u3_HL(cpu, 7);
        break;
    case 0xFF: // SET 7, A
        t_cycles = SET_u3_r8(cpu, 7, &cpu->registers.A);
        break;
    default:
        printf("invalid CB opcode: %02x\n", opcode);
        printf("PC: %02x\n", cpu->PC);
        exit(1);
        break;
    }

    return t_cycles;
}

void update_IME(CPU *cpu, __uint8_t opcode)
{
    if (opcode != 0xFB && cpu->ime_delay)
    {
        cpu->IME = 1;
        cpu->ime_delay = 0;
    }
}

__uint8_t handle_interrupts(CPU *cpu, FILE *file)
{
    if (!cpu->IME || !(cpu->memory[0xff0f] & cpu->memory[0xffff]))
        return 0;
    cpu->IME = 0;
    print_cpu(cpu, file);
    __uint8_t flags = cpu->memory[0xff0f];
    PUSH_PC(cpu);
    // update_timer(cpu, 12);
    if (flags & (1u))
    {
        printf("vblank handle\n");
        cpu->memory[0xFF0F] &= ~(1u);
        cpu->PC = VBLANK_ADDR;
        return 1;
    }
    if (flags & (1u << 1))
    {
        printf("lcd handle\n");
        cpu->memory[0xFF0F] &= ~(1u << 1);
        cpu->PC = LCD_STAT_ADDR;
        return 1;
    }
    if (flags & (1u << 2))
    {
        printf("timer handle\n");
        cpu->memory[0xFF0F] &= ~(1u << 2);
        cpu->PC = TIMER_ADDR;
        return 1;
    }
    if (flags & (1u << 3))
    {
        printf("serial handle\n");
        cpu->memory[0xFF0F] &= ~(1u << 3);
        cpu->PC = SERIAL_ADDR;
        return 1;
    }
    if (flags & (1u << 4))
    {
        printf("joypad handle\n");
        cpu->memory[0xFF0F] &= ~(1u << 4);
        cpu->PC = JOYPAD_ADDR;
        return 1;
    }

    return 0;
}

void update_timer(CPU *cpu, __uint8_t t_cycles)
{
    // cpu->div_cycles += cpu->current_t_cycles;
    cpu->div_cycles += t_cycles;
    cpu->memory[0xFF04] = cpu->div_cycles >> 8;

    if (cpu->memory[0xFF07] & 0x04)
    {
        __uint16_t freq = 0;

        switch (cpu->memory[0xFF07] & 0x03)
        {
        case 0:
            freq = 1024;
            break;
        case 1:
            freq = 16;
            break;
        case 2:
            freq = 64;
            break;
        case 3:
            freq = 256;
            break;
        default:
            printf("Unreachable\n");
            exit(1);
        }

        cpu->tima_cycles += t_cycles;
        while (cpu->tima_cycles >= freq)
        {
            cpu->memory[0xFF05]++;
            cpu->tima_cycles -= freq;
            if (cpu->memory[0xFF05] == 0)
            {
                write_memory(cpu, 0xFF05, cpu->memory[0xFF06]);
                write_memory(cpu, 0xFF0F, cpu->memory[0xFF0F] | 0x04);
            }
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Provide ROM path\n");
        exit(1);
    }
    FILE *file = fopen("output.txt", "w");
    SDL_Window *window = SDL_Window_init();
    SDL_Renderer *renderer = SDL_Renderer_init(window);

    if (file == NULL)
    {
        perror("Error opening file");
        return 1;
    }

    const char *filename = argv[1];
    CPU cpu = {0};
    __uint8_t *buffer = read_file(filename, cpu.memory);
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
    write_memory(&cpu, 0xFF44, 0x90);
    cpu.Z = 1;
    cpu.N = 0;
    cpu.H = 1;
    cpu.C = 1;

    cpu.ppu.vram = &cpu.memory[0x8000];
    print_cpu(&cpu, file);
    int cnt = 0;
    int quit = 0;
    SDL_Event e;
    while (cnt < 7500000 && !quit)
    {
        while (SDL_PollEvent(&e) != 0)
        {
            if (e.type == SDL_QUIT)
            {
                quit = 1;
            }
        }

        cnt++;

        if (cpu.halted)
        {
            __uint8_t t_cycles = 4;
            // cpu.current_t_cycles += t_cycles;
            update_timer(&cpu, t_cycles);

            if (cpu.IME && (cpu.memory[0xFF0F] & cpu.memory[0xFFFF]))
            {
                cpu.halted = 0;
                __uint8_t handled = handle_interrupts(&cpu, file);
            }
            else if (!cpu.IME && cpu.memory[0xFF0F])
            {
                cpu.halted = 0;
            }
            continue;
        }

        __uint8_t opcode = read_opcode(&cpu);
        __uint8_t t_cycles = 4;
        update_timer(&cpu, t_cycles);

        switch (opcode)
        {
        case 0x00: // NOP
            t_cycles = NOP(&cpu);
            break;
        case 0xF0: // LDH A, [a8]
            t_cycles = LD_A_a8(&cpu);
            break;
        case 0xF3: // DI
            t_cycles = DI(&cpu);
            break;
        case 0x44: // LD B, H
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.B, cpu.registers.H);
            break;
        case 0xFE: // CP A, n8
            t_cycles = CP_A_n8(&cpu);
            break;
        case 0x38: // JR C, e8
            t_cycles = JR_CC_n16(&cpu, cpu.C == 1);
            break;
        case 0xC3: // JP a16
            t_cycles = JP_n16(&cpu);
            break;
        case 0xAF: // XOR A, A
            t_cycles = XOR_A_r8(&cpu, cpu.registers.A);
            break;
        case 0xE0: // LDH [a8], A
            t_cycles = LD_a8_A(&cpu);
            break;
        case 0x20: // JR NZ, e8
            t_cycles = JR_CC_n16(&cpu, cpu.Z == 0);
            break;
        case 0x21: // LD HL, n16
            t_cycles = LD_HL_n16(&cpu);
            break;
        case 0x11: // LD DE, n16
            t_cycles = LD_DE_n16(&cpu);
            break;
        case 0x01: // LD BC, n16
            t_cycles = LD_BC_n16(&cpu);
            break;
        case 0x1A: // LD A, [DE]
            t_cycles = LD_A_DE(&cpu);
            break;
        case 0x22: // LD [HL+], A
            t_cycles = LD_HLI_A(&cpu);
            break;
        case 0x13: // INC DE
            t_cycles = INC_DE(&cpu);
            break;
        case 0x0B: // DEC BC
            t_cycles = DEC_BC(&cpu);
            break;
        case 0x78:
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.A, cpu.registers.B);
            break;
        case 0xB1: // OR A, C
            t_cycles = OR_A_r8(&cpu, cpu.registers.C);
            break;
        case 0xA7: // AND A, A
            t_cycles = AND_A_r8(&cpu, cpu.registers.A);
            break;
        case 0x3E: // LD A, n8
            t_cycles = LD_r8_n8(&cpu, &cpu.registers.A);
            break;
        case 0x47:
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.B, cpu.registers.A);
            break;
        case 0x0E: // LD C, n8
            t_cycles = LD_r8_n8(&cpu, &cpu.registers.C);
            break;
        case 0x2A: // LD A, [HL+]
            t_cycles = LD_A_HLI(&cpu);
            break;
        case 0x12: // LD [DE], A
            t_cycles = LD_DE_A(&cpu);
            break;
        case 0x1C: // INC E
            t_cycles = INC_r8(&cpu, &cpu.registers.E);
            break;
        case 0x14: // INC D
            t_cycles = INC_r8(&cpu, &cpu.registers.D);
            break;
        case 0x0D: // DEC C
            t_cycles = DEC_r8(&cpu, &cpu.registers.C);
            break;
        case 0x31: // LD SP, n16
            t_cycles = LD_SP_n16(&cpu);
            break;
        case 0xEA: // LD [a16], A
            t_cycles = LD_a16_A(&cpu);
            break;
        case 0xCD: // CALL a16
            t_cycles = CALL_n16(&cpu);
            break;
        case 0x7D: // LD A, L
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.A, cpu.registers.L);
            break;
        case 0x7C: // LD A, H
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.A, cpu.registers.H);
            break;
        case 0xC9: // RET
            t_cycles = RET(&cpu);
            break;
        case 0xE5: // PUSH HL
            t_cycles = PUSH_HL(&cpu);
            break;
        case 0xE1: // POP HL
            t_cycles = POP_HL(&cpu);
            break;
        case 0xF5: // PUSH AF
            t_cycles = PUSH_AF(&cpu);
            break;
        case 0x23: // INC HL
            t_cycles = INC_HL(&cpu);
            break;
        case 0xF1: // POP AF
            t_cycles = POP_AF(&cpu);
            break;
        case 0x18: // JR e8
            t_cycles = JR_n16(&cpu);
            break;
        case 0xC5: // PUSH BC
            t_cycles = PUSH_BC(&cpu);
            break;
        case 0x03: // INC BC
            t_cycles = INC_BC(&cpu);
            break;
        case 0x28: // JR Z, e8
            t_cycles = JR_CC_n16(&cpu, cpu.Z == 1);
            break;
        case 0xC1: // POP BC
            t_cycles = POP_BC(&cpu);
            break;
        case 0xFA: // LD A, [a16]
            t_cycles = LD_A_a16(&cpu);
            break;
        case 0xE6: // AND A, n8
            t_cycles = AND_A_n8(&cpu);
            break;
        case 0xC4: // CALL NZ, a16
            t_cycles = CALL_CC_n16(&cpu, cpu.Z == 0);
            break;
        case 0x06: // LD B, n8
            t_cycles = LD_r8_n8(&cpu, &cpu.registers.B);
            break;
        case 0x77: // LD [HL], A
            t_cycles = LD_HL_r8(&cpu, cpu.registers.A);
            break;
        case 0x2C: // INC L
            t_cycles = INC_r8(&cpu, &cpu.registers.L);
            break;
        case 0x24: // INC H
            t_cycles = INC_r8(&cpu, &cpu.registers.H);
            break;
        case 0x05: // DEC B
            t_cycles = DEC_r8(&cpu, &cpu.registers.B);
            break;
        case 0xA9: // XOR A, C
            t_cycles = XOR_A_r8(&cpu, cpu.registers.C);
            break;
        case 0xC6: // ADD A, n8
            t_cycles = ADD_A_n8(&cpu);
            break;
        case 0x32: // LD [HL-], A
            t_cycles = LD_HLD_A(&cpu);
            break;
        case 0xD6: // SUB A, n8
            t_cycles = SUB_A_n8(&cpu);
            break;
        case 0xB7: // OR A, A
            t_cycles = OR_A_r8(&cpu, cpu.registers.A);
            break;
        case 0xD5: // PUSH DE
            t_cycles = PUSH_DE(&cpu);
            break;
        case 0x46: // LD B, [HL]
            t_cycles = LD_r8_HL(&cpu, &cpu.registers.B);
            break;
        case 0x2D: // DEC L
            t_cycles = DEC_r8(&cpu, &cpu.registers.L);
            break;
        case 0x4E: // LD C, [HL]
            t_cycles = LD_r8_HL(&cpu, &cpu.registers.C);
            break;
        case 0x56: // LD D, [HL]
            t_cycles = LD_r8_HL(&cpu, &cpu.registers.D);
            break;
        case 0xAE: // XOR A, [HL]
            t_cycles = XOR_A_HL(&cpu);
            break;
        case 0x26: // LD H, n8
            t_cycles = LD_r8_n8(&cpu, &cpu.registers.H);
            break;
        case 0xCB: // PREFIX
            t_cycles = exec_CB(&cpu);
            break;
        case 0x1F: // RRA
            t_cycles = RRA(&cpu, &cpu.registers.A);
            break;
        case 0x30: // JR NC, e8
            t_cycles = JR_CC_n16(&cpu, cpu.C == 0);
            break;
        case 0x5F: // LD E, A
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.E, cpu.registers.A);
            break;
        case 0xEE: // XOR A, n8
            t_cycles = XOR_A_n8(&cpu);
            break;
        case 0x79: // LD A, C
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.A, cpu.registers.C);
            break;
        case 0x4F: // LD C, A
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.C, cpu.registers.A);
            break;
        case 0x7A: // LD A, D
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.A, cpu.registers.D);
            break;
        case 0x57: // LD D, A
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.D, cpu.registers.A);
            break;
        case 0x7B: // LD A, E
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.A, cpu.registers.E);
            break;
        case 0x25: // DEC H
            t_cycles = DEC_r8(&cpu, &(cpu.registers.H));
            break;
        case 0x72: // LD [HL], D
            t_cycles = LD_HL_r8(&cpu, cpu.registers.D);
            break;
        case 0x71: // LD [HL], C
            t_cycles = LD_HL_r8(&cpu, cpu.registers.C);
            break;
        case 0x70: // LD [HL], B
            t_cycles = LD_HL_r8(&cpu, cpu.registers.B);
            break;
        case 0xD1: // POP DE
            t_cycles = POP_DE(&cpu);
            break;
        case 0xCE: // ADC A, n8
            t_cycles = ADC_A_n8(&cpu);
            break;
        case 0xD0: // RET NC
            t_cycles = RET_CC(&cpu, cpu.C == 0);
            break;
        case 0xC8: // RET Z
            t_cycles = RET_CC(&cpu, cpu.Z == 1);
            break;
        case 0x3D: // DEC A
            t_cycles = DEC_r8(&cpu, &(cpu.registers.A));
            break;
        case 0xB6: // OR A, [HL]
            t_cycles = OR_A_HL(&cpu);
            break;
        case 0x35: // DEC [HL]
            t_cycles = DEC_HL_a16(&cpu);
            break;
        case 0x6E: // LD L, [HL]
            t_cycles = LD_r8_HL(&cpu, &cpu.registers.L);
            break;
        case 0x6F: // LD L, A
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.L, cpu.registers.A);
            break;
        case 0x29: // ADD HL, HL
            t_cycles = ADD_HL_r16(&cpu, get_HL(&cpu));
            break;
        case 0x1D: // DEC E
            t_cycles = DEC_r8(&cpu, &cpu.registers.E);
            break;
        case 0xE9: // JP HL
            t_cycles = JP_HL(&cpu);
            break;
        case 0x2E: // LD L, n8
            t_cycles = LD_r8_n8(&cpu, &cpu.registers.L);
            break;
        case 0x5D: // LD E, L
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.E, cpu.registers.L);
            break;
        case 0x1B: // DEC DE
            t_cycles = DEC_DE_r16(&cpu);
            break;
        case 0x73: // LD [HL], E
            t_cycles = LD_HL_r8(&cpu, cpu.registers.E);
            break;
        case 0x5E: // LD E, [HL]
            t_cycles = LD_r8_HL(&cpu, &cpu.registers.E);
            break;
        case 0x08: // LD [a16], SP
            t_cycles = LD_a16_SP(&cpu);
            break;
        case 0x66: // LD H, [HL]
            t_cycles = LD_r8_HL(&cpu, &cpu.registers.H);
            break;
        case 0xF9: // LD SP, HL
            t_cycles = LD_SP_HL(&cpu);
            break;
        case 0x62: // LD H, D
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.H, cpu.registers.D);
            break;
        case 0x6B: // LD L, E
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.L, cpu.registers.E);
            break;
        case 0x33: // INC SP
            t_cycles = INC_SP(&cpu);
            break;
        case 0xAD: // XOR A, L
            t_cycles = XOR_A_r8(&cpu, cpu.registers.L);
            break;
        case 0x7E: // LD A, [HL]
            t_cycles = LD_r8_HL(&cpu, &cpu.registers.A);
            break;
        case 0x67: // LD H, A
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.H, cpu.registers.A);
            break;
        case 0xB0: // OR A, B
            t_cycles = OR_A_r8(&cpu, cpu.registers.B);
            break;
        case 0x3B: // DEC SP
            t_cycles = DEC_SP(&cpu);
            break;
        case 0x39: // ADD HL, SP
            t_cycles = ADD_HL_r16(&cpu, cpu.SP);
            break;
        case 0xE8: // ADD SP, e8
            t_cycles = ADD_SP_s8(&cpu);
            break;
        case 0xF8: // LD HL, SP + e8
            t_cycles = LD_HL_SP_s8(&cpu);
            break;
        case 0x3C: // INC A
            t_cycles = INC_r8(&cpu, &cpu.registers.A);
            break;
        case 0xC2: // JP NZ, a16
            t_cycles = JP_CC_n16(&cpu, cpu.Z == 0);
            break;
        case 0xBB: // CP A, E
            t_cycles = CP_A_r8(&cpu, cpu.registers.E);
            break;
        case 0x04: // INC B
            t_cycles = INC_r8(&cpu, &cpu.registers.B);
            break;
        case 0x0C: // INC C
            t_cycles = INC_r8(&cpu, &cpu.registers.C);
            break;
        case 0x27: // DAA
            t_cycles = DAA(&cpu);
            break;
        case 0xBA: // CP A, D
            t_cycles = CP_A_r8(&cpu, cpu.registers.D);
            break;
        case 0xB9: // CP A, C
            t_cycles = CP_A_r8(&cpu, cpu.registers.C);
            break;
        case 0xB8: // CP A, B
            t_cycles = CP_A_r8(&cpu, cpu.registers.B);
            break;
        case 0xFB: // EI
            t_cycles = EI(&cpu);
            break;
        case 0xCA: // JP Z, a16
            t_cycles = JP_CC_n16(&cpu, cpu.Z == 1);
            break;
        case 0x76: // HALT
            t_cycles = HALT(&cpu);
            break;
        case 0xD8: // RET C
            t_cycles = RET_CC(&cpu, cpu.C == 1);
            break;
        case 0x36: // LD [HL], n8
            t_cycles = LD_HL_n8(&cpu);
            break;
        case 0x16: // LD D, n8
            t_cycles = LD_r8_n8(&cpu, &cpu.registers.D);
            break;
        case 0x1E: // LD E, n8
            t_cycles = LD_r8_n8(&cpu, &cpu.registers.E);
            break;
        case 0xF6: // OR A, n8
            t_cycles = OR_A_n8(&cpu);
            break;
        case 0xDE: // SBC A, n8
            t_cycles = SBC_A_n8(&cpu);
            break;
        case 0x2B: // DEC HL
            t_cycles = DEC_HL_r16(&cpu);
            break;
        case 0x09: // ADD HL, BC
            t_cycles = ADD_HL_r16(&cpu, get_BC(&cpu));
            break;
        case 0x19: // ADD HL, DE
            t_cycles = ADD_HL_r16(&cpu, get_DE(&cpu));
            break;
        case 0x40: // LD B, B
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.B, cpu.registers.B);
            break;
        case 0x41: // LD B, C
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.B, cpu.registers.C);
            break;
        case 0x42: // LD B, D
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.B, cpu.registers.D);
            break;
        case 0x43: // LD B, E
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.B, cpu.registers.E);
            break;
        case 0x45: // LD B, L
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.B, cpu.registers.L);
            break;
        case 0x48: // LD C, B
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.C, cpu.registers.B);
            break;
        case 0x49: // LD C, C
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.C, cpu.registers.C);
            break;
        case 0x4A: // LD C, D
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.C, cpu.registers.D);
            break;
        case 0x4B: // LD C, E
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.C, cpu.registers.E);
            break;
        case 0x4C: // LD C, H
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.C, cpu.registers.H);
            break;
        case 0x4D: // LD C, L
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.C, cpu.registers.L);
            break;
        case 0x50: // LD D, B
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.D, cpu.registers.B);
            break;
        case 0x51: // LD D, C
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.D, cpu.registers.C);
            break;
        case 0x52: // LD D, D
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.D, cpu.registers.D);
            break;
        case 0x53: // LD D, E
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.D, cpu.registers.E);
            break;
        case 0x54: // LD D, H
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.D, cpu.registers.H);
            break;
        case 0x55: // LD D, L
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.D, cpu.registers.L);
            break;
        case 0x58: // LD E, B
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.E, cpu.registers.B);
            break;
        case 0x59: // LD E, C
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.E, cpu.registers.C);
            break;
        case 0x5A: // LD E, D
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.E, cpu.registers.D);
            break;
        case 0x5B: // LD E, E
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.E, cpu.registers.E);
            break;
        case 0x5C: // LD E, H
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.E, cpu.registers.H);
            break;
        case 0x60: // LD H, B
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.H, cpu.registers.B);
            break;
        case 0x61: // LD H, C
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.H, cpu.registers.C);
            break;
        case 0x63: // LD H, E
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.H, cpu.registers.E);
            break;
        case 0x64: // LD H, H
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.H, cpu.registers.H);
            break;
        case 0x65: // LD H, L
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.H, cpu.registers.L);
            break;
        case 0x68: // LD L, B
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.L, cpu.registers.B);
            break;
        case 0x69: // LD L, C
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.L, cpu.registers.C);
            break;
        case 0x6A: // LD L, D
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.L, cpu.registers.D);
            break;
        case 0x6C: // LD L, H
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.L, cpu.registers.H);
            break;
        case 0x6D: // LD L, L
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.L, cpu.registers.L);
            break;
        case 0x74: // LD [HL], H
            t_cycles = LD_HL_r8(&cpu, cpu.registers.H);
            break;
        case 0x75: // LD [HL], L
            t_cycles = LD_HL_r8(&cpu, cpu.registers.L);
            break;
        case 0x7F: // LD [HL], L
            t_cycles = LD_r8_r8(&cpu, &cpu.registers.A, cpu.registers.A);
            break;
        case 0xD2: // JP NC, a16
            t_cycles = JP_CC_n16(&cpu, cpu.C == 0);
            break;
        case 0xDA: // JP C, a16
            t_cycles = JP_CC_n16(&cpu, cpu.C == 1);
            break;
        case 0xCC: // CALL Z, a16
            t_cycles = CALL_CC_n16(&cpu, cpu.Z == 1);
            break;
        case 0xD4: // CALL NC, a16
            t_cycles = CALL_CC_n16(&cpu, cpu.C == 0);
            break;
        case 0xDC: // CALL C, a16
            t_cycles = CALL_CC_n16(&cpu, cpu.C == 1);
            break;
        case 0xC0: // RET NZ
            t_cycles = RET_CC(&cpu, cpu.Z == 0);
            break;
        case 0xD9: // RETI
            t_cycles = RETI(&cpu);
            break;
        case 0xC7: // RST $00
            t_cycles = RST_vec(&cpu, 0x0);
            break;
        case 0xCF: // RST $08
            t_cycles = RST_vec(&cpu, 0x08);
            break;
        case 0xD7: // RST $10
            t_cycles = RST_vec(&cpu, 0x10);
            break;
        case 0xDF: // RST $18
            t_cycles = RST_vec(&cpu, 0x18);
            break;
        case 0xE7: // RST $20
            t_cycles = RST_vec(&cpu, 0x20);
            break;
        case 0xEF: // RST $28
            t_cycles = RST_vec(&cpu, 0x28);
            break;
        case 0xF7: // RST $30
            t_cycles = RST_vec(&cpu, 0x30);
            break;
        case 0xFF: // RST $38
            t_cycles = RST_vec(&cpu, 0x38);
            break;
        case 0xF2: // LDH A, [C]
            t_cycles = LD_A_C(&cpu);
            break;
        case 0xE2: // LDH [C], A
            t_cycles = LD_C_A(&cpu);
            break;
        case 0x2F: // CPL
            t_cycles = CPL(&cpu);
            break;
        case 0x37: // SCF
            t_cycles = SCF(&cpu);
            break;
        case 0x3F: // CCF
            t_cycles = CCF(&cpu);
            break;
        case 0xB2: // OR A, D
            t_cycles = OR_A_r8(&cpu, cpu.registers.D);
            break;
        case 0xB3: // OR A, E
            t_cycles = OR_A_r8(&cpu, cpu.registers.E);
            break;
        case 0xB4: // OR A, H
            t_cycles = OR_A_r8(&cpu, cpu.registers.H);
            break;
        case 0xB5: // OR A, L
            t_cycles = OR_A_r8(&cpu, cpu.registers.L);
            break;
        case 0xBC: // OR A, H
            t_cycles = CP_A_r8(&cpu, cpu.registers.H);
            break;
        case 0xBD: // OR A, L
            t_cycles = CP_A_r8(&cpu, cpu.registers.L);
            break;
        case 0xBF: // OR A, A
            t_cycles = CP_A_r8(&cpu, cpu.registers.A);
            break;
        case 0x80: // ADD A, B
            t_cycles = ADD_A_r8(&cpu, cpu.registers.B);
            break;
        case 0x81: // ADD A, C
            t_cycles = ADD_A_r8(&cpu, cpu.registers.C);
            break;
        case 0x82: // ADD A, D
            t_cycles = ADD_A_r8(&cpu, cpu.registers.D);
            break;
        case 0x83: // ADD A, E
            t_cycles = ADD_A_r8(&cpu, cpu.registers.E);
            break;
        case 0x84: // ADD A, H
            t_cycles = ADD_A_r8(&cpu, cpu.registers.H);
            break;
        case 0x85: // ADD A, L
            t_cycles = ADD_A_r8(&cpu, cpu.registers.L);
            break;
        case 0x87: // ADD A, L
            t_cycles = ADD_A_r8(&cpu, cpu.registers.A);
            break;
        case 0x88: // ADC A, B
            t_cycles = ADC_A_r8(&cpu, cpu.registers.B);
            break;
        case 0x89: // ADC A, C
            t_cycles = ADC_A_r8(&cpu, cpu.registers.C);
            break;
        case 0x8A: // ADC A, D
            t_cycles = ADC_A_r8(&cpu, cpu.registers.D);
            break;
        case 0x8B: // ADC A, E
            t_cycles = ADC_A_r8(&cpu, cpu.registers.E);
            break;
        case 0x8C: // ADC A, H
            t_cycles = ADC_A_r8(&cpu, cpu.registers.H);
            break;
        case 0x8D: // ADC A, L
            t_cycles = ADC_A_r8(&cpu, cpu.registers.L);
            break;
        case 0x8F: // ADC A, A
            t_cycles = ADC_A_r8(&cpu, cpu.registers.A);
            break;
        case 0x90: // SUB A, B
            t_cycles = SUB_A_r8(&cpu, cpu.registers.B);
            break;
        case 0x91: // SUB A, C
            t_cycles = SUB_A_r8(&cpu, cpu.registers.C);
            break;
        case 0x92: // SUB A, D
            t_cycles = SUB_A_r8(&cpu, cpu.registers.D);
            break;
        case 0x93: // SUB A, E
            t_cycles = SUB_A_r8(&cpu, cpu.registers.E);
            break;
        case 0x94: // SUB A, H
            t_cycles = SUB_A_r8(&cpu, cpu.registers.H);
            break;
        case 0x95: // SUB A, L
            t_cycles = SUB_A_r8(&cpu, cpu.registers.L);
            break;
        case 0x97: // SUB A, A
            t_cycles = SUB_A_r8(&cpu, cpu.registers.A);
            break;
        case 0x98: // SUB A, B
            t_cycles = SBC_A_r8(&cpu, cpu.registers.B);
            break;
        case 0x99: // SUB A, C
            t_cycles = SBC_A_r8(&cpu, cpu.registers.C);
            break;
        case 0x9A: // SUB A, D
            t_cycles = SBC_A_r8(&cpu, cpu.registers.D);
            break;
        case 0x9B: // SUB A, E
            t_cycles = SBC_A_r8(&cpu, cpu.registers.E);
            break;
        case 0x9C: // SUB A, H
            t_cycles = SBC_A_r8(&cpu, cpu.registers.H);
            break;
        case 0x9D: // SUB A, L
            t_cycles = SBC_A_r8(&cpu, cpu.registers.L);
            break;
        case 0x9F: // SUB A, A
            t_cycles = SBC_A_r8(&cpu, cpu.registers.A);
            break;
        case 0xA0: // AND A, B
            t_cycles = AND_A_r8(&cpu, cpu.registers.B);
            break;
        case 0xA1: // AND A, C
            t_cycles = AND_A_r8(&cpu, cpu.registers.C);
            break;
        case 0xA2: // AND A, D
            t_cycles = AND_A_r8(&cpu, cpu.registers.D);
            break;
        case 0xA3: // AND A, E
            t_cycles = AND_A_r8(&cpu, cpu.registers.E);
            break;
        case 0xA4: // AND A, H
            t_cycles = AND_A_r8(&cpu, cpu.registers.H);
            break;
        case 0xA5: // AND A, L
            t_cycles = AND_A_r8(&cpu, cpu.registers.L);
            break;
        case 0xA8: // XOR A, B
            t_cycles = XOR_A_r8(&cpu, cpu.registers.B);
            break;
        case 0xAA: // XOR A, D
            t_cycles = XOR_A_r8(&cpu, cpu.registers.D);
            break;
        case 0xAB: // XOR A, E
            t_cycles = XOR_A_r8(&cpu, cpu.registers.E);
            break;
        case 0xAC: // XOR A, H
            t_cycles = XOR_A_r8(&cpu, cpu.registers.H);
            break;
        case 0x15: // DEC D
            t_cycles = DEC_r8(&cpu, &cpu.registers.D);
            break;
        case 0x07: // RLCA
            t_cycles = RLCA(&cpu);
            break;
        case 0x17: // RLA
            t_cycles = RLA(&cpu);
            break;
        case 0x0F: // RRCA
            t_cycles = RRCA(&cpu);
            break;
        case 0x0A: // LD A, [BC]
            t_cycles = LD_A_r16(&cpu, get_BC(&cpu));
            break;
        case 0x02: // LD [BC], A
            t_cycles = LD_r16_A(&cpu, get_BC(&cpu));
            break;
        case 0x3A: // LD A, [HL-]
            t_cycles = LD_A_HLD(&cpu);
            break;
        case 0xBE: // CP A, [HL]
            t_cycles = CP_A_HL(&cpu);
            break;
        case 0x86: // ADD A, [HL]
            t_cycles = ADD_A_HL(&cpu);
            break;
        case 0x8E: // ADC A, [HL]
            t_cycles = ADC_A_HL(&cpu);
            break;
        case 0x96: // SUB A, [HL]
            t_cycles = SUB_A_HL(&cpu);
            break;
        case 0x9E: // SBC A, [HL]
            t_cycles = SBC_A_HL(&cpu);
            break;
        case 0xA6: // AND A, [HL]
            t_cycles = AND_A_HL(&cpu);
            break;
        case 0x34: // INC [HL]
            t_cycles = INC_aHL(&cpu);
            break;
        default:
            printf("invalid opcode: %02x\n", opcode);
            printf("PC: %02x\n", cpu.PC);
            exit(1);
            break;
        }
        // cpu.current_t_cycles = t_cycles;
        // update_timer(&cpu, t_cycles);
        update_ppu(&cpu, t_cycles, window, renderer);
        update_IME(&cpu, opcode);
        __uint8_t handled = handle_interrupts(&cpu, file);
        if (!handled)
            print_cpu(&cpu, file);

        // cpu.current_t_cycles = 0;
    }

    // free(buffer);
    fclose(file);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}