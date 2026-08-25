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

    // It lists its buttons backwards, so the first bit of its report is Button 18 and the last is
    // Button 1. Anything reading by bit position mislabels every button on it.
    static const uint8_t expected[15] = {18, 17, 20, 19, 13, 12, 11, 15, 14, 8, 7, 5, 4, 2, 1};
    for (int b = 0; b < 15; b++) {
        assert(layout.button_usage[b] == expected[b]);
    }

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

/// The axes past X and Y, which is where a gamepad puts its right stick and its triggers
static void test_extra_axes(void) {
    hid_layout_t layout;

    // Stadia: X and Y, then Z and Rz for the other stick
    assert(hid_layout_parse(gamepad1_desc, gamepad1_len, &layout));
    check_field(&layout.z, 40, 8);
    check_field(&layout.rz, 48, 8);
    assert(!layout.rx.present);
    assert(!layout.ry.present);

    // DualShock 4 clone: Z and Rz for the stick, Rx and Ry for the triggers, well apart from it
    assert(hid_layout_parse(gamepad2_desc, gamepad2_len, &layout));
    check_field(&layout.z, 16, 8);
    check_field(&layout.rz, 24, 8);
    check_field(&layout.rx, 56, 8);
    check_field(&layout.ry, 64, 8);

    // DualShock 3
    assert(hid_layout_parse(gamepad3_desc, gamepad3_len, &layout));
    check_field(&layout.z, 56, 8);
    check_field(&layout.rz, 64, 8);

    // Competition Pro
    assert(hid_layout_parse(gamepad4_desc, gamepad4_len, &layout));
    check_field(&layout.z, 40, 8);
    check_field(&layout.rz, 48, 8);

    ESP_LOGI(TAG, "Axes past X and Y");
}

/// The analog triggers and the sideways wheel, which live on pages of their own
static void test_simulation_and_consumer(void) {
    hid_layout_t layout;

    // Stadia: Brake then Accelerator, after both sticks
    assert(hid_layout_parse(gamepad1_desc, gamepad1_len, &layout));
    check_field(&layout.brake, 56, 8);
    check_field(&layout.accelerator, 64, 8);

    const uint8_t* data   = pad1_reports[2];
    int            length = 11;
    assert(hid_layout_strip_report_id(&layout, &data, &length));
    assert(hid_layout_read(data, length, &layout.brake) == 255);
    assert(hid_layout_read(data, length, &layout.accelerator) == 255);

    data   = pad1_reports[0];
    length = 11;
    assert(hid_layout_strip_report_id(&layout, &data, &length));
    assert(hid_layout_read(data, length, &layout.brake) == 0);
    assert(hid_layout_read(data, length, &layout.accelerator) == 0);

    // Competition Pro: the same pair, the other way round
    assert(hid_layout_parse(gamepad4_desc, gamepad4_len, &layout));
    check_field(&layout.accelerator, 56, 8);
    check_field(&layout.brake, 64, 8);

    // A gamepad that names no simulation controls has none
    assert(hid_layout_parse(gamepad3_desc, gamepad3_len, &layout));
    assert(!layout.accelerator.present);
    assert(!layout.brake.present);

    // The Logitech M705 reports its sideways wheel as a consumer control, not as an axis
    assert(hid_layout_parse(mouse1_desc, mouse1_len, &layout));
    check_field(&layout.pan, 48, 8);
    assert(layout.pan.relative);
    assert(hid_layout_parse(mouse3_desc, mouse3_len, &layout));
    assert(!layout.pan.present);

    ESP_LOGI(TAG, "Triggers and the sideways wheel");
}

/// Every input report of a device, rather than the first one that turns up
static void test_multiple_reports(void) {
    // Report one holds two buttons, report two an axis pair and eight buttons, and then the
    // descriptor goes back to report one for a hat switch
    static const uint8_t desc[] = {
        0x05, 0x01, 0x09, 0x05, 0xa1, 0x01,                                                  //
        0x85, 0x01,                                                                          // Report ID 1
        0x05, 0x09, 0x19, 0x01, 0x29, 0x02, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x02,  //
        0x81, 0x02,                                                                          // Two buttons
        0x95, 0x06, 0x81, 0x03,                                                              // Six bits padding
        0x85, 0x02,                                                                          // Report ID 2
        0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08,        //
        0x95, 0x02, 0x81, 0x02,                                                              // X and Y
        0x05, 0x09, 0x19, 0x01, 0x29, 0x08, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08,  //
        0x81, 0x02,                                                                          // Eight buttons
        0x85, 0x01,                                                                          // Report ID 1 again
        0x05, 0x01, 0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42,  // Hat
        0xc0};

    hid_layouts_t layouts;
    assert(hid_layout_parse_all(desc, sizeof(desc), &layouts));
    assert(layouts.count == 2);

    const hid_layout_t* first = hid_layouts_find(&layouts, 1);
    assert(first != NULL);
    assert(first->button_count == 2);
    // The hat comes after eight bits of report one, not after anything report two described
    check_field(&first->hat, 8, 4);

    const hid_layout_t* second = hid_layouts_find(&layouts, 2);
    assert(second != NULL);
    check_field(&second->x, 0, 8);
    check_field(&second->y, 8, 8);
    check_field(&second->buttons, 16, 1);
    assert(second->button_count == 8);

    assert(hid_layouts_find(&layouts, 3) == NULL);

    // Two axes and eight buttons beat two buttons and a hat
    const hid_layout_t* best = hid_layouts_best(&layouts);
    assert(best == second);

    hid_layout_t picked;
    assert(hid_layout_parse(desc, sizeof(desc), &picked));
    assert(picked.report_id == 2);

    // A mouse that describes a consumer control report as well still parses as its first report
    assert(hid_layout_parse_all(mouse1_desc, mouse1_len, &layouts));
    assert(layouts.count == 4);  // Report two and three input reports holding nothing usable
    assert(hid_layouts_best(&layouts)->report_id == 2);
    assert(hid_layouts_find(&layouts, 3) != NULL);
    assert(!hid_layouts_find(&layouts, 3)->valid);

    ESP_LOGI(TAG, "Every input report");
}

/// More input reports than the table holds
static void test_report_table_full(void) {
    uint8_t desc[512];
    size_t  n = 0;

    const uint8_t header[] = {0x05, 0x01, 0x09, 0x05, 0xa1, 0x01};
    memcpy(desc, header, sizeof(header));
    n = sizeof(header);

    for (unsigned id = 1; id <= HID_LAYOUT_MAX_REPORTS + 4; id++) {
        const uint8_t report[] = {0x85, (uint8_t)id,                                      // Report ID
                                  0x05, 0x09, 0x19, 0x01, 0x29, 0x08, 0x15, 0x00, 0x25,   //
                                  0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02};              // Eight buttons
        assert(n + sizeof(report) < sizeof(desc));
        memcpy(&desc[n], report, sizeof(report));
        n += sizeof(report);
    }
    desc[n++] = 0xc0;

    hid_layouts_t layouts;
    assert(hid_layout_parse_all(desc, n, &layouts));
    assert(layouts.count == HID_LAYOUT_MAX_REPORTS);
    assert(hid_layouts_find(&layouts, 1) != NULL);
    assert(hid_layouts_find(&layouts, HID_LAYOUT_MAX_REPORTS + 1) == NULL);

    ESP_LOGI(TAG, "More reports than the table holds");
}

/// Push and Pop, which put the global items back the way a collection found them
static void test_push_pop(void) {
    static const uint8_t desc[] = {
        0x05, 0x01, 0x09, 0x04, 0xa1, 0x01,                                                  //
        0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x02,                                // Two eight bit fields
        0xa4,                                                                                // Push
        0x05, 0x09, 0x19, 0x01, 0x29, 0x04, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x04,  //
        0x81, 0x02,                                                                          // Four buttons
        0xb4,                                                                                // Pop
        0x09, 0x30, 0x09, 0x31, 0x81, 0x02,                                                  // X and Y
        0xc0};

    hid_layout_t layout;
    assert(hid_layout_parse(desc, sizeof(desc), &layout));

    check_field(&layout.buttons, 0, 1);
    assert(layout.button_count == 4);

    // Everything the button collection changed has to be back: the usage page, the report size
    // and the logical range
    check_field(&layout.x, 4, 8);
    check_field(&layout.y, 12, 8);
    assert(layout.x.logical_min == 0);
    assert(layout.x.logical_max == 255);

    ESP_LOGI(TAG, "Push and Pop");
}

/// Buttons split over two input items
static void test_split_buttons(void) {
    static const uint8_t joined[] = {
        0x05, 0x01, 0x09, 0x05, 0xa1, 0x01,                                                  //
        0x05, 0x09, 0x19, 0x01, 0x29, 0x08, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08,  //
        0x81, 0x02,                                                                          // Eight buttons
        0x19, 0x09, 0x29, 0x10, 0x95, 0x08, 0x81, 0x02,                                      // Eight more
        0xc0};

    hid_layout_t layout;
    assert(hid_layout_parse(joined, sizeof(joined), &layout));
    check_field(&layout.buttons, 0, 1);
    assert(layout.button_count == 16);

    const uint8_t report[] = {0x00, 0x80};
    assert(hid_layout_read_button(report, sizeof(report), &layout, 15));
    assert(!hid_layout_read_button(report, sizeof(report), &layout, 14));

    // The same, with four bits of padding in between, which no run of button numbers survives
    static const uint8_t apart[] = {
        0x05, 0x01, 0x09, 0x05, 0xa1, 0x01,                                                  //
        0x05, 0x09, 0x19, 0x01, 0x29, 0x08, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08,  //
        0x81, 0x02,                                                                          // Eight buttons
        0x95, 0x04, 0x81, 0x03,                                                              // Four bits padding
        0x19, 0x09, 0x29, 0x10, 0x95, 0x08, 0x81, 0x02,                                      // Eight more
        0xc0};

    assert(hid_layout_parse(apart, sizeof(apart), &layout));
    assert(layout.button_count == 8);

    ESP_LOGI(TAG, "Buttons split over two items");
}

/// Which way a hat switch is pushed
static void test_hat_directions(void) {
    hid_layout_t layout;
    assert(hid_layout_parse(gamepad1_desc, gamepad1_len, &layout));

    bool up = true, down = true, left = true, right = true;

    // A hat resting outside the range it described clears everything
    const uint8_t* data   = pad1_reports[1];
    int            length = 11;
    assert(hid_layout_strip_report_id(&layout, &data, &length));
    hid_layout_hat_directions(data, length, &layout.hat, &up, &down, &left, &right);
    assert(!up && !down && !left && !right);

    // Six of eight positions is west
    data   = pad1_reports[0];
    length = 11;
    assert(hid_layout_strip_report_id(&layout, &data, &length));
    hid_layout_hat_directions(data, length, &layout.hat, &up, &down, &left, &right);
    assert(left && !right && !up && !down);

    // Zero is north
    data   = pad1_reports[3];
    length = 11;
    assert(hid_layout_strip_report_id(&layout, &data, &length));
    hid_layout_hat_directions(data, length, &layout.hat, &up, &down, &left, &right);
    assert(up && !down && !left && !right);

    // A diagonal is two of them at once
    const uint8_t northeast[] = {0x01, 0x00, 0x00, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00};
    hid_layout_hat_directions(northeast, sizeof(northeast), &layout.hat, &up, &down, &left, &right);
    assert(up && right && !down && !left);

    // A hat with four positions, counted from one
    static const uint8_t desc[] = {
        0x05, 0x01, 0x09, 0x05, 0xa1, 0x01,                                                  //
        0x09, 0x39, 0x15, 0x01, 0x25, 0x04, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42,              // Hat, 1 to 4
        0x95, 0x01, 0x81, 0x01,                                                              // Four bits padding
        0x05, 0x09, 0x19, 0x01, 0x29, 0x01, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01,  //
        0x81, 0x02,                                                                          // One button
        0xc0};

    assert(hid_layout_parse(desc, sizeof(desc), &layout));
    check_field(&layout.hat, 0, 4);

    const uint8_t east[] = {0x02};
    hid_layout_hat_directions(east, sizeof(east), &layout.hat, &up, &down, &left, &right);
    assert(right && !up && !down && !left);

    const uint8_t west[] = {0x04};
    hid_layout_hat_directions(west, sizeof(west), &layout.hat, &up, &down, &left, &right);
    assert(left && !up && !down && !right);

    const uint8_t resting[] = {0x00};
    hid_layout_hat_directions(resting, sizeof(resting), &layout.hat, &up, &down, &left, &right);
    assert(!up && !down && !left && !right);

    // A device without one clears them too
    hid_layout_t empty;
    memset(&empty, 0, sizeof(empty));
    up = down = left = right = true;
    hid_layout_hat_directions(east, sizeof(east), &empty.hat, &up, &down, &left, &right);
    assert(!up && !down && !left && !right);

    ESP_LOGI(TAG, "Hat directions");
}

/// Reading past the end of what the device or the caller handed over
static void test_bounds(void) {
    hid_layout_t layout;
    assert(hid_layout_parse(gamepad4_desc, gamepad4_len, &layout));

    const uint8_t* data   = pad4_reports[2];
    int            length = 9;

    // A button the device does not have is not pressed, whatever the bits after it say
    assert(layout.button_count == 15);
    assert(!hid_layout_read_button(data, length, &layout, 15));
    assert(!hid_layout_read_button(data, length, &layout, 0xffff));

    // A report shorter than the descriptor promises reads as zero rather than past its end
    assert(hid_layout_read(data, 1, &layout.x) == 0);
    assert(hid_layout_read(data, 0, &layout.x) == 0);
    assert(hid_layout_read(NULL, 9, &layout.x) == 0);
    assert(!hid_layout_read_button(data, 0, &layout, 0));

    // Both outputs are written even when there is nothing to read
    bool low = true, high = true;
    hid_layout_axis_directions(data, length, &layout.x, &low, &high);
    assert(high && !low);
    hid_layout_axis_directions(data, 1, &layout.x, &low, &high);
    assert(!low && !high);

    hid_layout_t empty;
    memset(&empty, 0, sizeof(empty));
    low = high = true;
    hid_layout_axis_directions(data, length, &empty.x, &low, &high);
    assert(!low && !high);

    // An axis of no width at all, which a descriptor is free to describe
    static const uint8_t no_width[] = {
        0x05, 0x01, 0x09, 0x05, 0xa1, 0x01,                                      //
        0x09, 0x30, 0x15, 0x81, 0x25, 0x7f, 0x75, 0x00, 0x95, 0x01, 0x81, 0x02,  // Report Size 0
        0xc0};
    assert(hid_layout_parse(no_width, sizeof(no_width), &layout));
    assert(layout.x.present);
    assert(layout.x.bit_size == 0);
    assert(hid_layout_read(data, length, &layout.x) == 0);

    // A report wider than a bit offset can name
    static const uint8_t too_wide[] = {
        0x05, 0x01, 0x09, 0x05, 0xa1, 0x01,                                            //
        0x09, 0x30, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x20, 0x96, 0xff, 0xff,        // Report Count 65535
        0x81, 0x02,                                                                    //
        0x09, 0x31, 0x81, 0x02,                                                        //
        0xc0};
    assert(hid_layout_parse(too_wide, sizeof(too_wide), &layout));
    assert(layout.x.present);
    assert(!layout.y.present);  // Parsing stopped rather than wrapping round to an offset it fits

    ESP_LOGI(TAG, "Stayed inside the report");
}

int main(void) {
    test_logitech_m705();
    test_trust_mouse();
    test_fujitsu_m520();
    test_stadia();
    test_dualshock4_clone();
    test_dualshock3();
    test_competition_pro();
    test_extra_axes();
    test_simulation_and_consumer();
    test_multiple_reports();
    test_report_table_full();
    test_push_pop();
    test_split_buttons();
    test_hat_directions();
    test_bounds();
    test_rejected();

    printf("All report descriptor tests passed\n");
    return 0;
}
