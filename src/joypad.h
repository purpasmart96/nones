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

typedef union
{
    uint8_t raw;
    struct
    {
        uint8_t a : 1;
        uint8_t b : 1;
        uint8_t select : 1;
        uint8_t start : 1;
        uint8_t up : 1;
        uint8_t down : 1;
        uint8_t left : 1;
        uint8_t right : 1;
    };

} ButtonStatus;

typedef struct
{
    bool strobe;
    uint8_t button_index;
    ButtonStatus button_status;
} JoyPad;

void JoyPadSetButton(JoyPad *joy_pad, JoypadButton button, bool pressed);
void WriteJoyPadReg(JoyPad *joy_pad, uint8_t data);
uint8_t ReadJoyPadReg(JoyPad *joy_pad);

#endif
