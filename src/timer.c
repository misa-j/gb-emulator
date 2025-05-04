#include "timer.h"
#include "ppu.h"

void update_timer(CPU *cpu, __uint8_t t_cycles)
{
    cpu->div_cycles += t_cycles;
    cpu->memory[DIV] = cpu->div_cycles >> 8;

    if (cpu->memory[TAC] & 0x04)
    {
        __uint16_t freq = 0;

        switch (cpu->memory[TAC] & 0x03)
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
            cpu->memory[TIMA]++;
            cpu->tima_cycles -= freq;
            if (cpu->memory[TIMA] == 0)
            {
                cpu->memory[TIMA] = cpu->memory[TMA];
                cpu->memory[IF] |= 0x04;
            }
        }
    }
    update_ppu(cpu, t_cycles);
}
