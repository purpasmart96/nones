# Nones

A simple NTSC NES emulator written in C.

### Current State

NROM, UxROM, AxRom, CNROM, MMC1, are currently supported along with most MMC3 based games.

Supports all of the original NES audio channels, which includes square, triangle, noise, and dmc audio channels.

Basic gamepad support, currently, button layout is fixed to how it was on the original joypad.

### Building on Linux

1. Install the required dependencies using your system's package manager

- ##### Debian

    `sudo apt install gcc make libsdl3-dev` 

- ##### Fedora

    `sudo dnf install gcc make SDL3-devel`

- ##### Arch
    `sudo pacman -S gcc make sdl3`

2. run `make` in the project root directory to create the binary

### Building on MacOS

1. Install the Homebrew package manager

2. Install the required dependencies `brew install gcc make sdl3`

3. run `make` in the project root directory to create the binary

### Building on Windows

1. Install `MSYS2`

2. Launch the UCRT64 environment that MSYS2 created and run the following command inside the terminal to install the required packages: `pacman -S mingw-w64-ucrt-x86_64-gcc make mingw-w64-ucrt-x86_64-sdl3 p7zip git`

3. Run the following commannd to download the repo: `git clone https://github.com/purpasmart96/nones.git`

4. run `make` in the project root directory to create the binary

### Running

After building you should be able run the program via `./nones "game.nes"`

### Commands/Hotkeys:

* `Esc`

Exit emulator

* `F1`

Enable/Disable CPU debug stats

* `F2`

Soft Reset

* `F6`

Pause/Unpause

* `F10`

Step by one frame and pause

* `F11`

Step by one instruction and pause
