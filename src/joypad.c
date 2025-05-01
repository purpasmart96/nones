#include <stdbool.h>
#include <stdint.h>
#include "joypad.h"

//static JoyPad joy_pad;

void WriteJoyPadReg(JoyPad *joy_pad, uint8_t data)
{
    joy_pad->strobe = data & 1;

    if (joy_pad->strobe)
    {
        joy_pad->button_index = 0;
    }
}

uint8_t ReadJoyPadReg(JoyPad *joy_pad)
{
    if (joy_pad->button_index > 7)
    {
        return 1;
    }

    uint8_t response = (joy_pad->button_status & (1 << joy_pad->button_index)) >> joy_pad->button_index;

    if (!joy_pad->strobe && joy_pad->button_index <= 7)
    {
        joy_pad->button_index += 1;
    }

    return response;
}

// Set button pressed state
void JoyPadSetButton(JoyPad *joy_pad, JoypadButton button, bool pressed)
{
    if (pressed)
    {
        joy_pad->button_status |= button;
    }
    else
    {
        joy_pad->button_status &= ~button;
    }
}
