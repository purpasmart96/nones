

#include <stdlib.h>
#include <stdint.h>
#include "stdbool.h"

#include <SDL2/SDL.h>

#include "mem.h"
#include "loader.h"
#include "cpu.h"
#include "apu.h"
#include "ppu.h"

//uint8_t *g_prg_rom = NULL; 
//uint8_t *g_chr_rom = NULL;
//uint8_t *g_sram = NULL;

#define SCREEN_WIDTH 340
#define SCREEN_HEIGHT 260
#define FRAMERATE 60
#define FRAME_TIME_MS (1000.0 / FRAMERATE)

typedef struct {
    Cpu cpu;
    //Apu *apu;
    Ppu ppu;
    Cart cart;
} Nones;

void NonesRun(Nones *nones, const char *path)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        SDL_Log("SDL Init Error: %s", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    SDL_Window *window = SDL_CreateWindow("nones", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_WIDTH, SCREEN_HEIGHT, 0);
    if (!window)
    {
        SDL_Log("Window Error: %s", SDL_GetError());
        SDL_Quit();
        exit(EXIT_FAILURE);
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer)
    {
        SDL_Log("Renderer Error: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        exit(EXIT_FAILURE);
    }

    SDL_Texture *texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH, SCREEN_HEIGHT);
    SDL_Surface *window_surface = SDL_GetWindowSurface(window);

    // Allocate pixel buffer
    uint32_t *pixels = malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));

    // Fill buffer with dummy NES image (red and blue checkerboard pattern)
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            uint8_t r = (x % 2 == y % 2) ? 255 : 0;
            uint8_t g = 0;
            uint8_t b = (x % 2 != y % 2) ? 255 : 0;
            uint8_t a = 255;
            pixels[y * SCREEN_WIDTH + x] = (a << 24) | (b << 16) | (g << 8) | r;
        }
    }

    CPU_Init(&nones->cpu);
    //MemInit();

    NES2_Header hdr;

    /*
    if (argc < 2)
    {
        printf("No file was provided\n");
        return EXIT_FAILURE;
    }
    */

    LoaderLoadRom(path, &hdr);
    PPU_Init(&nones->ppu, hdr.name_table_layout);
    //MapMem(g_prg_rom, &hdr, hdr.mapper_number_d3d0, &cpu.pc);
    //free(rom);


    CPU_Reset(&nones->cpu);

    bool quit = false;
    SDL_Event event;
    //uint64_t frames = 0, updates = 0;
    //uint64_t timer = SDL_GetTicks64();

    while (!quit)
    {
        //uint64_t start_time = SDL_GetTicks64();

        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
                quit = true;
        }

        nones->ppu.frame_finished = false;
        do {
            CPU_Update(&nones->cpu);
            APU_Update(nones->cpu.cycles);
            PPU_Update(&nones->ppu, nones->cpu.cycles);
        } while (!nones->ppu.frame_finished);
         // } while ((cpu.cycles % 29780) != 0);
        //updates++;

        //SDL_SetRenderDrawColor(renderer, 0xFF, 0xFF, 0xFF, 0xFF);
        SDL_UpdateTexture(texture, NULL, pixels, SCREEN_WIDTH * 4);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
        //frames++;

        //if (SDL_GetTicks64() - timer >= 1000) {
        //    printf("UPS: %lu, FPS: %lu\r", updates, frames);
        //    fflush(stdout);
        //    updates = frames = 0;
        //    timer += 1000;
        //}

        //uint64_t frame_time = SDL_GetTicks64() - start_time;
        //if (frame_time < FRAME_TIME_MS) {
        //    SDL_Delay(FRAME_TIME_MS - frame_time);
        //}
        SDL_Delay(FRAME_TIME_MS);
    }

    PPU_Reset();
    CPU_Reset(&nones->cpu);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    free(pixels);
    free(g_prg_rom);
    if (g_chr_rom)
        free(g_chr_rom);
    if (g_sram)
        free(g_sram);
    return;
}