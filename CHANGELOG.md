# Changelog

Notable changes per release, newest first. Versions follow
[semantic versioning](https://semver.org), and each release is tagged `vX.Y.Z` and published to the
ESP Component Registry as `badgeteam/hid-host`.

## Unreleased

### Added

- `hid_keyboard.h`: the keys a report says are down, as a set of usages rather than as whichever
  shape the keyboard used to say it. The boot report names up to six keys by value and cannot say a
  seventh is held; a keyboard with no such limit sends a bitmap under its own report ID instead.
  Both decode to the same state, so a caller writes one code path. State belongs to the caller, so
  two keyboards plugged in at once no longer overwrite each other, and releasing what a keyboard
  held when it was unplugged is the change walk against a zeroed state rather than a second call.
- `hid_layout.c` reads Array items, which is what a keyboard names its keys with: slots holding the
  usages that are down rather than a bit per control. Everything the parser handled until now was a
  Variable item, one control to a fixed slot, so the Variable flag had never been looked at. With
  it come the modifier bitmap and the key bitmap, and `hid_layout_read_key()`,
  `hid_layout_read_modifier()` and `hid_layout_read_key_bit()` to read them.
- Two keyboard descriptors, taking the corpus to twenty nine: a Dell QuietKey captured off a
  machine here, which is the boot shape every keyboard falls back to, and a hand built one
  describing both shapes at once under two report IDs.

### Fixed

- A report saying rollover is refused rather than decoded as an empty keyboard. A keyboard with
  more keys down than it can name says so in place of naming any of them, and the keys that were
  down are still down.
- A report too short to hold the whole key array is refused rather than half read, since the slots
  that were missed are indistinguishable from keys that came up.

### Note

A keyboard report is valid but scores nothing, so `hid_layout_parse()` still returns false for a
keyboard-only descriptor and `hid_layouts_best()` still picks the mouse out of a composite device.
Existing mouse and gamepad callers are unaffected. Ask for a keyboard through `hid_keyboard_open()`.

## 0.3.2

### Added

- `CONTRIBUTING.md`, `docs/ARCHITECTURE.md` and this changelog.
- Fourteen more captured report descriptors, taking the corpus to twenty seven devices: a stick
  with no axes at all, an Xbox 360 guitar, an Xbox 360 call button, the five descriptors the Linux
  kernel substitutes for what a device sent, the BigBen PS3OFMINIPAD's own descriptor, and five
  dumps of hardware people actually own — an iBuffalo SNES pad whose d-pad is a pair of axes, the
  descriptor a DragonRise JS19 really hands out, a Thrustmaster T.16000M with fourteen bit axes, a
  Logitech WingMan Force 3D with vendor data between its stick and its hat, and a Saitek X-56 Rhino
  throttle with thirty six buttons.

## 0.3.1

### Added

- Six more captured descriptors, from controllers published at
  [DJm00n/ControllersInfo](https://github.com/DJm00n/ControllersInfo). Between them they cover
  sixteen bit axes, analog triggers on the simulation page, a hat switch starting mid byte, four
  byte usages that name their own page, and a device with an X axis and no Y.

### Fixed

- The install snippet in the README pointed at a version older than the one with button usages.

## 0.3.0

### Added

- `hid_layout_t.button_usage[]` and `hid_gamepad_state_t.usage_buttons`, which name a button by
  what the descriptor called it rather than by where the report holds it. A device is free to list
  its buttons in any order, and a Stadia controller lists them backwards, so a caller reading by bit
  position called its Capture button "button one".
- `hid_gamepad_state_t.dpad_up` and its three siblings, carrying only the hat switch and a d-pad
  hiding in the buttons. `state.up` and friends still merge in a pushed stick, which is what a menu
  wants; something drawing the pad itself wants the difference.

## 0.2.0

### Added

- `hid_gamepad.h`: directions and buttons out of a report, without a caller having to know whether
  this pad steers with a stick, a hat switch, or four of its buttons. What a button *means* stays
  with the caller.
- The quirk table, holding what a descriptor cannot say. Its one row is the DualShock 3, which sends
  nothing until the host asks it to start reporting and whose d-pad is four ordinary buttons.
- `hid_layout_parse_all()`, `hid_layouts_best()` and `hid_layouts_find()`. The parser reads every
  input report a device describes rather than giving up at the second report ID, so a pad hiding
  behind a keyboard report is still found.
- Push and Pop, without which every global item after one is wrong.
- The axes past X and Y, where a modern pad keeps its right stick; the simulation controls, where a
  pad that names its analog triggers puts them; and consumer AC Pan, where a sideways mouse wheel
  lives.
- `hid_layout_hat_directions()`, for hats of four and of eight positions, resting outside their own
  range.
- Native tests under the address and undefined behaviour sanitizers, and a libFuzzer harness over
  the parser, both in CI.

### Changed

- Buttons split over two contiguous Input items now count as one run.

### Fixed

- A signed overflow on a hostile logical range, found by the fuzzer.
- A field a short report does not hold used to read as zero, which for an absolute axis meant the
  caller was told the stick was pushed hard left rather than nothing at all. Every reader now checks
  that the report holds the field first, and the direction functions write all of their outputs.

## 0.1.0

### Added

- `hid_layout.h`: the report descriptor parser. Reads the axes, the wheel, the hat switch and the
  buttons out of a descriptor by usage rather than by guessing at report lengths, so a device it has
  never seen still works.
- Native tests against descriptors captured from real hardware, and publishing to the ESP Component
  Registry on a tag.
