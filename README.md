# hid-parser

[![Release](https://img.shields.io/github/v/release/augustoflorentino/hid-parser?style=for-the-badge&color=0b6e78)](https://github.com/augustoflorentino/hid-parser/releases/latest)
[![License](https://img.shields.io/github/license/augustoflorentino/hid-parser?style=for-the-badge&color=4b5d61)](LICENSE)
![USB HID 1.11](https://img.shields.io/badge/USB%20HID-1.11-0b6e78?style=for-the-badge)

## Overview

A report descriptor is the small program a USB or Bluetooth HID device hands the host to say
what its reports contain. hid-parser reads that program and gives back every field with its
bit offset, size, range and usages, then reads and builds the reports themselves. Where the HID
specification leaves room, it settles the question the way `drivers/hid/hid-core.c` does, so a
descriptor means here what it means to Linux. Plain C99, nothing system-specific.

## Quick look

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/images/descriptor-dark.svg">
  <img src="docs/images/descriptor-light.svg" alt="The 83-byte report descriptor of a mouse from the test corpus, main items highlighted, next to the collections and fields hiddump prints for it" width="900">
</picture>

Left, the descriptor of a mouse from `tests/corpus`; the highlighted bytes are main items
(Collection, Input, End Collection). Right, what [`examples/hiddump.c`](examples/hiddump.c)
makes of it: the collection tree, a 13-byte input report and its five fields.

## What it supports

- a tokenizer that walks the items through a callback and allocates nothing
- the parsed form: one flat list of fields per report (type, report ID, bit offset, size, count,
  flags, logical and physical range, unit, usages), the collection tree and each report's size
- three modes: the kernel's rules by default, `HIDP_PARSE_STRICT`, and `HIDP_PARSE_TOLERANT`
  for the broken descriptors real devices ship, where each problem becomes a warning with its
  byte offset
- field values as `int64_t`, signed when the logical minimum is negative, so unsigned 32-bit
  fields keep their full range; array values mapped back to usages; null state checked with
  `hidp_field_in_range`
- whole-report decoding through a callback, writing a control by its usage, and encoding short
  items to build descriptors
- `hidp_desc_application` to split composite devices, such as a keyboard with media keys, by
  top-level application collection
- device kinds (`hidp_desc_kind`) and usage roles (`hidp_usage_role`: button, axis, hat, key,
  LED, haptic)
- usage names from the HID Usage Tables 1.7, in an object file of their own that a static link
  only pulls in when a name function is called

## Install

```sh
cmake -S . -B build -DBUILD_SHARED_LIBS=ON
cmake --build build
cmake --install build --prefix /usr/local
```

```cmake
find_package(hid-parser REQUIRED)
target_link_libraries(app PRIVATE hid-parser::hid-parser)
```

Without CMake: `cc app.c $(pkg-config --cflags --libs hid-parser)`.

There is also a one-file build. `scripts/amalgamate.sh` produces `dist/hid-parser/hid-parser.h`;
in a single `.c` file of your project put `#define HIDP_IMPLEMENTATION` before
`#include "hid-parser/hid-parser.h"`, and add `HIDP_STATIC` if no other file should see the
functions.

## API

The reference is the header, [`include/hid-parser/hid-parser.h`](include/hid-parser/hid-parser.h).
Reading the X axis of a mouse report:

```c
hidp_desc_t *desc;
size_t err;
int status = hidp_desc_parse(bytes, len, NULL, &desc, &err);

if (status != HIDP_OK) {
    fprintf(stderr, "byte %zu: %s\n", err, hidp_strerror(status));
    return 1;
}

size_t field, index;
int64_t x;

if (hidp_desc_find_usage(desc, HIDP_INPUT, 0x00010030, 0, &field, &index) == HIDP_OK
    && hidp_field_read(hidp_desc_field(desc, field), report, report_len, index, &x) == HIDP_OK)
    printf("X = %lld\n", (long long)x);
hidp_desc_destroy(desc);
```

Going the other way, `hidp_desc_find_report` finds an output or feature report,
`hidp_report_init` clears the buffer and writes the report ID, and `hidp_field_write` or
`hidp_report_set_usage` fills it in. `hidp_report_decode` walks a received report and calls
back with the usage and value of every variable control and every usage present in an array;
fields wider than 32 bits are skipped. `hidp_item_write` encodes one short item in the smallest
size that keeps its value, treating Logical/Physical Minimum and Maximum and Unit Exponent as
signed.

Rules that hold across the API:

- functions that can fail return `HIDP_OK` or an error code, which `hidp_strerror` explains
- a descriptor from `hidp_desc_parse` is freed only by `hidp_desc_destroy`, which accepts
  `NULL`; its fields, collections, reports and warnings live as long as it does
- `hidp_parse_options_t` carries the mode flags and an optional allocator (`alloc_fn` and
  `free_fn`, both or neither); `NULL` options mean the default mode with `malloc`
- usages are 32-bit extended values with the usage page in the high half
- `bit_offset` counts from the first byte of the report, report ID included
- a parsed descriptor never changes and there is no global state, so any number of threads can
  read one at the same time

`hiddump` prints a descriptor as an item tree (`--items`), as reports and fields, or one line
per value (`--slots`). `--hex` takes hex text instead of binary, and `--report` decodes a
report given in hex, as input unless `--type output` or `--type feature` says otherwise:

```sh
hiddump /sys/class/hidraw/hidraw0/device/report_descriptor
hiddump --report "05 00 fd ff 03 00 01 ff 00 00 00 00 00" mouse.bin
```

## Conformance

HID 1.11 describes the items; the kernel decides the corner cases, and hid-parser follows the
kernel:

- a short usage takes the usage page current at the main item, looking back until an earlier
  one has it; Usage Minimum/Maximum expand at the maximum, capped at 12288 usages with the
  kernel's 32-bit arithmetic, so a maximum below the minimum fills the table from the minimum
- Logical and Physical Maximum are signed only when the minimum is negative
- a 4-bit Unit Exponent is sign-extended
- the report ID is a global item and follows Push/Pop; the global stack has 4 entries
- inside a delimiter set only the first branch counts, and nested delimiters are an error
- a collection takes its first usage
- rejected: long items, unknown global items, report size above 256, report count above 12288,
  report ID 0 or above 255, logical minimum above maximum, reports longer than 16383 bytes,
  unbalanced collections or delimiters, truncated items, empty descriptors
- unknown main items become warnings; unknown local items and reserved item types are skipped

One deliberate difference: padding without usages stays in the field list, so every bit of a
report belongs to some field.

`HIDP_PARSE_STRICT` goes further than the kernel and refuses a Usage Maximum without or below
its Usage Minimum, a Usage Minimum never closed by a maximum, a maximum that only fits when read
as signed, and descriptors that mix reports with and without an ID. `HIDP_PARSE_TOLERANT` goes
the other way: it skips long and unknown items, takes report sizes and counts up to 65535,
ignores bad report IDs and logical ranges, allows 64 nested Push and closes whatever is left
open.

How that is checked:

- `tests/corpus` holds descriptors of real devices and their expected fields, run by `ctest`
- `tests/oracle/compare.py` matches every field against the `hid-decode` parser of hid-tools
  over the descriptors of the Linux and hid-tools test suites, which
  `scripts/fetch-corpus.sh` downloads (they are GPL and stay out of this repository); the only
  difference is a documented hid-tools one, which ignores Usage Maximum without Usage Minimum
- `tests/kernel-oracle` matches every field against the kernel's own dump in
  `/sys/kernel/debug/hid/*/rdesc`; its `vm/run-vm.sh` boots a stock kernel in QEMU and feeds
  the descriptors through uhid, so the host needs no debugfs
- `fuzz/fuzz_parse.c` parses in all three modes and reads and writes every field
- the tests also pass on 32-bit x86 and on Windows with mingw

## Development

```sh
cmake --preset dev && cmake --build build/dev && ctest --preset dev
cmake --preset fuzz && cmake --build build/fuzz
```

The first line builds with ASan, UBSan and `-Werror` and runs the tests. Field parsing borrows
a private single-file copy of [bitspan](https://github.com/augustoflorentino/bitspan) in
`src/hidp_bitspan.h`.

Releases follow Semantic Versioning: inside a major version the API and ABI only grow and the
public structs keep their layout, anything due to go is deprecated in a minor release first,
and a breaking release bumps the major version and the soname. The
[CHANGELOG](CHANGELOG.md) has the details and the upgrade notes. Bugs go to the
[issues](https://github.com/augustoflorentino/hid-parser/issues) with the descriptor in hex;
security problems follow [SECURITY.md](SECURITY.md).

MIT license ([LICENSE](LICENSE)). Usage names are generated from the HID Usage Tables published
by the USB Implementers Forum.
