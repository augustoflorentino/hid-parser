# Changelog

Each release lists what changed, what updating means for existing code, and every security fix.

## Unreleased

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
