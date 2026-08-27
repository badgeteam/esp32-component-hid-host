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
  hid_keyboard.c               Report → the set of keys that are down
          │
          ▼
  Your integration             Directions, buttons and keys → whatever they mean here
```

Only the bottom layer knows what a button *means*, and only the top one holds a USB handle. This
component is the middle two, and deliberately neither of the ends: it never opens a device, never
sends anything, and never decides that button 1 is confirm. A launcher wants a confirm key and an
emulator wants a fire button; both are served by the same decoder precisely because it declines to
choose.

The two middle layers are separable too. `hid_layout.h` on its own is enough for a mouse, or for
anything wanting raw field access. `hid_gamepad.h` builds on it for the pad case, where a caller
would rather not know whether this device steers with a stick, a hat switch, or four of its buttons.
`hid_keyboard.h` builds on it the same way, and is a sibling of the gamepad layer rather than a
layer under it: neither knows about the other.

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

### Arrays, and why keyboards needed new code

Every field described so far is a **Variable** item: one control, one fixed slot in the report, read
by looking at where it sits. Bit 1 of an Input item's flags says so, and until keyboards arrived
nothing here had to look at it — a mouse and a gamepad describe Variable items and nothing else.

A keyboard's key list is an **Array**. Its slots hold the *usages of the keys that are down*, in no
particular order, rather than a bit per key: six bytes cover any six keys of the hundred a keyboard
has. Slot order means nothing, and a keyboard is free to move a held key from one slot to the next
between reports — comparing slot to slot rather than set to set invents a release and a press that
never happened.

So `take_keys()` is a third branch beside `take_buttons()` and `take_axes()`, chosen by usage page
0x07, and it sorts three shapes out by the Variable flag and the first usage:

- Not Variable, so an Array: the keys, `key_count` slots of `report_size` bits each.
- Variable, one bit, first usage at or above 0xe0: the eight modifiers, which are a bitmap of their
  own whatever shape the rest of the keyboard uses.
- Variable, one bit, anything lower: a bitmap of keys, one bit per usage, which is the only way a
  keyboard can say that a seventh key is held.

A keyboard describing both shapes describes them under separate report IDs, which the report slots
already handle.

### Keyboards are valid but score nothing

`layout_score()` is deliberately not taught about keys. A keyboard report is `valid`, so
`hid_layout_parse_all()` returns it, but it scores zero, so `hid_layouts_best()` never picks it and
`hid_layout_parse()` returns false for a keyboard-only descriptor.

That asymmetry is the point rather than an oversight. `hid_layout_parse()` is the convenience entry
point mice and gamepads use, and a keyboard turning up in it would change what every existing caller
sees from a composite device. A keyboard is asked for through `hid_keyboard_open()`, which knows it
wants one. There is a test pinning this, so it stays deliberate.

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

## The keyboard layer

`hid_keyboard_open()` takes every input report carrying keys, rather than the one
`hid_layouts_best()` likes, and refuses a device that names no keys at all. Modifiers alone are not
enough to be a keyboard: taking them would claim anything that happens to carry a modifier byte.

`hid_keyboard_decode()` replaces the state rather than adding to it, because a report is a complete
statement of what is down in the shape it uses. Two reports it refuses outright:

- One saying **rollover**, where a keyboard with more keys down than it can name says so instead of
  naming any. The keys that were down are still down; handing back an empty state releases all of
  them. Every implementation surveyed before this one had that bug.
- One **too short to hold the whole key array**, since a half read array is indistinguishable from
  the keys in the missed slots having come up.

The state is a set of usages rather than a copy of either report shape, which is what lets one
caller serve both, and it is caller-owned, so two keyboards plugged in at once keep their own.

Changes are walked rather than collected into a fixed array: `hid_keyboard_next_change()` takes two
states and yields one usage at a time. Nothing is capped, so a keyboard holding a hundred keys
enumerates all hundred — and releasing everything on unplug is the same call against a zeroed state
rather than a second entry point.

The lock lamps are not driven. That needs Output items, which the parser skips, and is left until
there is a descriptor to test it against.

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
It opens the same bytes as a keyboard too, decodes two states out of two windows of the input and
walks the changes between them both ways round, since the keyboard layer indexes a set with a usage
that came out of a report body.
`CONTRIBUTING.md` covers running all of it.
