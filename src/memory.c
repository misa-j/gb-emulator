#include "memory.h"
#include "cpu.h"

Cartridge *load_cartridge(const char *rom_path)
{
    FILE *f = fopen(rom_path, "rb");
    if (!f)
    {
        perror("Failed to open ROM");
        exit(1);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    size_t rom_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *rom_data = malloc(rom_size);
    if (!rom_data)
    {
        perror("ROM malloc failed");
        fclose(f);
        return NULL;
    }

    fread(rom_data, 1, rom_size, f);
    fclose(f);

    Cartridge *cart = calloc(1, sizeof(Cartridge));
    cart->rom_data = rom_data;
    cart->rom_size = rom_size;

    uint8_t type = rom_data[0x147];
    switch (type)
    {
    case 0x00:
        cart->type = ROM_ONLY;
        break;
    case 0x01:
    case 0x02:
    case 0x03:
        cart->type = MBC1;
        break;
    default:
        printf("Unsupported cartridge type: 0x%02X\n", type);
        exit(1);
    }

    uint8_t ram_size_code = rom_data[0x149];
    switch (ram_size_code)
    {
    case 0x00:
        cart->ram_size = 0;
        cart->ram_data = NULL;
        break;
    case 0x01:
        cart->ram_size = 2 * 1024;
        break;
    case 0x02:
        cart->ram_size = 8 * 1024;
        break;
    case 0x03:
        cart->ram_size = 32 * 1024;
        break;
    case 0x04:
        cart->ram_size = 128 * 1024;
        break;
    case 0x05:
        cart->ram_size = 64 * 1024;
        break;
    default:
        printf("Unknown RAM size code: 0x%02X\n", ram_size_code);
        exit(1);
    }

    if (cart->ram_size > 0)
    {
        cart->ram_data = calloc(1, cart->ram_size);
        if (!cart->ram_data)
        {
            perror("RAM malloc failed");
            exit(1);
        }
    }

    cart->rom_bank = 1; // bank 0 is fixed
    cart->ram_bank = 0;
    cart->ram_enabled = false;
    cart->banking_mode = false;
    return cart;
}

void write_memory(CPU *cpu, uint16_t address, uint8_t value)
{
    Cartridge *cartridge = cpu->cartridge;

    if (address >= 0x0000 && address <= 0x1FFF)
    {
        cartridge->ram_enabled = (value & 0xA) == 0xA;
    }
    else if (address >= 0x2000 && address <= 0x3FFF)
    {
        // TODO: get bitmask based on total ROM size
        if (value == 0)
            cartridge->rom_bank = 1;
        else
            cartridge->rom_bank = value & 0b00000011;
    }
    else if (address >= 0x4000 && address <= 0x5FFF)
    {
        cartridge->ram_bank = value & 0b00000011;
    }
    else if (address >= 0x6000 && address <= 0x7FFF)
    {
        cartridge->banking_mode = value & 0x1;
    }
    else if (address >= 0xA000 && address <= 0xBFFF)
    {
        if (!cartridge->ram_enabled)
            return;
        size_t offset;
        if (cartridge->ram_size <= 8 * 1024)
        {
            offset = (address - 0xA000) % cartridge->ram_size;
        }
        else if (cartridge->banking_mode == true)
        {
            offset = cartridge->ram_bank * 0x2000 + (address - 0xA000);
        }
        else
        {
            offset = address - 0xA000;
        }
        if (offset >= cartridge->ram_size)
        {
            printf("RAM write out of bounds: address=0x%04X, offset=0x%X, ram_size=0x%lX\n",
                   address, offset, (unsigned long)cartridge->ram_size);
        }
        cartridge->ram_data[offset] = value;
    }
    else if (address == BOOT_ROM_ENABLE)
    {
        printf("Write to 0xFF50: value=0x%02X\n", value);
    }
    else if (address == DMA)
    {
        cpu->dma_cycles = 160;
        cpu->memory[address] = value;
        update_dma(cpu);
    }
    else if (address == IO_JOYPAD)
    {
        cpu->memory[address] = (value | 0x0F);
    }
    else if (address == 0xFF04)
    {
        cpu->div_cycles = 0;
        cpu->memory[0xFF04] = 0;
    }
    else
    {
        cpu->memory[address] = value;
    }
}

__uint8_t read_memory(CPU *cpu, uint16_t address)
{
    Cartridge *cart = cpu->cartridge;

    if (address < 0x4000)
    {
        return cart->rom_data[address];
    }
    else if (address >= 0x4000 && address < 0x8000)
    {
        if (cart->type == MBC1)
        {
            size_t offset = cart->rom_bank * 0x4000 + (address - 0x4000);
            return cart->rom_data[offset % cart->rom_size];
        }
        else
        {
            return cart->rom_data[address]; // ROM only
        }
    }
    else if (address >= 0xA000 && address < 0xC000)
    {
        if (cart->ram_data == NULL || !cart->ram_enabled)
            return 0xFF;

        if (cart->type == MBC1)
        {
            size_t offset = cart->ram_bank * 0x2000 + (address - 0xA000);
            return cart->ram_data[offset % cart->ram_size];
        }
        else
        {
            return cart->ram_data[address - 0xA000]; // ROM only
        }
    }
    else
    {
        return cpu->memory[address];
    }
}

__uint8_t read_opcode(CPU *cpu)
{
    return read_memory(cpu, cpu->PC++);
}