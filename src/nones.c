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

#define HIGH_RATE_SAMPLES 14890
#define LOW_RATE_SAMPLES 735
//#define LOW_RATE_SAMPLES 800

static SDL_AudioStream *stream = NULL;

static void SampleTo44khz(const float *high_rate_buffer, int16_t *output_44khz_buffer)
{
    const double step = (double)(HIGH_RATE_SAMPLES) / LOW_RATE_SAMPLES;

    double pos = 0.0;
    for (int i = 0; i < LOW_RATE_SAMPLES; i++)
    {
        int index = (int)pos;
        double frac = pos - index;

        // Simple linear interpolation
        float a = high_rate_buffer[index];
        float b = (index + 1 < (HIGH_RATE_SAMPLES)) ? high_rate_buffer[index + 1] : a;

        float sample = (float)((1.0 - frac) * a + frac * b);
        // convert to s16
        output_44khz_buffer[i] = (int16_t)(sample * 32767);

        pos += step;
    }
}

void NonesPutSoundData(Apu *apu)
{
    // Buffer size of 4096 samples
    const int minimum_audio = (4096 * sizeof(int16_t));
    if (SDL_GetAudioStreamQueued(stream) < minimum_audio)
    {
        SampleTo44khz(apu->buffer, apu->outbuffer);

        SDL_PutAudioStreamData(stream, apu->outbuffer, sizeof(apu->outbuffer));
    }
}

static void NonesDrawDebugInfo(Nones *nones, NonesInfo *info)
{
    if (!nones->debug_info)
        return;

    SDL_SetRenderDrawColor(nones->renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
    snprintf(info->cpu_msg, sizeof(info->cpu_msg), "A:%02X X:%02X Y:%02X S:%02X P:%02X", nones->system->cpu->a,
             nones->system->cpu->x, nones->system->cpu->y, nones->system->cpu->sp, nones->system->cpu->status.raw);

    SDL_RenderDebugText(nones->renderer, 2, 1, info->cpu_msg);
    SDL_RenderDebugText(nones->renderer, 2, 9, nones->system->cpu->debug_msg);

    ++info->frames;
    if (SDL_GetTicks() - info->timer >= 1000)
    {
        snprintf(info->fps_msg, sizeof(info->fps_msg), "FPS:%lu", info->frames);
        snprintf(info->ups_msg, sizeof(info->ups_msg), "UPS:%lu", info->updates);
        info->updates = info->frames = 0;
        info->timer += 1000;
    }

    SDL_SetRenderDrawColor(nones->renderer, 255, 255, 84, SDL_ALPHA_OPAQUE);
    SDL_RenderDebugText(nones->renderer, 200, 1, info->fps_msg);
    SDL_RenderDebugText(nones->renderer, 200, 9, info->ups_msg);
}

static void NonesSetIntegerScale(Nones *nones, int scale)
{
    SDL_SetWindowSize(nones->window, SCREEN_WIDTH * scale, SCREEN_HEIGHT * scale);
    SDL_SetRenderScale(nones->renderer, scale, scale);
    //SDL_SetRenderLogicalPresentation(nones->renderer, SCREEN_WIDTH * scale, SCREEN_HEIGHT * scale, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    // Doesn't work on Wayland...
    SDL_SetWindowPosition(nones->window,  SDL_WINDOWPOS_CENTERED,  SDL_WINDOWPOS_CENTERED);
}

static void NonesHandleInput(Nones *nones)
{
    const bool *kb_state  = SDL_GetKeyboardState(NULL);

    nones->buttons[0] = kb_state[SDL_SCANCODE_SPACE];
    nones->buttons[1] = kb_state[SDL_SCANCODE_LSHIFT];
    nones->buttons[2] = kb_state[SDL_SCANCODE_UP] || kb_state[SDL_SCANCODE_W];
    nones->buttons[3] = kb_state[SDL_SCANCODE_DOWN] || kb_state[SDL_SCANCODE_S];
    nones->buttons[4] = kb_state[SDL_SCANCODE_LEFT] || kb_state[SDL_SCANCODE_A];
    nones->buttons[5] = kb_state[SDL_SCANCODE_RIGHT] || kb_state[SDL_SCANCODE_D];
    nones->buttons[6] = kb_state[SDL_SCANCODE_RETURN];
    nones->buttons[7] = kb_state[SDL_SCANCODE_TAB];

    if (nones->gamepad1)
    {
        nones->buttons[0] |= SDL_GetGamepadButton(nones->gamepad1, SDL_GAMEPAD_BUTTON_EAST);
        nones->buttons[1] |= SDL_GetGamepadButton(nones->gamepad1, SDL_GAMEPAD_BUTTON_SOUTH);
        nones->buttons[2] |= SDL_GetGamepadButton(nones->gamepad1, SDL_GAMEPAD_BUTTON_DPAD_UP);
        nones->buttons[3] |= SDL_GetGamepadButton(nones->gamepad1, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        nones->buttons[4] |= SDL_GetGamepadButton(nones->gamepad1, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        nones->buttons[5] |= SDL_GetGamepadButton(nones->gamepad1, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        nones->buttons[6] |= SDL_GetGamepadButton(nones->gamepad1, SDL_GAMEPAD_BUTTON_START);
        nones->buttons[7] |= SDL_GetGamepadButton(nones->gamepad1, SDL_GAMEPAD_BUTTON_BACK);
    }

    if (nones->gamepad2)
    {
        nones->buttons[8]  = SDL_GetGamepadButton(nones->gamepad2, SDL_GAMEPAD_BUTTON_EAST);
        nones->buttons[9]  = SDL_GetGamepadButton(nones->gamepad2, SDL_GAMEPAD_BUTTON_SOUTH);
        nones->buttons[10] = SDL_GetGamepadButton(nones->gamepad2, SDL_GAMEPAD_BUTTON_DPAD_UP);
        nones->buttons[11] = SDL_GetGamepadButton(nones->gamepad2, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        nones->buttons[12] = SDL_GetGamepadButton(nones->gamepad2, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        nones->buttons[13] = SDL_GetGamepadButton(nones->gamepad2, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        nones->buttons[14] = SDL_GetGamepadButton(nones->gamepad2, SDL_GAMEPAD_BUTTON_START);
        nones->buttons[15] = SDL_GetGamepadButton(nones->gamepad2, SDL_GAMEPAD_BUTTON_BACK);
    }

    SystemUpdateJPButtons(nones->system, nones->buttons);

    nones->quit |= kb_state[SDL_SCANCODE_ESCAPE];

    // TODO: This could be just done when polling for events
    if (kb_state[SDL_SCANCODE_1])
        NonesSetIntegerScale(nones, 1);
    else if (kb_state[SDL_SCANCODE_2])
        NonesSetIntegerScale(nones, 2);
    else if (kb_state[SDL_SCANCODE_3])
        NonesSetIntegerScale(nones, 3);
    else if (kb_state[SDL_SCANCODE_4])
        NonesSetIntegerScale(nones, 4);
    else if (kb_state[SDL_SCANCODE_5])
        NonesSetIntegerScale(nones, 5);
}

static void NonesInit(Nones *nones, const char *path, const char *audio_driver)
{
    memset(nones, 0, sizeof(*nones));
    nones->arena = ArenaCreate(1024 * 1024 * 3);
    nones->system = SystemCreate(nones->arena);

    if (SystemLoadCart(nones->arena, nones->system, path))
    {
        ArenaDestroy(nones->arena);
        exit(EXIT_FAILURE);
    }

    if (audio_driver != NULL)
    {
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, audio_driver);
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))
    {
        SDL_Log("SDL Init Error: %s", SDL_GetError());
        ArenaDestroy(nones->arena);
        exit(EXIT_FAILURE);
    }

    SDL_Log("SDL audio driver: %s\n", SDL_GetCurrentAudioDriver());

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
        ArenaDestroy(nones->arena);
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

    // Handles textures as well, so no need to call SDL_DestroyTexture here
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

void NonesRun(Nones *nones, bool ppu_warmup, const char *path, const char *audio_driver)
{
    NonesInit(nones, path, audio_driver);

    // Allocate pixel buffers (back and front)
    uint32_t *buffers[2];
    const uint32_t buffer_size = (SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));
    buffers[0] = ArenaPush(nones->arena, buffer_size);
    buffers[1] = ArenaPush(nones->arena, buffer_size);

    SystemInit(nones->system, ppu_warmup, buffers, buffer_size);
    SDL_Event event;
    void *raw_pixels;
    int raw_pitch;

    NonesInfo info = {
        .cpu_msg = {'\0'},
        .fps_msg = {'\0'},
        .ups_msg = {'\0'},
        .frames = 0,
        .updates = 0,
        .timer = SDL_GetTicks(),
    };

    uint64_t previous_time = 0;
    uint64_t current_time = 0;
    uint64_t accumulator = 0;

    while (!nones->quit)
    {
        uint64_t start_time = SDL_GetTicksNS();
        previous_time = current_time;
        current_time = start_time;
        uint64_t delta_time = current_time - previous_time;
        accumulator += delta_time;

        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
                case SDL_EVENT_QUIT:
                    nones->quit = true;
                    break;
                case SDL_EVENT_KEY_UP:
                    switch (event.key.key)
                    {
                        case SDLK_F1:
                            nones->debug_info = !nones->debug_info;
                            break;
                        case SDLK_F2:
                            NonesReset(nones);
                            break;
                        case SDLK_F6:
                            nones->state ^= PAUSED;
                            break;
                        case SDLK_F10:
                            nones->state = STEP_FRAME;
                            break;
                        case SDLK_F11:
                            nones->state = STEP_INSTR;
                            break;
                    }
                    break;
            }
        }

        NonesHandleInput(nones);

        while (accumulator >= FRAME_TIME_NS)
        {
            SystemRun(nones->system, nones->state, nones->debug_info);

            if (nones->state > PAUSED)
                nones->state = PAUSED;

            accumulator -= FRAME_TIME_NS;
            ++info.updates;
        }

        SDL_LockTexture(nones->texture, NULL, &raw_pixels, &raw_pitch);
        memcpy(raw_pixels, nones->system->ppu->buffers[1], buffer_size);
        SDL_UnlockTexture(nones->texture);

        SDL_RenderClear(nones->renderer);
        SDL_RenderTexture(nones->renderer, nones->texture, NULL, NULL);

        NonesDrawDebugInfo(nones, &info);

        SDL_RenderPresent(nones->renderer);

        uint64_t frame_time = SDL_GetTicksNS() - start_time;
        if (frame_time < FRAME_CAP_NS)
        {
            SDL_DelayNS(FRAME_CAP_NS - frame_time);
        }
    }

    NonesShutdown(nones);
}

