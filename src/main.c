#include "cpu.h"
#include "ppu.h"

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Provide ROM path\n");
        exit(1);
    }
    const char *filename = argv[1];
    FILE *file = fopen("output.txt", "w");
    SDL_Window *window = SDL_Window_init();
    SDL_Renderer *renderer = SDL_Renderer_init(window);
    SDL_Event e;

    if (file == NULL)
    {
        perror("Error opening file");
        return 1;
    }

    CPU cpu = {0};
    Fetcher fetcher = {0};

    CPU_init(&cpu, &fetcher, filename, window, renderer, file);
    CPU_start(&cpu, &e, file);

    fclose(file);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}