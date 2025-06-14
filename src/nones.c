#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "stdbool.h"
#include <stdalign.h>

#include <SDL3/SDL.h>

#include "system.h"
#include "cart.h"
#include "nones.h"

#define HIGH_RATE_SAMPLES 29780
#define LOW_RATE_SAMPLES 735
//#define LOW_RATE_SAMPLES 800

static SDL_AudioStream *stream = NULL;

static void upsample_to_44khz(const float *high_rate_buffer, int16_t *output_44khz_buffer, bool odd_frame)
{
    const double step = (double)(HIGH_RATE_SAMPLES + odd_frame) / LOW_RATE_SAMPLES;

    double pos = 0.0;
    for (int i = 0; i < LOW_RATE_SAMPLES; i++)
    {
        int index = (int)pos;
        double frac = pos - index;

        // Simple linear interpolation
        float a = high_rate_buffer[index];
        float b = (index + 1 < (HIGH_RATE_SAMPLES + odd_frame)) ? high_rate_buffer[index + 1] : a;

        float sample = (float)((1.0 - frac) * a + frac * b);
        // convert to s16
        output_44khz_buffer[i] = (int16_t)(sample * 32767);

        pos += step;
    }
}

void NonesPutSoundData(Apu *apu)
{
    const int minimum_audio = (4096 * sizeof(int16_t)); // * 2) / 2; // Stereo samples
    if (SDL_GetAudioStreamQueued(stream) < minimum_audio) {

        upsample_to_44khz(apu->buffer, apu->outbuffer, apu->odd_frame);

        SDL_PutAudioStreamData(stream, apu->outbuffer, sizeof(apu->outbuffer));
    }
}

static void NonesInit(Nones *nones, const char *path)
{
    nones->arena = ArenaCreate(1024 * 1024 * 2);
    nones->system = SystemCreate(nones->arena);

    if (SystemLoadCart(nones->arena, nones->system, path))
    {
        ArenaDestroy(nones->arena);
        exit(EXIT_FAILURE);
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))
    {
        SDL_Log("SDL Init Error: %s", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    nones->window = SDL_CreateWindow("nones", SCREEN_WIDTH * 2, SCREEN_HEIGHT * 2, 0);
    if (!nones->window)
    {
        SDL_Log("Window Error: %s", SDL_GetError());
        SDL_Quit();
        exit(EXIT_FAILURE);
    }

    nones->renderer = SDL_CreateRenderer(nones->window, NULL);
    if (!nones->renderer)
    {
        SDL_Log("Renderer Error: %s", SDL_GetError());
        SDL_DestroyWindow(nones->window);
        SDL_Quit();
        exit(EXIT_FAILURE);
    }

    if (!SDL_SetRenderVSync(nones->renderer, 1))
    {
        SDL_Log("Could not enable VSync! SDL error: %s\n", SDL_GetError());
    }

    nones->gamepads = SDL_GetGamepads(&nones->num_gamepads);
    if (nones->gamepads)
    {
        nones->gamepad1 = SDL_OpenGamepad(nones->gamepads[0]);
        char *gamepad1_info = SDL_GetGamepadMapping(nones->gamepad1);
        printf("Gamepad1: %s\n", gamepad1_info);
        SDL_free(gamepad1_info);

        if (nones->num_gamepads > 1)
        {
            nones->gamepad2 = SDL_OpenGamepad(nones->gamepads[1]);
            char *gamepad2_info = SDL_GetGamepadMapping(nones->gamepad2);
            printf("Gamepad2: %s\n", gamepad2_info);
            SDL_free(gamepad2_info);
        }
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
        SDL_DestroyRenderer(nones->renderer);
        SDL_DestroyWindow(nones->window);
        SDL_Quit();
        exit(EXIT_FAILURE);
    }

    SDL_ResumeAudioStreamDevice(stream);

    //SDL_SetRenderLogicalPresentation(nones->renderer, SCREEN_WIDTH, SCREEN_WIDTH,  SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    SDL_SetRenderScale(nones->renderer, 2, 2);

    nones->texture = SDL_CreateTexture(nones->renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH, SCREEN_HEIGHT);

    SDL_SetTextureScaleMode(nones->texture, SDL_SCALEMODE_NEAREST);
}

static void NonesShutdown(Nones *nones)
{
    SystemShutdown(nones->system);

    SDL_DestroyTexture(nones->texture);
    SDL_DestroyRenderer(nones->renderer);
    SDL_free(nones->gamepads);
    if (nones->gamepad1)
        SDL_CloseGamepad(nones->gamepad1);
    if (nones->gamepad2)
        SDL_CloseGamepad(nones->gamepad2);
    SDL_DestroyAudioStream(stream);
    SDL_DestroyWindow(nones->window);
    SDL_Quit();

    ArenaDestroy(nones->arena);
}

static void NonesReset(Nones *nones)
{
    SystemReset(nones->system);
}

static void NonesSetIntegerScale(Nones *nones, int scale)
{
    SDL_SetWindowSize(nones->window, SCREEN_WIDTH * scale, SCREEN_HEIGHT * scale);
    SDL_SetRenderScale(nones->renderer, scale, scale);
    SDL_SetWindowPosition(nones->window,  SDL_WINDOWPOS_CENTERED,  SDL_WINDOWPOS_CENTERED);
}

void NonesRun(Nones *nones, const char *path)
{
    NonesInit(nones, path);

    // Allocate pixel buffers (back and front)
    uint32_t *buffers[2];
    const uint32_t buffer_size = (SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));
    buffers[0] = ArenaPush(nones->arena, buffer_size);
    buffers[1] = ArenaPush(nones->arena, buffer_size);

    SystemInit(nones->system, buffers);

    bool quit = false;
    bool buttons[16];
    SDL_Event event;
    void *raw_pixels;
    int raw_pitch;

    //uint64_t frames = 0, updates = 0;
    //uint64_t timer = SDL_GetTicks();

    char debug_cpu[128] = {'\0'};
    bool debug_stats = false;
    bool paused = false;
    bool step_frame = false;
    bool step_instr = false;
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
                        case SDLK_F2:
                            NonesReset(nones);
                            break;
                        case SDLK_F6:
                            paused = !paused;
                            break;
                        case SDLK_F10:
                            step_frame = true;
                            break;
                        case SDLK_F11:
                            step_instr = true;
                            break;
                    }
                    break;
            }
        }

        const bool *kb_state  = SDL_GetKeyboardState(NULL);

        buttons[0] = kb_state[SDL_SCANCODE_SPACE]  || SDL_GetGamepadButton(nones->gamepad1, SDL_GAMEPAD_BUTTON_EAST);
        buttons[1] = kb_state[SDL_SCANCODE_LSHIFT] || SDL_GetGamepadButton(nones->gamepad1, SDL_GAMEPAD_BUTTON_SOUTH);
        buttons[2] = kb_state[SDL_SCANCODE_UP]     || kb_state[SDL_SCANCODE_W] || SDL_GetGamepadButton(nones->gamepad1, SDL_GAMEPAD_BUTTON_DPAD_UP);
        buttons[3] = kb_state[SDL_SCANCODE_DOWN]   || kb_state[SDL_SCANCODE_S] || SDL_GetGamepadButton(nones->gamepad1, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        buttons[4] = kb_state[SDL_SCANCODE_LEFT]   || kb_state[SDL_SCANCODE_A] || SDL_GetGamepadButton(nones->gamepad1, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        buttons[5] = kb_state[SDL_SCANCODE_RIGHT]  || kb_state[SDL_SCANCODE_D] || SDL_GetGamepadButton(nones->gamepad1, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        buttons[6] = kb_state[SDL_SCANCODE_RETURN] || SDL_GetGamepadButton(nones->gamepad1, SDL_GAMEPAD_BUTTON_START);
        buttons[7] = kb_state[SDL_SCANCODE_TAB]    || SDL_GetGamepadButton(nones->gamepad1, SDL_GAMEPAD_BUTTON_BACK);

        buttons[8]  = SDL_GetGamepadButton(nones->gamepad2, SDL_GAMEPAD_BUTTON_EAST);
        buttons[9]  = SDL_GetGamepadButton(nones->gamepad2, SDL_GAMEPAD_BUTTON_SOUTH);
        buttons[10] = SDL_GetGamepadButton(nones->gamepad2, SDL_GAMEPAD_BUTTON_DPAD_UP);
        buttons[11] = SDL_GetGamepadButton(nones->gamepad2, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        buttons[12] = SDL_GetGamepadButton(nones->gamepad2, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        buttons[13] = SDL_GetGamepadButton(nones->gamepad2, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        buttons[14] = SDL_GetGamepadButton(nones->gamepad2, SDL_GAMEPAD_BUTTON_START);
        buttons[15] = SDL_GetGamepadButton(nones->gamepad2, SDL_GAMEPAD_BUTTON_BACK);

        SystemUpdateJPButtons(nones->system, buttons);

        if (kb_state[SDL_SCANCODE_ESCAPE])
            quit = true;
        else if (kb_state[SDL_SCANCODE_1])
            NonesSetIntegerScale(nones, 1);
        else if (kb_state[SDL_SCANCODE_2])
            NonesSetIntegerScale(nones, 2);
        else if (kb_state[SDL_SCANCODE_3])
            NonesSetIntegerScale(nones, 3);
        else if (kb_state[SDL_SCANCODE_4])
            NonesSetIntegerScale(nones, 4);
        else if (kb_state[SDL_SCANCODE_5])
            NonesSetIntegerScale(nones, 5);

        SystemRun(nones->system, paused, step_instr, step_frame);
        if (step_instr | step_frame)
        {
            step_instr = step_frame = false;
            paused = true;
        }

        SDL_LockTexture(nones->texture, NULL, &raw_pixels, &raw_pitch);
        memcpy(raw_pixels, nones->system->ppu->buffers[1], buffer_size);
        SDL_UnlockTexture(nones->texture);

        SDL_RenderClear(nones->renderer);
        SDL_RenderTexture(nones->renderer, nones->texture, NULL, NULL);

        if (debug_stats)
        {
            SDL_SetRenderDrawColor(nones->renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
            snprintf(debug_cpu, sizeof(debug_cpu), "A:%02X X:%02X Y:%02X S:%02X P:%02X", nones->system->cpu->a,
                     nones->system->cpu->x, nones->system->cpu->y, nones->system->cpu->sp, nones->system->cpu->status.raw);

            SDL_RenderDebugText(nones->renderer, 2, 1, debug_cpu);
            SDL_RenderDebugText(nones->renderer, 2, 9, nones->system->cpu->debug_msg);
        }

        SDL_RenderPresent(nones->renderer);
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

    NonesShutdown(nones);
}

