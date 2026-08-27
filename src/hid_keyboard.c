#include "hid_keyboard.h"
#include <string.h>
#include "esp_log.h"

static const char* TAG = "hid_keyboard";

// Where the boot report keeps things, which is fixed and the same on every keyboard there is
#define BOOT_MODIFIER_BITS   8
#define BOOT_KEY_OFFSET_BITS 16  // Past the modifier byte and the reserved byte after it
#define BOOT_KEY_BITS        8
#define BOOT_KEY_SLOTS       6

/// @brief Mark one usage as down, ignoring anything this state has no room to name
///
/// A descriptor is free to claim a usage past the end of the page, and a report is free to hold
/// whatever byte it likes in a key slot. Neither is worth refusing the whole report over.
static void mark(hid_keyboard_state_t* state, uint16_t usage) {
    if (usage >= HID_KEYBOARD_USAGES) {
        return;
    }

    uint32_t bit = (uint32_t)1 << (usage % 32);
    if (state->keys[usage / 32] & bit) {
        return;
    }

    state->keys[usage / 32] |= bit;
    state->key_count++;
}

/// @brief The layout of the report in hand, or NULL when this keyboard describes no such report
static const hid_layout_t* layout_for(const hid_keyboard_t* keyboard, const uint8_t* data, int length) {
    if (length < 1) {
        return NULL;
    }

    // A device that puts a report ID in front of its reports says so in every layout it described,
    // so the first one is enough to know which kind of device this is
    if (keyboard->layouts.count > 0 && keyboard->layouts.report[0].report_id != 0) {
        return hid_layouts_find(&keyboard->layouts, data[0]);
    }

    return &keyboard->layouts.report[0];
}

/// @brief Read the six-slot shape, where a report names the keys that are down by usage
static bool decode_keys(const hid_layout_t* layout, const uint8_t* data, int length, hid_keyboard_state_t* state) {
    // Every slot has to be there before any of them is believed. A key array read halfway looks
    // exactly like the keys in the slots that were missed having been let go of.
    uint32_t end = (uint32_t)layout->keys.bit_offset + (uint32_t)layout->key_count * layout->keys.bit_size;
    if (end > (uint32_t)length * 8) {
        return false;
    }

    for (uint16_t slot = 0; slot < layout->key_count; slot++) {
        uint16_t usage = 0;
        if (!hid_layout_read_key(data, length, layout, slot, &usage)) {
            continue;  // An empty slot, which says nothing about the slots after it
        }

        // A keyboard with more keys down than it can name says so in place of naming any of them,
        // and the keys that were down are still down. None of this is a report of what is held.
        if (usage <= HID_KEYBOARD_USAGE_ERROR_LAST) {
            state->rollover = true;
            return false;
        }

        mark(state, usage);
    }

    return true;
}

/// @brief Read the bitmap shape, where a report holds one bit per usage
static void decode_key_bits(const hid_layout_t* layout, const uint8_t* data, int length, hid_keyboard_state_t* state) {
    for (uint16_t i = 0; i < layout->key_bit_count; i++) {
        uint16_t usage = (uint16_t)(layout->key_bit_first + i);

        // The bottom four usages of the page are the empty slot and the three errors. A bitmap
        // covering them from usage zero still has no key to report there.
        if (usage <= HID_KEYBOARD_USAGE_ERROR_LAST) {
            continue;
        }

        if (hid_layout_read_key_bit(data, length, layout, usage)) {
            mark(state, usage);
        }
    }
}

bool hid_keyboard_open(hid_keyboard_t* keyboard, const uint8_t* report_descriptor, size_t length) {
    memset(keyboard, 0, sizeof(*keyboard));

    if (!hid_layout_parse_all(report_descriptor, length, &keyboard->layouts)) {
        return false;
    }

    // Keys are what makes this a keyboard. Modifiers on their own are not enough: a keyboard that
    // names no keys has nothing to say, and taking it would claim devices that merely carry a
    // modifier byte alongside something else.
    bool any = false;
    for (uint8_t r = 0; r < keyboard->layouts.count; r++) {
        const hid_layout_t* layout = &keyboard->layouts.report[r];
        any |= layout->keys.present || layout->key_bits.present;
    }

    if (!any) {
        ESP_LOGW(TAG, "No keys in the report descriptor, ignoring this device");
        return false;
    }

    keyboard->open = true;
    return true;
}

void hid_keyboard_open_boot(hid_keyboard_t* keyboard) {
    memset(keyboard, 0, sizeof(*keyboard));

    hid_layout_t* layout = &keyboard->layouts.report[0];

    layout->valid                 = true;
    layout->modifiers.present     = true;
    layout->modifiers.bit_offset  = 0;
    layout->modifiers.bit_size    = 1;
    layout->modifiers.logical_max = 1;
    layout->modifier_count        = BOOT_MODIFIER_BITS;
    layout->modifier_first        = HID_KEYBOARD_USAGE_FIRST_MODIFIER;

    layout->keys.present     = true;
    layout->keys.bit_offset  = BOOT_KEY_OFFSET_BITS;
    layout->keys.bit_size    = BOOT_KEY_BITS;
    layout->keys.logical_max = 0xff;
    layout->key_count        = BOOT_KEY_SLOTS;

    keyboard->layouts.count = 1;
    keyboard->open          = true;
    keyboard->boot          = true;
}

void hid_keyboard_close(hid_keyboard_t* keyboard) {
    memset(keyboard, 0, sizeof(*keyboard));
}

bool hid_keyboard_is_open(const hid_keyboard_t* keyboard) {
    return keyboard->open;
}

bool hid_keyboard_decode(const hid_keyboard_t* keyboard, const uint8_t* data, int length,
                         hid_keyboard_state_t* state) {
    memset(state, 0, sizeof(*state));

    if (!keyboard->open || data == NULL || length <= 0) {
        return false;
    }

    const hid_layout_t* layout = layout_for(keyboard, data, length);
    if (layout == NULL) {
        return false;
    }

    if (!hid_layout_strip_report_id(layout, &data, &length)) {
        return false;
    }

    for (uint16_t m = 0; m < layout->modifier_count && m < BOOT_MODIFIER_BITS; m++) {
        if (hid_layout_read_modifier(data, length, layout, m)) {
            state->modifiers |= (uint8_t)(1u << m);
            mark(state, (uint16_t)(layout->modifier_first + m));
        }
    }

    if (layout->keys.present && !decode_keys(layout, data, length, state)) {
        bool rollover = state->rollover;
        memset(state, 0, sizeof(*state));
        state->rollover = rollover;
        return false;
    }

    if (layout->key_bits.present) {
        decode_key_bits(layout, data, length, state);
    }

    return true;
}

bool hid_keyboard_is_down(const hid_keyboard_state_t* state, uint16_t usage) {
    if (usage >= HID_KEYBOARD_USAGES) {
        return false;
    }

    return (state->keys[usage / 32] & ((uint32_t)1 << (usage % 32))) != 0;
}

bool hid_keyboard_next_down(const hid_keyboard_state_t* state, uint16_t* usage) {
    for (uint16_t u = (uint16_t)(*usage + 1); u < HID_KEYBOARD_USAGES; u++) {
        if (hid_keyboard_is_down(state, u)) {
            *usage = u;
            return true;
        }
    }

    return false;
}

bool hid_keyboard_next_change(const hid_keyboard_state_t* previous, const hid_keyboard_state_t* current,
                              uint16_t* usage, bool* down) {
    for (uint16_t u = (uint16_t)(*usage + 1); u < HID_KEYBOARD_USAGES; u++) {
        bool was = hid_keyboard_is_down(previous, u);
        bool now = hid_keyboard_is_down(current, u);
        if (was != now) {
            *usage = u;
            *down  = now;
            return true;
        }
    }

    return false;
}
