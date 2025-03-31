
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

#include "arena.h"
#include "loader.h"
#include "cpu.h"
#include "apu.h"
#include "ppu.h"
#include "joypad.h"
#include "bus.h"
#include "nones.h"

static void NonesInit(Nones *nones, const char *path)
{
    nones->arena = ArenaCreate(1024 * 1024);
    nones->bus = BusCreate(nones->arena);

    if (BusLoadCart(nones->arena, nones->bus, path))
    {
        ArenaDestroy(nones->arena);
        exit(EXIT_FAILURE);
    }
}

void NonesRun(Nones *nones, const char *path)
{
    NonesInit(nones, path);

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
    uint32_t *pixels = ArenaPush(nones->arena, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));

    CPU_Init(nones->bus->cpu);
    PPU_Init(nones->bus->ppu, nones->bus->cart->mirroring, pixels);

    CPU_Reset(nones->bus->cpu);

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
            }
        }

        const bool *kb_state  = SDL_GetKeyboardState(NULL);

        JoyPadSetButton(JOYPAD_A, kb_state[SDL_SCANCODE_SPACE]);
        JoyPadSetButton(JOYPAD_B, kb_state[SDL_SCANCODE_LSHIFT]);
        JoyPadSetButton(JOYPAD_UP, kb_state[SDL_SCANCODE_UP] || kb_state[SDL_SCANCODE_W]);
        JoyPadSetButton(JOYPAD_DOWN, kb_state[SDL_SCANCODE_DOWN] || kb_state[SDL_SCANCODE_S]);
        JoyPadSetButton(JOYPAD_LEFT, kb_state[SDL_SCANCODE_LEFT] || kb_state[SDL_SCANCODE_A]);
        JoyPadSetButton(JOYPAD_RIGHT, kb_state[SDL_SCANCODE_RIGHT] || kb_state[SDL_SCANCODE_D]);
        JoyPadSetButton(JOYPAD_START, kb_state[SDL_SCANCODE_RETURN]);
        JoyPadSetButton(JOYPAD_SELECT, kb_state[SDL_SCANCODE_TAB]);
        //JoyPadSetButton(JOYPAD_A, kb_state[SDL_SCANCODE_SPACE]);
        //JoyPadSetButton(JOYPAD_A, kb_state[SDL_SCANCODE_SPACE]);

        if (kb_state[SDL_SCANCODE_ESCAPE])
            quit = true;


        nones->bus->ppu->frame_finished = false;

        do {
            CPU_Update(nones->bus->cpu);
            APU_Update(nones->bus->cpu->cycles);
            PPU_Update(nones->bus->ppu, nones->bus->cpu->cycles);
        } while (!nones->bus->ppu->frame_finished);
        //} while ((nones->cpu.cycles % 29780) != 0);
        //updates++;

        SDL_LockTexture(texture, NULL, &raw_pixels, &raw_pitch);
        memcpy(raw_pixels, pixels, raw_pitch * SCREEN_HEIGHT);
        SDL_UnlockTexture(texture);

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

    if (nones->bus->cart->battery)
    {
        char save_path[128];
        snprintf(save_path, sizeof(save_path), "%s.sav", nones->bus->cart->name);

        FILE *sav = fopen(save_path, "wb");
        if (sav != NULL)
        {
            fwrite(nones->bus->cart->ram, CART_RAM_SIZE, 1, sav);
            fclose(sav);
        }
    }

    PPU_Reset();
    CPU_Reset(nones->bus->cpu);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    ArenaDestroy(nones->arena);
}

