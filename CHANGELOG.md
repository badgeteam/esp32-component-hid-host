# Changelog

Notable changes per release, newest first. Versions follow
[semantic versioning](https://semver.org), and each release is tagged `vX.Y.Z` and published to the
ESP Component Registry as `badgeteam/hid-host`.

## Unreleased

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
