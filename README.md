# HID host

Report descriptor parser and gamepad decoder for ESP-IDF projects that talk to keyboards, mice and
gamepads over USB host.

Devices of the same kind do not agree on a report layout. One gamepad puts its stick in a hat
switch, the next reports X and Y and never touches the hat. A mouse may or may not put a report ID
in front of its report, and a five byte report means different things depending on whether it does.
Guessing by report length only ever works for the devices it was written against.

This component reads the report descriptor instead and looks the controls up by usage, so a device
it has never seen still works.

## Use

Add it to `idf_component.yml`:

```yaml
dependencies:
  badgeteam/hid-host: "^0.2.0"
```

Ask the HID host driver for the report descriptor, parse it once when the device connects, then
read fields out of each report as it arrives:

```c
#include "hid_layout.h"

static hid_layout_t layout;

// On connect
size_t         length     = 0;
const uint8_t* descriptor = hid_host_get_report_descriptor(handle, &length);
if (!hid_layout_parse(descriptor, length, &layout)) {
    // Nothing usable in there
}

// On every report
void on_report(const uint8_t* data, int length) {
    if (!hid_layout_strip_report_id(&layout, &data, &length)) {
        return;  // A report for some other part of the device
    }

    bool left = false, right = false;
    hid_layout_axis_directions(data, length, &layout.x, &left, &right);

    bool fire = hid_layout_read_button(data, length, &layout, 0);
    int32_t wheel = hid_layout_read(data, length, &layout.wheel);
}
```

`hid_layout_t` says where the axes, the hat switch and the buttons sit, and how many buttons there
are. Every field carries its own logical minimum and maximum, so a stick that idles at 128 and one
that idles at 0 both work without a special case.

A gamepad reports its left stick through `x` and `y`. The right stick is usually `z` and `rz`, and
sometimes `rx` and `ry`; a pad that names its analog triggers as simulation controls puts them in
`accelerator` and `brake`, and one that does not puts them in whichever axis pair is left over. A
mouse with a sideways wheel reports it as a consumer control, which lands in `pan`.

A hat switch reports an angle rather than a direction, and rests at a value outside its own range:

```c
bool up = false, down = false, left = false, right = false;
hid_layout_hat_directions(data, length, &layout.hat, &up, &down, &left, &right);
```

`hid_layout_hat_directions()` and `hid_layout_axis_directions()` always write every output, so a
centered control, a control the device does not have, and a report too short to hold one all clear
them rather than reading as a direction.

### Devices with more than one report

A device that describes several input reports names the interesting one nowhere. `hid_layout_parse()`
takes the one holding the most of what this parser looks for, which is what a mouse or a gamepad
wants. Reach past it when you need the others:

```c
hid_layouts_t layouts;
hid_layout_parse_all(descriptor, length, &layouts);

const hid_layout_t* keys = hid_layouts_find(&layouts, 1);
const hid_layout_t* pad  = hid_layouts_best(&layouts);
```

Only reports carrying input items take up a slot, so the output and feature reports a gamepad hands
out by the dozen cost nothing. `HID_LAYOUT_MAX_REPORTS` caps the table at eight; define it higher if
a device outgrows that.

### Telling a mouse from a gamepad

Both report X and Y. What separates them is that a mouse reports a change and a gamepad reports a
position, which the descriptor states in the `Relative` flag of its Input item. `hid_field_t` keeps
it:

```c
bool absolute_axes = (layout.x.present && !layout.x.relative) ||
                     (layout.y.present && !layout.y.relative);
if (!absolute_axes && !layout.hat.present) {
    // Not a gamepad, whatever else it may be
}
```

## Gamepads

`hid_gamepad.h` builds on the parser and hands back directions and buttons, so a caller does not
have to know whether this pad steers with a stick, a hat switch, or four of its buttons:

```c
#include "hid_gamepad.h"

static hid_gamepad_t gamepad;

// On connect, with the vendor and product ID from hid_host_get_device_info()
if (!hid_gamepad_open(&gamepad, descriptor, length, vid, pid)) {
    // Nothing to steer with, so not a gamepad
}

// On every report
hid_gamepad_state_t state;
if (hid_gamepad_decode(&gamepad, data, length, &state)) {
    if (state.up)    { /* ... */ }
    if (state.buttons & (1u << 14)) { /* button fifteen */ }
}
```

`state.buttons` numbers the buttons the way the gamepad reports them, and leaves out any that act
as a d-pad, since those turned into directions.

What a button *means* is deliberately not decided here. A launcher wants a confirm key, an
emulator wants a fire button, and neither belongs in a report decoder. Map `state.buttons` at your
own edge.

### What the descriptor cannot tell you

Some devices lie, or stay quiet. A DualShock 3 hands out a perfectly good report descriptor and
then sends nothing at all until the host asks it to start reporting, and its d-pad is four ordinary
buttons rather than the hat switch the descriptor implies.

`hid_gamepad_quirks` in `hid_gamepad.c` holds that per device, keyed on vendor and product ID, and
`hid_gamepad_open()` applies it. The table stays small, because everything the descriptor *can*
express is already handled. Supporting another awkward gamepad is a row in it.

The one part a quirk cannot do for you is sending the feature report that wakes a device up, since
this component holds no USB handle. `hid_gamepad_open()` leaves it on `gamepad.quirk` for you:

```c
if (gamepad.quirk != NULL && gamepad.quirk->enable_report != NULL) {
    hid_class_request_set_report(handle, HID_REPORT_TYPE_FEATURE, gamepad.quirk->enable_report_id,
                                 (uint8_t*)gamepad.quirk->enable_report,
                                 gamepad.quirk->enable_report_length);
}
```

## Tests

`hid_layout.c` is plain C with no ESP-IDF dependencies beyond logging, so it builds and runs on a
host against report descriptors captured from real hardware:

```
make -C test_native test
```

A report descriptor comes off a device and is as untrusted as input gets, so the same tests also run
under the address and undefined behaviour sanitizers, and the parser is fed random descriptors:

```
make -C test_native test-sanitized
make -C test_native fuzz
```

Fuzzing needs clang. It starts from the captured descriptors, which `make -C test_native corpus`
writes out, and keeps whatever it finds in `test_native/`. CI runs both on every push.

The descriptors in `test_native/test_descriptors.h` come from actual devices: two mice, a Stadia
controller, a DualShock 4 clone, a DualShock 3 and a Speedlink Competition Pro. The mice, the Stadia
controller and the DualShock 4 clone were captured for
[konsool-HID](https://github.com/annejan/konsool-HID/pull/2), the rest on a Tanmatsu and on a Linux
host.

## License

MIT
