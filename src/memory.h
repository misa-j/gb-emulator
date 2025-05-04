#pragma once
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#define DMA 0xFF46
#define BOOT_ROM_ENABLE 0xFF50

typedef struct CPU CPU;

typedef enum
{
    ROM_ONLY,
    MBC1,
} CartridgeType;

typedef struct Cartridge
{
    __uint8_t *rom_data;
    size_t rom_size;

    __uint8_t *ram_data;
    size_t ram_size;

    CartridgeType type;

    __uint8_t rom_bank;
    __uint8_t ram_bank;
    bool ram_enabled;
    bool banking_mode; // false = ROM, true = RAM (MBC1)

} Cartridge;

__uint8_t read_memory(CPU *cpu, uint16_t address);
void write_memory(CPU *cpu, uint16_t address, uint8_t value);
__uint8_t read_opcode(CPU *cpu);
Cartridge *load_cartridge(const char *rom_path);