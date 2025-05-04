#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 720
#define DMA 0xFF46
#define OAM_ADDR 0xFE00
#define OAM_ADDR_END 0xFE9F
#define LCDC 0xFF40
#define SCY 0xFF42
#define SCX 0xFF43
#define WY 0xFF4A
#define WX 0xFF4B
#define STAT 0xFF41
#define LYC 0xFF45
#define BGP 0xFF47
#define OBP0 0xFF48
#define OBP1 0xFF49

typedef struct CPU CPU;

typedef struct Pixel
{
    __uint8_t color_number;
    __uint8_t palette;
    __uint8_t background_priority;
    __uint8_t x;
    __uint8_t y;
} Pixel;

typedef struct SpriteAttributes
{
    __uint8_t x;
    __uint8_t y;
    __uint8_t tile_number;
    __uint8_t flags;
    bool fetched;
} SpriteAttributes;

typedef struct PixelQueue
{
    Pixel queue[8];
    __uint8_t size;
} PixelQueue;

typedef struct SpriteBuffer
{
    SpriteAttributes buffer[10];
    __uint8_t size;
} SpriteBuffer;

typedef struct PPU
{
    __uint32_t cycles;
    __uint32_t line_cycles;
    __uint8_t frame[144 * 160];
    __uint8_t prev_ly;
    PixelQueue bg_queue;
    PixelQueue sprite_queue;
    SpriteBuffer sprite_buffer;
} PPU;

typedef struct Fetcher
{
    __uint8_t x_offset;
    __uint8_t curr_p;
    __uint8_t window_line_counter;
    bool fetching_window_pixels;
} Fetcher;

SDL_Window *SDL_Window_init();
SDL_Renderer *SDL_Renderer_init(SDL_Window *window);
void update_dma(CPU *cpu);
void update_ppu(CPU *cpu, __uint8_t t_cycles);