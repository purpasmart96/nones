#ifndef JOYPAD_H
#define JOYPAD_H

typedef enum 
{
    JOYPAD_A,
    JOYPAD_B,
    JOYPAD_SELECT,
    JOYPAD_START,
    JOYPAD_UP,
    JOYPAD_DOWN,
    JOYPAD_LEFT,
    JOYPAD_RIGHT
} Buttons;

typedef enum
{
    JOYPAD_BUTTON_RIGHT  = 1 << 7,
    JOYPAD_BUTTON_LEFT   = 1 << 6,
    JOYPAD_BUTTON_DOWN   = 1 << 5,
    JOYPAD_BUTTON_UP     = 1 << 4,
    JOYPAD_BUTTON_START  = 1 << 3,
    JOYPAD_BUTTON_SELECT = 1 << 2,
    JOYPAD_BUTTON_B      = 1 << 1,
    JOYPAD_BUTTON_A      = 1 << 0,
} JoypadButton;

void JoyPadSetButton(JoypadButton button, bool pressed);

//void WriteJoyPadReg(Buttons button);
void WriteJoyPadReg(uint8_t data);
uint8_t ReadJoyPadReg();

#endif
