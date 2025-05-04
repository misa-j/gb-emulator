#include "timer.h"
#include "ppu.h"

void update_timer(CPU *cpu, __uint8_t t_cycles)
{
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
                cpu->memory[0xFF05] = cpu->memory[0xFF06];
                cpu->memory[0xFF0F] |= 0x04;
            }
        }
    }
    update_ppu(cpu, t_cycles);
}
