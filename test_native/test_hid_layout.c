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

    // A real device that describes nothing this parser wants: one bit on a vendor page, inside
    // a consumer control collection, and three bytes of padding after it
    assert(!hid_layout_parse(callbutton_desc, callbutton_len, &layout));
    assert(!layout.valid);

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

/// Xbox Wireless Controller: sixteen bit sticks, and analog triggers on the simulation page
static void test_xbox_wireless(void) {
    hid_layout_t layout;
    assert(hid_layout_parse(gamepad5_desc, gamepad5_len, &layout));

    assert(layout.report_id == 1);
    check_field(&layout.x, 0, 16);
    check_field(&layout.y, 16, 16);
    check_field(&layout.z, 32, 16);
    check_field(&layout.rz, 48, 16);
    check_field(&layout.brake, 64, 10);
    check_field(&layout.accelerator, 80, 10);
    check_field(&layout.hat, 96, 4);
    check_field(&layout.buttons, 104, 1);
    assert(layout.button_count == 15);

    // A sixteen bit axis that says its range is nought to 65535 is unsigned, so the top half of
    // it has to come out positive rather than as a negative number
    assert(layout.x.logical_min == 0);
    assert(layout.x.logical_max == 65535);
    assert(layout.brake.logical_max == 1023);

    const uint8_t* data   = pad5_reports[2];
    int            length = 17;
    assert(hid_layout_strip_report_id(&layout, &data, &length));
    assert(hid_layout_read(data, length, &layout.x) == 65535);
    assert(hid_layout_read(data, length, &layout.y) == 0);

    // Ten bits that do not end on a byte boundary, one either side of the padding between them
    assert(hid_layout_read(data, length, &layout.brake) == 0);
    assert(hid_layout_read(data, length, &layout.accelerator) == 1023);

    data   = pad5_reports[1];
    length = 17;
    assert(hid_layout_strip_report_id(&layout, &data, &length));
    assert(hid_layout_read(data, length, &layout.brake) == 1023);
    assert(hid_layout_read(data, length, &layout.accelerator) == 0);

    // The Record button after them is a consumer control rather than a button, so it is left
    // where it is instead of being counted as a sixteenth
    assert(layout.button_count == 15);

    ESP_LOGI(TAG, "Xbox Wireless Controller");
}

/// Switch Pro Controller: four byte usages, which name the page they belong to themselves
static void test_switch_pro(void) {
    hid_layouts_t layouts;
    assert(hid_layout_parse_all(gamepad6_desc, gamepad6_len, &layouts));

    // Two vendor reports come after the one worth having, and neither holds anything usable
    assert(layouts.count == 3);
    const hid_layout_t* layout = hid_layouts_best(&layouts);
    assert(layout != NULL);
    assert(layout->report_id == 0x30);
    assert(hid_layouts_find(&layouts, 0x21) != NULL);
    assert(!hid_layouts_find(&layouts, 0x21)->valid);

    // Its axes and its hat switch are named by usages four bytes wide, which carry the generic
    // desktop page in their top half while the global page is still the button page. Reading
    // only the bottom half puts them on the wrong page and loses every one of them.
    check_field(&layout->x, 16, 16);
    check_field(&layout->y, 32, 16);
    check_field(&layout->z, 48, 16);
    check_field(&layout->rz, 64, 16);
    check_field(&layout->hat, 80, 4);

    // Buttons one to ten and eleven to fourteen are separate items that carry straight on from
    // one another, so they count as one run. Fifteen to eighteen sit past the hat switch and
    // cannot be numbered in the same run, so they are left.
    check_field(&layout->buttons, 0, 1);
    assert(layout->button_count == 14);
    for (int b = 0; b < 14; b++) {
        assert(layout->button_usage[b] == b + 1);
    }

    // This hat numbers its directions from nought rather than from one
    assert(layout->hat.logical_min == 0);
    assert(layout->hat.logical_max == 7);

    ESP_LOGI(TAG, "Switch Pro Controller");
}

/// Xbox 360 wired controller: no report ID, and a hat switch that starts mid byte
static void test_xbox_360(void) {
    hid_layout_t layout;
    assert(hid_layout_parse(gamepad7_desc, gamepad7_len, &layout));

    assert(layout.report_id == 0);
    check_field(&layout.x, 0, 16);
    check_field(&layout.y, 16, 16);
    check_field(&layout.rx, 32, 16);
    check_field(&layout.ry, 48, 16);
    check_field(&layout.z, 64, 8);
    check_field(&layout.rz, 72, 8);
    check_field(&layout.buttons, 80, 1);
    assert(layout.button_count == 10);

    // Ten buttons leave the hat switch starting at bit ninety, four bits spread over two bytes
    check_field(&layout.hat, 90, 4);

    bool up = false, down = false, left = false, right = false;
    hid_layout_hat_directions(pad7_reports[1], 14, &layout.hat, &up, &down, &left, &right);
    assert(up && !down && !left && !right);

    // Its triggers are ordinary axes rather than simulation controls
    assert(!layout.accelerator.present);
    assert(!layout.brake.present);
    assert(hid_layout_read(pad7_reports[2], 14, &layout.z) == 255);
    assert(hid_layout_read(pad7_reports[2], 14, &layout.rz) == 255);

    ESP_LOGI(TAG, "Xbox 360 wired controller");
}

/// Luna controller: a hat switch a whole byte wide
static void test_luna(void) {
    hid_layout_t layout;
    assert(hid_layout_parse(gamepad8_desc, gamepad8_len, &layout));

    assert(layout.report_id == 1);
    check_field(&layout.buttons, 0, 1);
    assert(layout.button_count == 12);
    check_field(&layout.hat, 16, 8);
    check_field(&layout.x, 24, 8);
    check_field(&layout.y, 32, 8);
    check_field(&layout.z, 40, 8);
    check_field(&layout.rz, 48, 8);
    check_field(&layout.rx, 56, 8);
    check_field(&layout.ry, 64, 8);

    const uint8_t* data   = pad8_reports[0];
    int            length = 10;
    assert(hid_layout_strip_report_id(&layout, &data, &length));

    // A byte wide hat that rests at eight, one past the seven it described
    bool up = false, down = false, left = false, right = false;
    hid_layout_hat_directions(data, length, &layout.hat, &up, &down, &left, &right);
    assert(!up && !down && !left && !right);

    data   = pad8_reports[1];
    length = 10;
    assert(hid_layout_strip_report_id(&layout, &data, &length));
    hid_layout_hat_directions(data, length, &layout.hat, &up, &down, &left, &right);
    assert(up && !down && !left && !right);

    ESP_LOGI(TAG, "Luna controller");
}

/// DualSense: six axes in one item, and vendor data between them and the hat switch
static void test_dualsense(void) {
    hid_layout_t layout;
    assert(hid_layout_parse(gamepad9_desc, gamepad9_len, &layout));

    assert(layout.report_id == 1);
    check_field(&layout.x, 0, 8);
    check_field(&layout.y, 8, 8);
    check_field(&layout.z, 16, 8);
    check_field(&layout.rz, 24, 8);
    check_field(&layout.rx, 32, 8);
    check_field(&layout.ry, 40, 8);

    // A byte of vendor data sits between the axes and the hat switch. It names no usage this
    // parser wants, but it still takes up room, so everything after it has to move along.
    check_field(&layout.hat, 56, 4);
    // Thirteen more vendor bits follow the buttons, which are not buttons however close they sit
    check_field(&layout.buttons, 60, 1);
    assert(layout.button_count == 15);

    ESP_LOGI(TAG, "DualSense");
}

/// Xbox 360 racing wheel: steering in X, and no Y at all
static void test_racing_wheel(void) {
    hid_layout_t layout;
    assert(hid_layout_parse(gamepad10_desc, gamepad10_len, &layout));

    assert(layout.report_id == 0);
    check_field(&layout.x, 0, 16);
    assert(!layout.y.present);
    check_field(&layout.z, 16, 8);
    check_field(&layout.buttons, 32, 1);
    assert(layout.button_count == 10);
    check_field(&layout.hat, 48, 4);

    // Its pedal is a Throttle, which is a simulation control this parser has no field for, so it
    // is left alone rather than mistaken for one of the triggers
    assert(!layout.accelerator.present);
    assert(!layout.brake.present);

    bool up = false, down = false, left = false, right = false;
    hid_layout_axis_directions(pad10_reports[1], 8, &layout.x, &left, &right);
    assert(left && !right);
    hid_layout_axis_directions(pad10_reports[2], 8, &layout.x, &left, &right);
    assert(!left && right);

    // Nothing reads a direction out of an axis the device does not have
    hid_layout_axis_directions(pad10_reports[2], 8, &layout.y, &up, &down);
    assert(!up && !down);

    ESP_LOGI(TAG, "Xbox 360 racing wheel");
}

/// Arcade stick: buttons and a hat switch, and no axis anywhere
static void test_arcade_stick(void) {
    hid_layout_t layout;
    assert(hid_layout_parse(gamepad11_desc, gamepad11_len, &layout));

    assert(layout.report_id == 0);
    check_field(&layout.buttons, 0, 1);
    assert(layout.button_count == 10);
    check_field(&layout.hat, 16, 4);
    assert(!layout.x.present);
    assert(!layout.y.present);

    // Its buttons come with no logical minimum or maximum at all, so the globals in force are
    // still nought and nought. A button is one bit either way, and the parser says so itself
    // rather than believing a range that would make every button read as unpressed.
    assert(layout.buttons.logical_min == 0);
    assert(layout.buttons.logical_max == 1);
    assert(hid_layout_read_button(pad11_reports[1], 4, &layout, 0));
    assert(!hid_layout_read_button(pad11_reports[1], 4, &layout, 1));
    for (int b = 0; b < 10; b++) {
        assert(hid_layout_read_button(pad11_reports[2], 4, &layout, b));
    }

    // An eleventh button is not one it has
    assert(!hid_layout_read_button(pad11_reports[2], 4, &layout, 10));

    ESP_LOGI(TAG, "Arcade stick");
}

/// Guitar controller: a Slider and a Dial, which are generic desktop usages with no field here
static void test_guitar(void) {
    hid_layout_t layout;
    assert(hid_layout_parse(gamepad12_desc, gamepad12_len, &layout));

    assert(layout.report_id == 0);

    // The whammy bar is an Rz. The Slider at bit sixteen and the Dial at bit thirty-two are not
    // taken, but they still take up room, so what follows has to be found past them.
    check_field(&layout.rz, 0, 16);
    assert(!layout.x.present);
    assert(!layout.y.present);
    assert(!layout.z.present);
    assert(!layout.wheel.present);
    check_field(&layout.buttons, 48, 1);
    assert(layout.button_count == 10);
    check_field(&layout.hat, 64, 4);

    assert(hid_layout_read(pad12_reports[0], 10, &layout.rz) == 0x8000);
    assert(hid_layout_read(pad12_reports[2], 10, &layout.rz) == 65535);

    bool up = false, down = false, left = false, right = false;
    hid_layout_hat_directions(pad12_reports[1], 10, &layout.hat, &up, &down, &left, &right);
    assert(down && !up && !left && !right);

    ESP_LOGI(TAG, "Guitar controller");
}

/// BigBen pad: buttons before the hat switch, and the sticks behind both
static void test_bigben(void) {
    hid_layout_t layout;
    assert(hid_layout_parse(gamepad13_desc, gamepad13_len, &layout));

    assert(layout.report_id == 0);
    check_field(&layout.buttons, 0, 1);
    assert(layout.button_count == 13);
    check_field(&layout.hat, 16, 4);
    check_field(&layout.x, 24, 8);
    check_field(&layout.y, 32, 8);
    check_field(&layout.z, 40, 8);
    check_field(&layout.rz, 48, 8);

    // Three padding bits sit between the thirteen buttons and the hat switch, and four more
    // between the hat and the sticks. Both have to be counted or nothing lines up.
    assert(hid_layout_read(pad13_reports[2], 27, &layout.x) == 0);
    assert(hid_layout_read(pad13_reports[2], 27, &layout.y) == 255);
    assert(hid_layout_read_button(pad13_reports[1], 27, &layout, 0));

    // Twelve vendor bytes and then four vendor axes follow the sticks, and none of them is
    // anything this parser takes
    assert(!layout.rx.present);
    assert(!layout.ry.present);
    assert(!layout.wheel.present);

    ESP_LOGI(TAG, "BigBen pad");
}

/// SteelSeries SRW-S1: a signed axis, twelve bit axes and buttons starting at bit forty seven
static void test_srws1(void) {
    hid_layout_t layout;
    assert(hid_layout_parse(wheel1_desc, wheel1_len, &layout));

    check_field(&layout.x, 0, 16);
    assert(layout.x.logical_min == -1800);
    assert(layout.x.logical_max == 1800);
    check_field(&layout.y, 16, 12);
    check_field(&layout.z, 28, 12);
    check_field(&layout.hat, 40, 4);
    check_field(&layout.buttons, 47, 1);
    assert(layout.button_count == 17);

    // Two more axes, four bits each, whose ranges stop nowhere near a power of two
    check_field(&layout.rx, 64, 4);
    assert(layout.rx.logical_max == 11);
    check_field(&layout.rz, 72, 4);
    assert(layout.rz.logical_max == 3);

    // A negative logical minimum means the field is signed, so the top half of its range has to
    // come out below nought rather than as a large positive number
    assert(hid_layout_read(wheel1_reports[2], 16, &layout.x) == -1800);
    assert(hid_layout_read(wheel1_reports[3], 16, &layout.x) == 1800);
    assert(hid_layout_read(wheel1_reports[0], 16, &layout.x) == 0);

    // Its middle is nought rather than half of its range, and a quarter of the range either side
    // of that is still not a direction
    bool left = false, right = false;
    hid_layout_axis_directions(wheel1_reports[0], 16, &layout.x, &left, &right);
    assert(!left && !right);
    hid_layout_axis_directions(wheel1_reports[2], 16, &layout.x, &left, &right);
    assert(left && !right);
    hid_layout_axis_directions(wheel1_reports[3], 16, &layout.x, &left, &right);
    assert(!left && right);

    // Seventeen buttons that start seven bits into a byte and run over two more
    assert(hid_layout_read_button(wheel1_reports[2], 16, &layout, 0));
    assert(!hid_layout_read_button(wheel1_reports[2], 16, &layout, 1));
    for (int b = 0; b < 17; b++) {
        assert(hid_layout_read_button(wheel1_reports[3], 16, &layout, b));
    }
    assert(!hid_layout_read_button(wheel1_reports[3], 16, &layout, 17));

    ESP_LOGI(TAG, "SteelSeries SRW-S1");
}

/// Driving Force Pro: a fourteen bit axis with the buttons packed in behind it
static void test_driving_force_pro(void) {
    hid_layout_t layout;
    assert(hid_layout_parse(wheel2_desc, wheel2_len, &layout));

    check_field(&layout.x, 0, 14);
    assert(layout.x.logical_max == 16383);
    check_field(&layout.buttons, 14, 1);
    assert(layout.button_count == 14);
    check_field(&layout.hat, 28, 4);
    check_field(&layout.y, 40, 8);
    check_field(&layout.rz, 48, 8);

    assert(hid_layout_read(wheel2_reports[0], 8, &layout.x) == 8192);
    assert(hid_layout_read(wheel2_reports[1], 8, &layout.x) == 0);
    assert(hid_layout_read(wheel2_reports[2], 8, &layout.x) == 16383);

    // The first button is the two bits after the axis, in the same byte as the top of it
    assert(hid_layout_read_button(wheel2_reports[1], 8, &layout, 0));
    assert(!hid_layout_read_button(wheel2_reports[1], 8, &layout, 1));

    ESP_LOGI(TAG, "Driving Force Pro");
}

/// PhoenixRC: usages this parser has no field for, in the middle of an item rather than after it
static void test_phoenixrc(void) {
    hid_layout_t layout;
    assert(hid_layout_parse(rc1_desc, rc1_len, &layout));

    // One item names X, Slider, Y, Z, Rx, Ry, Rz, Dial. The Slider is second and the Dial last,
    // and neither has a field here, but both take their turn, so Y is a byte further along than
    // a reader that skipped them would look.
    check_field(&layout.x, 0, 8);
    check_field(&layout.y, 16, 8);
    check_field(&layout.z, 24, 8);
    check_field(&layout.rx, 32, 8);
    check_field(&layout.ry, 40, 8);
    check_field(&layout.rz, 48, 8);

    // The report that proves it: the Slider is hard over and Y is centered, so anything reading
    // Y one byte early gets 255 instead
    assert(hid_layout_read(rc1_reports[1], 8, &layout.x) == 0);
    assert(hid_layout_read(rc1_reports[1], 8, &layout.y) == 0x80);
    assert(hid_layout_read(rc1_reports[2], 8, &layout.y) == 0);

    assert(!layout.buttons.present);
    assert(!layout.hat.present);
    assert(layout.button_count == 0);

    ESP_LOGI(TAG, "PhoenixRC adapter");
}

/// VRC-2: two axes and nothing else whatsoever
static void test_vrc2(void) {
    hid_layout_t layout;
    assert(hid_layout_parse(rc2_desc, rc2_len, &layout));

    check_field(&layout.x, 0, 16);
    check_field(&layout.y, 16, 16);
    assert(layout.x.logical_max == 2047);
    assert(!layout.hat.present);
    assert(!layout.buttons.present);
    assert(layout.button_count == 0);

    // No buttons means no button reads, whichever one is asked for
    assert(!hid_layout_read_button(rc2_reports[1], 7, &layout, 0));
    assert(!hid_layout_read_button(rc2_reports[1], 7, &layout, 31));

    bool low = false, high = false;
    hid_layout_axis_directions(rc2_reports[0], 7, &layout.x, &low, &high);
    assert(!low && !high);
    hid_layout_axis_directions(rc2_reports[1], 7, &layout.x, &low, &high);
    assert(low && !high);
    hid_layout_axis_directions(rc2_reports[2], 7, &layout.y, &low, &high);
    assert(low && !high);

    ESP_LOGI(TAG, "VRC-2 adapter");
}

/// DragonRise JS19: three bytes of padding before anything worth reading
static void test_dragonrise(void) {
    hid_layout_t layout;
    assert(hid_layout_parse(gamepad14_desc, gamepad14_len, &layout));

    check_field(&layout.x, 24, 8);
    check_field(&layout.y, 32, 8);
    check_field(&layout.buttons, 44, 1);
    assert(layout.button_count == 10);
    assert(!layout.hat.present);

    // Four padding bits sit between the sticks and the buttons, so the first button is the fifth
    // bit of its byte rather than the first
    assert(hid_layout_read_button(pad14_reports[1], 8, &layout, 0));
    assert(!hid_layout_read_button(pad14_reports[1], 8, &layout, 1));
    for (int b = 0; b < 10; b++) {
        assert(hid_layout_read_button(pad14_reports[2], 8, &layout, b));
    }

    // Ten more button bits follow, but the descriptor calls them constant, so they are padding
    assert(layout.button_count == 10);

    ESP_LOGI(TAG, "DragonRise JS19");
}

int main(void) {
    test_logitech_m705();
    test_trust_mouse();
    test_fujitsu_m520();
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
    test_phoenixrc();
    test_vrc2();
    test_dragonrise();
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
