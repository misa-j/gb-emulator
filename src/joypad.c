#include "joypad.h"

void unset_bit(__uint8_t *byte, __uint8_t n)
{
    __uint8_t mask = ~(1u << n);
    *byte &= mask;
}

void set_bit(__uint8_t *byte, __uint8_t n)
{
    __uint8_t mask = (1u << n);
    *byte |= mask;
}

void update_joypad(CPU *cpu)
{
    const Uint8 *state = SDL_GetKeyboardState(NULL);
    __uint8_t selected = cpu->memory[IO_JOYPAD] & 0x30;
    // printf("%.2x %.2x\n", selected, cpu->memory[IO_JOYPAD]);
    cpu->memory[IO_JOYPAD] |= 0x0F;
    if (selected == SELECT_NONE)
        return;
    if (selected == SELECT_BUTTONS)
    {
        if (state[SDL_SCANCODE_RIGHT])
        {
            unset_bit(&cpu->memory[IO_JOYPAD], 0);
        }

        if (state[SDL_SCANCODE_LEFT])
        {
            unset_bit(&cpu->memory[IO_JOYPAD], 1);
        }
        if (state[SDL_SCANCODE_UP])
        {
            unset_bit(&cpu->memory[IO_JOYPAD], 2);
        }
        if (state[SDL_SCANCODE_DOWN])
        {
            unset_bit(&cpu->memory[IO_JOYPAD], 3);
        }
    }
    if (selected == SELECT_DPAD)
    {
        if (state[SDL_SCANCODE_Z])
        {
            unset_bit(&cpu->memory[IO_JOYPAD], 0);
        }
        if (state[SDL_SCANCODE_X])
        {
            unset_bit(&cpu->memory[IO_JOYPAD], 1);
        }
        if (state[SDL_SCANCODE_SPACE])
        {
            unset_bit(&cpu->memory[IO_JOYPAD], 2);
        }
        if (state[SDL_SCANCODE_RETURN])
        {
            unset_bit(&cpu->memory[IO_JOYPAD], 3);
        }
    }

    // printf("%.2x\n", cpu->memory[IO_JOYPAD]);
}