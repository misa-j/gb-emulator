#pragma once
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <SDL2/SDL.h>
#include "ppu.h"
#define VBLANK_ADDR 0x0040
#define LCD_STAT_ADDR 0x0048
#define TIMER_ADDR 0x0050
#define SERIAL_ADDR 0x0058
#define JOYPAD_ADDR 0x0060
#define IF 0xFF0F
#define IE 0xFFFF
#define IO_JOYPAD 0xFF00
#define LY 0xFF44

typedef struct PPU PPU;
typedef struct Cartridge Cartridge;
typedef struct Fetcher Fetcher;

typedef struct Registers
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

typedef struct CPU
{
    __uint16_t div_cycles;
    __uint16_t tima_cycles;
    // __uint8_t current_t_cycles;
    __uint8_t halted;
    __uint8_t halt_bug;
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
    Cartridge *cartridge;
    // TODO: remove this
    SDL_Window *window;
    SDL_Renderer *renderer;
    Fetcher *fetcher;
    __uint8_t dma_cycles;
    bool vblank;
    bool hblank;
    bool oam_scan;
    bool pixel_transfer;
} CPU;

void CPU_init(CPU *cpu, Fetcher *fetcher, const char *filename, SDL_Window *window, SDL_Renderer *renderer, FILE *file);
void CPU_start(CPU *cpu, SDL_Event *e, FILE *file);