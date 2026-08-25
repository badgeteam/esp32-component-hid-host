// Runs recorded gamepads through the decoder and checks the directions and buttons that come out.
//
// What a button means is not decided here, so these tests only check that the right bit moved.

#include <assert.h>
#include <stdio.h>
#include "esp_log.h"
#include "hid_gamepad.h"
#include "test_descriptors.h"
#include "test_reports.h"

static const char TAG[] = "test";

#define SONY_VID       0x054c
#define DUALSHOCK3_PID 0x0268

// A gamepad nothing is known about beyond its report descriptor
#define UNKNOWN_VID 0x0000
#define UNKNOWN_PID 0x0000

#define BUTTON(n) ((uint32_t)1 << ((n) - 1))

static hid_gamepad_t gamepad;

static hid_gamepad_state_t feed(const uint8_t* report, int length) {
    hid_gamepad_state_t state;
    assert(hid_gamepad_decode(&gamepad, report, length, &state));
    return state;
}

static void check(hid_gamepad_state_t state, bool up, bool down, bool left, bool right, uint32_t buttons) {
    assert(state.up == up);
    assert(state.down == down);
    assert(state.left == left);
    assert(state.right == right);
    assert(state.buttons == buttons);
}

/// Stadia controller: directions from its hat switch, and a stick that also steers
static void test_stadia(void) {
    assert(hid_gamepad_open(&gamepad, gamepad1_desc, gamepad1_len, UNKNOWN_VID, UNKNOWN_PID));
    assert(hid_gamepad_is_open(&gamepad));
    assert(gamepad.quirk == NULL);

    // Hat at 6 is left, sticks centered
    check(feed(pad1_reports[0], 11), false, false, true, false, 0);

    // Hat at 8 means centered, but the left stick is pushed down
    check(feed(pad1_reports[1], 11), false, true, false, false, 0);

    // Hat at 9 is out of range, so centered, and the sticks are at rest
    check(feed(pad1_reports[2], 11), false, false, false, false, 0);

    // Hat up while the stick is pushed down and left: both are reported, the caller decides
    check(feed(pad1_reports[3], 11), true, true, true, false, BUTTON(1) | BUTTON(2));

    // Those two bits are the first two of the report, which this pad calls Button 18 and 17
    assert(feed(pad1_reports[3], 11).usage_buttons == (BUTTON(18) | BUTTON(17)));

    // Hat at 15 means centered, every one of its fifteen buttons held
    check(feed(pad1_reports[4], 11), false, false, false, false, 0x7fff);

    hid_gamepad_close(&gamepad);
    assert(!hid_gamepad_is_open(&gamepad));

    ESP_LOGI(TAG, "Stadia controller");
}

/// DualShock 4 clone: axes before the hat switch, and a hat that the parser has to go find
static void test_dualshock4_clone(void) {
    assert(hid_gamepad_open(&gamepad, gamepad2_desc, gamepad2_len, UNKNOWN_VID, UNKNOWN_PID));

    // Sticks centered, hat at 8 meaning centered
    check(feed(pad2_reports[0], 64), false, false, false, false, 0);

    // Same, with square held
    check(feed(pad2_reports[1], 64), false, false, false, false, BUTTON(1));

    // Left stick hard up and to the left
    check(feed(pad2_reports[2], 64), true, false, true, false, 0);

    hid_gamepad_close(&gamepad);
    ESP_LOGI(TAG, "DualShock 4 clone");
}

/// DualShock 3: silent without its quirk, no hat switch, d-pad in its buttons
static void test_dualshock3(void) {
    assert(hid_gamepad_open(&gamepad, gamepad3_desc, gamepad3_len, SONY_VID, DUALSHOCK3_PID));

    // Its quirk was found and applied
    assert(gamepad.quirk != NULL);
    assert(gamepad.dpad_is_buttons);
    assert(gamepad.dpad_first == 4);

    // Centered and nothing pressed
    check(feed(pad3_reports[0], 11), false, false, false, false, 0);

    // Cross is button fifteen, past the d-pad, so it stays a button
    check(feed(pad3_reports[1], 11), false, false, false, false, BUTTON(15));

    // Its quirk carries the feature report that gets it talking at all. Sending it is the
    // caller's job, since the caller is the side holding the USB handle.
    const hid_gamepad_quirk_t* quirk = hid_gamepad_find_quirk(SONY_VID, DUALSHOCK3_PID);
    assert(quirk != NULL);
    assert(quirk->enable_report != NULL);
    assert(quirk->enable_report_id == 0xf4);
    assert(quirk->enable_report_length == 4);
    assert(quirk->dpad_first_button == 4);

    // Without the quirk its d-pad would come out as four ordinary buttons
    assert(hid_gamepad_find_quirk(UNKNOWN_VID, UNKNOWN_PID) == NULL);

    hid_gamepad_close(&gamepad);
    ESP_LOGI(TAG, "DualShock 3");
}

/// Competition Pro: no report ID, and a stick that steers through X and Y as well as the hat
static void test_competition_pro(void) {
    assert(hid_gamepad_open(&gamepad, gamepad4_desc, gamepad4_len, UNKNOWN_VID, UNKNOWN_PID));

    check(feed(pad4_reports[0], 9), false, false, false, false, 0);
    check(feed(pad4_reports[1], 9), false, false, true, false, 0);
    check(feed(pad4_reports[2], 9), false, false, false, true, BUTTON(1));

    hid_gamepad_close(&gamepad);
    ESP_LOGI(TAG, "Competition Pro");
}

/// A mouse is not a gamepad: its axes report how far it moved rather than where it is, so there
/// is no center to push away from and no direction to be had
static void test_mice_are_rejected(void) {
    assert(!hid_gamepad_open(&gamepad, mouse1_desc, mouse1_len, UNKNOWN_VID, UNKNOWN_PID));
    assert(!hid_gamepad_open(&gamepad, mouse2_desc, mouse2_len, UNKNOWN_VID, UNKNOWN_PID));
    assert(!hid_gamepad_open(&gamepad, mouse3_desc, mouse3_len, UNKNOWN_VID, UNKNOWN_PID));
    assert(!hid_gamepad_is_open(&gamepad));

    // A report from a device that was never opened decodes to nothing at all
    hid_gamepad_state_t state;
    assert(!hid_gamepad_decode(&gamepad, mouse1_reports[1], 8, &state));

    ESP_LOGI(TAG, "Mice are left alone");
}

int main(void) {
    test_stadia();
    test_dualshock4_clone();
    test_dualshock3();
    test_competition_pro();
    test_mice_are_rejected();

    printf("All gamepad tests passed\n");
    return 0;
}
