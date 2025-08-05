#include <stdbool.h>
#include <stdint.h>
#include "joypad.h"


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

    uint8_t response = (joy_pad->button_status.raw & (1 << joy_pad->button_index)) >> joy_pad->button_index;

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
        ButtonStatus prev_status = joy_pad->button_status;
        joy_pad->button_status.raw |= button;

        joy_pad->button_status.up &= ~prev_status.down;
        joy_pad->button_status.down &= ~prev_status.up;
        joy_pad->button_status.left &= ~prev_status.right;
        joy_pad->button_status.right &= ~prev_status.left;
    }
    else
    {
        joy_pad->button_status.raw &= ~button;
    }
}
