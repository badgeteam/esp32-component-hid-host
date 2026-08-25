// Runs the report descriptor parser against descriptors captured from real devices.

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "hid_layout.h"
#include "test_descriptors.h"
#include "test_reports.h"

static const char TAG[] = "test";

static void check_field(const hid_field_t* field, uint16_t offset, uint8_t size) {
    assert(field->present);
    assert(field->bit_offset == offset);
    assert(field->bit_size == size);
}

/// Logitech M705: a report ID, sixteen buttons and twelve bit axes
static void test_logitech_m705(void) {
    hid_layout_t layout;
    assert(hid_layout_parse(mouse1_desc, mouse1_len, &layout));

    assert(layout.report_id == 2);
    check_field(&layout.buttons, 0, 1);
    assert(layout.button_count == 16);
    check_field(&layout.x, 16, 12);
    check_field(&layout.y, 28, 12);
    check_field(&layout.wheel, 40, 8);

    // A mouse reports how far it moved, not where it is
    assert(layout.x.relative);
    assert(layout.y.relative);

    // Twelve bit axes are signed, and the report ID has to come off first
    const uint8_t* data   = mouse1_reports[1];
    int            length = 8;
    assert(hid_layout_strip_report_id(&layout, &data, &length));
    assert(length == 7);
    assert(hid_layout_read(data, length, &layout.x) == -1);
    assert(hid_layout_read(data, length, &layout.y) == 4);
    assert(hid_layout_read(data, length, &layout.wheel) == 0);

    data   = mouse1_reports[0];
    length = 8;
    assert(hid_layout_strip_report_id(&layout, &data, &length));
    assert(hid_layout_read(data, length, &layout.wheel) == -2);

    ESP_LOGI(TAG, "Logitech M705");
}

/// Trust wireless mouse: five byte reports that start with a report ID
///
/// This is the shape that a parser guessing a layout from the report length reads one byte off,
/// taking the report ID for the buttons and the Y axis for the wheel.
static void test_trust_mouse(void) {
    hid_layout_t layout;
    assert(hid_layout_parse(mouse2_desc, mouse2_len, &layout));

    assert(layout.report_id == 1);
    check_field(&layout.buttons, 0, 1);
    assert(layout.button_count == 5);
    check_field(&layout.x, 8, 8);
    check_field(&layout.y, 16, 8);
    check_field(&layout.wheel, 24, 8);

    const uint8_t* data   = mouse2_reports[1];
    int            length = 5;
    assert(hid_layout_strip_report_id(&layout, &data, &length));
    assert(length == 4);
    assert(hid_layout_read_button(data, length, &layout, 0));
    assert(!hid_layout_read_button(data, length, &layout, 1));
    assert(hid_layout_read(data, length, &layout.x) == -3);
    assert(hid_layout_read(data, length, &layout.y) == 11);
    assert(hid_layout_read(data, length, &layout.wheel) == 2);

    // Nothing pressed, and no phantom button from the report ID
    data   = mouse2_reports[0];
    length = 5;
    assert(hid_layout_strip_report_id(&layout, &data, &length));
    for (uint16_t b = 0; b < layout.button_count; b++) {
        assert(!hid_layout_read_button(data, length, &layout, b));
    }

    ESP_LOGI(TAG, "Trust wireless mouse");
}

/// Fujitsu M520: no report ID, and fewer usages than the report has button bits
static void test_fujitsu_m520(void) {
    hid_layout_t layout;
    assert(hid_layout_parse(mouse3_desc, mouse3_len, &layout));

    assert(layout.report_id == 0);
    check_field(&layout.buttons, 0, 1);
    assert(layout.button_count == 3);  // Eight bits are reported, three of them are named
    check_field(&layout.x, 8, 8);
    check_field(&layout.y, 16, 8);
    check_field(&layout.wheel, 24, 8);

    const uint8_t* data   = mouse3_reports[0];
    int            length = 4;
    assert(hid_layout_strip_report_id(&layout, &data, &length));
    assert(length == 4);  // Nothing to strip
    assert(hid_layout_read_button(data, length, &layout, 0));
    assert(!hid_layout_read_button(data, length, &layout, 1));
    assert(hid_layout_read_button(data, length, &layout, 2));
    assert(hid_layout_read(data, length, &layout.x) == 16);
    assert(hid_layout_read(data, length, &layout.y) == -16);
    assert(hid_layout_read(data, length, &layout.wheel) == 1);

    ESP_LOGI(TAG, "Fujitsu M520");
}

/// Stadia controller: a hat switch, and buttons that are named one usage at a time
static void test_stadia(void) {
    hid_layout_t layout;
    assert(hid_layout_parse(gamepad1_desc, gamepad1_len, &layout));

    assert(layout.report_id == 3);
    check_field(&layout.hat, 0, 4);
    check_field(&layout.buttons, 8, 1);
    assert(layout.button_count == 15);
    check_field(&layout.x, 24, 8);
    check_field(&layout.y, 32, 8);

    // A stick reports where it is, which is what makes it usable as a direction
    assert(!layout.x.relative);
    assert(!layout.y.relative);

    bool           left = false, right = false, up = false, down = false;
    const uint8_t* data   = pad1_reports[3];
    int            length = 11;
    assert(hid_layout_strip_report_id(&layout, &data, &length));
    hid_layout_axis_directions(data, length, &layout.x, &left, &right);
    hid_layout_axis_directions(data, length, &layout.y, &up, &down);
    assert(left && !right);
    assert(down && !up);

    ESP_LOGI(TAG, "Stadia controller");
}

/// DualShock 4 clone: axes before the hat switch, the other way around from most
static void test_dualshock4_clone(void) {
    hid_layout_t layout;
    assert(hid_layout_parse(gamepad2_desc, gamepad2_len, &layout));

    assert(layout.report_id == 1);
    check_field(&layout.x, 0, 8);
    check_field(&layout.y, 8, 8);
    check_field(&layout.hat, 32, 4);
    check_field(&layout.buttons, 36, 1);
    assert(layout.button_count == 14);

    ESP_LOGI(TAG, "DualShock 4 clone");
}

/// DualShock 3: nineteen buttons, no hat switch, and a pile of feature reports after
static void test_dualshock3(void) {
    hid_layout_t layout;
    assert(hid_layout_parse(gamepad3_desc, gamepad3_len, &layout));

    assert(layout.report_id == 1);
    assert(!layout.hat.present);
    check_field(&layout.buttons, 8, 1);
    assert(layout.button_count == 19);
    check_field(&layout.x, 40, 8);
    check_field(&layout.y, 48, 8);

    // Centered and nothing pressed
    bool           left = false, right = false, up = false, down = false;
    const uint8_t* data   = pad3_reports[0];
    int            length = 11;
    assert(hid_layout_strip_report_id(&layout, &data, &length));
    hid_layout_axis_directions(data, length, &layout.x, &left, &right);
    hid_layout_axis_directions(data, length, &layout.y, &up, &down);
    assert(!left && !right && !up && !down);
    for (uint16_t b = 0; b < layout.button_count; b++) {
        assert(!hid_layout_read_button(data, length, &layout, b));
    }

    // Cross is button fifteen
    data   = pad3_reports[1];
    length = 11;
    assert(hid_layout_strip_report_id(&layout, &data, &length));
    assert(hid_layout_read_button(data, length, &layout, 14));
    assert(!hid_layout_read_button(data, length, &layout, 13));

    // A report belonging to another report ID is not ours to decode
    const uint8_t other[]  = {0x02, 0x00, 0x00};
    const uint8_t* rejected = other;
    int            rejected_length = sizeof(other);
    assert(!hid_layout_strip_report_id(&layout, &rejected, &rejected_length));

    ESP_LOGI(TAG, "DualShock 3");
}

/// Competition Pro: no report ID, buttons first, and a stick that reports through X and Y
static void test_competition_pro(void) {
    hid_layout_t layout;
    assert(hid_layout_parse(gamepad4_desc, gamepad4_len, &layout));

    assert(layout.report_id == 0);
    check_field(&layout.buttons, 0, 1);
    assert(layout.button_count == 15);
    check_field(&layout.hat, 16, 4);
    check_field(&layout.x, 24, 8);
    check_field(&layout.y, 32, 8);

    bool           left = false, right = false, up = false, down = false;
    const uint8_t* data   = pad4_reports[1];
    int            length = 9;
    hid_layout_axis_directions(data, length, &layout.x, &left, &right);
    hid_layout_axis_directions(data, length, &layout.y, &up, &down);
    assert(left && !right);
    assert(!up && !down);  // The stick only moved sideways

    left = right = false;
    data  = pad4_reports[2];
    hid_layout_axis_directions(data, length, &layout.x, &left, &right);
    assert(right && !left);
    assert(hid_layout_read_button(data, length, &layout, 0));

    ESP_LOGI(TAG, "Competition Pro");
}

/// A descriptor that describes nothing usable, and a few malformed ones
static void test_rejected(void) {
    hid_layout_t layout;

    assert(!hid_layout_parse(NULL, 0, &layout));
    assert(!layout.valid);

    const uint8_t empty[] = {0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0xc0};
    assert(!hid_layout_parse(empty, sizeof(empty), &layout));

    // An item that claims more data than the descriptor holds
    const uint8_t truncated[] = {0x05, 0x01, 0x26};
    assert(!hid_layout_parse(truncated, sizeof(truncated), &layout));

    // A long item, which this parser stops at rather than reading as a short one
    const uint8_t long_item[] = {0xfe, 0x02, 0x00, 0x00, 0x00};
    assert(!hid_layout_parse(long_item, sizeof(long_item), &layout));

    ESP_LOGI(TAG, "Rejected what it cannot use");
}

int main(void) {
    test_logitech_m705();
    test_trust_mouse();
    test_fujitsu_m520();
    test_stadia();
    test_dualshock4_clone();
    test_dualshock3();
    test_competition_pro();
    test_rejected();

    printf("All report descriptor tests passed\n");
    return 0;
}
