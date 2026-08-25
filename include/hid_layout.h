#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Most input reports one device is looked at
///
/// A device describes one report per report ID. Only the ones carrying input items take up a
/// slot here; the output and feature reports a gamepad hands out by the dozen do not.
#ifndef HID_LAYOUT_MAX_REPORTS
#define HID_LAYOUT_MAX_REPORTS 8
#endif

/// @brief One control within an input report
typedef struct {
    bool     present;
    bool     relative;  // Reports a change rather than a position, as a mouse does
    uint16_t bit_offset;
    uint8_t  bit_size;
    int32_t  logical_min;
    int32_t  logical_max;
} hid_field_t;

/// @brief Where the controls of a device sit within one of its input reports
///
/// USB HID devices do not agree on a report layout, they describe it. This holds the part of
/// that description a keyboardless device needs: the axes, the wheel, the hat switch and the
/// buttons.
///
/// A gamepad reports its left stick through x and y. The right stick is usually z and rz, and
/// sometimes rx and ry; a pad that names no simulation controls puts its analog triggers in
/// whichever pair is left over. Which is which the descriptor does not say, so a device that
/// reports all six needs a look at the values before either pair can be called a stick.
typedef struct {
    bool        valid;
    uint8_t     report_id;  // Zero when the reports carry no report ID
    hid_field_t x;
    hid_field_t y;
    hid_field_t z;
    hid_field_t rx;
    hid_field_t ry;
    hid_field_t rz;
    hid_field_t wheel;
    hid_field_t pan;          // Sideways wheel, which a mouse reports as a consumer control
    hid_field_t accelerator;  // Analog trigger, for a gamepad that names it as one
    hid_field_t brake;        // The other analog trigger
    hid_field_t hat;
    hid_field_t buttons;  // bit_size is one, the number of them is in button_count
    uint16_t    button_count;
} hid_layout_t;

/// @brief The layout of every input report of a device
typedef struct {
    hid_layout_t report[HID_LAYOUT_MAX_REPORTS];
    uint8_t      count;
} hid_layouts_t;

/// @brief Work out the layout of every input report of a device from its report descriptor
///
/// @return true when at least one report held something usable
bool hid_layout_parse_all(const uint8_t* report_descriptor, size_t length, hid_layouts_t* layouts);

/// @brief The report holding the most of what this parser looks for
///
/// A device that reports through more than one report ID names the interesting one nowhere, so
/// the one describing the most axes, hat switch and buttons is taken. Ties go to the report
/// the descriptor names first.
///
/// @return NULL when no report held anything usable
const hid_layout_t* hid_layouts_best(const hid_layouts_t* layouts);

/// @brief The layout of one report, by report ID
///
/// @return NULL when the device describes no input report with that ID
const hid_layout_t* hid_layouts_find(const hid_layouts_t* layouts, uint8_t report_id);

/// @brief Work out the layout of the most usable input report of a device
///
/// A shorthand for hid_layout_parse_all() followed by hid_layouts_best(), for devices that
/// report through one report ID and hosts that only care about one.
///
/// @return true when the descriptor held anything usable
bool hid_layout_parse(const uint8_t* report_descriptor, size_t length, hid_layout_t* layout);

/// @brief Strip the report ID from a report, when the layout says it has one
///
/// The bit offsets in the layout do not count the report ID, so it has to come off first.
///
/// @return false when the report belongs to another report ID and should be ignored
bool hid_layout_strip_report_id(const hid_layout_t* layout, const uint8_t** data, int* length);

/// @brief Read one field out of an input report
///
/// Bits past the end of the report read as zero, so a report shorter than the descriptor
/// promises returns a value rather than reading past it.
int32_t hid_layout_read(const uint8_t* data, int length, const hid_field_t* field);

/// @brief Read one button out of an input report
///
/// @return false for a button the device does not have, or one the report is too short to hold
bool hid_layout_read_button(const uint8_t* data, int length, const hid_layout_t* layout, uint16_t button);

/// @brief Whether an axis is pushed far enough from the center of its range to count as a direction
///
/// Both outputs are always written, false included, so an axis that is centered, missing, or
/// cut off by a report shorter than the descriptor promises clears them rather than reading as
/// a direction.
void hid_layout_axis_directions(const uint8_t* data, int length, const hid_field_t* field, bool* low, bool* high);

/// @brief Which way a hat switch is pushed
///
/// A hat reports a direction as an angle counted clockwise from north, and rests at a value
/// outside its logical range. Diagonals set two of the four. All four outputs are always
/// written, false included.
void hid_layout_hat_directions(const uint8_t* data, int length, const hid_field_t* hat, bool* up, bool* down,
                               bool* left, bool* right);

#ifdef __cplusplus
}
#endif
