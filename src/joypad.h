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

typedef enum {
    JOYPAD_BUTTON_RIGHT  = 0b10000000,
    JOYPAD_BUTTON_LEFT   = 0b01000000,
    JOYPAD_BUTTON_DOWN   = 0b00100000,
    JOYPAD_BUTTON_UP     = 0b00010000,
    JOYPAD_BUTTON_START  = 0b00001000,
    JOYPAD_BUTTON_SELECT = 0b00000100,
    JOYPAD_BUTTON_B      = 0b00000010,
    JOYPAD_BUTTON_A      = 0b00000001
} JoypadButton;

void joypad_set_button_pressed_status(JoypadButton button, bool pressed);
//extern uint8_t g_joypad_reg;

//void WriteJoyPadReg(Buttons button);
void WriteJoyPadReg(uint8_t data);
uint8_t ReadJoyPadReg();

#endif
