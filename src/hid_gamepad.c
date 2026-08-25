// Turns a raw HID gamepad report into directions and buttons.
//
// Gamepads do not agree on a report layout: one pad puts its stick in a hat switch, the next one
// reports X and Y and never touches the hat, and a DualShock 3 has its d-pad in its buttons.
// Where those live comes from the report descriptor, see hid_layout.c. What the descriptor cannot
// express comes from the quirk table below.
//
// What a button means is deliberately not decided here. A launcher wants a confirm key, an
// emulator wants a fire button, and neither belongs in a report decoder.

#include "hid_gamepad.h"
#include <string.h>
#include "esp_log.h"

static const char* TAG = "hid_gamepad";

// A DualShock 3 enumerates and hands out its report descriptor, but stays silent until the host
// asks it to start reporting. It has no hat switch either, its d-pad sits in buttons five
// through eight.
static const uint8_t dualshock3_enable_reporting[] = {0x42, 0x0c, 0x00, 0x00};

static const hid_gamepad_quirk_t hid_gamepad_quirks[] = {
    {
        .vid                  = 0x054c,
        .pid                  = 0x0268,
        .name                 = "DualShock 3",
        .enable_report_id     = 0xf4,
        .enable_report        = dualshock3_enable_reporting,
        .enable_report_length = sizeof(dualshock3_enable_reporting),
        .dpad_first_button    = 4,
    },
};

const hid_gamepad_quirk_t* hid_gamepad_find_quirk(uint16_t vid, uint16_t pid) {
    for (size_t i = 0; i < sizeof(hid_gamepad_quirks) / sizeof(hid_gamepad_quirks[0]); i++) {
        if (hid_gamepad_quirks[i].vid == vid && hid_gamepad_quirks[i].pid == pid) {
            return &hid_gamepad_quirks[i];
        }
    }
    return NULL;
}

void hid_gamepad_close(hid_gamepad_t* gamepad) {
    if (gamepad != NULL) {
        memset(gamepad, 0, sizeof(*gamepad));
    }
}

bool hid_gamepad_is_open(const hid_gamepad_t* gamepad) {
    return gamepad != NULL && gamepad->layout.valid;
}

bool hid_gamepad_open(hid_gamepad_t* gamepad, const uint8_t* report_descriptor, size_t length, uint16_t vid,
                      uint16_t pid) {
    if (gamepad == NULL) {
        return false;
    }
    hid_gamepad_close(gamepad);

    if (!hid_layout_parse(report_descriptor, length, &gamepad->layout)) {
        ESP_LOGW(TAG, "Nothing usable in the report descriptor, ignoring this device");
        return false;
    }

    // A mouse has an X and a Y axis too. What sets a gamepad apart is that its axes report where
    // the stick is rather than how far something moved.
    bool absolute_axes = (gamepad->layout.x.present && !gamepad->layout.x.relative) ||
                         (gamepad->layout.y.present && !gamepad->layout.y.relative);
    if (!absolute_axes && !gamepad->layout.hat.present) {
        ESP_LOGW(TAG, "No usable directions in the report descriptor, ignoring this device");
        hid_gamepad_close(gamepad);
        return false;
    }

    const hid_gamepad_quirk_t* quirk = hid_gamepad_find_quirk(vid, pid);
    if (quirk != NULL) {
        gamepad->quirk = quirk;
        if (quirk->dpad_first_button != HID_GAMEPAD_NO_DPAD_BUTTONS && gamepad->layout.buttons.present &&
            gamepad->layout.button_count > quirk->dpad_first_button + 3) {
            gamepad->dpad_is_buttons = true;
            gamepad->dpad_first      = (uint16_t)quirk->dpad_first_button;
            ESP_LOGI(TAG, "%s: buttons %d to %d are a d-pad", quirk->name, quirk->dpad_first_button + 1,
                     quirk->dpad_first_button + 4);
        }
    }

    ESP_LOGI(TAG, "Gamepad layout: report id %d, x %d, y %d, hat %d, %d buttons at %d", gamepad->layout.report_id,
             gamepad->layout.x.present ? gamepad->layout.x.bit_offset : -1,
             gamepad->layout.y.present ? gamepad->layout.y.bit_offset : -1,
             gamepad->layout.hat.present ? gamepad->layout.hat.bit_offset : -1, gamepad->layout.button_count,
             gamepad->layout.buttons.present ? gamepad->layout.buttons.bit_offset : -1);

    return true;
}

bool hid_gamepad_decode(const hid_gamepad_t* gamepad, const uint8_t* data, int length, hid_gamepad_state_t* state) {
    if (!hid_gamepad_is_open(gamepad) || state == NULL) {
        return false;
    }
    if (!hid_layout_strip_report_id(&gamepad->layout, &data, &length)) {
        return false;
    }

    memset(state, 0, sizeof(*state));
    state->button_count = gamepad->layout.button_count;

    hid_layout_axis_directions(data, length, &gamepad->layout.x, &state->left, &state->right);
    hid_layout_axis_directions(data, length, &gamepad->layout.y, &state->up, &state->down);

    if (gamepad->layout.hat.present) {
        // Eight directions clockwise starting at up, anything else means centered
        int32_t hat       = hid_layout_read(data, length, &gamepad->layout.hat) - gamepad->layout.hat.logical_min;
        state->dpad_up    = (hat == 0 || hat == 1 || hat == 7);
        state->dpad_right = (hat == 1 || hat == 2 || hat == 3);
        state->dpad_down  = (hat == 3 || hat == 4 || hat == 5);
        state->dpad_left  = (hat == 5 || hat == 6 || hat == 7);

        state->up    = state->up || state->dpad_up;
        state->right = state->right || state->dpad_right;
        state->down  = state->down || state->dpad_down;
        state->left  = state->left || state->dpad_left;
    }

    for (uint16_t b = 0; b < gamepad->layout.button_count && b < HID_GAMEPAD_MAX_BUTTONS; b++) {
        bool pressed = hid_layout_read_button(data, length, &gamepad->layout, b);

        if (gamepad->dpad_is_buttons && b >= gamepad->dpad_first && b < gamepad->dpad_first + 4) {
            // These four are a d-pad, so they are directions rather than buttons
            switch (b - gamepad->dpad_first) {
                case 0:
                    state->dpad_up = state->dpad_up || pressed;
                    state->up      = state->up || pressed;
                    break;
                case 1:
                    state->dpad_right = state->dpad_right || pressed;
                    state->right      = state->right || pressed;
                    break;
                case 2:
                    state->dpad_down = state->dpad_down || pressed;
                    state->down      = state->down || pressed;
                    break;
                default:
                    state->dpad_left = state->dpad_left || pressed;
                    state->left      = state->left || pressed;
                    break;
            }
            continue;
        }

        if (pressed) {
            state->buttons |= (uint32_t)1 << b;

            uint16_t usage = (b < HID_LAYOUT_MAX_BUTTONS) ? gamepad->layout.button_usage[b] : 0;
            if (usage >= 1 && usage <= 32) {
                state->usage_buttons |= (uint32_t)1 << (usage - 1);
            }
        }
    }

    return true;
}
