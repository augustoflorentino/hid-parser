# Changelog

Each release lists what changed, what updating means for existing code, and every security fix.

## Unreleased

Incompatible with 1.x: the soname is now `libhid-parser.so.2`.

- values are `int64_t` across the API: `hidp_value_t.value`, `hidp_field_in_range` and
  `hidp_field_array_usage`. An unsigned 32-bit field above `INT32_MAX` used to reach
  `hidp_report_decode` callers as a negative number
- `hidp_field_get` and `hidp_field_set`, deprecated in 1.2.0, are removed

### Upgrading

- `hidp_field_get` becomes `hidp_field_read` and `hidp_field_set` becomes `hidp_field_write`,
  with `int64_t` values
- read `hidp_value_t.value` as `int64_t`

## 1.2.0 - 2026-07-03

- `hidp_item_write`: encode one short item in the smallest size that holds its value; signed
  globals get the size that keeps their sign, so a Logical Maximum of 255 takes two bytes
- `hidp_field_read` and `hidp_field_write` take the value as `int64_t`. `hidp_field_get`
  returned an unsigned 32-bit field above `INT32_MAX` as a negative number, and
  `hidp_field_set` could not tell a negative value from a large unsigned one

### Deprecated

- `hidp_field_get` and `hidp_field_set`: use `hidp_field_read` and `hidp_field_write`. They
  stay in 1.x and go away in 2.0

## 1.0.0 - 2026-07-03

The API is now stable: see Versioning in the README. The soname is `libhid-parser.so.1`.

- `hidp_desc_parse` takes its mode and allocator in a `hidp_parse_options_t` instead of four
  loose arguments, so a call that only wants the defaults passes `NULL`
- the header is checked to build as C++ with `-Wconversion -Wold-style-cast`

### Upgrading

```c
hidp_desc_parse(data, len, flags, alloc_fn, free_fn, ctx, &desc, &err);
```

becomes

```c
hidp_parse_options_t options = {flags, alloc_fn, free_fn, ctx};
hidp_desc_parse(data, len, &options, &desc, &err);
```

or `hidp_desc_parse(data, len, NULL, &desc, &err)` for the default mode with `malloc`.

## 0.4.0 - 2026-07-02

- `hidp_report_decode`: walk an input, output or feature report through a callback, with the
  usage and value of each variable control and the usages present in array fields; usage ID
  0, reserved on every page, means "no control" in an array and is skipped
- `hiddump --report` uses it
- `hidp_desc_match_report` on a descriptor that mixes reports with and without ID now does
  what the kernel does: once any report of that type has an ID, the first byte selects the
  report. It used to pick the report without ID, longer than what was received
- `hidp_desc_find_usage` looks the usage up range by range instead of usage by usage: on a
  field of 12288 buttons it went from quadratic to linear in the number of ranges

## 0.2.0 - 2026-06-30

- the private copy of bitspan is now 3.1.0
- `HIDP_VERSION_MAJOR`, `HIDP_VERSION_MINOR`, `HIDP_VERSION_PATCH`, `HIDP_VERSION` and
  `hidp_version()`
- `hiddump --report HEX` decodes an input report: the value of each variable field and the
  usages present in array fields

## 0.1.0 - 2026-06-25

First release.

- tokenizer over a callback and a descriptor parser that follows the Linux kernel rules,
  with strict and tolerant modes and warnings with byte offsets
- fields, collections and reports; read and write values of up to 32 bits; array usages;
  output and feature reports
- device and usage classification
- usage names from HID Usage Tables 1.7 (HidUsageTables.json attached to hut1_7.pdf, sha256
  07e8ca728d78271cb0186482e51479deee32f1ca34016473623173638a2144d8)
- vendors bitspan 0.1.0 as a private single-file copy
- `hiddump` example
- checked field by field against the kernel parser, in a QEMU guest fed through uhid, and
  against the hid-tools decoder, on 136 descriptors
