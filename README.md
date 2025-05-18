# Nones

A simple NTSC NES emulator written in C.

### Current State

Currently only NROM, UxROM, MMC1, and some MMC3 based games are currently supported.

Supports all of the origianl NES audio channels, which includes square, triangle, noise, and dmc audio channels.

Basic gamepad support, currently, button layout is fixed to how it was on the original joypad.

### Building and running

Requires a C11 compiler and SDL3

Run `make` in the project root directory to create the binary

You should be able run the program via `./nones "game.nes"`

### Commands/Hotkeys:

* `Esc`

Exit emulator

* `F1`

Enable/Disable CPU debug stats

* `F2`

Soft Reset

* `F6`

Pause/Unpause

* `F11`

Step by one instruction and pause
