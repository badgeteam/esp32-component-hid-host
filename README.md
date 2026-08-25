# HID host

Parser for USB HID report descriptors, for ESP-IDF projects that talk to keyboards, mice and
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
  badgeteam/hid-host: "^0.1.0"
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

### What the descriptor cannot tell you

Some devices lie, or stay quiet. A DualShock 3 hands out a perfectly good report descriptor and
then sends nothing at all until the host asks it to start reporting, and its d-pad is four ordinary
buttons rather than the hat switch the descriptor implies. Keep a quirk table keyed on vendor and
product ID for that; it stays small, because everything the descriptor *can* express is already
handled here.

## Tests

`hid_layout.c` is plain C with no ESP-IDF dependencies beyond logging, so it builds and runs on a
host against report descriptors captured from real hardware:

```
make -C test_native test
```

The descriptors in `test_native/test_descriptors.h` come from actual devices: two mice, a Stadia
controller, a DualShock 4 clone, a DualShock 3 and a Speedlink Competition Pro. The mice, the Stadia
controller and the DualShock 4 clone were captured for
[konsool-HID](https://github.com/annejan/konsool-HID/pull/2), the rest on a Tanmatsu and on a Linux
host.

## License

MIT
