#include <stdbool.h>
#include <stdint.h>
#include "joypad.h"

static JoyPad joy_pad;

void WriteJoyPadReg(uint8_t data)
{
    joy_pad.strobe = (data & 1);
    //joy_pad.strobe = !joy_pad.strobe;
    if (joy_pad.strobe)
    {
        joy_pad.button_index = 0;
    }
    //switch (button) {
    //    case JOYPAD_A:
    //    {
    //        joy_pad.input = JOYPAD_A;
    //        joy_pad.status = 0b00000001;
    //        joy_pad.strobe
    //        break;
    //    }
    //    case JOYPAD_B:
    //        break;
    //    case JOYPAD_SELECT:
    //        g_joypad_reg = 0b00000100;
    //    case JOYPAD_START:
    //    case JOYPAD_UP:
    //    case JOYPAD_DOWN:
    //    case JOYPAD_LEFT:
    //    case JOYPAD_RIGHT:
    //        break;
    //    }
    //joy_pad.strobe = !joy_pad.strobe;
}


uint8_t ReadJoyPadReg(void)
{
    if (joy_pad.button_index > 7)
    {
        return 1;
    }

    uint8_t response = (joy_pad.button_status & (1 << joy_pad.button_index)) >> joy_pad.button_index;

    if (!joy_pad.strobe && joy_pad.button_index <= 7)
    {
        joy_pad.button_index += 1;
    }

    return response;
}

// Set button pressed state
void JoyPadSetButton(JoypadButton button, bool pressed)
{
    if (pressed)
    {
        joy_pad.button_status |= button;
    }
    else
    {
        joy_pad.button_status &= ~button;
    }
}
