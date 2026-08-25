// Fuzzing entry point for the report descriptor parser.
//
// The parser reads a buffer a device handed over, which is as untrusted as input gets, so it is
// fed random bytes here and has to come out of it without reading anything it was not given.
//
// Build and run it with:
//
//     make -C test_native fuzz

#include <stddef.h>
#include <stdint.h>
#include "hid_layout.h"

// Bytes of the input handed back as a report, so the readers get exercised as well
#define REPORT_BYTES 64

static void read_everything(const hid_layout_t* layout, const uint8_t* data, int length) {
    const hid_field_t* fields[] = {&layout->x,  &layout->y,  &layout->z,     &layout->rx,
                                   &layout->ry, &layout->rz, &layout->wheel, &layout->hat};

    for (size_t f = 0; f < sizeof(fields) / sizeof(fields[0]); f++) {
        bool low = false, high = false;
        hid_layout_read(data, length, fields[f]);
        hid_layout_axis_directions(data, length, fields[f], &low, &high);
    }

    bool up = false, down = false, left = false, right = false;
    hid_layout_hat_directions(data, length, &layout->hat, &up, &down, &left, &right);

    for (uint32_t b = 0; b <= layout->button_count; b++) {
        hid_layout_read_button(data, length, layout, (uint16_t)b);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    hid_layouts_t layouts;

    if (!hid_layout_parse_all(data, size, &layouts)) {
        return 0;
    }

    int report_length = (int)(size < REPORT_BYTES ? size : REPORT_BYTES);

    for (uint8_t r = 0; r < layouts.count; r++) {
        const hid_layout_t* layout = &layouts.report[r];

        const uint8_t* report = data;
        int            length = report_length;
        if (!hid_layout_strip_report_id(layout, &report, &length)) {
            continue;
        }
        read_everything(layout, report, length);
    }

    hid_layout_t best;
    if (hid_layout_parse(data, size, &best)) {
        read_everything(&best, data, report_length);
    }

    return 0;
}
