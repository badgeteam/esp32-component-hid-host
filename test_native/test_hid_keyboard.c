/// Tests for the keyboard layer, against descriptors captured from real hardware
///
/// The reports here are written by hand rather than captured, because a boot report is eight bytes
/// of which the layout is fixed and stating them inline says more than a name would.

#include "hid_keyboard.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "test_descriptors.h"

static const char* TAG = "test";

// Usages of the keys these tests press
#define KEY_A          0x04
#define KEY_B          0x05
#define KEY_C          0x06
#define LEFT_CTRL      0xe0
#define LEFT_SHIFT     0xe1

/// @brief Decode one report, asserting it was accepted
static void feed(const hid_keyboard_t* keyboard, const uint8_t* data, int length, hid_keyboard_state_t* state) {
    assert(hid_keyboard_decode(keyboard, data, length, state));
}

/// @brief How many usages one state says are down, counted the long way round
static uint16_t count_down(const hid_keyboard_state_t* state) {
    uint16_t n     = 0;
    uint16_t usage = 0;
    while (hid_keyboard_next_down(state, &usage)) {
        n++;
    }
    return n;
}

/// @brief How many keys went down and came up between two states
static void count_changes(const hid_keyboard_state_t* previous, const hid_keyboard_state_t* current,
                          uint16_t* pressed, uint16_t* released) {
    *pressed      = 0;
    *released     = 0;
    uint16_t usage = 0;
    bool     down  = false;
    while (hid_keyboard_next_change(previous, current, &usage, &down)) {
        if (down) {
            (*pressed)++;
        } else {
            (*released)++;
        }
    }
}

/// A boot keyboard typing: the modifier byte and the six slots, read as a set of usages
static void test_boot_report(void) {
    hid_keyboard_t keyboard;
    assert(hid_keyboard_open(&keyboard, keyboard1_desc, keyboard1_len));
    assert(hid_keyboard_is_open(&keyboard));

    // Left shift held, 'a' down
    const uint8_t        press[8] = {0x02, 0x00, KEY_A, 0x00, 0x00, 0x00, 0x00, 0x00};
    hid_keyboard_state_t state;
    feed(&keyboard, press, sizeof(press), &state);

    assert(state.modifiers == HID_KEYBOARD_LEFT_SHIFT);
    assert(hid_keyboard_is_down(&state, KEY_A));
    assert(!hid_keyboard_is_down(&state, KEY_B));
    assert(!state.rollover);

    // The modifier is a key as well as a byte, which is the whole point of holding a set
    assert(hid_keyboard_is_down(&state, LEFT_SHIFT));
    assert(state.key_count == 2);
    assert(count_down(&state) == 2);

    hid_keyboard_close(&keyboard);
    assert(!hid_keyboard_is_open(&keyboard));

    ESP_LOGI(TAG, "Boot report");
}

/// A keyboard moving a held key between slots is not a release and a press
static void test_slot_shuffle(void) {
    hid_keyboard_t keyboard;
    assert(hid_keyboard_open(&keyboard, keyboard1_desc, keyboard1_len));

    const uint8_t first[8]  = {0x00, 0x00, KEY_A, KEY_B, 0x00, 0x00, 0x00, 0x00};
    const uint8_t second[8] = {0x00, 0x00, KEY_B, KEY_A, 0x00, 0x00, 0x00, 0x00};

    hid_keyboard_state_t before;
    hid_keyboard_state_t after;
    feed(&keyboard, first, sizeof(first), &before);
    feed(&keyboard, second, sizeof(second), &after);

    uint16_t pressed  = 0;
    uint16_t released = 0;
    count_changes(&before, &after, &pressed, &released);
    assert(pressed == 0 && released == 0);

    hid_keyboard_close(&keyboard);
    ESP_LOGI(TAG, "Slot shuffle");
}

/// A keyboard reporting rollover is not reporting an empty keyboard
static void test_rollover(void) {
    hid_keyboard_t keyboard;
    assert(hid_keyboard_open(&keyboard, keyboard1_desc, keyboard1_len));

    const uint8_t        held[8] = {0x00, 0x00, KEY_A, KEY_B, KEY_C, 0x00, 0x00, 0x00};
    hid_keyboard_state_t before;
    feed(&keyboard, held, sizeof(held), &before);
    assert(count_down(&before) == 3);

    // Every slot full of ErrorRollover, which is what a keyboard sends when it cannot say more
    const uint8_t        all_over[8] = {0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01};
    hid_keyboard_state_t state;
    assert(!hid_keyboard_decode(&keyboard, all_over, sizeof(all_over), &state));
    assert(state.rollover);

    // And the same when only the first slot says so, which is the other way keyboards spell it
    const uint8_t one_over[8] = {0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
    assert(!hid_keyboard_decode(&keyboard, one_over, sizeof(one_over), &state));
    assert(state.rollover);

    // A refused report is not a report of what is held, so the caller keeps what it had. Nothing
    // was released, which is the bug every implementation of this has shipped.
    assert(count_down(&before) == 3);
    assert(hid_keyboard_is_down(&before, KEY_A));

    hid_keyboard_close(&keyboard);
    ESP_LOGI(TAG, "Rollover");
}

/// Everything a keyboard held when it was unplugged comes up, without a second way of asking
static void test_disconnect(void) {
    hid_keyboard_t keyboard;
    assert(hid_keyboard_open(&keyboard, keyboard1_desc, keyboard1_len));

    const uint8_t        held[8] = {0x01, 0x00, KEY_A, KEY_B, 0x00, 0x00, 0x00, 0x00};
    hid_keyboard_state_t before;
    feed(&keyboard, held, sizeof(held), &before);

    const hid_keyboard_state_t gone = {0};

    uint16_t pressed  = 0;
    uint16_t released = 0;
    count_changes(&before, &gone, &pressed, &released);

    // Two keys and the control that was held with them
    assert(pressed == 0);
    assert(released == 3);

    hid_keyboard_close(&keyboard);
    ESP_LOGI(TAG, "Disconnect");
}

/// A report too short to hold the key array says nothing, rather than that the keys came up
static void test_short_report(void) {
    hid_keyboard_t keyboard;
    assert(hid_keyboard_open(&keyboard, keyboard1_desc, keyboard1_len));

    const uint8_t        full[8] = {0x00, 0x00, KEY_A, KEY_B, 0x00, 0x00, 0x00, 0x00};
    hid_keyboard_state_t state;
    feed(&keyboard, full, sizeof(full), &state);
    assert(count_down(&state) == 2);

    // Four bytes holds the modifiers and two slots. Believing it would release KEY_B.
    assert(!hid_keyboard_decode(&keyboard, full, 4, &state));
    assert(!hid_keyboard_decode(&keyboard, full, 1, &state));
    assert(!hid_keyboard_decode(&keyboard, full, 0, &state));
    assert(!hid_keyboard_decode(&keyboard, NULL, 8, &state));

    hid_keyboard_close(&keyboard);
    ESP_LOGI(TAG, "Short report");
}

/// A keyboard that says which keys are down by position rather than by naming them
static void test_bitmap(void) {
    hid_keyboard_t keyboard;
    assert(hid_keyboard_open(&keyboard, keyboard2_desc, keyboard2_len));

    // Report 2 is the bitmap: a report ID, then a hundred and twenty bits from usage zero. Ten
    // keys at once, which the six slots of a boot report cannot say at all.
    uint8_t bitmap[16] = {0};
    bitmap[0]          = 0x02;
    for (uint16_t usage = KEY_A; usage < KEY_A + 10; usage++) {
        bitmap[1 + usage / 8] |= (uint8_t)(1u << (usage % 8));
    }

    hid_keyboard_state_t state;
    feed(&keyboard, bitmap, sizeof(bitmap), &state);

    assert(count_down(&state) == 10);
    assert(hid_keyboard_is_down(&state, KEY_A));
    assert(hid_keyboard_is_down(&state, KEY_A + 9));
    assert(!hid_keyboard_is_down(&state, KEY_A + 10));
    assert(!state.rollover);

    // The same keyboard's other report is the boot shape, under its own report ID
    const uint8_t boot[9] = {0x01, 0x00, 0x00, KEY_A, 0x00, 0x00, 0x00, 0x00, 0x00};
    feed(&keyboard, boot, sizeof(boot), &state);
    assert(count_down(&state) == 1);
    assert(hid_keyboard_is_down(&state, KEY_A));

    // A report ID this keyboard never described belongs to something else
    const uint8_t stranger[9] = {0x09, 0x00, 0x00, KEY_A, 0x00, 0x00, 0x00, 0x00, 0x00};
    assert(!hid_keyboard_decode(&keyboard, stranger, sizeof(stranger), &state));

    hid_keyboard_close(&keyboard);
    ESP_LOGI(TAG, "Bitmap keyboard");
}

/// The boot layout, for a keyboard whose descriptor is not worth reading or never arrived
static void test_boot_fallback(void) {
    hid_keyboard_t descriptor;
    hid_keyboard_t boot;
    assert(hid_keyboard_open(&descriptor, keyboard1_desc, keyboard1_len));
    hid_keyboard_open_boot(&boot);
    assert(hid_keyboard_is_open(&boot));
    assert(boot.boot);

    const uint8_t report[8] = {0x22, 0x00, KEY_A, KEY_C, 0x00, 0x00, 0x00, 0x00};

    hid_keyboard_state_t from_descriptor;
    hid_keyboard_state_t from_boot;
    feed(&descriptor, report, sizeof(report), &from_descriptor);
    feed(&boot, report, sizeof(report), &from_boot);

    // The Dell's descriptor describes exactly the boot report, so the two must agree bit for bit
    assert(memcmp(&from_descriptor, &from_boot, sizeof(from_boot)) == 0);
    assert(from_boot.modifiers == (HID_KEYBOARD_LEFT_SHIFT | HID_KEYBOARD_RIGHT_SHIFT));

    hid_keyboard_close(&descriptor);
    hid_keyboard_close(&boot);
    ESP_LOGI(TAG, "Boot fallback");
}

/// Something that is not a keyboard is not opened as one
static void test_rejected(void) {
    hid_keyboard_t keyboard;

    assert(!hid_keyboard_open(&keyboard, mouse1_desc, mouse1_len));
    assert(!hid_keyboard_is_open(&keyboard));
    assert(!hid_keyboard_open(&keyboard, gamepad1_desc, gamepad1_len));
    assert(!hid_keyboard_open(&keyboard, stick1_desc, stick1_len));

    // A descriptor that ends before it says anything
    const uint8_t nothing[] = {0x05, 0x01};
    assert(!hid_keyboard_open(&keyboard, nothing, sizeof(nothing)));

    // And a closed keyboard decodes nothing rather than reading through a zeroed layout
    const uint8_t        report[8] = {0x00, 0x00, KEY_A, 0x00, 0x00, 0x00, 0x00, 0x00};
    hid_keyboard_state_t state;
    assert(!hid_keyboard_decode(&keyboard, report, sizeof(report), &state));

    ESP_LOGI(TAG, "Not keyboards");
}

/// Asking about usages that are not there, at both ends of the range
static void test_bounds(void) {
    hid_keyboard_t keyboard;
    assert(hid_keyboard_open(&keyboard, keyboard1_desc, keyboard1_len));

    const uint8_t        report[8] = {0x00, 0x00, KEY_A, 0x00, 0x00, 0x00, 0x00, 0x00};
    hid_keyboard_state_t state;
    feed(&keyboard, report, sizeof(report), &state);

    assert(!hid_keyboard_is_down(&state, HID_KEYBOARD_USAGES));
    assert(!hid_keyboard_is_down(&state, 0xffff));
    assert(!hid_keyboard_is_down(&state, HID_KEYBOARD_USAGE_NONE));

    // Walking from the last usage there is finds nothing rather than running off the end
    uint16_t usage = HID_KEYBOARD_USAGES - 1;
    assert(!hid_keyboard_next_down(&state, &usage));

    bool down = false;
    usage     = HID_KEYBOARD_USAGES - 1;
    assert(!hid_keyboard_next_change(&state, &state, &usage, &down));

    hid_keyboard_close(&keyboard);
    ESP_LOGI(TAG, "Bounds");
}

int main(void) {
    test_boot_report();
    test_slot_shuffle();
    test_rollover();
    test_disconnect();
    test_short_report();
    test_bitmap();
    test_boot_fallback();
    test_rejected();
    test_bounds();

    printf("All keyboard tests passed\n");
    return 0;
}
