#include <stdio.h>
#include <stdlib.h>

#include "nones.h"

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        printf("No file was provided\n");
        return EXIT_FAILURE;
    }

    Nones nones;
    NonesRun(&nones, argv[1]);
    return EXIT_SUCCESS;
}
