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
    hid_gamepad_state_t pushed = feed(pad1_reports[3], 11);
    check(pushed, true, true, true, false, BUTTON(1) | BUTTON(2));

    // Only the hat is a d-pad press. Down and left came from the stick, and stay out of it.
    assert(pushed.dpad_up);
    assert(!pushed.dpad_down);
    assert(!pushed.dpad_left);
    assert(!pushed.dpad_right);

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

/// Xbox Wireless Controller: a hat that numbers its directions from one, and analog triggers
static void test_xbox_wireless(void) {
    assert(hid_gamepad_open(&gamepad, gamepad5_desc, gamepad5_len, UNKNOWN_VID, UNKNOWN_PID));
    assert(gamepad.quirk == NULL);

    // Sticks at the middle of a sixteen bit range and a hat resting at nought, one below the
    // one to eight it described
    check(feed(pad5_reports[0], 17), false, false, false, false, 0);

    // Left stick hard left with the hat pushed south, so both directions come out and only the
    // hat one counts as a d-pad press
    hid_gamepad_state_t pushed = feed(pad5_reports[1], 17);
    check(pushed, false, true, true, false, BUTTON(1));
    assert(pushed.dpad_down);
    assert(!pushed.dpad_left);

    // Right and up, hat back at rest, every one of its fifteen buttons held
    hid_gamepad_state_t all = feed(pad5_reports[2], 17);
    check(all, true, false, false, true, 0x7fff);
    assert(!all.dpad_up && !all.dpad_right);

    // This one numbers its buttons in the order the report holds them, so both come out the same
    assert(all.usage_buttons == 0x7fff);
    assert(all.button_count == 15);

    hid_gamepad_close(&gamepad);
    ESP_LOGI(TAG, "Xbox Wireless Controller");
}

/// Switch Pro Controller: axes named by four byte usages, and buttons past the ones it counted
static void test_switch_pro(void) {
    assert(hid_gamepad_open(&gamepad, gamepad6_desc, gamepad6_len, UNKNOWN_VID, UNKNOWN_PID));

    check(feed(pad6_reports[0], 64), false, false, false, false, 0);

    // Hat west, and the first button its descriptor names Button 1
    hid_gamepad_state_t pressed = feed(pad6_reports[1], 64);
    check(pressed, false, false, true, false, BUTTON(1));
    assert(pressed.dpad_left);
    assert(pressed.usage_buttons == BUTTON(1));

    // Left stick hard right and down, with the four buttons past the counted ones held. Those
    // sit after the hat switch, so they are not part of the run and stay out of the state.
    hid_gamepad_state_t stick = feed(pad6_reports[2], 64);
    check(stick, false, true, false, true, 0);
    assert(!stick.dpad_down && !stick.dpad_right);
    assert(stick.button_count == 14);

    hid_gamepad_close(&gamepad);
    ESP_LOGI(TAG, "Switch Pro Controller");
}

/// Xbox 360 wired controller: no report ID, so nothing comes off the front of a report
static void test_xbox_360(void) {
    assert(hid_gamepad_open(&gamepad, gamepad7_desc, gamepad7_len, UNKNOWN_VID, UNKNOWN_PID));
    assert(gamepad.layout.report_id == 0);

    check(feed(pad7_reports[0], 14), false, false, false, false, 0);

    // Hat north, buttons one and ten, which for this pad are the first and last bits of its run
    hid_gamepad_state_t pressed = feed(pad7_reports[1], 14);
    check(pressed, true, false, false, false, BUTTON(1) | BUTTON(10));
    assert(pressed.dpad_up);
    assert(pressed.usage_buttons == (BUTTON(1) | BUTTON(10)));

    // Left stick hard left and down, from a pair of sixteen bit axes
    hid_gamepad_state_t stick = feed(pad7_reports[2], 14);
    check(stick, false, true, true, false, 0);
    assert(!stick.dpad_left && !stick.dpad_down);

    hid_gamepad_close(&gamepad);
    ESP_LOGI(TAG, "Xbox 360 wired controller");
}

/// Luna controller: a hat switch a byte wide, numbering its directions from nought
static void test_luna(void) {
    assert(hid_gamepad_open(&gamepad, gamepad8_desc, gamepad8_len, UNKNOWN_VID, UNKNOWN_PID));

    check(feed(pad8_reports[0], 10), false, false, false, false, 0);

    // Hat north with the twelfth button held
    hid_gamepad_state_t pressed = feed(pad8_reports[1], 10);
    check(pressed, true, false, false, false, BUTTON(12));
    assert(pressed.dpad_up);

    check(feed(pad8_reports[2], 10), false, true, true, false, 0);

    hid_gamepad_close(&gamepad);
    ESP_LOGI(TAG, "Luna controller");
}

/// DualSense: a hat switch and buttons that sit behind a byte of vendor data
static void test_dualsense(void) {
    assert(hid_gamepad_open(&gamepad, gamepad9_desc, gamepad9_len, UNKNOWN_VID, UNKNOWN_PID));

    check(feed(pad9_reports[0], 64), false, false, false, false, 0);

    // Hat south with the sixth button held
    hid_gamepad_state_t pressed = feed(pad9_reports[1], 64);
    check(pressed, false, true, false, false, BUTTON(6));
    assert(pressed.dpad_down);

    // Left stick hard left, both triggers pulled, everything pressed
    hid_gamepad_state_t all = feed(pad9_reports[2], 64);
    check(all, false, false, true, false, 0x7fff);
    assert(!all.dpad_left);
    assert(hid_layout_read(pad9_reports[2] + 1, 63, &gamepad.layout.rx) == 255);
    assert(hid_layout_read(pad9_reports[2] + 1, 63, &gamepad.layout.ry) == 255);

    hid_gamepad_close(&gamepad);
    ESP_LOGI(TAG, "DualSense");
}

/// A racing wheel steers, so it has a left and a right and never an up or a down
static void test_racing_wheel(void) {
    assert(hid_gamepad_open(&gamepad, gamepad10_desc, gamepad10_len, UNKNOWN_VID, UNKNOWN_PID));

    check(feed(pad10_reports[0], 8), false, false, false, false, 0);
    check(feed(pad10_reports[1], 8), false, false, true, false, 0);
    check(feed(pad10_reports[2], 8), false, false, false, true, BUTTON(1));

    hid_gamepad_close(&gamepad);
    ESP_LOGI(TAG, "Xbox 360 racing wheel");
}

/// A stick with no axes at all still has directions, because it has a hat switch
static void test_arcade_stick(void) {
    assert(hid_gamepad_open(&gamepad, gamepad11_desc, gamepad11_len, UNKNOWN_VID, UNKNOWN_PID));
    assert(!gamepad.layout.x.present);
    assert(!gamepad.layout.y.present);

    check(feed(pad11_reports[0], 4), false, false, false, false, 0);

    // Hat north, first button. Nothing but the hat can move a direction here, so every one of
    // them is a d-pad press.
    hid_gamepad_state_t pressed = feed(pad11_reports[1], 4);
    check(pressed, true, false, false, false, BUTTON(1));
    assert(pressed.dpad_up);

    // Hat north west, so up and left together, with all ten buttons held
    hid_gamepad_state_t corner = feed(pad11_reports[2], 4);
    check(corner, true, false, true, false, 0x3ff);
    assert(corner.dpad_up && corner.dpad_left);
    assert(corner.usage_buttons == 0x3ff);

    hid_gamepad_close(&gamepad);
    ESP_LOGI(TAG, "Arcade stick");
}

/// Guitar controller: a hat switch, ten buttons, and a whammy bar that is none of the above
static void test_guitar(void) {
    assert(hid_gamepad_open(&gamepad, gamepad12_desc, gamepad12_len, UNKNOWN_VID, UNKNOWN_PID));

    check(feed(pad12_reports[0], 10), false, false, false, false, 0);

    hid_gamepad_state_t pressed = feed(pad12_reports[1], 10);
    check(pressed, false, true, false, false, BUTTON(1));
    assert(pressed.dpad_down);

    // The whammy bar hard over is not a direction, whatever else is going on
    hid_gamepad_state_t whammy = feed(pad12_reports[2], 10);
    check(whammy, false, false, false, true, 0x3ff);
    assert(whammy.dpad_right);

    hid_gamepad_close(&gamepad);
    ESP_LOGI(TAG, "Guitar controller");
}

/// BigBen pad: the one descriptor here that came off the device rather than out of a driver
static void test_bigben(void) {
    assert(hid_gamepad_open(&gamepad, gamepad13_desc, gamepad13_len, UNKNOWN_VID, UNKNOWN_PID));

    check(feed(pad13_reports[0], 27), false, false, false, false, 0);

    hid_gamepad_state_t pressed = feed(pad13_reports[1], 27);
    check(pressed, true, false, false, false, BUTTON(1));
    assert(pressed.dpad_up);

    // Left stick hard left and down, all thirteen buttons, hat back at rest
    hid_gamepad_state_t all = feed(pad13_reports[2], 27);
    check(all, false, true, true, false, 0x1fff);
    assert(!all.dpad_down && !all.dpad_left);
    assert(all.button_count == 13);

    hid_gamepad_close(&gamepad);
    ESP_LOGI(TAG, "BigBen pad");
}

/// SteelSeries SRW-S1: a signed axis, so its middle is nought rather than half of its range
static void test_srws1(void) {
    assert(hid_gamepad_open(&gamepad, wheel1_desc, wheel1_len, UNKNOWN_VID, UNKNOWN_PID));

    check(feed(wheel1_reports[0], 16), false, false, false, false, 0);

    // Hat west with the wheel straight
    hid_gamepad_state_t hat = feed(wheel1_reports[1], 16);
    check(hat, false, false, true, false, 0);
    assert(hat.dpad_left);

    // Wheel hard left with the hat at rest, which is the same direction from the other source
    hid_gamepad_state_t turned = feed(wheel1_reports[2], 16);
    check(turned, false, false, true, false, BUTTON(1));
    assert(!turned.dpad_left);

    // Hard right, and every one of its seventeen buttons. Y is a pedal here rather than a stick,
    // but the parser has no way to know that and reports it pushed all the same.
    hid_gamepad_state_t all = feed(wheel1_reports[3], 16);
    check(all, true, false, false, true, 0x1ffff);
    assert(all.usage_buttons == 0x1ffff);
    assert(all.button_count == 17);

    hid_gamepad_close(&gamepad);
    ESP_LOGI(TAG, "SteelSeries SRW-S1");
}

/// Driving Force Pro: fourteen bits of steering, then the buttons
static void test_driving_force_pro(void) {
    assert(hid_gamepad_open(&gamepad, wheel2_desc, wheel2_len, UNKNOWN_VID, UNKNOWN_PID));

    check(feed(wheel2_reports[0], 8), false, false, false, false, 0);
    check(feed(wheel2_reports[1], 8), false, false, true, false, BUTTON(1));

    hid_gamepad_state_t all = feed(wheel2_reports[2], 8);
    check(all, false, true, false, true, 0x3fff);
    assert(all.dpad_down && !all.dpad_right);

    hid_gamepad_close(&gamepad);
    ESP_LOGI(TAG, "Driving Force Pro");
}

/// A device with axes and no buttons at all is still something to steer with
static void test_adapters_without_buttons(void) {
    assert(hid_gamepad_open(&gamepad, rc1_desc, rc1_len, UNKNOWN_VID, UNKNOWN_PID));

    check(feed(rc1_reports[0], 8), false, false, false, false, 0);

    // X hard over with the Slider between it and Y hard over the other way. Only X is a
    // direction, and Y has to be read from where it really is.
    hid_gamepad_state_t left = feed(rc1_reports[1], 8);
    check(left, false, false, true, false, 0);
    assert(left.button_count == 0);

    check(feed(rc1_reports[2], 8), true, false, false, false, 0);
    hid_gamepad_close(&gamepad);

    assert(hid_gamepad_open(&gamepad, rc2_desc, rc2_len, UNKNOWN_VID, UNKNOWN_PID));
    check(feed(rc2_reports[0], 7), false, false, false, false, 0);
    check(feed(rc2_reports[1], 7), false, true, true, false, 0);
    check(feed(rc2_reports[2], 7), true, false, false, true, 0);

    hid_gamepad_close(&gamepad);
    ESP_LOGI(TAG, "Adapters with no buttons");
}

/// DragonRise JS19: padding before the sticks, and more before the buttons
static void test_dragonrise(void) {
    assert(hid_gamepad_open(&gamepad, gamepad14_desc, gamepad14_len, UNKNOWN_VID, UNKNOWN_PID));
    assert(!gamepad.layout.hat.present);

    check(feed(pad14_reports[0], 8), false, false, false, false, 0);

    // Nothing here is a d-pad, so a direction can only ever come from the stick
    hid_gamepad_state_t left = feed(pad14_reports[1], 8);
    check(left, false, false, true, false, BUTTON(1));
    assert(!left.dpad_left);

    hid_gamepad_state_t all = feed(pad14_reports[2], 8);
    check(all, false, true, false, false, 0x3ff);
    assert(all.usage_buttons == 0x3ff);

    hid_gamepad_close(&gamepad);
    ESP_LOGI(TAG, "DragonRise JS19");
}

int main(void) {
    test_stadia();
    test_dualshock4_clone();
    test_dualshock3();
    test_competition_pro();
    test_xbox_wireless();
    test_switch_pro();
    test_xbox_360();
    test_luna();
    test_dualsense();
    test_racing_wheel();
    test_arcade_stick();
    test_guitar();
    test_bigben();
    test_srws1();
    test_driving_force_pro();
    test_adapters_without_buttons();
    test_dragonrise();
    test_mice_are_rejected();

    printf("All gamepad tests passed\n");
    return 0;
}
