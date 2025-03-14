
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "stdbool.h"
#include <stdalign.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_oldnames.h>
#include <SDL3/SDL_scancode.h>

#include "mem.h"
#include "loader.h"
#include "cpu.h"
#include "apu.h"
#include "ppu.h"
#include "joypad.h"
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


static void DrawSprite2(uint32_t *buffer, int xpos, int ypos, int h, int w, uint8_t *rgb)
{
    //buffer[y * SCREEN_WIDTH + x] = 0;
    //buffer[y * SCREEN_WIDTH + x + 1] = 0;
    //buffer[y * SCREEN_WIDTH + x + 2] = 0;
    //buffer[y * SCREEN_WIDTH + x + 3] = 0;

    for (int x = xpos; x < xpos + w; x++)
    {
        for (int y = ypos; y < ypos + h; y++)
        {
            uint8_t a = 255;
            buffer[y * SCREEN_WIDTH + x] = (a << 24) | (rgb[2] << 16) | (rgb[1] << 8) | rgb[0];
        }
    }
}

typedef struct Test {
    uint32_t test1;
    uint8_t test2;
    uint16_t test3;
    uint8_t test4[44];
} Test;

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

    Test *test = arena_add(arena, sizeof(*test));
    size_t aligned = alignof(sizeof(*test));

    test->test1 = 5500000;
    test->test2 = 33;
    test->test3 = 55555;
    test->test4[0] = 255;
    test->test4[43] = 255;

    int *ptr3 = arena_add(arena, sizeof(int));
    *ptr3 = 77;
    printf("ptr = %p: v = %d\n", ptr3, *ptr3);

    printf("uint32 test: %u\n", test->test1);
    printf("uint8 test: %u\n", test->test2);
    printf("uint16 test: %u\n", test->test3);
    printf("uint8 array begin test: %u\n", test->test4[0]);
    printf("uint8 array end test: %u\n", test->test4[43]);

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

    SDL_Window *window = SDL_CreateWindow("nones", SCREEN_WIDTH * 2, SCREEN_HEIGHT * 2, 0);
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

    //SDL_SetRenderLogicalPresentation(renderer, SCREEN_WIDTH, SCREEN_WIDTH,  SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    SDL_SetRenderScale(renderer,2, 2);

    SDL_Texture *texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH, SCREEN_HEIGHT);

    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

    // Allocate pixel buffer
    uint32_t *pixels = malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));

    CPU_Init(&nones->cpu);

    NES2_Header hdr;
    LoaderLoadRom(path, &hdr);
    CPU_Init(&nones->cpu);
    PPU_Init(&nones->ppu, hdr.name_table_layout, pixels);

    CPU_Reset(&nones->cpu);

    bool quit = false;
    SDL_Event event;
    void *raw_pixels;
    int raw_pitch;

    //uint64_t frames = 0, updates = 0;
    //uint64_t timer = SDL_GetTicks();

    while (!quit)
    {
        //memset(pixels, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));
        uint64_t start_time = SDL_GetTicksNS();

        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
                case SDL_EVENT_QUIT:
                    quit = true;
                    break;
                case SDL_EVENT_KEY_UP:
                {
                    switch (event.key.key) {
                        case SDLK_SPACE:
                            joypad_set_button_pressed_status(JOYPAD_BUTTON_A, false);
                            break;
                        case SDLK_W:
                        case SDLK_UP:
                            joypad_set_button_pressed_status(JOYPAD_BUTTON_UP, false);
                            break;
                        case SDLK_S:
                        case SDLK_DOWN:
                             joypad_set_button_pressed_status(JOYPAD_BUTTON_DOWN, false);
                            break;
                        case SDLK_A:
                        case SDLK_LEFT:
                            joypad_set_button_pressed_status(JOYPAD_BUTTON_LEFT, false);
                            break;                
                        case SDLK_D:
                        case SDLK_RIGHT:
                            joypad_set_button_pressed_status(JOYPAD_BUTTON_RIGHT, false);
                            break;
                        case SDLK_RETURN:
                            joypad_set_button_pressed_status(JOYPAD_BUTTON_START, false);
                            break;  
                    }
                    break;
                }
            }
        }

        const bool *kb_state  = SDL_GetKeyboardState(NULL);

        if (kb_state[SDL_SCANCODE_SPACE])
        {
            joypad_set_button_pressed_status(JOYPAD_BUTTON_A, true);
            printf("A Pressed!\n");
        }
        else if (kb_state[SDL_SCANCODE_UP] || kb_state[SDL_SCANCODE_W])
        {
            joypad_set_button_pressed_status(JOYPAD_BUTTON_UP, true);
            printf("Up Pressed!\n");
        }
        else if (kb_state[SDL_SCANCODE_DOWN] || kb_state[SDL_SCANCODE_S])
        {
            joypad_set_button_pressed_status(JOYPAD_BUTTON_DOWN, true);
            printf("Down Pressed!\n");
        }
        else if (kb_state[SDL_SCANCODE_LEFT] || kb_state[SDL_SCANCODE_A])
        {
            joypad_set_button_pressed_status(JOYPAD_BUTTON_LEFT, true);
            printf("Left Pressed!\n");
        }
        else if (kb_state[SDL_SCANCODE_RIGHT] || kb_state[SDL_SCANCODE_D])
        {
            printf("Right Pressed!\n");
            //WriteJoyPadReg(JOYPAD_BUTTON_START);
            joypad_set_button_pressed_status(JOYPAD_BUTTON_RIGHT, true);
            //joypad_set_button_pressed_status(JOYPAD_BUTTON_START, true);
        }
        else if (kb_state[SDL_SCANCODE_RETURN])
        {
            printf("Enter/Start Pressed!\n");
            //WriteJoyPadReg(JOYPAD_BUTTON_START);
            joypad_set_button_pressed_status(JOYPAD_BUTTON_START, true);
            //joypad_set_button_pressed_status(JOYPAD_BUTTON_START, true);
        }

        nones->ppu.frame_finished = false;
        do {
            CPU_Update(&nones->cpu);
            APU_Update(nones->cpu.cycles);
            PPU_Update(&nones->ppu, nones->cpu.cycles);
        } while (!nones->ppu.frame_finished);
        //} while ((nones->cpu.cycles % 29780) != 0);
        //updates++;

        SDL_LockTexture(texture, NULL, &raw_pixels, &raw_pitch);
        memcpy(raw_pixels, pixels, raw_pitch * SCREEN_HEIGHT);
        SDL_UnlockTexture(texture);

        //SDL_UpdateTexture(texture, NULL, pixels, SCREEN_WIDTH * 4);

        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
        //frames++;

        //if (SDL_GetTicks() - timer >= 1000) {
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

