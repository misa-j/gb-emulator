#pragma once
#include <SDL2/SDL.h>
#include <stdlib.h>
#include <stdint.h>
#include "cpu.h"
#define SELECT_DPAD 0x10
#define SELECT_BUTTONS 0x20
#define SELECT_NONE 0x30
#define IO_JOYPAD 0xFF00

void update_joypad(CPU *cpu);