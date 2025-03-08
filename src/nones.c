

#include <SDL3/SDL_timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "stdbool.h"

#include <SDL3/SDL.h>

#include "mem.h"
#include "loader.h"
#include "cpu.h"
#include "apu.h"
#include "ppu.h"
#include "nones.h"

#include "arena.h"

//uint8_t *g_prg_rom = NULL; 
//uint8_t *g_chr_rom = NULL;
//uint8_t *g_sram = NULL;

static void DrawSprite(uint32_t *buffer, int xpos, int ypos, int h, int w)
{
    //buffer[y * SCREEN_WIDTH + x] = 0;
    //buffer[y * SCREEN_WIDTH + x + 1] = 0;
    //buffer[y * SCREEN_WIDTH + x + 2] = 0;
    //buffer[y * SCREEN_WIDTH + x + 3] = 0;

    for (int x = xpos; x < xpos + w; x++)
    {
        for (int y = ypos; y < ypos + h; y++)
        {
            buffer[y * SCREEN_WIDTH + x] = 0;
        }
    }
    // Fill buffer with dummy NES image (red and blue checkerboard pattern)
    //for (int y = 0; y < SCREEN_HEIGHT; y++) {
    //    for (int x = 0; x < SCREEN_WIDTH; x++) {
    //        uint8_t r = 255;// (x % 2 == y % 2) ? 255 : 0;
    //        uint8_t g = 123;
    //        uint8_t b = 255; //(x % 2 != y % 2) ? 255 : 0;
    //        uint8_t a = 255;
    //        buffer[y * SCREEN_WIDTH + x] = (a << 24) | (b << 16) | (g << 8) | r;
    //    }
    //}
}

static void ArenaTest(void)
{
    Arena *arena = arena_create(0x10000);

    int *ptr = arena_add(arena, sizeof(int) * 0x1000);

    for (int i = 0; i < 0x1000; i++)
    {
        ptr[i] = i;
    }

    for (int i = 0; i < 0x1000; i++)
    {
        printf("ptr[%d] = %d\n", i, ptr[i]);
    }

    int *ptr2 = arena_add(arena, sizeof(int));
    *ptr2 = 55;
    printf("ptr = %p: v = %d\n", ptr2, *ptr2);

    for (int i = 0; i < 0x1000; i++)
    {
        printf("ptr[%d] = %d\n", i, ptr[i]);
    }

    arena_destroy(arena);
    exit(EXIT_SUCCESS);
}

void NonesRun(Nones *nones, const char *path)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("SDL Init Error: %s", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    SDL_Window *window = SDL_CreateWindow("nones", SCREEN_WIDTH, SCREEN_HEIGHT, 0);
    if (!window)
    {
        SDL_Log("Window Error: %s", SDL_GetError());
        SDL_Quit();
        exit(EXIT_FAILURE);
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);
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

    NES2_Header hdr;
    LoaderLoadRom(path, &hdr);
    CPU_Init(&nones->cpu);
    PPU_Init(&nones->ppu, hdr.name_table_layout);

    CPU_Reset(&nones->cpu);

    bool quit = false;
    SDL_Event event;
    //uint64_t frames = 0, updates = 0;
    //uint64_t timer = SDL_GetTicks();

    while (!quit)
    {
        memset(pixels, 255, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));
        uint64_t start_time = SDL_GetTicksNS();

        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
                case SDL_EVENT_QUIT:
                    quit = true;
                    break;
            }
        }

        nones->ppu.frame_finished = false;
        do {
            CPU_Update(&nones->cpu);
            APU_Update(nones->cpu.cycles);
            PPU_Update(&nones->ppu, nones->cpu.cycles);
        } while (!nones->ppu.frame_finished);
         // } while ((cpu.cycles % 29780) != 0);
        //updates++;

        int rand_x = rand() % SCREEN_WIDTH / 2;
        int rand_y = rand() % SCREEN_HEIGHT / 2;
        DrawSprite(pixels, rand_x, rand_y, 50, 50);

        SDL_UpdateTexture(texture, NULL, pixels, SCREEN_WIDTH * 4);
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
        //frames++;

        //if (SDL_GetTicks64() - timer >= 1000) {
        //    printf("UPS: %lu, FPS: %lu\r", updates, frames);
        //    fflush(stdout);
        //    updates = frames = 0;
        //    timer += 1000;
        //}

        uint64_t frame_time = SDL_GetTicksNS() - start_time;
        if (frame_time < FRAME_TIME_NS) {
            SDL_DelayNS(FRAME_TIME_NS - frame_time);
        }
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

