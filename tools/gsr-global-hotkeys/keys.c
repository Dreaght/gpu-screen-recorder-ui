#include "keys.h"
#include <linux/input-event-codes.h>

bool is_key_or_mouse_button(uint32_t keycode) {
    return (keycode >= KEY_ESC && keycode <= KEY_KPDOT)
        || (keycode >= KEY_ZENKAKUHANKAKU && keycode <= KEY_F24)
        || (keycode >= KEY_PLAYCD && keycode <= KEY_MICMUTE)
        || (keycode >= BTN_MISC && keycode <= BTN_TASK)
        || (keycode >= BTN_JOYSTICK && keycode <= BTN_THUMBR)
        || (keycode >= BTN_DIGI && keycode <= BTN_GEAR_UP)
        || (keycode >= KEY_OK && keycode <= KEY_HANGUP_PHONE)
        || (keycode >= KEY_DEL_EOL && keycode <= KEY_DEL_LINE)
        || (keycode >= KEY_FN && keycode <= KEY_FN_RIGHT_SHIFT)
        || (keycode >= KEY_BRL_DOT1 && keycode <= KEY_BRL_DOT10)
        || (keycode >= KEY_NUMERIC_0 && keycode <= KEY_LIGHTS_TOGGLE)
        || (keycode >= BTN_DPAD_UP && keycode <= KEY_BRIGHTNESS_MAX)
        || (keycode >= KEY_KBDINPUTASSIST_PREV && keycode <= KEY_MACRO30)
        || (keycode >= KEY_MACRO_RECORD_START && keycode <= KEY_MACRO_PRESET3)
        || (keycode >= KEY_KBD_LCD_MENU1 && keycode <= KEY_KBD_LCD_MENU5)
        || (keycode >= BTN_TRIGGER_HAPPY && keycode <= BTN_TRIGGER_HAPPY40);
}

bool is_mouse_button(uint32_t keycode) {
    return (keycode >= BTN_MISC && keycode <= BTN_GEAR_UP)
        || (keycode >= BTN_TRIGGER_HAPPY && keycode <= BTN_TRIGGER_HAPPY40)
        || (keycode >= BTN_DPAD_UP && keycode <= BTN_DPAD_RIGHT);
}
