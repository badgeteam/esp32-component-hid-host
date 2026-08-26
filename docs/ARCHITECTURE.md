# Architecture

Three layers, each of which can be understood without the one above it.

```
  USB HID host driver          Holds the handle, hands over descriptors and reports
          │
          ▼
  hid_layout.c                 Report descriptor → where every control sits
          │
          ▼
  hid_gamepad.c                Report → directions and buttons, quirks applied
          │
          ▼
  Your integration             Directions and buttons → whatever they mean here
```

Only the bottom layer knows what a button *means*, and only the top one holds a USB handle. This
component is the middle two, and deliberately neither of the ends: it never opens a device, never
sends anything, and never decides that button 1 is confirm. A launcher wants a confirm key and an
emulator wants a fire button; both are served by the same decoder precisely because it declines to
choose.

The two middle layers are separable too. `hid_layout.h` on its own is enough for a mouse, or for
anything wanting raw field access. `hid_gamepad.h` builds on it for the pad case, where a caller
would rather not know whether this device steers with a stick, a hat switch, or four of its buttons.

A working example of the layer below is
[tanmatsu-launcher's `usb-host` component](https://github.com/Nicolai-Electronics/tanmatsu-launcher/tree/main/components/usb-host),
which drives the USB host driver above and injects BSP input events below.

## The parser

`hid_layout_parse_all()` is one pass over the descriptor. A report descriptor is a flat byte stream
of items, each a one byte prefix followed by nought to four bytes of data, and the parser walks it
keeping the state those items build up.

### Global, local and main items

HID splits items three ways, and the difference drives the whole loop:

- **Global** items — usage page, logical minimum and maximum, report size, report count, report ID —
  set state that stays in force until something changes it. `globals_t` holds the ones used here.
- **Local** items — usage, usage minimum, usage maximum — apply to the *next* main item only, and
  are cleared once it consumes them. The parser buffers up to `MAX_LOCAL_USAGES` of them.
- **Main** items — Input, Output, Feature, Collection, End Collection — do something with whatever
  state has accumulated, and clear the local items afterwards.

An Input item is where a field is actually created. Everything else is bookkeeping toward one.

Anything the parser has no use for falls to `default:` and is skipped without clearing the local
items, because a unit or a physical range sitting between a usage and its Input item is entirely
normal and must not lose the usage.

### Push and Pop

`Push` copies the whole global state onto a stack, `Pop` takes it back. Real descriptors use it to
describe several similar controls without restating the ranges each time.

The stack is `GLOBAL_STACK_DEPTH` deep, which is four. A descriptor nesting deeper than that keeps
getting its globals back off the top of the stack rather than losing track of them altogether —
degrading beats failing, since the fields after the over-deep section still line up.

### Bit offsets

Every field records the bit offset at which it starts within its report, and a running offset moves
forward by `report_size × report_count` for each Input item, padding included. Padding is an Input
item with the Constant flag set: it takes up space and names nothing, which is exactly how it is
treated.

The offsets **exclude the report ID**. A device that puts one in front of its reports still has its
first field at offset 0, and `hid_layout_strip_report_id()` removes the byte before anything reads
from the data. Keeping the two separate is what lets the same field code serve devices with and
without report IDs.

`MAX_REPORT_BITS` caps an offset at what a `uint16_t` can name. A descriptor claiming more than that
has nothing lining up after the overflow point, so parsing stops there rather than wrapping.

### Reports and their slots

A device describes one report per report ID, and `report_slot()` hands out a slot per ID as they are
first seen. Two things matter about it:

Only **Input** items call it. A DualShock 4 clone describes some forty feature report IDs, and a
table that gave each one a slot would fill up before reaching the input report anybody wants.
Output and feature reports are skipped, so `HID_LAYOUT_MAX_REPORTS` of eight is generous rather than
tight.

The running bit offsets live in a separate array indexed by slot, not inside `hid_layout_t`. A
descriptor is free to describe report 1, then report 2, then go back to report 1, and each has to
resume where it left off.

### Which field a usage lands in

`field_for_usage()` maps a (usage page, usage) pair to a member of `hid_layout_t`. Usages are looked
up rather than counted, which is the whole point: a device that reports X, Y and a hat switch is
understood the same way whichever order it lists them in, and a device this parser has never seen
still works.

A usage may name its own page in its top sixteen bits, and when it does that page wins over the
global one. A Switch Pro Controller gives every one of its usages in full, and reading them against
the global page would put all of them in the wrong place.

Buttons take a different path. Their page decides it: an Input item on the Button page goes to
`take_buttons()`, everything else to `take_axes()`. Buttons are one bit each, and an item packing
them any other way is not followed.

`name_buttons()` records what the descriptor called each one. A device may list its buttons in any
order, and a Stadia controller lists them backwards, so the third bit of a report is not
necessarily Button 3. Position and name are both kept because callers need both, for different
reasons.

Buttons split over two Input items are joined only when the second carries straight on from the
first. Buttons separated by padding or by another control cannot be numbered as one run, so the
rest are left rather than mislabelled.

### Picking a report

`layout_score()` counts four per axis, hat or wheel present, plus one per button.
`hid_layouts_best()` takes the highest scoring valid report, ties going to whichever the descriptor
named first.

A device with several input reports names the interesting one nowhere — there is no flag for "this
is the one a host should read". Scoring is a heuristic, and it is the reason `hid_layout_parse()`
can stay a single-report convenience for the common case while `hid_layout_parse_all()` remains
available for anything that needs the others.

A report is `valid` when it has an X, a Y, a hat switch or buttons. That is the line between "this
descriptor contained something usable" and a device this component has no business claiming.

## Hostile input

A report descriptor arrives from a USB device, so it is untrusted input, and two habits in the code
follow from that.

**Arithmetic is done wide enough for what a descriptor may claim.** A logical range of
`-2147483625` to `66` is legal to state, and `logical_max - logical_min` in `int32_t` is undefined
behaviour on it. The centre and margin calculations in `hid_layout_axis_directions()`, and the
position arithmetic in `hid_layout_hat_directions()`, are `int64_t` for this reason. The fuzzer
found the overflow within a minute of first being pointed at the parser.

**A field that runs past the end of a report is not read.** Bits past the end read as zero, which
sounds harmless until an absolute axis ranging 0 to 255 reads zero and the caller is told the stick
is pushed hard left. `field_fits()` answers whether the report is long enough, and
`hid_layout_read_button()`, `hid_layout_axis_directions()` and `hid_layout_hat_directions()` all
check it before answering. This is why those two write *every* output, false included: a missing
control, a centred one and a truncated one all have to clear the outputs rather than leave a stale
direction standing.

Beyond that the parser truncates rather than rejects. A descriptor that ends mid-item, or contains a
long item — after which nothing can be trusted to line up — stops the walk and keeps whatever was
understood up to that point. Devices ship with descriptors that are wrong in small ways, and
refusing them outright helps nobody.

## The gamepad layer

`hid_gamepad_open()` parses the descriptor, then applies any quirk for the vendor and product ID,
then refuses anything with neither absolute axes nor a hat switch. That last check is what keeps a
mouse from being taken for a gamepad: both report X and Y, and what separates them is that a mouse
reports a change and a pad reports a position, which the descriptor states in the Relative flag of
its Input item.

`hid_gamepad_decode()` produces both a set of directions and two button bitmaps.

Directions come from the stick, the hat switch, or four buttons acting as a d-pad, whichever this
pad has. `state.up` and its siblings answer "is it pushed", which is what a menu wants.
`state.dpad_up` and its siblings answer "is the d-pad pushed", which is what a picture of the pad
wants — it should not light the d-pad up because the stick moved.

The buttons come twice for the same reason: `usage_buttons` is indexed by what the descriptor calls
each button, `buttons` by where the report holds it. Anything acting as a d-pad appears in neither,
since it turned into a direction.

## Quirks

`hid_gamepad_quirks` is a table keyed on vendor and product ID, holding what a descriptor cannot
express. Today it has one row, for the DualShock 3, which hands out a perfectly good descriptor and
then says nothing at all until the host sends it a feature report, and whose d-pad is four ordinary
buttons rather than the hat switch its descriptor implies.

The table is meant to stay small. Everything a descriptor *can* express is read from the
descriptor, so a device that states what it means works without anybody adding a row for it — which
is the difference between supporting the devices somebody owned and supporting the ones nobody has
plugged in yet.

The one thing a quirk cannot do is send the feature report, since this component holds no USB
handle. `hid_gamepad_open()` leaves it on `gamepad.quirk` for the caller, which is the side that
does.

## Tests

`test_native/` builds the sources with a stub `esp_log.h` and runs them on a host. There is no
ESP-IDF in the loop, which is what makes sanitizers and a fuzzer practical.

The descriptors in `test_descriptors.h` are captured from real hardware, and the tests assert the
offsets that hardware implies. They are the specification: the parser is correct when it agrees
with what the devices in there actually send. `test_reports.h` carries captured input reports
alongside them, so decoding is checked against reality too and not just parsing.

A handful of synthetic descriptors are hand-built inside the tests, for corners no device to hand
covers — a full report table, a Push/Pop pair, buttons split across items.

`fuzz_hid_layout.c` parses its input as a descriptor and then reuses the same bytes as a report
against every reader, seeded from the captured descriptors so it starts past the first few bytes.
`CONTRIBUTING.md` covers running all of it.
