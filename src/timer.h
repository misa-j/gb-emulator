#pragma once
#include "cpu.h"
#define DIV 0xFF04
#define TIMA 0xFF05
#define TMA 0xFF06
#define TAC 0xFF07

void update_timer(CPU *cpu, __uint8_t t_cycles);