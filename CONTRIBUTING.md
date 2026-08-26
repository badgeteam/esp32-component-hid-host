# Contributing

Almost every change here is the same shape: a device did something the parser had not seen, so its
report descriptor joins the tests and the code learns to handle it. This describes that path, and
the few others.

Everything builds and runs on a host. You do not need an ESP32, or ESP-IDF, to work on this.

```
make -C test_native test
```

## Adding a device

The tests are the documentation of what real hardware actually sends, so a descriptor is worth
adding even when the parser already handles it. Twenty seven of them are in there for exactly that
reason: they pin down behaviour that would otherwise quietly change.

### Capture the descriptor

On Linux the kernel keeps the descriptor of every HID device it has bound, in hexadecimal:

```
sudo cat /sys/kernel/debug/hid/*/rdesc
```

Each entry starts with the descriptor and follows it with the parsed listing, which is a useful
second opinion on what the bytes mean. `usbhid-dump` from usbutils gets the same bytes straight off
the wire, which is what you want for a device the kernel has already applied a fixup to:

```
sudo usbhid-dump -d 054c:0268 -e descriptor
```

Elsewhere, ask the HID host driver on the device itself and print what it hands back.
`hid_host_get_report_descriptor()` under ESP-IDF, `HidD_GetPreparsedData` on Windows, or any of the
published dumps — several tests here come from repositories that collect them, and a link to one of
those is a perfectly good source as long as it says which device it came off.

Capture a handful of input reports too, if you can. A layout that parses and a layout that decodes
correctly are two different claims, and only reports prove the second.

### Add it to the tests

Descriptors go in `test_native/test_descriptors.h`, reports in `test_native/test_reports.h`, both as
a byte array and a `sizeof` length next to it:

```c
// Speedlink Competition Pro
static const uint8_t gamepad4_desc[] = {
    0x05, 0x01, 0x09, 0x04, ...
};
static const size_t gamepad4_len = sizeof(gamepad4_desc);
```

Say in a comment where it came from. A descriptor with no provenance cannot be checked against the
hardware later, and half the value of the corpus is being able to go back and ask.

Add a line to `test_native/dump_corpus.c` as well, so the fuzzer starts from your descriptor too:

```c
failed |= write_one("gamepad4", gamepad4_desc, gamepad4_len);
```

### Write the test

`test_hid_layout.c` checks what the parser found — which fields are present, where they sit, how
wide they are, what they range between. Start from the parsed listing the kernel printed and assert
the offsets it implies:

```c
/// Speedlink Competition Pro: fifteen buttons, then a four bit hat and a pair of byte axes
static void test_competition_pro(void) {
    hid_layout_t layout;
    assert(hid_layout_parse(gamepad4_desc, gamepad4_len, &layout));

    assert(layout.report_id == 0);
    check_field(&layout.buttons, 0, 1);
    assert(layout.button_count == 15);
    check_field(&layout.hat, 16, 4);
    check_field(&layout.x, 24, 8);
    check_field(&layout.y, 32, 8);

    ESP_LOGI(TAG, "Competition Pro");
}
```

`check_field()` takes the field, its bit offset and its width, since those three go together in
every assertion. If you captured reports, read values out of them too and assert what they mean —
`hid_layout_axis_directions()` on a report where the stick was held left should say left and
nothing else.

`test_hid_gamepad.c` checks what a caller sees for a pad: the directions and buttons a report turns
into. It has `feed()` to decode one report and `check()` to assert the four directions and the
button bitmap in one line.

Then call the test from `main()` at the bottom of the file. Both files list the real devices first,
in roughly the order they were added, and keep the synthetic descriptors that exercise one corner of
the parser after them. Give it a one-line `///` comment saying what is interesting about this
device, and close it with `ESP_LOGI(TAG, ...)` naming the device — that is what makes the output
readable when one of the fifty-odd tests trips.

Bit offsets in a layout do **not** count the report ID. A device that puts one in front of its
reports has its first field at offset 0 all the same, and `hid_layout_strip_report_id()` takes the
byte off before anything is read.

### Run everything

```
make -C test_native test             # the tests
make -C test_native test-sanitized   # the same, under ASan and UBSan
make -C test_native fuzz             # random descriptors, needs clang, Ctrl-C to stop
```

CI runs all three on every push, with the fuzzer capped at two minutes. Run at least the first two
before opening a pull request; the fuzzer is worth a minute or two whenever you touch the parser
itself, since that is where it has found things.

## When the descriptor is not enough

Some devices lie, and some say nothing until asked. `hid_gamepad_quirks` in `src/hid_gamepad.c`
holds what a descriptor cannot express, keyed on vendor and product ID:

```c
{
    .vid                  = 0x054c,
    .pid                  = 0x0268,
    .name                 = "DualShock 3",
    .enable_report_id     = 0xf4,
    .enable_report        = dualshock3_enable_reporting,
    .enable_report_length = sizeof(dualshock3_enable_reporting),
    .dpad_first_button    = 4,
},
```

Reach for a row here last. The table stays small on purpose: a device whose descriptor *does* say
what it means should be handled by reading the descriptor, so that the next device saying the same
thing works without anybody adding a row for it. A quirk is for what the descriptor genuinely
cannot state — a d-pad wired as four ordinary buttons, or a feature report that has to be sent
before the device says anything at all.

## Changing the parser

`hid_layout.c` is fed bytes that came off a USB device, which makes it as untrusted as input gets.
Two rules follow from that, and both have already caught real bugs:

- Arithmetic on anything a descriptor stated is done wide enough to hold whatever it said. A
  logical range of `-2147483625` to `66` is a legal thing for a descriptor to claim, and subtracting
  one from the other in `int32_t` is undefined behaviour. The fuzzer found exactly that.
- A field that runs past the end of a report is not read. Reading it as zero sounds harmless until
  an absolute axis with a range of 0 to 255 reads zero and the caller is told the stick is pushed
  hard left. `field_fits()` exists for this, and everything answering a question about a field
  checks it first.

`docs/ARCHITECTURE.md` covers how the parser is put together and why, which is worth a read before
changing it.

## Style

Match the surrounding code. It wraps at 120 columns, aligns the initialisers in a run of
declarations, and comments the *why* rather than the what — there are plenty of examples of all
three. Public functions carry a `///` block in the header, static ones carry theirs above the
definition in the source.

Commit messages are written as sentences and say what changed and why, in the style of the log.

## Releasing

`idf_component.yml` holds the version and a tag publishes it. The publish workflow refuses a tag
that disagrees with the manifest, so both move together:

1. Bump `version:` in `idf_component.yml`.
2. Add the release to `CHANGELOG.md`.
3. Merge that, then tag the merge commit `vX.Y.Z` and push the tag.

The workflow runs the tests once more and uploads to the ESP Component Registry as
`badgeteam/hid-host`.
