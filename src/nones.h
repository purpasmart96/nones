#ifndef NONES_H
#define NONES_H

//#define SCREEN_WIDTH 340
//#define SCREEN_HEIGHT 260
#define SCREEN_WIDTH 256
#define SCREEN_HEIGHT 240
#define FRAMERATE 60
#define FRAMECAP 500
//#define FRAMERATE 60.098477556112265
#define FRAME_TIME_MS (1000.0 / FRAMERATE)
#define FRAME_CAP_MS (1000.0 / FRAMECAP)
#define FRAME_TIME_NS (1000000000.0 / FRAMERATE)
#define FRAME_CAP_NS (1000000000.0 / FRAMECAP)

typedef struct
{
    char cpu_msg[128];
    char fps_msg[8];
    char ups_msg[8];
    uint64_t frames;
    uint64_t updates;
    uint64_t timer;
} NonesInfo;

typedef struct {
    bool buttons[16];
    Arena *arena;
    System *system;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Gamepad *gamepad1;
    SDL_Gamepad *gamepad2;
    SDL_JoystickID *gamepads;
    int num_gamepads;
    SystemState state;
    bool debug_info;
    bool quit;
} Nones;

void NonesRun(Nones *nones, const char *path);
void NonesPutSoundData(Apu *apu);

#endif
