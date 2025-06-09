#ifndef NONES_H
#define NONES_H

//#define SCREEN_WIDTH 340
//#define SCREEN_HEIGHT 260
#define SCREEN_WIDTH 256
#define SCREEN_HEIGHT 240
#define FRAMERATE 60
//#define FRAMERATE 60.098477556112265
#define FRAME_TIME_MS (1000.0 / FRAMERATE)
#define FRAME_TIME_NS (1000000000.0 / FRAMERATE)

typedef struct {
    Arena *arena;
    System *system;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    int num_gamepads;
    SDL_Gamepad *gamepad1;
    SDL_Gamepad *gamepad2;
    SDL_JoystickID *gamepads;
} Nones;

void NonesRun(Nones *nones, const char *path);
void NonesPutSoundData(Apu *apu);

#endif
