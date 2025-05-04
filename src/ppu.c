#include "ppu.h"
#include "cpu.h"

SDL_Window *SDL_Window_init()
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return NULL;
    }

    SDL_Window *window = SDL_CreateWindow("GB-emu",
                                          SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED,
                                          WINDOW_WIDTH,
                                          WINDOW_HEIGHT,
                                          SDL_WINDOW_SHOWN);
    if (window == NULL)
    {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        return NULL;
    }

    return window;
}

SDL_Renderer *SDL_Renderer_init(SDL_Window *window)
{
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == NULL)
    {
        printf("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        return NULL;
    }

    return renderer;
}

__uint8_t display_frame(SDL_Window *window, SDL_Renderer *renderer, __uint8_t *frame)
{
    __uint8_t cell_width = WINDOW_WIDTH / 160;
    __uint8_t cell_height = WINDOW_HEIGHT / 144;

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    for (__uint8_t y = 0; y < 144; y++)
    {
        for (__uint8_t x = 0; x < 160; x++)
        {
            switch (frame[y * 160 + x])
            {
            case 0: // White
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                break;
            case 1: // Grey
                SDL_SetRenderDrawColor(renderer, 166, 166, 166, 255);
                break;
            case 2: // Dark grey
                SDL_SetRenderDrawColor(renderer, 77, 77, 77, 255);
                break;
            case 3: // Black
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                break;
            }

            SDL_Rect cell = {
                x * cell_width,
                y * cell_height,
                cell_width,
                cell_height};
            SDL_RenderFillRect(renderer, &cell);
        }
    }

    SDL_RenderPresent(renderer);
}

void PixelQueue_push(PixelQueue *queue, Pixel pixel)
{
    if (queue->size >= 8)
    {
        printf("Max PixelQueue exceeded\n");
        exit(1);
        return;
    }
    Pixel *current_pixel = &queue->queue[queue->size++];
    current_pixel->background_priority = pixel.background_priority;
    current_pixel->color_number = pixel.color_number;
    current_pixel->palette = pixel.palette;
    current_pixel->x = pixel.x;
    current_pixel->y = pixel.y;
}

Pixel *PixelQueue_pop(PixelQueue *queue)
{
    if (queue->size == 0)
    {
        printf("Cannot pop empty queue\n");
        exit(1);
        return NULL;
    }

    return &queue->queue[--queue->size];
}

void PixelQueue_clear(PixelQueue *queue)
{
    queue->size = 0;
}

__uint8_t PixelQueue_size(PixelQueue *queue)
{
    return queue->size;
}

// Sprite buffer

void SpriteBuffer_push(SpriteBuffer *buffer, SpriteAttributes sprite)
{
    if (buffer->size >= 10)
    {
        printf("Max SpriteBuffer exceeded %d\n", buffer->size);
        exit(1);
        return;
    }
    SpriteAttributes *current_sprite = &buffer->buffer[buffer->size++];
    current_sprite->x = sprite.x;
    current_sprite->y = sprite.y;
    current_sprite->tile_number = sprite.tile_number;
    current_sprite->flags = sprite.flags;
    current_sprite->fetched = false;
}

SpriteAttributes *SpriteBuffer_pop(SpriteBuffer *buffer)
{
    if (buffer->size == 0)
    {
        printf("Cannot pop empty buffer\n");
        exit(1);
        return NULL;
    }

    return &buffer->buffer[--buffer->size];
}

void SpriteBuffer_clear(SpriteBuffer *buffer)
{
    buffer->size = 0;
}

__uint8_t SpriteBuffer_size(SpriteBuffer *buffer)
{
    return buffer->size;
}

void oam_scan(CPU *cpu)
{
    SpriteBuffer *sprite_buffer = &cpu->ppu.sprite_buffer;
    __uint8_t ly_offset = cpu->memory[LY] + 16;
    bool tall_sprite_enabled = cpu->memory[LCDC] & (1u << 2);

    for (__uint16_t addr = OAM_ADDR; addr < OAM_ADDR_END; addr += 4)
    {
        __uint8_t y = cpu->memory[addr];
        __uint8_t x = cpu->memory[addr + 1];
        __uint8_t tile_number = cpu->memory[addr + 2];
        __uint8_t flags = cpu->memory[addr + 3];
        __uint8_t sprite_height = tall_sprite_enabled ? 16 : 8; // 8 in Normal Mode, 16 in Tall-Sprite-Mode
        if (SpriteBuffer_size(sprite_buffer) < 10 && x > 0 && ly_offset >= y && ly_offset < (y + sprite_height))
        {
            SpriteAttributes sa = {0};
            sa.y = y;
            sa.x = x;
            sa.tile_number = tile_number;
            sa.flags = flags;
            SpriteBuffer_push(sprite_buffer, sa);
        }
    }
    // if (SpriteBuffer_size(sprite_buffer))
    // {

    //     for (int i = 0; i < SpriteBuffer_size(sprite_buffer); i++)
    //     {
    //         printf("n: %.2x, pc: %.2x; ", sprite_buffer->buffer[i].tile_number, cpu->PC);
    //     }
    //     printf("----------\n");
    // }
}

void update_dma(CPU *cpu)
{
    __uint16_t dma_source = cpu->memory[DMA] << 8;
    for (int i = 0; i < 160; i++)
    {
        cpu->memory[OAM_ADDR + i] = cpu->memory[dma_source + i];
    }
}

void mark_fetched(CPU *cpu, __uint8_t x, __uint8_t y)
{
    SpriteAttributes *sprite_buffer = cpu->ppu.sprite_buffer.buffer;
    for (int i = 0; i < SpriteBuffer_size(&cpu->ppu.sprite_buffer); i++)
    {
        if (sprite_buffer[i].x == x && sprite_buffer[i].y == y)
            sprite_buffer[i].fetched = true;
    }
}

SpriteAttributes *sprite_to_fetch(CPU *cpu, Fetcher *fetcher)
{
    SpriteAttributes *sprite_buffer = cpu->ppu.sprite_buffer.buffer;
    __uint8_t current_x = (fetcher->curr_p);

    for (int i = 0; i < SpriteBuffer_size(&cpu->ppu.sprite_buffer); i++)
    {
        SpriteAttributes *sa = &sprite_buffer[i];
        if (sa->fetched)
            continue;
        __uint8_t sprite_start_x = sa->x - 8;
        __uint8_t sprite_end_x = sa->x;
        if (current_x >= sprite_start_x)
        {
            mark_fetched(cpu, sa->x, sa->y);
            return sa;
        }
    }

    return NULL;
}

void fetch_sprite_pixels(CPU *cpu, Fetcher *fetcher, __uint8_t ly)
{
    SpriteAttributes *sa = sprite_to_fetch(cpu, fetcher);
    if (sa == NULL)
        return;

    __uint8_t lcdc = cpu->memory[LCDC];
    __uint8_t obp0 = cpu->memory[OBP0];
    __uint8_t obp1 = cpu->memory[OBP1];
    __uint16_t tiledata = 0x8000;
    bool tall_sprite_enabled = cpu->memory[LCDC] & (1u << 2);
    bool x_flip = sa->flags & (1 << 5);
    bool y_flip = sa->flags & (1 << 6);
    __uint8_t background_priority = (sa->flags & (1u << 7)) ? 1 : 0;
    __uint8_t palette = (sa->flags & (1u << 4)) ? obp1 : obp0;
    __uint8_t tile_n = sa->tile_number;
    __uint8_t sprite_row = (ly + 16 - sa->y);

    if (y_flip)
    {
        sprite_row = (tall_sprite_enabled ? 15 : 7) - sprite_row;
    }
    if (tall_sprite_enabled)
    {
        tile_n &= 0xFE;
        if (sprite_row >= 8)
        {
            tile_n += 1;
            sprite_row -= 8;
        }
    }

    __uint16_t tile_addr = tiledata + (tile_n * 16) + sprite_row * 2;
    __uint8_t low = cpu->memory[tile_addr];
    __uint8_t high = cpu->memory[tile_addr + 1];
    __uint8_t mask = 0x80;
    __uint8_t start_x = sa->x - 8;

    for (int i = 0; i < 8; i++)
    {
        __uint8_t b1 = (low & mask) ? 1 : 0;
        __uint8_t b2 = (high & mask) ? 1 : 0;
        __uint8_t color_number = (b2 << 1) | b1;
        mask >>= 1;

        Pixel pixel = {0};
        pixel.color_number = color_number;
        pixel.background_priority = background_priority;
        pixel.palette = palette;
        pixel.x = ly;
        pixel.y = x_flip ? (start_x + (7 - i)) : start_x + i;
        PixelQueue_push(&cpu->ppu.sprite_queue, pixel);
    }
}

void fetch_window_pixels(CPU *cpu, Fetcher *fetcher, __uint8_t ly)
{
    __uint8_t lcdc = cpu->memory[LCDC];
    __uint8_t bgp = cpu->memory[BGP];
    __uint8_t wx = cpu->memory[WX];
    __uint8_t wy = cpu->memory[WY];
    __uint16_t tilemap = (lcdc & (1u << 6)) ? 0x9C00 : 0x9800;
    __uint16_t tiledata = (lcdc & (1u << 4)) ? 0x8000 : 0x9000;
    __uint16_t window_enable = lcdc & (1u << 5);

    if (!window_enable || wy > ly || (wx - 7) > fetcher->curr_p)
    {
        return;
    };

    // (2 T cycles)
    __uint8_t x_offset = fetcher->x_offset;
    __uint16_t row_offset = 32 * (fetcher->window_line_counter / 8);
    __uint16_t tile_n_addr = tilemap + row_offset + x_offset;
    __uint8_t tile_n = cpu->memory[tile_n_addr];

    // (2 T cycles)
    __uint16_t tile_offset = (tiledata == 0x9000) ? (int8_t)(tile_n) : tile_n;
    __uint16_t tile_addr = tiledata + (tile_offset * 16) + (2 * (fetcher->window_line_counter % 8));
    __uint8_t low = cpu->memory[tile_addr];

    // (2 T cycles)
    __uint8_t high = cpu->memory[tile_addr + 1];

    // Fetching pixels
    __uint8_t mask = 0x80;
    for (int i = 0; i < 8; i++)
    {
        __uint8_t b1 = (low & mask) ? 1 : 0;
        __uint8_t b2 = (high & mask) ? 1 : 0;
        mask >>= 1;
        __uint8_t color_number = (b2 << 1) | b1;
        Pixel pixel = {0};
        pixel.color_number = color_number;
        pixel.background_priority = 0;
        pixel.palette = bgp;
        pixel.x = ly;
        pixel.y = fetcher->curr_p + i;
        PixelQueue_push(&cpu->ppu.bg_queue, pixel);
    }
}

void render_scanline(CPU *cpu, Fetcher *fetcher, __uint8_t ly)
{
    __uint8_t lcdc = cpu->memory[LCDC];
    __uint8_t bgp = cpu->memory[BGP];
    __uint8_t obp0 = cpu->memory[OBP0];
    __uint8_t obp1 = cpu->memory[OBP1];
    __uint8_t scy = cpu->memory[SCY];
    __uint8_t scx = cpu->memory[SCX];
    __uint16_t tilemap = (lcdc & (1u << 3)) ? 0x9C00 : 0x9800;
    __uint16_t tiledata = (lcdc & (1u << 4)) ? 0x8000 : 0x9000;
    __uint16_t bw_enable = (lcdc & 1u);
    bool obj_enable = lcdc & (1 << 1);

    if (!bw_enable)
    {
        return;
    };

    // (2 T cycles)
    __uint8_t x_offset = fetcher->x_offset;
    __uint16_t col_offset = (scx / 8) % 32;
    __uint16_t row_offset = 32 * (((ly + scy) & 0xFF) / 8);
    __uint16_t tile_n_addr = tilemap + col_offset + row_offset + x_offset;
    __uint8_t tile_n = cpu->memory[tile_n_addr];

    // (2 T cycles)
    __uint16_t tile_offset = (tiledata == 0x9000) ? (int8_t)(tile_n) : tile_n;
    __uint16_t tile_addr = tiledata + (tile_offset * 16) + (2 * ((ly + scy) % 8));
    __uint8_t low = cpu->memory[tile_addr];

    // (2 T cycles)
    __uint8_t high = cpu->memory[tile_addr + 1];

    // Fetching pixels
    __uint8_t mask = 0x80;
    for (int i = 0; i < 8; i++)
    {
        __uint8_t b1 = (low & mask) ? 1 : 0;
        __uint8_t b2 = (high & mask) ? 1 : 0;
        mask >>= 1;
        __uint8_t color_number = (b2 << 1) | b1;
        Pixel pixel = {0};
        pixel.color_number = color_number;
        pixel.background_priority = 0;
        pixel.palette = bgp;
        pixel.x = ly;
        pixel.y = fetcher->curr_p + i;
        PixelQueue_push(&cpu->ppu.bg_queue, pixel);
    }
    fetch_sprite_pixels(cpu, fetcher, ly);

    // Push pixels
    if (PixelQueue_size(&cpu->ppu.bg_queue))
    {

        while (PixelQueue_size(&cpu->ppu.bg_queue))
        {
            Pixel *pixel = PixelQueue_pop(&cpu->ppu.bg_queue);
            if ((pixel->x * 160 + pixel->y) < 144 * 160)
            {
                __uint8_t color = (pixel->palette >> (pixel->color_number * 2)) & 0x3;
                cpu->ppu.frame[pixel->x * 160 + pixel->y] = color;
                // printf("color_number: %d, x: %d, y: %d\n", pixel->color_number, pixel->x, pixel->y);
            }
            else
            {
                // TODO: fix this
                printf("out of bounds %d, max: %d\n", (pixel->x * 160 + pixel->y), 144 * 160);
            }
        }
        // return;
    }
    // Push pixels
    // __uint8_t new_color = 0;
    // if(SpriteBuffer_size(&cpu->ppu.sprite_buffer) == 1) {
    //     printf("%.2x %0.2x\n", ly, cpu->ppu.sprite_buffer.buffer[0].tile_number);
    //     new_color = 2;
    // }

    fetch_window_pixels(cpu, fetcher, ly);

    // Push pixels
    if (PixelQueue_size(&cpu->ppu.bg_queue))
    {

        while (PixelQueue_size(&cpu->ppu.bg_queue))
        {
            Pixel *pixel = PixelQueue_pop(&cpu->ppu.bg_queue);
            if ((pixel->x * 160 + pixel->y) < 144 * 160)
            {
                __uint8_t color = (pixel->palette >> (pixel->color_number * 2)) & 0x3;
                cpu->ppu.frame[pixel->x * 160 + pixel->y] = color;
                // printf("color_number: %d, x: %d, y: %d\n", pixel->color_number, pixel->x, pixel->y);
            }
            else
            {
                // TODO: fix this
                printf("out of bounds %d, max: %d\n", (pixel->x * 160 + pixel->y), 144 * 160);
            }
        }
        // return;
    }

    if (PixelQueue_size(&cpu->ppu.sprite_queue))
    {
        while (PixelQueue_size(&cpu->ppu.sprite_queue))
        {
            Pixel *pixel = PixelQueue_pop(&cpu->ppu.sprite_queue);
            if ((pixel->x * 160 + pixel->y) < 144 * 160)
            {
                __uint8_t color = (pixel->palette >> (pixel->color_number * 2)) & 0x3;
                if (pixel->color_number == 0 || !obj_enable)
                    continue;
                else if (pixel->background_priority && cpu->ppu.frame[pixel->x * 160 + pixel->y] != (bgp & 0x3))
                    continue;
                cpu->ppu.frame[pixel->x * 160 + pixel->y] = color;
                // printf("color_number: %d, x: %d, y: %d\n", pixel->color_number, pixel->x, pixel->y);
            }
            else
            {
                // TODO: fix this
                printf("out of bounds %d, max: %d\n", (pixel->x * 160 + pixel->y), 144 * 160);
            }
        }
        // return;
    }

    fetcher->x_offset = (fetcher->x_offset + 1) % 32;
    fetcher->curr_p += 8;
}

void reset_ppu(CPU *cpu)
{
    Fetcher *fetcher = cpu->fetcher;
    __uint8_t *ly = &cpu->memory[LY];

    cpu->ppu.cycles = 0;
    cpu->ppu.line_cycles = 0;
    *ly = 0;
    fetcher->curr_p = 0;
    fetcher->x_offset = 0;
    cpu->vblank = 0;
    cpu->hblank = 0;
    cpu->oam_scan = 0;
    cpu->pixel_transfer = false;
    PixelQueue_clear(&cpu->ppu.bg_queue);
    SpriteBuffer_clear(&cpu->ppu.sprite_buffer);
}

__uint8_t get_ppu_mode(CPU *cpu)
{
    if (cpu->memory[LY] >= 144) // V-Blank (lines 144-153)
        return 0b01;
    else
    {
        __uint16_t line_cycles = cpu->ppu.line_cycles;

        if (line_cycles < 80) // OAM Search (first 80 cycles)
            return 0b10;
        else if (line_cycles < 252) // Pixel Transfer (approx 80-252, varies by sprites)
            return 0b11;
        else // H-Blank (remaining cycles up to 456)
            return 0b00;
    }
}

void update_ppu(CPU *cpu, __uint8_t t_cycles)
{
    __uint8_t lcd_enabled = cpu->memory[LCDC] & 0x80;
    Fetcher *fetcher = cpu->fetcher;
    __uint8_t *ly = &cpu->memory[LY];

    if (!lcd_enabled)
    {
        reset_ppu(cpu);
        cpu->memory[STAT] = (cpu->memory[STAT] & 0xFC) | 0;
        return;
    }

    __uint8_t lcdc = cpu->memory[LCDC];
    __uint8_t wx = cpu->memory[WX];
    __uint8_t wy = cpu->memory[WY];
    __uint16_t window_enable = lcdc & (1u << 5);

    __uint8_t mode = get_ppu_mode(cpu);

    cpu->ppu.cycles += t_cycles;
    cpu->ppu.line_cycles += t_cycles;

    bool new_line = *ly != cpu->ppu.prev_ly;
    cpu->ppu.prev_ly = *ly;
    cpu->memory[STAT] = (cpu->memory[STAT] & 0xFC) | mode;

    if (*ly == cpu->memory[LYC])
    {
        cpu->memory[STAT] |= (1u << 2);
        if ((cpu->memory[STAT] & (1u << 6)) && new_line) // STAT.6 - LYC=LY STAT Interrupt Enable
        {
            cpu->memory[IF] |= (1u << 1);
            // printf("(LYC=LY) %d\n", *ly);
        }
    }
    else
    {
        cpu->memory[STAT] &= ~(1u << 2);
    }

    if (mode == 2 && !cpu->oam_scan)
    {
        oam_scan(cpu);
        cpu->oam_scan = 1;
        if (cpu->memory[STAT] & (1u << 5)) // STAT.5	Mode 2 (OAM Scan)
        {
            cpu->memory[IF] |= (1u << 1);
            // printf("(OAM Scan)\n");
        }
    }
    if (mode == 1 && !cpu->vblank)
    {
        fetcher->window_line_counter = 0;
        cpu->memory[IF] |= (1u << 0);
        cpu->vblank = 1;
        // printf("(VBlank) at: %d\n", *ly);
        if (cpu->memory[STAT] & (1u << 4)) // STAT.4	Mode 1 (VBlank)
        {
            cpu->memory[IF] |= (1u << 1);
            // printf("(VBlank)\n");
        }
    }
    if (mode == 0 && !cpu->hblank)
    {
        cpu->hblank = 1;
        if (cpu->memory[STAT] & (1u << 3)) // STAT.3	Mode 0 (HBlank)
        {
            cpu->memory[IF] |= (1u << 1);
            // printf("(HBlank)\n");
        }
    }
    if (window_enable && wy <= *ly && (wx - 7) <= fetcher->curr_p)
    {
        if (!fetcher->fetching_window_pixels)
            fetcher->x_offset = 0;
        fetcher->fetching_window_pixels = true;
    }
    else
    {
        fetcher->fetching_window_pixels = false;
    }

    if (cpu->ppu.line_cycles < 456)
    {
        if (mode == 3 && fetcher->curr_p < 160 && *ly < 144)
        {
            cpu->pixel_transfer = true;
            render_scanline(cpu, fetcher, *ly);
        }
    }
    else
    {
        if (fetcher->fetching_window_pixels)
        {
            fetcher->window_line_counter++;
        };

        cpu->ppu.line_cycles -= 456;
        *ly = *ly + 1;
        fetcher->curr_p = 0;
        fetcher->x_offset = 0;
        cpu->vblank = 0;
        cpu->hblank = 0;
        cpu->oam_scan = 0;
        cpu->pixel_transfer = false;
        cpu->fetcher->x_offset = 0;
        PixelQueue_clear(&cpu->ppu.bg_queue);
        SpriteBuffer_clear(&cpu->ppu.sprite_buffer);
    }
    if (cpu->ppu.cycles >= 70224)
    {
        // printf("one frame line_cycles: %d\n", cpu->ppu.line_cycles);
        cpu->ppu.cycles -= 70224;
        cpu->ppu.line_cycles = 0;
        fetcher->curr_p = 0;
        cpu->vblank = 0;
        cpu->hblank = 0;
        cpu->oam_scan = 0;
        cpu->pixel_transfer = false;
        cpu->fetcher->window_line_counter = 0;
        *ly = 0;
        cpu->fetcher->x_offset = 0;
        display_frame(cpu->window, cpu->renderer, cpu->ppu.frame);
        PixelQueue_clear(&cpu->ppu.bg_queue);
        SpriteBuffer_clear(&cpu->ppu.sprite_buffer);
    }
}