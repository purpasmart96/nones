#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "system.h"
#include <SDL3/SDL.h>
#include "nones.h"

#define VERSION "v0.3.1"

static void About(void)
{
    printf("nones " VERSION " by Matt W\n");
}

static void Usage(void)
{
    About();
    printf("Usage: nones \"game.nes\" [options...]\n\n");
}

static void Help(void)
{
    Usage();
    printf("Options:\n"
           "  --help                              Display this information\n"
           "  --version                           Display version information\n"
           "  --sdl-audio-driver=\"driver-name\"    Set the preferred audio driver for SDL to use\n"
           "  --ppu-warmup                        Enable the ppu warm up delay found on the NES-001(Will break some famicom games)\n");
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("No file was provided\n");
        Usage();
        return EXIT_FAILURE;
    }

    bool ppu_warmup = false;
    bool override_audio_driver = false;
    char audio_driver[128] = {"\0"};

    for (int i = 1; i < argc; i++)
    {
        if (i > 1 && strlen(argv[i]) > 127)
            continue;

        if (!strcmp((argv[i]), "--help"))
        {
            Help();
            return EXIT_SUCCESS;
        }

        if (!strcmp((argv[i]), "--version"))
        {
            About();
            return EXIT_SUCCESS;
        }

        if (!strcmp((argv[i]), "--ppu-warmup"))
            ppu_warmup = true;

        if (strstr((argv[i]), "--sdl-audio-driver="))
        {
            char *delim_pos = strchr(argv[i], '=');
            if (delim_pos != NULL)
            {
                override_audio_driver = true;
                char *driver_name = delim_pos + 1;
                snprintf(audio_driver, sizeof(audio_driver), "%s", driver_name);
            }
        }
    }

    Nones nones;
    NonesRun(&nones, ppu_warmup, argv[1], override_audio_driver ? audio_driver : NULL);
    return EXIT_SUCCESS;
}
