#include "keys.h"
#include <linux/input-event-codes.h>

bool is_key_or_mouse_button(uint32_t keycode) {
    return (keycode >= KEY_ESC && keycode <= KEY_KPDOT)
        || (keycode >= KEY_ZENKAKUHANKAKU && keycode <= KEY_F24)
        || (keycode >= KEY_PLAYCD && keycode <= KEY_MICMUTE)
        || (keycode >= BTN_MISC && keycode <= BTN_TASK)
        || (keycode >= BTN_JOYSTICK && keycode <= BTN_THUMBR)
        || (keycode >= BTN_DIGI && keycode <= BTN_GEAR_UP)
        || (keycode >= KEY_OK && keycode <= KEY_IMAGES)
        || (keycode >= KEY_DEL_EOL && keycode <= KEY_DEL_LINE)
        || (keycode >= KEY_FN && keycode <= KEY_FN_B)
        || (keycode >= KEY_BRL_DOT1 && keycode <= KEY_BRL_DOT10)
        || (keycode >= KEY_NUMERIC_0 && keycode <= KEY_LIGHTS_TOGGLE)
        || (keycode >= BTN_DPAD_UP && keycode <= BTN_DPAD_RIGHT)
        || (keycode == KEY_ALS_TOGGLE)
        || (keycode >= KEY_BUTTONCONFIG && keycode <= KEY_VOICECOMMAND)
        || (keycode >= KEY_BRIGHTNESS_MIN && keycode <= KEY_BRIGHTNESS_MAX)
        || (keycode >= KEY_KBDINPUTASSIST_PREV && keycode <= KEY_ONSCREEN_KEYBOARD);
}

bool is_mouse_button(uint32_t keycode) {
    return (keycode >= BTN_MOUSE && keycode <= BTN_TASK);
}
