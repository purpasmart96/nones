#ifndef NONES_H
#define NONES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "cpu.h"
#include "ppu.h"
#include "apu.h"
#include "arena.h"
#include "loader.h"
#include "arena.h"
#include "bus.h"

//#define SCREEN_WIDTH 340
//#define SCREEN_HEIGHT 260
#define SCREEN_WIDTH 256
#define SCREEN_HEIGHT 240
//#define FRAMERATE 60
#define FRAMERATE 60.098477556112265
#define FRAME_TIME_MS (1000.0 / FRAMERATE)
#define FRAME_TIME_NS (1000000000.0 / FRAMERATE)

typedef struct {
    Arena *arena;
    Bus *bus;
} Nones;

void NonesRun(Nones *nones, const char *path);

#endif
