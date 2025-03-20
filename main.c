#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

__uint8_t *read_file(const char *filename, __uint8_t *buffer) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror("Failed to open file");
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long filesize = ftell(file);
    rewind(file);

    size_t bytesRead = fread(buffer, 1, filesize, file);
    if (bytesRead != filesize) {
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
    __uint16_t PC;
    __uint16_t SP;
    Registers registers;
    __uint8_t Z; // Zero Flag
    __uint8_t N; // Subtract Flag
    __uint8_t H; // Half Carry Flag
    __uint8_t C; // Carry Flag
    __uint8_t IME; // IME flag
    bool ime_delay;
} CPU;

typedef struct
{
    __uint8_t memory[0xFFFF];
} Memory;

__uint8_t get_opcode(__uint8_t* buffer, CPU *cpu) {
    return buffer[cpu->PC];
}

__uint8_t get_F(CPU *cpu) {
    return (cpu->Z << 7) | (cpu->N << 6) | (cpu->H << 5) | (cpu->C << 4);
}


void print_cpu(CPU *cpu, Memory *memory, FILE *file) {
    fprintf(file, "A:%02X F:%02X B:%02X C:%02X D:%02X E:%02X H:%02X L:%02X SP:%04X PC:%04X PCMEM:%02X,%02X,%02X,%02X\n",
        cpu->registers.A,get_F(cpu),cpu->registers.B,cpu->registers.C,cpu->registers.D,cpu->registers.E,cpu->registers.H,
        cpu->registers.L,cpu->SP,cpu->PC,memory->memory[cpu->PC],memory->memory[cpu->PC + 1],memory->memory[cpu->PC + 2],memory->memory[cpu->PC+ 3]
    );
}

__int16_t sign_extend(__uint8_t value) {
    return (__int16_t)((__int8_t) value);
}

int check_underflow_sub(__uint8_t a, __uint8_t b) {
    return (a < b);
}

__int16_t get_HL(CPU *cpu) {
    __uint16_t H = cpu->registers.H;
    __uint8_t L = cpu->registers.L;
    
    return (H << 8) | L;
}

__int16_t get_BC(CPU *cpu) {
    __uint16_t B = cpu->registers.B;
    __uint8_t C = cpu->registers.C;
    
    return (B << 8) | C;
}

__int16_t get_DE(CPU *cpu) {
    __uint16_t D = cpu->registers.D;
    __uint8_t E = cpu->registers.E;
   
    return (D << 8) | E;
}

__int16_t get_SP(CPU *cpu, Memory *memory) {
    __uint8_t v1 = memory->memory[cpu->SP++];
    __uint16_t v2 = memory->memory[cpu->SP++];

    return (v2 << 8) | v1;
}

__int16_t get_a16(CPU *cpu, Memory *memory) {
    __uint8_t v1 = memory->memory[cpu->PC++];
    __uint8_t v2 = memory->memory[cpu->PC++];

    return (v2 << 8) | v1;
}

void RR_r8(CPU *cpu, __uint8_t *r8) {
    __uint8_t val = *r8;
    __uint8_t C = val & 1u;
    val >>= 1;
    val |= (cpu->C << 7);

    *r8 = val;
    cpu->Z = val == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = C;
}

void DEC_r8(CPU *cpu, __uint8_t *r8) {
    __uint8_t val = *r8;
    cpu->H = (val & 0xF) == 0;
    val -= 1;
    cpu->Z = val == 0;
    cpu->N = 1;
    *r8 = val;
}

void INC_r8(CPU *cpu, __uint8_t *r8) {
    __uint8_t val = *r8;
    cpu->H = (val & 0x0F) + 1 > 0x0F;
    *r8 = ++val;
    cpu->Z = val == 0;
    cpu->N = 0;
}

void store_HL(CPU *cpu, __uint16_t val) {
    cpu->registers.H = (val & 0xFF00) >> 8;
    cpu->registers.L = (val & 0x00FF);
}

void store_DE(CPU *cpu, __uint16_t val) {
    cpu->registers.D = (val & 0xFF00) >> 8;
    cpu->registers.E = (val & 0x00FF);
}

void store_BC(CPU *cpu, __uint16_t val) {
    cpu->registers.B = (val & 0xFF00) >> 8;
    cpu->registers.C = (val & 0x00FF);
}

void DEC_HL(CPU *cpu, Memory *memory) {
    __uint16_t HL = get_HL(cpu);
    __uint8_t val = memory->memory[HL];
    cpu->H = (val & 0xF) == 0;
    val -= 1;
    cpu->Z = val == 0;
    cpu->N = 1;
    memory->memory[HL] = val;
}

void DEC_DE_r16(CPU *cpu) {
    __uint16_t DE = get_DE(cpu);
    DE--;
    store_DE(cpu, DE);
}

void DAA(CPU *cpu) {
    __uint8_t adj = 0;
    if(cpu->N) {
        if(cpu->H) adj += 0x6;
        if(cpu->C) adj += 0x60;
        cpu->registers.A -= adj;
    } else {
        if(cpu->H || ((cpu->registers.A & 0xF) > 0x9)) adj += 0x6;
        if(cpu->C || (cpu->registers.A > 0x99)) {
            adj += 0x60;
            cpu->C = 1;
        }
        cpu->registers.A += adj;
    }

    cpu->Z = cpu->registers.A == 0;
    cpu->H = 0;
}

void LD_r8_HL(CPU *cpu, Memory *memory, __uint8_t *r8) {
    __uint16_t HL = get_HL(cpu);
   *r8 = memory->memory[HL];
}

void LD_SP_HL(CPU *cpu) {
    __uint16_t HL = get_HL(cpu);
    cpu->SP = HL;
}

void LD_a16_SP(CPU *cpu, Memory *memory) {
    __uint8_t a1 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    __uint16_t a2 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    __uint16_t a16 = a1 | (a2 << 8);
    memory->memory[a16] = cpu->SP & 0x00FF;
    memory->memory[a16 + 1] = (cpu->SP & 0xFF00) >> 8;
}

void LD_r8_n8(CPU *cpu, Memory *memory, __uint8_t *r8) {
    __uint8_t n8 = get_opcode(memory->memory, cpu);
    cpu->PC++;
   *r8 = n8;
}

void XOR_A_r8(CPU *cpu, __uint8_t val) {
    cpu->registers.A ^= val;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = 0;
}

void OR_A(CPU *cpu, __uint8_t val) {
    cpu->registers.A |= val;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 0;
    cpu->H = 0;
    cpu->C = 0;
}

void LD_HL_r8(CPU *cpu, Memory *memory, __uint8_t r8) {
    __uint16_t HL = get_HL(cpu);
    memory->memory[HL] = r8;
}

void POP_DE(CPU *cpu, Memory *memory) {
    cpu->registers.E = memory->memory[cpu->SP++];
    cpu->registers.D = memory->memory[cpu->SP++];
}

void ADC_A(CPU *cpu, __uint8_t n8) {
    __uint8_t val = n8 + cpu->C;
    
    cpu->H = (cpu->registers.A & 0xF) + (val & 0xF) > 0xF;
    cpu->C = __builtin_add_overflow(cpu->registers.A, val, &(cpu->C));
    cpu->registers.A += val;
    cpu->Z = cpu->registers.A == 0;
    cpu->N = 0;
}

void ADD_HL_r16(CPU *cpu, __uint16_t r16) {
    __uint16_t HL = get_HL(cpu);
    __uint16_t result = HL + r16;
    cpu->H = (HL & 0xFFF) + (r16 & 0xFFF) > 0xFFF;
    cpu->C = result < HL; //__builtin_add_overflow(HL, r16, &(cpu->C));
    HL = result;
    cpu->N = 0;
    store_HL(cpu, HL);
}

void ADD_SP_s8(CPU *cpu, Memory *memory) {
    __uint8_t n8 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    __uint16_t s8 = sign_extend(n8);
    __uint16_t result = cpu->SP + s8;
    cpu->H = (cpu->SP & 0xF) + (s8 & 0xF) > 0xF;
    cpu->C = (cpu->SP & 0xFF) + (s8 & 0xFF) > 0xFF;
    cpu->Z = 0;
    cpu->N = 0;
    cpu->SP = result;
}

void LD_HL_SP_s8(CPU *cpu, Memory *memory) {
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
}

void CP_A_n8(CPU *cpu, Memory *memory) {
    __uint8_t n8 = get_opcode(memory->memory, cpu);
    cpu->PC++;
    __uint8_t result = cpu->registers.A - n8;

    cpu->Z = result == 0;
    cpu->N = 1;
    cpu->H = (cpu->registers.A & 0xF) < (n8 & 0xF);
    cpu->C = cpu->registers.A < n8;
}

void CP_A_r8(CPU *cpu, __uint8_t r8) {
    __uint8_t result = cpu->registers.A - r8;

    cpu->Z = result == 0;
    cpu->N = 1;
    cpu->H = (cpu->registers.A & 0xF) < (r8 & 0xF);
    cpu->C = cpu->registers.A < r8;
}

void exec_CB(CPU *cpu, Memory *memory) {
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

void update_IME(CPU *cpu) {
    if (cpu->ime_delay) {
        cpu->IME = 1;
        cpu->ime_delay = 0;
    }
}

int main() {
    FILE *file = fopen("output.txt", "w");

    if (file == NULL) {
        perror("Error opening file");
        return 1;
    }
    
    const char *filename = "./gb-test-roms-master/cpu_instrs/individual/03-op sp,hl.gb";
    Memory memory = {0};
    __uint8_t* buffer = read_file(filename, memory.memory);
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
    while (cnt < 10000000)
    {
        cnt++;
        __uint8_t opcode = get_opcode(buffer, &cpu);
        
        cpu.PC++;
        switch (opcode) {
            case 0x00:
                break;
            case 0xF0:
                __uint8_t a8 = get_opcode(buffer, &cpu);
                cpu.PC++;
                cpu.registers.A = memory.memory[0xFF00 + a8];
                break;
            case 0xF3:
                // TODO: implement opcode
                break;
            case 0x44:
                cpu.registers.B = cpu.registers.H;
                break;
            case 0xFE:
                CP_A_n8(&cpu, &memory);
                break;
            case 0x38:
                __uint8_t address = get_opcode(buffer, &cpu);
                cpu.PC++;
                if(cpu.C) {
                    cpu.PC += sign_extend(address);
                }
                break;
            case 0xC3:
                __uint8_t a1 = get_opcode(buffer, &cpu);
                cpu.PC++;
                __uint16_t a2 = (__uint16_t) get_opcode(buffer, &cpu);
                cpu.PC++;
                __uint16_t jmp_address = (a2 << 8) | a1;
                cpu.PC = jmp_address;
                break;
            case 0xAF:
                cpu.registers.A = 0;
                cpu.Z = 1;
                cpu.N = 0;
                cpu.H = 0;
                cpu.C = 0;
                break;
            case 0xE0:
                address = get_opcode(buffer, &cpu);
                cpu.PC++;
                memory.memory[0xFF00 + address] = cpu.registers.A;
                break;

            case 0x20:
                address = get_opcode(buffer, &cpu);
                cpu.PC++;
                if(cpu.Z == 0) {
                    cpu.PC += sign_extend(address);
                }
                break;

            case 0x21:
                __uint8_t v1 = get_opcode(buffer, &cpu);
                cpu.PC++;
                __uint8_t v2 = get_opcode(buffer, &cpu);
                cpu.PC++;
                cpu.registers.L = v1;
                cpu.registers.H = v2;
                break;
            case 0x11:
                v1 = get_opcode(buffer, &cpu);
                cpu.PC++;
                v2 = get_opcode(buffer, &cpu);
                cpu.PC++;
                cpu.registers.E = v1;
                cpu.registers.D = v2;
                break;
            case 0x01:
                v1 = get_opcode(buffer, &cpu);
                cpu.PC++;
                v2 = get_opcode(buffer, &cpu);
                cpu.PC++;
                cpu.registers.C = v1;
                cpu.registers.B = v2;
                break;
            case 0x1A:
                __uint16_t de = cpu.registers.E  | (cpu.registers.D << 8);
                cpu.registers.A = memory.memory[de];
                break;
            case 0x22:
                __uint16_t hl = cpu.registers.L  | (cpu.registers.H << 8);
                memory.memory[hl] = cpu.registers.A;
                hl++;

                cpu.registers.H = (hl & 0xFF00) >> 8;
                cpu.registers.L = (hl & 0x00FF);

                
                break;
            case 0x13:
                de = cpu.registers.E  | (cpu.registers.D << 8);
                de++;
                cpu.registers.D = (de & 0xFF00) >> 8;
                cpu.registers.E = (de & 0x00FF);

                break;
            case 0x0B:
                __uint16_t bc = cpu.registers.C | (cpu.registers.B << 8);
                bc--;
                cpu.registers.B = (bc & 0xFF00) >> 8;
                cpu.registers.C = (bc & 0x00FF);
                
                break;
            case 0x78:
                cpu.registers.A = cpu.registers.B;
                break;
            case 0xB1:
                cpu.registers.A |= cpu.registers.C;
                cpu.Z = cpu.registers.A == 0;
                cpu.N = 0;
                cpu.H = 0;
                cpu.C = 0;
                break;
            case 0xA7:
                // TODO: nop
                cpu.registers.A &= cpu.registers.A;
                break;
            case 0x3E:
                __uint8_t n8 = get_opcode(buffer, &cpu);
                cpu.PC++;
                cpu.registers.A = n8;
                break;
            case 0x47:
                cpu.registers.B = cpu.registers.A;
                break;
            case 0x0E:
                n8 = get_opcode(buffer, &cpu);
                cpu.PC++;
                cpu.registers.C = n8;
                break;
            case 0x2A:
                __uint16_t HL = get_HL(&cpu);
                cpu.registers.A = memory.memory[HL];
                HL++;

                cpu.registers.H = (HL & 0xFF00) >> 8;
                cpu.registers.L = (HL & 0x00FF);
                break;
            case 0x12:
                __uint16_t DE = get_DE(&cpu);
                memory.memory[DE] = cpu.registers.A;
                break;
            case 0x1C:
                cpu.H = (cpu.registers.E & 0xF) + 1 > 0xF;
                cpu.registers.E += 1;
                cpu.Z = cpu.registers.E == 0;
                cpu.N = 0;
                break;
            case 0x14:
                cpu.H = (cpu.registers.D & 0xF) + 1 > 0xF;
                cpu.registers.D += 1;
                cpu.Z = cpu.registers.D == 0;
                cpu.N = 0;
                break;
            case 0x0D:
                cpu.H = (cpu.registers.C & 0xF) == 0;
                cpu.registers.C -= 1;
                cpu.Z = cpu.registers.C == 0;
                cpu.N = 1;
                break;
            case 0x31:
                v1 = get_opcode(buffer, &cpu);
                cpu.PC++;
                v2 = get_opcode(buffer, &cpu);
                cpu.PC++;
                cpu.SP = v1 | (v2 << 8);
                break;
            case 0xEA:
                v1 = get_opcode(buffer, &cpu);
                cpu.PC++;
                v2 = get_opcode(buffer, &cpu);
                cpu.PC++;
                __uint16_t a16 = v1 | (v2 << 8);
                memory.memory[a16] = cpu.registers.A;
                break;
            case 0xCD:
                v1 = get_opcode(buffer, &cpu);
                cpu.PC++;
                v2 = get_opcode(buffer, &cpu);
                cpu.PC++;
                a16 = v1 | (v2 << 8);

                memory.memory[--cpu.SP] = (cpu.PC & 0xFF00) >> 8;
                memory.memory[--cpu.SP] = cpu.PC & 0xFF;

                cpu.PC = a16;
                
                break;
            case 0x7D:
                cpu.registers.A = cpu.registers.L;
                break;
            case 0x7C:
                cpu.registers.A = cpu.registers.H;
                break;
            case 0xC9: // RET
                v1 = memory.memory[cpu.SP++];
                v2 = memory.memory[cpu.SP++];
                a16 = v1 | (v2 << 8);
                cpu.PC = a16;
                break;
            case 0xE5:
                memory.memory[--cpu.SP] = cpu.registers.H;
                memory.memory[--cpu.SP] = cpu.registers.L;
                break;
            case 0xE1:
                cpu.registers.L = memory.memory[cpu.SP++];
                cpu.registers.H = memory.memory[cpu.SP++];
                break;
            case 0xF5:

                memory.memory[--cpu.SP] = cpu.registers.A;
                memory.memory[--cpu.SP] = get_F(&cpu);
                
                break;
            case 0x23: // INC HL
                HL = get_HL(&cpu);
                HL++;
                cpu.registers.H = (HL & 0xFF00) >> 8;
                cpu.registers.L = (HL & 0x00FF);

                break;
            case 0xF1: // POP AF
                cpu.registers.F = memory.memory[cpu.SP++];
                cpu.registers.A = memory.memory[cpu.SP++];
                cpu.Z = (cpu.registers.F & (1u << 7)) ? 1 : 0;
                cpu.N = (cpu.registers.F & (1u << 6)) ? 1 : 0;
                cpu.H = (cpu.registers.F & (1u << 5)) ? 1 : 0;
                cpu.C = (cpu.registers.F & (1u << 4)) ? 1 : 0;
                break;
            case 0x18: // JR e8
                n8 = get_opcode(buffer, &cpu);
                cpu.PC++;
                cpu.PC += sign_extend(n8);
                break;
            case 0xC5: // PUSH BC
                memory.memory[--cpu.SP] = cpu.registers.B;
                memory.memory[--cpu.SP] = cpu.registers.C;
                break;
            case 0x03: // INC BC
                __uint16_t BC = get_BC(&cpu);
                BC++;
                cpu.registers.B = (BC & 0xFF00) >> 8;
                cpu.registers.C = (BC & 0x00FF);
                break;
            case 0x28: // JR Z, e8
                __uint8_t e8 = get_opcode(buffer, &cpu);
                cpu.PC++;
                if(cpu.Z == 1) {
                    cpu.PC += sign_extend(e8);
                }
                break;
            case 0xC1: // POP BC
                cpu.registers.C = memory.memory[cpu.SP++];
                cpu.registers.B = memory.memory[cpu.SP++];
                break;
            case 0xFA: // LD A, [a16]
                v1 = get_opcode(buffer, &cpu);
                cpu.PC++;
                v2 = get_opcode(buffer, &cpu);
                cpu.PC++;
                a16 = v1 | (v2 << 8);

                cpu.registers.A = memory.memory[a16];
                break;
            case 0xE6: // AND A, n8
                n8 = get_opcode(buffer, &cpu);
                cpu.PC++;
                cpu.registers.A &= n8;
                cpu.Z = cpu.registers.A == 0;
                cpu.N = 0;
                cpu.H = 1;
                cpu.C = 0;
                break;
            case 0xC4: // CALL NZ, a16
                v1 = get_opcode(buffer, &cpu);
                cpu.PC++;
                v2 = get_opcode(buffer, &cpu);
                cpu.PC++;
                a16 = v1 | (v2 << 8);

                if(cpu.Z == 0) {
                    memory.memory[--cpu.SP] = (cpu.PC & 0xFF00) >> 8;
                    memory.memory[--cpu.SP] = cpu.PC & 0xFF;
    
                    cpu.PC = a16;
                }

                break;
            case 0x06: // LD B, n8
                n8 = get_opcode(buffer, &cpu);
                cpu.PC++;
                cpu.registers.B = n8;
                break;
            case 0x77: // LD [HL], A
                HL =  get_HL(&cpu);
                memory.memory[HL] = cpu.registers.A;
                break;
            case 0x2C: // INC L
                cpu.H = (cpu.registers.L & 0x0F) + 1 > 0x0F;
                cpu.registers.L += 1;
                cpu.Z = cpu.registers.L == 0;
                cpu.N = 0;
                break;
            case 0x24: // INC H
                cpu.H = (cpu.registers.H & 0x0F) + 1 > 0x0F;
                cpu.registers.H += 1;
                cpu.Z = cpu.registers.H == 0;
                cpu.N = 0;
                break;
            case 0x05: // DEC B
                DEC_r8(&cpu, &cpu.registers.B);
                break;
            case 0xA9: // XOR A, C
                cpu.registers.A ^= cpu.registers.C;
                cpu.Z = cpu.registers.A == 0;
                cpu.N = 0;
                cpu.H = 0;
                cpu.C = 0;
                break;
            case 0xC6: // ADD A, n8
                __uint8_t d8 =  get_opcode(buffer, &cpu);
                cpu.PC++;
                cpu.H = (cpu.registers.A & 0xF) + (d8 & 0xF) > 0xF;
                cpu.C = __builtin_add_overflow(cpu.registers.A, d8, &cpu.C);
                cpu.registers.A += d8;
                cpu.Z = cpu.registers.A == 0;
                cpu.N = 0;
                break;
            case 0x32: // LD [HL-], A
                HL = get_HL(&cpu);
                memory.memory[HL] = cpu.registers.A;
                HL--;
                cpu.registers.H = (HL & 0xFF00) >> 8;
                cpu.registers.L = (HL & 0x00FF);
                break;
            case 0xD6: // SUB A, n8
                n8 = get_opcode(buffer, &cpu);
                cpu.PC++;
                //cpu.H = (cpu.registers.A & 0xF) == 0;
                cpu.H = (cpu.registers.A & 0x0F) < (n8 & 0x0F);
                cpu.C = cpu.registers.A < n8;
                cpu.registers.A -= n8;
                cpu.Z = cpu.registers.A == 0;
                cpu.N = 1;
                break;
            case 0xB7: // OR A, A
                cpu.Z = cpu.registers.A == 0;
                cpu.N = 0;
                cpu.H = 0;
                cpu.C = 0;
                break;
            case 0xD5: // PUSH DE
                memory.memory[--cpu.SP] = cpu.registers.D;
                memory.memory[--cpu.SP] = cpu.registers.E;
                break;
            case 0x46: // LD B, [HL]
                HL = get_HL(&cpu);
                cpu.registers.B = memory.memory[HL];
                break;
            case 0x2D: // DEC L
                cpu.H = (cpu.registers.L & 0xF) == 0;
                cpu.registers.L -= 1;
                cpu.Z = cpu.registers.L == 0;
                cpu.N = 1;
                break;
            case 0x4E: // LD C, [HL]
                HL = get_HL(&cpu);
                cpu.registers.C = memory.memory[HL];
                cpu.N = 1;
                break;
            case 0x56: // LD D, [HL]
                LD_r8_HL(&cpu, &memory, &cpu.registers.D);
                break;
            case 0xAE: // XOR A, [HL]
                HL = get_HL(&cpu);
                cpu.registers.A ^= memory.memory[HL];
                cpu.Z = cpu.registers.A == 0;
                cpu.N = 0;
                cpu.H = 0;
                cpu.C = 0;
                break;
            case 0x26: // LD H, n8
                n8 = get_opcode(buffer, &cpu);
                cpu.PC++;
                cpu.registers.H = n8;
                break;
            case 0xCB: // PREFIX
                exec_CB(&cpu, &memory);
                break;
            case 0x1F: // RRA
                RR_r8(&cpu, &(cpu.registers.A));
                cpu.Z = 0;
                break;
            case 0x30: // JR NC, e8
                e8 = get_opcode(buffer, &cpu);
                cpu.PC++;
                if(cpu.C == 0) {
                    cpu.PC += sign_extend(e8);
                }
                break;
            case 0x5F: // LD E, A
                cpu.registers.E = cpu.registers.A;
                break;
            case 0xEE: // XOR A, n8
                n8 = get_opcode(buffer, &cpu);
                cpu.PC++;
                cpu.registers.A ^= n8;
                cpu.Z = cpu.registers.A == 0;
                cpu.N = 0;
                cpu.H = 0;
                cpu.C = 0;
                break;
            case 0x79: // LD A, C
                cpu.registers.A = cpu.registers.C;
                break;
            case 0x4F: // LD C, A
                cpu.registers.C = cpu.registers.A;
                break;
            case 0x7A: // LD A, D
                cpu.registers.A = cpu.registers.D;
                break;
            case 0x57: // LD D, A
                cpu.registers.D = cpu.registers.A;
                break;
            case 0x7B: // LD A, E
                cpu.registers.A = cpu.registers.E;
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
                n8 = get_opcode(buffer, &cpu);
                cpu.PC++;
                ADC_A(&cpu, n8);
                break;
            case 0xD0:
                if(cpu.C == 0) {
                    cpu.PC = get_SP(&cpu, &memory);
                }
                break;
            case 0xC8:
                if(cpu.Z == 1) {
                    cpu.PC = get_SP(&cpu, &memory);
                }
                break;
            case 0x3D:
                DEC_r8(&cpu, &(cpu.registers.A));
                break;
            case 0xB6:
                HL = get_HL(&cpu);
                OR_A(&cpu, memory.memory[HL]);
                break;
            case 0x35:
                DEC_HL(&cpu, &memory);
                break;
            case 0x6E: // LD L, [HL]
                LD_r8_HL(&cpu, &memory, &cpu.registers.L);
                break;
            case 0x6F: // LD L, A
                cpu.registers.L = cpu.registers.A;
                break;
            case 0x29:
                ADD_HL_r16(&cpu, get_HL(&cpu));
                break;
            case 0x1D: // DEC E
                DEC_r8(&cpu, &cpu.registers.E);
                break;
            case 0xE9: // JP HL
                cpu.PC = get_HL(&cpu);
                break;
            case 0x2E: // LD L, n8
                LD_r8_n8(&cpu, &memory, &cpu.registers.L);
                break;
            case 0x5D: // LD E, L
                cpu.registers.E = cpu.registers.L;
                break;
            case 0x1B:
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
                cpu.registers.H = cpu.registers.D;
                break;
            case 0x6B: // LD L, E
                cpu.registers.L = cpu.registers.E;
                break;
            case 0x33: // INC SP
                cpu.SP++;
                break;
            case 0xAD: // XOR A, L
                XOR_A_r8(&cpu, cpu.registers.L);
                break;
            case 0x7E: // LD A, [HL]
                LD_r8_HL(&cpu, &memory, &cpu.registers.A);
                break;
            case 0x67: // LD H, A
                cpu.registers.H = cpu.registers.A;
                break;
            case 0xB0: // OR A, B
                OR_A(&cpu, cpu.registers.B);
                break;
            case 0x3B: // DEC SP
                cpu.SP--;
                break;
            case 0x39: // ADD HL, SP
                ADD_HL_r16(&cpu, cpu.SP);
                break;
            case 0xE8: // ADD SP, e8
                ADD_SP_s8(&cpu, &memory);
                break;
            case 0xF8:
                LD_HL_SP_s8(&cpu, &memory);
                break;
            case 0x3C: // INC A
                INC_r8(&cpu, &cpu.registers.A);
                break;
            case 0xC2:
                a16 = get_a16(&cpu, &memory);
                if(cpu.Z == 0) {
                    cpu.PC = a16;
                }
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
            case 0x27: // INC C
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
                cpu.ime_delay = 1;
                break;
            default:
                printf("invalid opcode: %02x\n", opcode);
                printf("PC: %02x\n", cpu.PC);
                exit(1);
                break;
            
            if(opcode != 0xFB && cpu.ime_delay) {
                cpu.IME = 1;
                cpu.ime_delay = 0;
            }
        }
        print_cpu(&cpu, &memory, file);
    }
    
    free(buffer);
    fclose(file);  

    return 0;
}