#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "stdbool.h"
#include <stdalign.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>

#include "bus.h"
#include "cart.h"
#include "nones.h"

#define HIGH_RATE_SAMPLES 29780
#define LOW_RATE_SAMPLES 735
//#define LOW_RATE_SAMPLES 800

static SDL_AudioStream *stream = NULL;

static void downsample_to_44khz(const float *high_rate_buffer, int16_t *output_44khz_buffer)
{
    const double step = (double)HIGH_RATE_SAMPLES / LOW_RATE_SAMPLES;

    double pos = 0.0;
    for (int i = 0; i < LOW_RATE_SAMPLES; i++)
    {
        int index = (int)pos;
        double frac = pos - index;

        // Simple linear interpolation
        float a = high_rate_buffer[index];
        float b = (index + 1 < HIGH_RATE_SAMPLES) ? high_rate_buffer[index + 1] : a;

        float sample = (float)((1.0 - frac) * a + frac * b);
        // convert to s16
        output_44khz_buffer[i] = (int16_t)(sample * 32767);

        pos += step;
    }
}

void NonesPutSoundData(Apu *apu)
{
    const int minimum_audio = (44100 * sizeof(int16_t)); // * 2) / 2; // Stereo samples
    if (SDL_GetAudioStreamQueued(stream) < minimum_audio) {

        downsample_to_44khz(apu->buffer, apu->outbuffer);

        SDL_PutAudioStreamData(stream, apu->outbuffer, sizeof(apu->outbuffer));
    }
}

static void NonesInit(Nones *nones, const char *path)
{
    nones->arena = ArenaCreate(1024 * 1024 * 2);
    nones->bus = BusCreate(nones->arena);

    if (BusLoadCart(nones->arena, nones->bus, path))
    {
        ArenaDestroy(nones->arena);
        exit(EXIT_FAILURE);
    }
}

static void NonesSetIntegerScale(SDL_Window *window, SDL_Renderer *renderer, int scale)
{
    SDL_SetWindowSize(window, SCREEN_WIDTH * scale, SCREEN_HEIGHT * scale);
    SDL_SetRenderScale(renderer, scale, scale);
    SDL_SetWindowPosition(window,  SDL_WINDOWPOS_CENTERED,  SDL_WINDOWPOS_CENTERED);
}

void NonesRun(Nones *nones, const char *path)
{
    NonesInit(nones, path);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))
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

    if (!SDL_SetRenderVSync(renderer, 1))
    {
        SDL_Log( "Could not enable VSync! SDL error: %s\n", SDL_GetError());
    }

    int num_gamepads;
    SDL_Gamepad *gamepad = NULL;
    SDL_JoystickID *gamepads = SDL_GetGamepads(&num_gamepads);
    if (gamepads)
    {
        gamepad = SDL_OpenGamepad(gamepads[0]);
        char *gamepad_info = SDL_GetGamepadMapping(gamepad);
        printf("Gamepad: %s\n", gamepad_info);
        SDL_free(gamepad_info);
    }
    else
    {
        SDL_Log("No gamepad detected! SDL error: %s\n", SDL_GetError());
    }

    SDL_AudioSpec spec;
    spec.channels = 1;
    spec.format = SDL_AUDIO_S16;
    spec.freq = 44100;

    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!stream)
    {
        SDL_Log("Couldn't create audio stream: %s", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        exit(EXIT_FAILURE);
    }

    SDL_ResumeAudioStreamDevice(stream);

    //SDL_SetRenderLogicalPresentation(renderer, SCREEN_WIDTH, SCREEN_WIDTH,  SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    SDL_SetRenderScale(renderer,2, 2);

    SDL_Texture *texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH, SCREEN_HEIGHT);

    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

    // Allocate pixel buffers (back and front)
    uint32_t *buffers[2];
    const uint32_t buffer_size = (SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));
    buffers[0] = ArenaPush(nones->arena, buffer_size);
    buffers[1] = ArenaPush(nones->arena, buffer_size);

    CPU_Init(nones->bus->cpu);
    APU_Init(nones->bus->apu);
    PPU_Init(nones->bus->ppu, nones->bus->cart->mirroring, buffers);

    CPU_Reset(nones->bus->cpu);
    APU_Update(nones->bus->apu, nones->bus->cpu->cycles);
    PPU_Update(nones->bus->ppu, nones->bus->cpu->cycles);

    bool quit = false;
    bool buttons[8];
    SDL_Event event;
    void *raw_pixels;
    int raw_pitch;

    //uint64_t frames = 0, updates = 0;
    //uint64_t timer = SDL_GetTicks();

    char debug_cpu[128] = {'\0'};
    bool debug_stats = false;
    bool paused = false;
    bool step = false;
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
                    switch (event.key.key)
                    {
                        case SDLK_F1:
                            debug_stats = !debug_stats;
                            break;
                        case SDLK_F6:
                            paused = !paused;
                            break;
                        case SDLK_F11:
                            step = true;
                            break;
                    }
                    break;
            }
        }

        const bool *kb_state  = SDL_GetKeyboardState(NULL);

        buttons[0] = kb_state[SDL_SCANCODE_SPACE]  || SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST);
        buttons[1] = kb_state[SDL_SCANCODE_LSHIFT] || SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
        buttons[2] = kb_state[SDL_SCANCODE_UP]     || kb_state[SDL_SCANCODE_W] || SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP);
        buttons[3] = kb_state[SDL_SCANCODE_DOWN]   || kb_state[SDL_SCANCODE_S] || SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        buttons[4] = kb_state[SDL_SCANCODE_LEFT]   || kb_state[SDL_SCANCODE_A] || SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        buttons[5] = kb_state[SDL_SCANCODE_RIGHT]  || kb_state[SDL_SCANCODE_D] || SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        buttons[6] = kb_state[SDL_SCANCODE_RETURN] || SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_START);
        buttons[7] = kb_state[SDL_SCANCODE_TAB]    || SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_BACK);

        JoyPadSetButton(nones->bus->joy_pad, JOYPAD_A, buttons[0]);
        JoyPadSetButton(nones->bus->joy_pad, JOYPAD_B, buttons[1]);
        JoyPadSetButton(nones->bus->joy_pad, JOYPAD_UP, buttons[2]);
        JoyPadSetButton(nones->bus->joy_pad, JOYPAD_DOWN, buttons[3]);
        JoyPadSetButton(nones->bus->joy_pad, JOYPAD_LEFT, buttons[4]);
        JoyPadSetButton(nones->bus->joy_pad, JOYPAD_RIGHT, buttons[5]);
        JoyPadSetButton(nones->bus->joy_pad, JOYPAD_START, buttons[6]);
        JoyPadSetButton(nones->bus->joy_pad, JOYPAD_SELECT, buttons[7]);

        if (kb_state[SDL_SCANCODE_ESCAPE])
            quit = true;
        else if (kb_state[SDL_SCANCODE_1])
            NonesSetIntegerScale(window, renderer, 1);
        else if (kb_state[SDL_SCANCODE_2])
            NonesSetIntegerScale(window, renderer, 2);
        else if (kb_state[SDL_SCANCODE_3])
            NonesSetIntegerScale(window, renderer, 3);
        else if (kb_state[SDL_SCANCODE_4])
            NonesSetIntegerScale(window, renderer, 4);
        else if (kb_state[SDL_SCANCODE_5])
            NonesSetIntegerScale(window, renderer, 5);

        nones->bus->ppu->frame_finished = false;

        if (step)
        {
            paused = false;
        }
        if (!paused)
        {
            do {
                CPU_Update(nones->bus->cpu);
            } while (!nones->bus->ppu->frame_finished);
        }

        if (step)
        {
            step = false;
            paused = true;
        }

        SDL_LockTexture(texture, NULL, &raw_pixels, &raw_pitch);
        memcpy(raw_pixels, nones->bus->ppu->buffers[1], buffer_size);
        SDL_UnlockTexture(texture);

        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, NULL, NULL);

        if (debug_stats)
        {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
            snprintf(debug_cpu, sizeof(debug_cpu), "A:%02X X:%02X Y:%02X SP:%02X", nones->bus->cpu->a,
                     nones->bus->cpu->x, nones->bus->cpu->y, nones->bus->cpu->sp);

            SDL_RenderDebugText(renderer, 2, 9, nones->bus->cpu->debug_msg);
            SDL_RenderDebugText(renderer, 2, 1, debug_cpu);
        }

        SDL_RenderPresent(renderer);
        //frames++;

        //if (SDL_GetTicks() - timer >= 1000)
        //{
        //    printf("UPS: %lu, FPS: %lu\r", updates, frames);
        //    fflush(stdout);
        //    updates = frames = 0;
        //    timer += 1000;
        //}

        double frame_time = (SDL_GetTicksNS() - start_time);
        if (frame_time < FRAME_TIME_NS)
        {
            SDL_DelayPrecise(FRAME_TIME_NS - frame_time);
        }
    }

    CartSaveSram(nones->bus->cart);

    PPU_Reset();
    CPU_Reset(nones->bus->cpu);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_free(gamepads);
    if (gamepad)
        SDL_CloseGamepad(gamepad);
    SDL_DestroyAudioStream(stream);
    SDL_DestroyWindow(window);
    SDL_Quit();

    ArenaDestroy(nones->arena);
}

