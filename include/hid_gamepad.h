#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "hid_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Buttons of one gamepad that fit in hid_gamepad_state_t, the rest is ignored
#define HID_GAMEPAD_MAX_BUTTONS 32

/// Value of dpad_first_button for gamepads whose buttons are all just buttons
#define HID_GAMEPAD_NO_DPAD_BUTTONS (-1)

/// @brief Everything known about a gamepad that its report descriptor does not tell us
///
/// This is device knowledge rather than anything about what the buttons mean, so it lives here.
/// Add a row to hid_gamepad_quirks in hid_gamepad.c to support another gamepad.
typedef struct {
    uint16_t    vid;
    uint16_t    pid;
    const char* name;

    /// Feature report that makes the gamepad start sending input reports, NULL when it needs no nudge.
    /// Sending it is up to the caller, which is the side that holds the USB handle.
    uint8_t        enable_report_id;
    const uint8_t* enable_report;
    size_t         enable_report_length;

    /// Index of the first of four buttons that act as a d-pad, in the order up, right, down, left
    int dpad_first_button;
} hid_gamepad_quirk_t;

/// @brief A gamepad that has been opened, ready to decode reports
typedef struct {
    hid_layout_t layout;

    bool     dpad_is_buttons;  ///< Four of the buttons are a d-pad rather than face buttons
    uint16_t dpad_first;       ///< Index of the first of those, they run up, right, down, left

    const hid_gamepad_quirk_t* quirk;  ///< The quirks that were applied, NULL when it needed none
} hid_gamepad_t;

/// @brief What a report says, with the directions already worked out
///
/// The directions come from the stick, the hat switch and a d-pad in the buttons, whichever of
/// them this gamepad has, so a caller does not have to care which.
typedef struct {
    bool up;
    bool down;
    bool left;
    bool right;

    /// Bit b is set when button b is down, in the order the report holds them. Buttons that act
    /// as a d-pad are not in here, they turned into directions.
    uint32_t buttons;

    /// Bit n is set when the button the descriptor calls Button n+1 is down
    ///
    /// A device is free to list its buttons in any order, and some list them backwards, so the
    /// third bit of a report need not be Button 3. Name a button by this rather than by where it
    /// happens to sit. Buttons the descriptor left unnamed, or numbered past 32, are not in here.
    uint32_t usage_buttons;

    /// How many bits of buttons mean anything
    uint16_t button_count;
} hid_gamepad_state_t;

/// @brief Look up the quirks of a gamepad, NULL when it needs none
const hid_gamepad_quirk_t* hid_gamepad_find_quirk(uint16_t vid, uint16_t pid);

/// @brief Learn the report layout of a gamepad from its report descriptor
///
/// Applies any quirks for this vendor and product ID. A device with neither absolute axes nor a
/// hat switch has no directions to push and is refused, which is what keeps a mouse from being
/// taken for a gamepad.
///
/// @return true when the descriptor describes something usable as a gamepad
bool hid_gamepad_open(hid_gamepad_t* gamepad, const uint8_t* report_descriptor, size_t length, uint16_t vid,
                      uint16_t pid);

/// @brief Forget an opened gamepad
void hid_gamepad_close(hid_gamepad_t* gamepad);

/// @brief Whether this gamepad was opened and can decode reports
bool hid_gamepad_is_open(const hid_gamepad_t* gamepad);

/// @brief Decode one raw HID input report into directions and buttons
///
/// @return true when the report was for this gamepad and state was filled in
bool hid_gamepad_decode(const hid_gamepad_t* gamepad, const uint8_t* data, int length, hid_gamepad_state_t* state);

#ifdef __cplusplus
}
#endif
