// Parser for USB HID report descriptors.
//
// Devices of the same kind do not agree on a report layout: one gamepad puts its stick in a hat
// switch, the next reports X and Y and never touches the hat, and a mouse may or may not put a
// report ID in front of it all. Guessing by report length only ever works for the devices it was
// written against, so the descriptor is read instead and the controls are looked up by usage.

#include "hid_layout.h"
#include <string.h>

// Report descriptor item prefix, see HID 1.11 section 6.2.2.2
#define HID_ITEM_SIZE(prefix) ((prefix) & 0x03)
#define HID_ITEM_TAG(prefix)  ((prefix) & 0xfc)

#define HID_ITEM_INPUT          0x80
#define HID_ITEM_OUTPUT         0x90
#define HID_ITEM_FEATURE        0xb0
#define HID_ITEM_COLLECTION     0xa0
#define HID_ITEM_END_COLLECTION 0xc0
#define HID_ITEM_USAGE_PAGE     0x04
#define HID_ITEM_LOGICAL_MIN    0x14
#define HID_ITEM_LOGICAL_MAX    0x24
#define HID_ITEM_REPORT_SIZE    0x74
#define HID_ITEM_REPORT_ID      0x84
#define HID_ITEM_REPORT_COUNT   0x94
#define HID_ITEM_PUSH           0xa4
#define HID_ITEM_POP            0xb4
#define HID_ITEM_USAGE          0x08
#define HID_ITEM_USAGE_MIN      0x18
#define HID_ITEM_USAGE_MAX      0x28

// Input item is constant instead of data, so padding rather than a control
#define HID_INPUT_CONSTANT 0x01

// Input item reports a change rather than a position, as a mouse does
#define HID_INPUT_RELATIVE 0x04

#define HID_USAGE_PAGE_GENERIC_DESKTOP 0x01
#define HID_USAGE_PAGE_SIMULATION      0x02
#define HID_USAGE_PAGE_BUTTON          0x09
#define HID_USAGE_PAGE_CONSUMER        0x0c

#define HID_USAGE_X          0x30
#define HID_USAGE_Y          0x31
#define HID_USAGE_Z          0x32
#define HID_USAGE_RX         0x33
#define HID_USAGE_RY         0x34
#define HID_USAGE_RZ         0x35
#define HID_USAGE_WHEEL      0x38
#define HID_USAGE_HAT_SWITCH 0x39

// Simulation controls, which is where the analog triggers of a gamepad are
#define HID_USAGE_ACCELERATOR 0xc4
#define HID_USAGE_BRAKE       0xc5

// Consumer control, the one a mouse uses for its sideways wheel
#define HID_USAGE_AC_PAN 0x0238

// A long item, which carries its own size and holds nothing this parser wants
#define HID_LONG_ITEM_PREFIX 0xfe

// A usage item four bytes wide carries the usage page it belongs to in its top sixteen bits,
// for that usage alone, rather than taking the one the global items are on. See HID 1.11
// section 6.2.2.8. A Switch Pro controller names every one of its controls that way.
#define HID_USAGE_PAGE_OF(usage) ((uint16_t)((usage) >> 16))
#define HID_USAGE_ID_OF(usage)   ((uint16_t)((usage) & 0xffff))

// Most usages one input item can name before the rest is ignored
#define MAX_LOCAL_USAGES 32

// How deep a descriptor may nest Push before the rest of them are ignored
#define GLOBAL_STACK_DEPTH 4

// An input report is addressed by a sixteen bit offset, so it stops here
#define MAX_REPORT_BITS 0xffff

/// @brief The global items in force at one point in a descriptor, the part of them used here
typedef struct {
    uint16_t usage_page;
    int32_t  logical_min;
    int32_t  logical_max;
    uint8_t  report_size;
    uint16_t report_count;
    uint8_t  report_id;
} globals_t;

/// @brief Read the data of a report descriptor item, unsigned
static uint32_t item_data(const uint8_t* item, uint8_t size) {
    uint32_t value = 0;
    for (uint8_t i = 0; i < size; i++) {
        value |= (uint32_t)item[i] << (8 * i);
    }
    return value;
}

/// @brief Read the data of a report descriptor item, sign extended
static int32_t item_data_signed(const uint8_t* item, uint8_t size) {
    uint32_t value = item_data(item, size);
    if (size > 0 && size < 4 && (value & (1u << (8 * size - 1)))) {
        value |= 0xffffffffu << (8 * size);
    }
    return (int32_t)value;
}

/// @brief Whether a report is long enough to hold every bit of a field
///
/// A field that runs past the end of a report reads as zero, which for an absolute axis is a
/// direction rather than nothing, so anything answering a question about one checks first.
static bool field_fits(const hid_field_t* field, int length) {
    if (field->bit_size == 0 || length <= 0) {
        return false;
    }
    return ((uint32_t)field->bit_offset + field->bit_size + 7) / 8 <= (uint32_t)length;
}

int32_t hid_layout_read(const uint8_t* data, int length, const hid_field_t* field) {
    uint32_t value = 0;

    if (data == NULL || length <= 0 || field->bit_size == 0) {
        return 0;
    }

    for (uint8_t i = 0; i < field->bit_size && i < 32; i++) {
        uint32_t bit = (uint32_t)field->bit_offset + i;
        if (bit / 8 >= (uint32_t)length) {
            break;
        }
        if (data[bit / 8] & (1 << (bit % 8))) {
            value |= 1u << i;
        }
    }

    // Fields with a negative logical minimum hold signed values
    if (field->logical_min < 0 && field->bit_size < 32 && (value & (1u << (field->bit_size - 1)))) {
        value |= 0xffffffffu << field->bit_size;
    }

    return (int32_t)value;
}

bool hid_layout_read_button(const uint8_t* data, int length, const hid_layout_t* layout, uint16_t button) {
    if (!layout->buttons.present || button >= layout->button_count) {
        return false;
    }

    hid_field_t field = layout->buttons;
    if ((uint32_t)field.bit_offset + button > MAX_REPORT_BITS) {
        return false;
    }
    field.bit_offset = (uint16_t)(field.bit_offset + button);

    return field_fits(&field, length) && hid_layout_read(data, length, &field) != 0;
}

bool hid_layout_strip_report_id(const hid_layout_t* layout, const uint8_t** data, int* length) {
    if (layout->report_id == 0) {
        return true;
    }
    if (*length < 1 || (*data)[0] != layout->report_id) {
        return false;
    }
    (*data)++;
    (*length)--;
    return true;
}

void hid_layout_axis_directions(const uint8_t* data, int length, const hid_field_t* field, bool* low, bool* high) {
    *low  = false;
    *high = false;

    if (!field->present || field->relative || field->logical_max <= field->logical_min) {
        return;
    }
    if (!field_fits(field, length)) {
        return;
    }

    // A descriptor is free to name a range that overflows a subtraction, so the arithmetic on
    // it is done wide enough to hold whatever it said
    int64_t value  = hid_layout_read(data, length, field);
    int64_t center = ((int64_t)field->logical_min + field->logical_max) / 2;
    int64_t margin = ((int64_t)field->logical_max - field->logical_min) / 4;

    if (value < center - margin) {
        *low = true;
    }
    if (value > center + margin) {
        *high = true;
    }
}

void hid_layout_hat_directions(const uint8_t* data, int length, const hid_field_t* hat, bool* up, bool* down,
                               bool* left, bool* right) {
    *up    = false;
    *down  = false;
    *left  = false;
    *right = false;

    if (!hat->present || hat->logical_max <= hat->logical_min || !field_fits(hat, length)) {
        return;
    }

    int32_t value = hid_layout_read(data, length, hat);

    // Resting hats report a value outside the range they described, whichever end it sits at
    if (value < hat->logical_min || value > hat->logical_max) {
        return;
    }

    int64_t positions = (int64_t)hat->logical_max - hat->logical_min + 1;
    int64_t position  = (int64_t)value - hat->logical_min;

    if (positions == 4) {
        // North, east, south, west
        *up    = position == 0;
        *right = position == 1;
        *down  = position == 2;
        *left  = position == 3;
    } else if (positions == 8) {
        // The same, with a diagonal between each pair, so each direction covers three of them
        *up    = position == 7 || position <= 1;
        *right = position >= 1 && position <= 3;
        *down  = position >= 3 && position <= 5;
        *left  = position >= 5;
    }
}

/// @brief The input report with the given report ID, adding it to the table when it is new
///
/// Only input items call this, so the output and feature reports a gamepad hands out by the
/// dozen never take up a slot.
///
/// @return NULL when the table is full
static hid_layout_t* report_slot(hid_layouts_t* layouts, uint16_t* bit_offsets, uint8_t report_id,
                                 uint16_t** bit_offset) {
    for (uint8_t r = 0; r < layouts->count; r++) {
        if (layouts->report[r].report_id == report_id) {
            *bit_offset = &bit_offsets[r];
            return &layouts->report[r];
        }
    }

    if (layouts->count >= HID_LAYOUT_MAX_REPORTS) {
        return NULL;
    }

    hid_layout_t* layout    = &layouts->report[layouts->count];
    layout->report_id       = report_id;
    bit_offsets[layouts->count] = 0;
    *bit_offset                 = &bit_offsets[layouts->count];
    layouts->count++;
    return layout;
}

/// @brief The page a usage sits on, its own when it named one and the global one otherwise
static uint16_t usage_page_of(uint32_t usage, uint16_t global_page) {
    uint16_t page = HID_USAGE_PAGE_OF(usage);
    return page != 0 ? page : global_page;
}

/// @brief Remember what the descriptor called each of the buttons this item covers
static void name_buttons(hid_layout_t* layout, uint16_t first, uint16_t count, bool usage_range, uint32_t usage_min,
                         const uint32_t* usages, uint8_t usage_count) {
    for (uint16_t i = 0; i < count; i++) {
        uint16_t slot = (uint16_t)(first + i);
        if (slot >= HID_LAYOUT_MAX_BUTTONS) {
            return;
        }
        uint32_t usage = 0;
        if (usage_range) {
            usage = (uint32_t)HID_USAGE_ID_OF(usage_min) + i;
        } else if (i < usage_count) {
            usage = HID_USAGE_ID_OF(usages[i]);
        }
        layout->button_usage[slot] = (usage <= UINT8_MAX) ? (uint8_t)usage : 0;
    }
}

/// @brief Take one input item to be the buttons of a report, or more of the ones already found
///
/// Devices that report more buttons than a single item covers split them over two, which is
/// only worth following when the second carries straight on from the first. Buttons split by
/// padding or by another control cannot be numbered in one run, so the rest are left.
static void take_buttons(hid_layout_t* layout, uint16_t bit_offset, const globals_t* g, bool usage_range,
                         uint32_t usage_min, uint32_t usage_max, const uint32_t* usages, uint8_t usage_count) {
    // Buttons are one bit each; a device that packs them any other way is not followed
    if (g->report_size != 1) {
        return;
    }

    uint16_t count = g->report_count;
    if (usage_range && HID_USAGE_ID_OF(usage_max) >= HID_USAGE_ID_OF(usage_min)) {
        uint32_t named = (uint32_t)HID_USAGE_ID_OF(usage_max) - HID_USAGE_ID_OF(usage_min) + 1;
        if (named < count) {
            count = (uint16_t)named;
        }
    }

    if (!layout->buttons.present) {
        layout->buttons.present     = true;
        layout->buttons.bit_offset  = bit_offset;
        layout->buttons.bit_size    = 1;
        layout->buttons.logical_min = 0;
        layout->buttons.logical_max = 1;
        layout->button_count        = count;
        name_buttons(layout, 0, count, usage_range, usage_min, usages, usage_count);
        return;
    }

    if (bit_offset == layout->buttons.bit_offset + layout->button_count &&
        (uint32_t)layout->button_count + count <= MAX_REPORT_BITS) {
        name_buttons(layout, layout->button_count, count, usage_range, usage_min, usages, usage_count);
        layout->button_count = (uint16_t)(layout->button_count + count);
    }
}

/// @brief The field one usage belongs in, or NULL for a usage this parser has no place for
static hid_field_t* field_for_usage(hid_layout_t* layout, uint16_t usage_page, uint16_t usage) {
    if (usage_page == HID_USAGE_PAGE_GENERIC_DESKTOP) {
        switch (usage) {
            case HID_USAGE_X:
                return &layout->x;
            case HID_USAGE_Y:
                return &layout->y;
            case HID_USAGE_Z:
                return &layout->z;
            case HID_USAGE_RX:
                return &layout->rx;
            case HID_USAGE_RY:
                return &layout->ry;
            case HID_USAGE_RZ:
                return &layout->rz;
            case HID_USAGE_WHEEL:
                return &layout->wheel;
            case HID_USAGE_HAT_SWITCH:
                return &layout->hat;
            default:
                return NULL;
        }
    }

    if (usage_page == HID_USAGE_PAGE_SIMULATION) {
        switch (usage) {
            case HID_USAGE_ACCELERATOR:
                return &layout->accelerator;
            case HID_USAGE_BRAKE:
                return &layout->brake;
            default:
                return NULL;
        }
    }

    if (usage_page == HID_USAGE_PAGE_CONSUMER && usage == HID_USAGE_AC_PAN) {
        return &layout->pan;
    }

    return NULL;
}

/// @brief Take the axes an input item names, by usage
static void take_axes(hid_layout_t* layout, uint16_t bit_offset, const globals_t* g, const uint32_t* usages,
                      uint8_t usage_count, bool relative) {
    for (uint16_t f = 0; f < g->report_count && f < usage_count; f++) {
        hid_field_t* field =
            field_for_usage(layout, usage_page_of(usages[f], g->usage_page), HID_USAGE_ID_OF(usages[f]));
        if (field != NULL && !field->present) {
            field->present     = true;
            field->relative    = relative;
            field->bit_offset  = (uint16_t)(bit_offset + f * g->report_size);
            field->bit_size    = g->report_size;
            field->logical_min = g->logical_min;
            field->logical_max = g->logical_max;
        }
    }
}

/// @brief How much of what this parser looks for one report holds
static uint32_t layout_score(const hid_layout_t* layout) {
    const hid_field_t* fields[] = {&layout->x,           &layout->y,     &layout->z,   &layout->rx,
                                   &layout->ry,          &layout->rz,    &layout->hat, &layout->wheel,
                                   &layout->accelerator, &layout->brake, &layout->pan};
    uint32_t           score    = 0;

    for (size_t f = 0; f < sizeof(fields) / sizeof(fields[0]); f++) {
        if (fields[f]->present) {
            score += 4;
        }
    }

    return score + layout->button_count;
}

const hid_layout_t* hid_layouts_best(const hid_layouts_t* layouts) {
    const hid_layout_t* best       = NULL;
    uint32_t            best_score = 0;

    for (uint8_t r = 0; r < layouts->count; r++) {
        const hid_layout_t* layout = &layouts->report[r];
        uint32_t            score  = layout_score(layout);
        if (layout->valid && score > best_score) {
            best       = layout;
            best_score = score;
        }
    }

    return best;
}

const hid_layout_t* hid_layouts_find(const hid_layouts_t* layouts, uint8_t report_id) {
    for (uint8_t r = 0; r < layouts->count; r++) {
        if (layouts->report[r].report_id == report_id) {
            return &layouts->report[r];
        }
    }
    return NULL;
}

bool hid_layout_parse_all(const uint8_t* report_descriptor, size_t length, hid_layouts_t* layouts) {
    memset(layouts, 0, sizeof(*layouts));

    if (report_descriptor == NULL) {
        return false;
    }

    // Bit offset within its own report each report is filled up to, kept apart from the layouts
    // so that a descriptor may go back to a report it already described
    uint16_t bit_offsets[HID_LAYOUT_MAX_REPORTS] = {0};

    globals_t g       = {0};
    globals_t stack[GLOBAL_STACK_DEPTH];
    uint8_t   depth   = 0;

    uint32_t usages[MAX_LOCAL_USAGES];
    uint8_t  usage_count = 0;
    uint32_t usage_min   = 0;
    uint32_t usage_max   = 0;
    bool     usage_range = false;

    size_t i = 0;
    while (i < length) {
        uint8_t prefix = report_descriptor[i];
        if (prefix == HID_LONG_ITEM_PREFIX) {
            // Long items are sized differently, and nothing past one can be trusted to line up
            break;
        }
        uint8_t size = HID_ITEM_SIZE(prefix);
        if (size == 3) {
            size = 4;  // A size field of three means four bytes
        }
        if (i + 1 + size > length) {
            break;
        }
        const uint8_t* data = &report_descriptor[i + 1];
        i += 1 + size;

        switch (HID_ITEM_TAG(prefix)) {
            case HID_ITEM_USAGE_PAGE:
                g.usage_page = (uint16_t)item_data(data, size);
                break;
            case HID_ITEM_LOGICAL_MIN:
                g.logical_min = item_data_signed(data, size);
                break;
            case HID_ITEM_LOGICAL_MAX:
                // Only signed when the minimum is, otherwise 0xff means 255 rather than -1
                g.logical_max = g.logical_min < 0 ? item_data_signed(data, size) : (int32_t)item_data(data, size);
                break;
            case HID_ITEM_REPORT_SIZE:
                g.report_size = (uint8_t)item_data(data, size);
                break;
            case HID_ITEM_REPORT_COUNT:
                g.report_count = (uint16_t)item_data(data, size);
                break;
            case HID_ITEM_REPORT_ID:
                g.report_id = (uint8_t)item_data(data, size);
                break;
            case HID_ITEM_PUSH:
                // A descriptor that nests deeper than this gets its globals back off the top of
                // the stack, which beats losing track of them altogether
                if (depth < GLOBAL_STACK_DEPTH) {
                    stack[depth++] = g;
                }
                break;
            case HID_ITEM_POP:
                if (depth > 0) {
                    g = stack[--depth];
                }
                break;
            case HID_ITEM_USAGE:
                if (usage_count < MAX_LOCAL_USAGES) {
                    usages[usage_count++] = item_data(data, size);
                }
                break;
            case HID_ITEM_USAGE_MIN:
                usage_min   = item_data(data, size);
                usage_range = true;
                break;
            case HID_ITEM_USAGE_MAX:
                usage_max   = item_data(data, size);
                usage_range = true;
                break;
            case HID_ITEM_INPUT: {
                uint32_t  flags       = item_data(data, size);
                uint16_t* bit_offset  = NULL;
                hid_layout_t* layout  = report_slot(layouts, bit_offsets, g.report_id, &bit_offset);

                if (layout != NULL) {
                    if (!(flags & HID_INPUT_CONSTANT)) {
                        // Which page this item is on decides whether it holds buttons or axes,
                        // and a usage that named its own page is the one that says so
                        uint16_t page = g.usage_page;
                        if (usage_range) {
                            page = usage_page_of(usage_min, g.usage_page);
                        } else if (usage_count > 0) {
                            page = usage_page_of(usages[0], g.usage_page);
                        }

                        if (page == HID_USAGE_PAGE_BUTTON) {
                            take_buttons(layout, *bit_offset, &g, usage_range, usage_min, usage_max, usages,
                                         usage_count);
                        } else {
                            take_axes(layout, *bit_offset, &g, usages, usage_count,
                                      (flags & HID_INPUT_RELATIVE) != 0);
                        }
                    }

                    uint32_t width = (uint32_t)g.report_size * g.report_count;
                    if ((uint32_t)*bit_offset + width > MAX_REPORT_BITS) {
                        // Past what a bit offset can name, so nothing after this lines up
                        i = length;
                    } else {
                        *bit_offset = (uint16_t)(*bit_offset + width);
                    }
                }

                usage_count = 0;
                usage_range = false;
                break;
            }
            case HID_ITEM_OUTPUT:
            case HID_ITEM_FEATURE:
            case HID_ITEM_COLLECTION:
            case HID_ITEM_END_COLLECTION:
                // These take up no space in an input report, but like every main item they
                // do use up the usages named before them
                usage_count = 0;
                usage_range = false;
                break;
            default:
                // Anything else is a global or local item that says nothing this parser wants.
                // Local items live on until a main item consumes them, so they stay put: a unit
                // or a physical range between a usage and its input item is perfectly normal.
                break;
        }
    }

    bool any = false;
    for (uint8_t r = 0; r < layouts->count; r++) {
        hid_layout_t* layout = &layouts->report[r];
        layout->valid = layout->x.present || layout->y.present || layout->hat.present || layout->buttons.present;
        any |= layout->valid;
    }

    return any;
}

bool hid_layout_parse(const uint8_t* report_descriptor, size_t length, hid_layout_t* layout) {
    hid_layouts_t layouts;

    memset(layout, 0, sizeof(*layout));

    if (!hid_layout_parse_all(report_descriptor, length, &layouts)) {
        return false;
    }

    const hid_layout_t* best = hid_layouts_best(&layouts);
    if (best == NULL) {
        return false;
    }

    *layout = *best;
    return true;
}
