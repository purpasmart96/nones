# Nones

A small cycle accurate NTSC NES emulator written in C.

### Current State

Games that use the following mapper chips are supported:
<table>
    <tr>
        <td align="center">Mapper name</td>
        <td align="center">Mapper number</td>
    </tr>
    <tr>
        <td align="center">NROM</td><td align="center">0</td>
    </tr>
    <tr>
        <td align="center">MMC1</td><td align="center">1</td>
    </tr>
    <tr>
        <td align="center">UxROM</td><td align="center">2</td>
    </tr>
    <tr>
        <td align="center">CNROM</td><td align="center">3</td>
    </tr>
    <tr>
        <td align="center">MMC3</td><td align="center">4</td>
    </tr>
    <tr>
        <td align="center">AxROM</td><td align="center">7</td>
    </tr>
    <tr>
        <td align="center">Color Dreams</td><td align="center">11</td>
    </tr>
    <tr>
        <td align="center">Ninja + BNROM</td><td align="center">34</td>
    </tr>
    <tr>
        <td align="center">Nanjing</td><td align="center">163</td>
    </tr>
</table>

Supports all of the original NES audio channels, which includes square, triangle, noise, and dmc audio channels.

Basic gamepad support for up to two players, currently, button layout is fixed to how it was on the original joypad.

### Building on Linux

1. Install the required dependencies using your system's package manager

- ##### Debian (13/trixie+ only)

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

You can also apply additional arguments after specifying the rom path, which include the following:

* `--sdl-audio-driver="driver-name"`

Set the preferred audio driver for SDL to use.

* `--ppu-warmup`

Enable the ppu warm up delay found on the NES-001. (Will break some famicom games)

* `--apu-swap-duty-cycles`

Enable the use of swapped duty cycles for the square/pulse channels (Needed for older famiclone games)

### Hotkeys:

* `1 -> 5`

Set window size integer scale (1x, 2x, 3x ect. 5x max)

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
