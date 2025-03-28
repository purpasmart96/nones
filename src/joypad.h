#ifndef JOYPAD_H
#define JOYPAD_H

typedef enum
{
    JOYPAD_A      = 1 << 0,
    JOYPAD_B      = 1 << 1,
    JOYPAD_SELECT = 1 << 2,
    JOYPAD_START  = 1 << 3,
    JOYPAD_UP     = 1 << 4,
    JOYPAD_DOWN   = 1 << 5,
    JOYPAD_LEFT   = 1 << 6,
    JOYPAD_RIGHT  = 1 << 7
} JoypadButton;

typedef struct
{
    bool strobe;
    uint8_t button_index;
    uint8_t button_status;
} JoyPad;

void JoyPadSetButton(JoypadButton button, bool pressed);
void WriteJoyPadReg(uint8_t data);
uint8_t ReadJoyPadReg(void);

#endif
